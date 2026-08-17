# Plan: Baseline Code Generation and Optimized Self-Host Performance

Status: in progress; T1, P0, B1, B2, B3a, B3b, B3n, B4a, B4b, B4c, B4d1--B4d7, B5, and C1 complete

Date: 2026-08-17

## 1. Goal and scope

Improve the code produced by `cppgm++` without giving back its compile-time
advantage.  The work starts with target-specific, bounded backend improvements
that are valid at `-O0`, then moves to explicit `-O1` and `-O2` work in PA37
and PA38.

The concrete goals are:

- retain the current `-O0` compile-time lead over GCC;
- make total `-O1` and `-O2` compile time faster than GCC at the matching
  level in a controlled same-host comparison;
- materially reduce the frozen `semantic_overload.cpp` object and its text,
  LSDA, and unwind overhead;
- reduce the roughly sevenfold self-host slowdown observed while compiling
  `lowir_opt.cpp` with `cppgm++-self`;
- preserve the staged assignment contracts, especially PA29 `-O0` MIR, PA31
  host EH facts, PA37 LowIR optimization, and PA38 machine optimization; and
- keep the implementation within the linear or near-linear production bounds
  required by `spec.md`.

The absence of an `-O` option continues to select the maximum implemented
level.  At present `-O2` and `-O3` both select internal level 2.  Explicit
`-O0` remains the stable baseline representation lane; it is not a request for
deliberately long x86 encodings or a literal one-instruction encoding of every
MIR artifact.

This plan does not use PGO, persistent caches, benchmark-specific behavior,
host-compiler fallbacks, assembly as an internal transport, or the extended
PA39 checkout as a writable source tree.

## 2. Evidence at the starting point

The implementation baseline is commit `4f08a363`.  The completed work and its
evidence remain recorded in `PLAN-OBJECT-DEMAND.md` and
`PLAN-EMISSION-EFFICIENCY.md`; this plan does not reopen those changesets.

### 2.1 Frozen `-O0` object

The frozen input is:

`~/cppgm-extended-pa39-source-layout/benchmarks/self_compile/stable/semantic_overload.cpp`

After E2--E9, its relevant cppgm++ measurements are:

| Metric | Starting value |
| --- | ---: |
| Corrected three-run compile median | 6.12 s |
| Object bytes | 3,957,616 |
| `.text` bytes | 1,225,708 |
| `.gcc_except_table` bytes | 74,640 |
| `.eh_frame` bytes | 145,188 |
| Defined functions | 5,533 |

The last GCC comparison produced a 3,196,024-byte object with 599,783 bytes of
text and 4,980 defined functions.  The difference is diagnostic, not a parity
contract: GCC and cppgm++ do not instantiate exactly the same standard-library
implementation or COMDAT set.

The remaining measured opportunities include:

| Opportunity | Measured current upper bound |
| --- | ---: |
| Compact x86 memory displacements | 157,352 object bytes; 156,330 text bytes |
| Jump to the following instruction | 9,399 jumps; 18,798 text bytes |
| Zero materialization | about 10.6 KiB |
| `cmp reg, 0` to `test reg, reg` | about 2 KiB |
| Narrower zero-extension encodings | about 2 KiB |
| Redundant address/load/store copies | about 8--11 KiB |

The displacement result came from an isolated prototype.  It selected no
displacement or an 8-bit displacement where legal, reduced the object by
157,352 bytes, and passed all 1,274 PA29--PA38 report tests.  It is the first
implementation change in this plan.

### 2.2 EH and function evidence

The frozen object has substantially more repeated cleanup code than GCC:

| Call target | cppgm++ calls | GCC calls |
| --- | ---: | ---: |
| `_Unwind_Resume` | 1,672 | 467 |
| string destructor | 3,601 | 1,160 |
| `ExprInfo` destructor | 1,952 | 370 |
| `shared_ptr` destructor | 1,907 | 426 |

The cppgm++ LSDA is 74,640 bytes versus GCC's 16,263 bytes in the last
comparison.  Constructor-unwind suffix sharing has already landed as E1, and
native demand pruning has already removed 683 unreachable weak functions.
The remaining 553-function count difference must be classified by typed demand
reason before any further deletion; the number alone is not evidence that a
definition is invalid.

### 2.3 Self-host code-quality evidence

Three interleaved, CPU-pinned compiles of `dev/src/lowir_opt.cpp` gave medians
of 4.47 seconds for the host-built `cppgm++` and 31.64 seconds for
`cppgm++-self`.  Both emitted the exact same 2,122,552-byte object.  Every
measured compiler phase was slower in the self-hosted binary, rather than one
compiler phase accounting for the whole difference.

The self-hosted binary has about 19.8 MiB of text and 44,574 defined function
symbols, compared with about 6.4 MiB and 5,541 for the GCC-built compiler.
Sampling and disassembly show many out-of-line trivial STL helpers, larger
accessors and wrappers, repeated frame traffic, and constant division.  The
self-hosted binary contains 17,051 `div`/`idiv` instructions versus 1,263 in
the GCC-built compiler; 16,050 cppgm++ divisions have immediate divisors and
8,256 use a power-of-two divisor.

The generated `lowir_opt.cpp` objects show that the current optional levels do
usefully reduce calls and text, but do not remove function bodies:

| Compiler/level | Text bytes | Defined functions | Static calls |
| --- | ---: | ---: | ---: |
| cppgm++ `-O0` | 766,089 | 4,096 | 11,461 |
| cppgm++ `-O1` | 704,865 | 4,096 | 9,748 |
| cppgm++ `-O2`/`-O3` | 660,174 | 4,096 | 9,740 |
| GCC `-O0` | 456,105 | 3,456 | 8,418 |
| GCC `-O3` | 222,569 | 198 | 2,607 |

An allocator preload A/B reduced the self-hosted `lowir_opt.cpp` median from
31.85 to 30.39 seconds, about 4.6%, at roughly 10 MiB additional RSS.  The
host-built compiler already records jemalloc as a needed library, while the
PA39 self link currently places that library before the input objects and can
discard it under as-needed linking.  Link-order parity is therefore an early,
separate build-system item, not a substitute for generated-code work.

The PA37 inliner did not do zero work.  On `lowir_opt.cpp` it reported 6,399
inlined calls and 997 budget skips.  Its limited production effect follows
from its current scheduling and policy:

- eligibility and cost are computed from unsimplified callees;
- inlining runs before ordinary simplify/DCE/CFG cleanup;
- recursive components are identified, but nonrecursive expansion still
  follows definition order rather than a bottom-up call-graph schedule;
- a callee with EH or more than 40 instructions is normally rejected;
- explicit no-unwind EH stripping happens only while expanding a function,
  after earlier callers may already have considered it;
- the fast leaf path rejects any callee containing a call;
- each caller has a fixed 128-instruction budget; and
- optional PA37 inlining is not followed by a second native weak-function
  reachability pass.

Existing PA37 tests prove a number of individual inlining and EH cases.  They
do not adequately cover production-sized call chains whose eligibility is
created by simplification, adversarial definition order, or the removal of a
last call edge.

## 3. Optimization-level and ownership decisions

The implementation must distinguish target selection from optional IR
optimization.

| Work | Level and earliest owner | Representation rule |
| --- | --- | --- |
| Compact ModRM/SIB displacement | Baseline `-O0`, PA29 | Encoder only; no LowIR or MIR change |
| Omit a jump whose target is the next encoded instruction | Baseline `-O0`, PA29 | Final layout/encoder only; PA38 O1 may still remove it from MIR |
| Constant-divisor x86 selection | Baseline `-O0`, PA29 | Prefer encoder/native selection; any narrow MIR movement is tracked explicitly |
| Zero/test/movzx/addressing idioms | Baseline `-O0`, PA29 | Bounded target selection with explicit flag/register proofs |
| Coalesce equivalent adjacent LSDA call sites | Baseline `-O0`, PA31 | Object metadata only; no LowIR or MIR change |
| Generate one cleanup node for an identical frontend cleanup state | Potential baseline lowering, PA26 | Allowed only if the LowIR impact is narrow and reference-compatible |
| Merge arbitrary equivalent cleanup tails after LowIR exists | `-O1`, PA37 | An explicit optimizer transform, not O0 canonicalization |
| Delete definitions with no valid typed native demand root | Baseline native boundary, owning semantic PA plus PA32 | Correct demand, not optional optimization |
| Delete a weak definition made unreachable by optional inlining | `-O1/-O2` native path, PA37/PA32 | Run after optional inlining; never delete strong or address-taken roots |
| Better inlining and scalar/CFG optimization | `-O1`, PA37 | LowIR-visible and tested as such |
| More global value, loop, layout, and allocation work | `-O2`, PA37/PA38 | Explicit bounded level-2 budgets |

GCC's use of shifts, masks, `test`, and compact encodings at `-O0` is
consistent with this boundary.  These are target instruction-selection
choices, not source-level algebraic simplification.  Conversely, discovering
and merging arbitrary LowIR subgraphs is an optimizer operation even if the
resulting assembly resembles a baseline compiler's output.

## 4. Fixture and reducer policy

Every candidate gets a reducer before or with its implementation.  The reducer
is owned by the earliest assignment that owns the behavior:

- PA26 for source-generated exception cleanup;
- PA29 for native selection, register/stack semantics, and encoding;
- PA31 for host EH and LSDA facts;
- PA37 for LowIR `-O1/-O2` transformations and object round trips; and
- PA38 for MIR `-O1/-O2` transformations.

References are generated only through the documented `ref-test` target.  If
the reference agrees with the behavior and required shape, the test goes in
`cppgm.tests/course/paN/`.  If the source is useful but the reference uses a
different valid LowIR, MIR, or encoding, it goes in `proposed/paN/` with a
README entry explaining the disagreement.  A behavior-only test belongs in
the active course suite when it protects a real correctness boundary; a test
whose only value is proving our more efficient representation remains
proposed when the reference does not prove that representation.

PA26 and PA31 explicitly have no standalone reference binary.  For those
assignments, reference agreement means an existing checked-in or upstream
assignment fixture already establishes the result.  A new layout or inspect
oracle without that authority starts under `proposed/paN/`; do not manufacture
an active `.ref` sidecar from our own output.

### 4.1 Hard O0 fixture rule

The live tracker below records, for every `-O0` candidate:

- all changed checked-in LowIR fixtures, by exact path and count;
- all changed checked-in MIR fixtures, by exact path and count;
- whether runtime and object inspection remain correct;
- whether the reference compiler produces the same change; and
- the final decision and commit.

A pure encoder or metadata change is expected to change zero LowIR and zero MIR
fixtures.  A narrow MIR change may proceed only when the tracker lists every
affected fixture and explains why target selection necessarily moved.  Its
oracle is regenerated only when the reference agrees.  If the reference does
not agree and an existing fixture would have to be rewritten, the candidate is
deferred.

If an `-O0` candidate broadly changes LowIR, stop that experiment and mark it
`deferred: broad LowIR movement`.  Do not update the fixtures.  Recast the
idea as a PA37 `-O1` transform where appropriate.  Narrow source-lowering
cleanup sharing is not an exception to this rule.

### 4.2 E4--E9 backfill

The first tests-only batch backfills the already-landed encoder fixes.  It does
not alter compiler behavior.

| Existing fix | Reducer obligation | Expected placement |
| --- | --- | --- |
| E4 packed constant byte stores | Keep the existing runtime reducer; add an exact-shape reducer only if an existing inspection lane can prove it without binding register allocation | Existing `proposed/pa29/`; promote only on reference agreement |
| E5 immediate 64-bit frame reload forwarding | Store is still observable while the reload result is consumed; include same-register and different-register forms | PA29 behavior active if reference agrees; representation proof proposed otherwise |
| E6 single-use temporary-home elision | Adjacent single store/load positive case plus a second-use negative case | PA29 behavior active; exact frame omission proposed if reference differs |
| E7 one independent instruction | Positive load/LEA/move separators and call/store/source-redefinition barriers | PA29 behavior active; optimization-shape proof follows reference routing |
| E8 two-to-five independent instructions | Positive distances 2 and 5, distance 6 boundary, and a barrier in the window | PA29 behavior active; optimization-shape proof follows reference routing |
| E9 narrow reload forwarding | i8/i16/i32 cases that expose partial-register, zero-extension, and upper-bit behavior | PA29 behavior active; this is correctness coverage even without exact MIR enforcement |

One reducer should not be forced to prove multiple changesets when separate
small inputs make a regression attributable.  The old E4 proposed test stays
proposed unless a genuine active lane can observe the optimization itself.

T1 landed in `ce76bd95`.  E4 remains covered by the existing proposed
constant-byte-store witness because its only distinguishing property is the
native encoding.  Five new PA29 course behavior tests cover E5--E9:

- `immediate-frame-reload-forwarding.t` exercises same-register and
  different-register immediate 64-bit reloads;
- `single-use-temporary-home-forwarding.t` pairs an adjacent single-use
  temporary with a second-use negative case whose backing store must remain;
- `one-instruction-temporary-reload-forwarding.t` places one source-preserving
  `lea` between a temporary store and reload;
- `bounded-temporary-reload-forwarding.t` covers exact gaps of two and five,
  the six-instruction boundary, and a source-register redefinition barrier;
  and
- `narrow-frame-reload-forwarding.t` covers i8, u16, and i32 reloads plus
  their signed and zero-extended consumers.

The PA29 reference compiler accepts and successfully runs all five sources.
Its raw MIR performs some of these transfers earlier than ours, so these are
intentionally behavior fixtures without a MIR oracle; this is the existing
PA29 lane for source correctness without enforcing an interchangeable backend
layout.  No existing checked-in LowIR fixture changed and no existing
checked-in MIR fixture changed.  PA29 passes 183/183 assignment tests and
24/24 course tests, through PA29 passes 4,098/4,098, and the full report passes
5,176/5,176.

### 4.3 Live candidate tracker

Update this table immediately after each experiment, including reverted work.
Do not carry an unrecorded fixture-changing experiment into the next
candidate.

| ID | Candidate | Expected LowIR delta | Expected MIR delta | Initial status |
| --- | --- | ---: | ---: | --- |
| T1 | E4--E9 reducer backfill | 0 existing | 0 existing | **Landed** in `ce76bd95`; five active PA29 behavior reducers reference-agree, E4 byte-shape witness remains proposed, full report 5,176/5,176 |
| P0 | Retain jemalloc in the PA39 self link | 0 existing | 0 existing | **Landed** in `c6ee9be9`; exact-object A/B improves self-host frozen wall 1.25%, user 1.71%, and peak RSS 3.17%; 8-way inception matches; full report 5,176/5,176 |
| B1 | Compact memory displacements | 0 existing | 0 existing | **Landed** in `e46bde65`; deterministic object -157,352 bytes and text -156,330 bytes; behavior reference agrees while its unrelated MIR layout differs |
| B2 | Omit encoded jump to next instruction | 0 existing | 0 existing | **Landed** in `acf3d415`; 9,399 jumps and 18,798 text bytes removed with timing neutral |
| B3a | Unsigned power-of-two division/remainder | 0 existing | 0 existing | **Landed** in `65b62af8`; PA29 encoder peephole, active behavior reducer, frozen object byte-identical, timing neutral |
| B3b | Signed positive power-of-two division/remainder | 0 existing | 0 existing | **Landed** in `b37a6a93`; encoder peephole plus explicit-extension correction, active behavior reducers, frozen object -112 bytes, timing neutral |
| B3n | Signed negative power-of-two division/remainder | 0 existing | 0 existing | **Landed** in `e9cf0de9`; active behavior reducer, `-1` intentionally retained, frozen object byte-identical, timing neutral |
| B3c | General constant division using multiply-high magic | 0 preferred | 0 preferred; list narrow movement if unavoidable | Deferred until B3a/B3b evidence |
| B4a | Flag-safe zero materialization | 0 existing | 0 existing | **Landed** in `edd35810`; linear per-block flag liveness, proposed encoding reducer, frozen object -11,048 bytes, timing neutral |
| B4b | `cmp reg, 0` to `test reg, reg` | 0 existing | 0 existing | **Landed** in `fba50ba6`; width-aware native selection, proposed byte-shape reducer, frozen object -8,104 bytes, timing neutral |
| B4c | Narrow zero-extension encodings | 0 existing | 0 existing | **Landed** in `13fa0a10`; 32-bit `movzx` destinations and selective REX, proposed byte-shape reducer, frozen object -2,080 bytes, timing neutral |
| B4d1 | Fold a dead adjacent address calculation into its load | 0 existing | 0 existing | **Landed** in `551e530f`; 1,195 bounded proofs, proposed byte-shape reducer, frozen object -3,920 bytes, timing neutral |
| B4d2 | Fold a dead adjacent address-register copy into its load | 0 existing | 0 existing | **Landed** in `014b7594`; 144 bounded proofs, proposed byte-shape reducer, frozen object -336 bytes, timing neutral |
| B4d3 | Fold a dead copy/index/load chain | 0 existing | 0 existing | **Landed** in `2a9adc71`; 54 bounded proofs, proposed byte-shape reducer, frozen object -160 bytes, timing neutral |
| B4d4 | Fold a dead value copy into its store | 0 existing | 0 existing | **Landed** in `9082687e`; 154 bounded proofs after frame-forwarding barriers, proposed byte-shape reducer, frozen object -400 bytes, timing neutral |
| B4d5 | Fold a dead address calculation into its store | 0 existing | 0 existing | **Landed** in `40500a27`; 259 bounded proofs, proposed byte-shape reducer, frozen object -928 bytes, timing neutral |
| B4d6 | Fold a dead address-register copy into its store | 0 existing | 0 existing | **Landed** in `80327487`; 170 bounded proofs, proposed byte-shape reducer, frozen object -544 bytes, timing neutral |
| B4d7 | Fold a dead copied-and-indexed address into its store | 0 existing | 0 existing | **Landed** in `53a6f6ea`; 84 bounded proofs, proposed byte-shape reducer, frozen object -320 bytes, timing neutral; closes the safe baseline B4d inventory |
| B5 | Adjacent LSDA call-site coalescing | 0 existing | 0 existing | **Landed** in `236f78e7`; 5,672 protected calls become 3,294 LSDA entries, frozen object/LSDA -26,936 bytes, proposed PA31 end-to-end and boundary reducers, timing neutral |
| C1 | Direct cleanup-state suffix interning | Pre-existing LowIR shape | Pre-existing downstream shape | **Already landed** in the PA17/PA26 baseline (`c2b6fd68`, `e05062b1`, `8fd4193d`); exact action/context states and long `(action, tail)` chains are interned; 0 fixture changes in this run |
| C2a | Share terminal `resume` blocks | Intentional at O1 | Downstream only | **Landed** in `d3b9eca0`; 3 frozen blocks removed, object -120 bytes, PA37 88/88 and full report 5,177/5,177, timing neutral |
| C2b | Exact context-compatible cleanup-tail sharing | Intentional at O1 | Downstream only | **Landed** in `9bf96710`; 50 frozen groups, 97 LowIR instructions, 82 resume calls, and 5,984 object bytes removed; full report 5,178/5,178, timing neutral |
| C2c | Alpha-equivalent or cross-context cleanup-tail sharing | Potentially broad at O1 | Downstream only | Deferred: exact sharing captured the safe typed subset; cross-context ownership failed the PA36 reducer and alpha-renaming needs a separate SSA/liveness proof |
| D1 | Typed audit of remaining emitted definitions | 0 | 0 | Planned diagnosis |
| O1a | Simplify before inlining and bottom-up scheduling | Intentional at O1 | Downstream only | Planned |
| O1b | Post-optional-inline weak reachability | O1 native output only | Downstream only | Planned after O1a |
| O2a | Expanded slot/value promotion | Intentional at O2 | Downstream only | Profile-gated |
| O2b | Bounded improved register allocation | LowIR unchanged | Intentional at O2 | Profile-gated PA38 work |

For an accepted row, replace `Initial status` with the exact owner reports,
fixture counts and paths, frozen size/time delta, reference disposition,
commit, and final decision.  For a deferred row, record the same evidence and
the reason for deferral.

### 4.4 P0 result

`c6ee9be9` moves `INCEPTION_HOST_ALLOC_LIBS` after every PA39 link's input
objects.  The old command placed jemalloc before all inputs, so the default
as-needed link discarded it even though the host-built compiler retained it.
Both `cppgm++-self` and `cppgm++-inception` now contain a dynamic `NEEDED`
entry for `libjemalloc.so.2`.

The isolated A/B used binaries linked from the exact same current self-host
object tree.  The only difference was the position and retention of jemalloc;
all twelve frozen output objects were byte-identical (SHA-256
`52db5ca5...`) and 3,753,440 bytes.  Three-block immutable ABBA medians and
paired deltas were:

| Self-host link | Wall median | User median | Peak RSS median | Paired delta |
| --- | ---: | ---: | ---: | --- |
| allocator discarded | 44.895 s | 44.390 s | 380,866 KiB | baseline |
| jemalloc retained | 44.210 s | 43.520 s | 368,674 KiB | wall -1.25%, user -1.71%, RSS -3.17% |

No checked-in LowIR fixture changed and no checked-in MIR fixture changed;
this changes only the compiler process's allocator.  The full report passes
5,176/5,176 and the PA39 file audit has zero fatal findings.  An 8-way
inception rebuild matched every object and produced byte-identical 24,062,840
byte checkpoint binaries with SHA-256 `74e86398...`; it took 271.10 seconds
wall, 1,970.49 seconds user, and 333,528 KiB peak RSS.

One procedural caveat is retained with the evidence: a diagnostic
`make -C pa39 -n` invoked recursive recipes marked with `+` and refreshed the
canonical self-host object tree before the timed inception run.  That refresh
was not treated as clean self-build timing.  The P0 A/B remains valid because
both isolated links used that one immutable object tree.

### 4.5 B1 result

`e46bde65` selects no displacement or disp8 directly in the shared ModRM/SIB
encoder, while retaining the required displacement for RBP/R13 and disp32 for
larger offsets.  No existing LowIR or MIR fixture changed.  The new PA29
boundary reducer covers `-129`, `-128`, `-1`, `0`, `1`, `127`, and `128`; both
implementations execute it successfully.  The reference compiler's unrelated
register allocation differs, so the active test intentionally retains the
reference-generated behavior oracle without an exact MIR oracle.

Validation and frozen evidence:

- PA29: 183/183 assignment tests and 14/14 course tests;
- through PA29: 4,088/4,088;
- PA29--PA38 report: 1,275/1,275;
- full report: 5,166/5,166;
- PA39 file audit: zero fatal findings;
- object: 3,957,616 to 3,800,264 bytes;
- `.text`: 1,225,708 to 1,069,378 bytes;
- `.gcc_except_table`: 74,640 to 73,608 bytes;
- `.eh_frame`: unchanged at 145,188 bytes; and
- three-block immutable ABBA medians: wall +0.99%, user +1.55%, peak RSS
  +0.15%, all below the 3% gate.  All six outputs per compiler were
  deterministic; one candidate timing outlier was retained in the declared
  window.

### 4.6 B2 result

`acf3d415` extends the existing linear per-function branch compaction so an
unconditional branch whose target label is exactly its following instruction
emits no bytes.  It reuses the existing offset-translation path for labels,
fixups, short branches, and EH ranges.  No existing LowIR or MIR fixture
changed.  The new PA29 reducer has an exact reference MIR oracle and proves
that the O0 MIR still contains the fallthrough jumps while both encoded
programs execute successfully.

Validation and frozen evidence:

- PA29: 183/183 assignment tests and 15/15 course tests;
- through PA29: 4,089/4,089;
- PA29--PA38 report: 1,276/1,276;
- full report: 5,167/5,167;
- PA39 file audit: zero fatal findings;
- eliminated fallthrough jumps: 9,399;
- object: 3,800,264 to 3,781,392 bytes;
- `.text`: 1,069,378 to 1,050,580 bytes;
- `.gcc_except_table`: 73,608 to 73,529 bytes;
- `.eh_frame`: unchanged at 145,188 bytes; and
- three-block immutable ABBA paired deltas: wall +0.08%, user +0.09%, peak
  RSS +0.07%.  All six outputs per compiler were deterministic.

### 4.7 B3a result

`65b62af8` recognizes the existing unsigned constant-division MIR sequence in
the native encoder.  A power-of-two quotient is emitted as an in-place logical
shift and a remainder as an in-place mask; non-power-of-two divisors retain
hardware `div`.  This keeps the baseline LowIR and textual MIR contracts
unchanged.

No existing checked-in LowIR fixture changed and no existing checked-in MIR
fixture changed.  The new active reducer is
`cppgm.tests/course/pa29/unsigned-power-of-two-division.t`.  Both the reference
and implementation pass its behavior oracle.  The reference-generated MIR
uses a different valid register allocation and frame layout, so no MIR oracle
is checked in for this new behavior test; no existing oracle was removed or
regenerated.  A binary inspection of the reducer finds exactly the two
non-power-of-two control `div` instructions and none for its power-of-two
cases.

Validation and frozen evidence:

- PA29: 183/183 assignment tests and 16/16 course tests;
- through PA29: 4,090/4,090;
- PA29--PA38 report: 1,277/1,277;
- full report: 5,168/5,168;
- PA39 file audit: zero fatal findings;
- frozen object, text, LSDA, and unwind output: byte-identical because this
  translation unit has no matching unsigned power-of-two division sequence;
  and
- three-block immutable ABBA paired deltas: wall -0.97%, user -1.25%, peak
  RSS +0.22%.  All six outputs per compiler were deterministic.

### 4.8 B3b result

`b37a6a93` extends the encoder peephole to signed division and remainder by a
positive power of two.  It biases negative dividends before an arithmetic
shift so division truncates toward zero.  Remainder uses
`((n + bias) & mask) - bias`, preserving the dividend-sign rule without a
hardware divide.  The same changeset corrects explicit integer extension
selection: `zext` and `sext` now choose their MIR opcode from the conversion
operator rather than the spelling of the source type.

No existing checked-in LowIR fixture changed and no existing checked-in MIR
fixture changed.  The two new active behavior reducers are:

- `cppgm.tests/course/pa29/explicit-integer-extension-spelling.t`; and
- `cppgm.tests/course/pa29/signed-power-of-two-division.t`.

Both references agree with the tested behavior.  Their reference MIR uses
different valid register allocation and frame layout, so neither new test
checks in a MIR oracle.  No existing MIR oracle was edited, removed, or
regenerated.  The signed reducer covers i8/i16/i32/i64, zero, positive and
negative nonmultiples, extrema, divisors 1/2/4/large powers, and three negative
controls.  Its encoded `idiv rcx` count falls from 16 to the three controls.

The first signed encoding experiment was rejected and replaced before commit:
although correct, constructing the quotient in the scratch register grew the
frozen object by 176 bytes and `.text` by 180 bytes.  Updating the dividend in
place and using the direct remainder identity reversed that regression.

Validation and final frozen evidence:

- PA29: 183/183 assignment tests and 18/18 course tests;
- through PA29: 4,092/4,092;
- full report: 5,170/5,170;
- PA39 file audit: zero fatal findings; the new MIR builder was separated from
  `lowir_native.cpp` to keep that file at the 3,000-line limit;
- matching frozen `idiv rcx` sites: 166 to 76;
- object: 3,781,392 to 3,781,280 bytes;
- `.text`: 1,050,580 to 1,050,488 bytes;
- `.gcc_except_table`: 73,529 to 73,515 bytes;
- `.eh_frame`: unchanged at 145,188 bytes; and
- three-block immutable ABBA paired deltas on the exact audited compiler:
  wall +0.25%, user -0.09%, peak RSS +0.26%.  All six outputs per compiler
  were deterministic.

### 4.9 B3n result

`e9cf0de9` applies the B3b bias sequence to negative power-of-two divisors and
negates only a quotient; remainder is identical to the corresponding positive
divisor because its sign follows the dividend.  The magnitude calculation is
unsigned so `-9223372036854775808` is handled without compiler-side signed
overflow.  Divisor `-1` remains on hardware `idiv`: without a dividend value
proof, replacing it with `neg` would remove the existing overflow trap for
`INT64_MIN / -1`.

No existing checked-in LowIR fixture changed and no existing checked-in MIR
fixture changed.  The new active behavior reducer is
`cppgm.tests/course/pa29/negative-power-of-two-division.t`; the reference and
implementation agree on behavior, while no representation-specific MIR oracle
is checked in.  Its encoded `idiv rcx` count falls from eight to the three
intentional `-7` and `-1` controls.

Validation and frozen evidence:

- PA29: 183/183 assignment tests and 19/19 course tests;
- through PA29: 4,093/4,093;
- PA29--PA38 report: 1,280/1,280;
- full report: 5,171/5,171;
- PA39 file audit: zero fatal findings;
- frozen object, text, LSDA, and unwind output: byte-identical because this
  translation unit has no matching negative power-of-two divisor; and
- three-block immutable ABBA paired deltas: wall -0.41%, user -0.36%, peak
  RSS +0.02%.  All six outputs per compiler were deterministic.

### 4.10 B4a result

`edd35810` precomputes condition-flag liveness once per MIR block and encodes a
zero register move as `xor r32,r32` only when no compare/test result is live.
The liveness transfer explicitly preserves compare facts across instructions
whose x86 encoding preserves EFLAGS and ends them at a consumer or clobber.
The zero peephole runs after multi-instruction encoding peepholes so it cannot
steal the setup instruction from byte-store coalescing or reload forwarding.
Analysis and emission remain linear in block instruction count.

No existing checked-in LowIR fixture changed and no existing checked-in MIR
fixture changed.  Existing PA29 immediate-move and branch behavior tests are
the active correctness gate.  The new representation-only candidate is
`proposed/pa29/flag-safe-zero-materialization.t`: program behavior alone would
duplicate active coverage, while PA29 has no native-byte oracle.  Manual
inspection confirms its textual MIR retains two `mov ..., 0` instructions and
its executable contains two `xor eax,eax` encodings.

Validation and frozen evidence:

- PA29: 183/183 assignment tests and 19/19 course tests;
- through PA29: 4,093/4,093;
- full report: 5,171/5,171;
- PA39 file audit: zero fatal findings;
- object: 3,781,280 to 3,770,232 bytes;
- `.text`: 1,050,488 to 1,039,496 bytes;
- `.gcc_except_table`: 73,515 to 73,463 bytes;
- `.eh_frame`: unchanged at 145,188 bytes; and
- three-block immutable ABBA paired deltas on the final compiler: wall
  -0.41%, user +0.54%, peak RSS +0.23%.  All six outputs per compiler were
  deterministic.

### 4.11 B4b result

`fba50ba6` encodes a register comparison with immediate zero as the
width-correct `test reg,reg` form.  For the equality, signed, and unsigned x86
conditions used by MIR, both instructions produce the same relevant ZF, SF,
PF, CF, and OF values.  The selection is local and constant-time.

No existing checked-in LowIR fixture changed and no existing checked-in MIR
fixture changed.  Existing PA29 zero-compare branch tests remain the active
behavior and MIR gates.  `proposed/pa29/zero-compare-test-encoding.t` retains
`cmp ..., 0` in MIR while manual inspection proves u32 and i64 `test`
encodings; it remains proposed because the active harness has no native-byte
oracle and its behavior duplicates existing coverage.

Validation and frozen evidence:

- PA29: 183/183 assignment tests and 19/19 course tests;
- through PA29: 4,093/4,093;
- full report: 5,171/5,171;
- PA39 file audit: zero fatal findings;
- object: 3,770,232 to 3,762,128 bytes;
- `.text`: 1,039,496 to 1,031,422 bytes;
- `.gcc_except_table`: 73,463 to 73,434 bytes;
- `.eh_frame`: unchanged at 145,188 bytes; and
- three-block immutable ABBA paired deltas: wall +0.08%, user -0.18%, peak
  RSS +0.06%.  All six outputs per compiler were deterministic.

### 4.12 B4c result

`13fa0a10` emits byte and word zero extension into a 32-bit destination, whose
x86-64 architectural write semantics clear the upper 32 bits.  It also emits
a bare REX prefix for byte registers only when the source encoding requires
SPL/BPL/SIL/DIL or either operand uses an extended register.  Signed extension
continues to use `REX.W`.  The selection is local and constant-time.

No existing checked-in LowIR fixture changed and no existing checked-in MIR
fixture changed.  Existing PA29 extension tests remain the active behavior
gate.  `proposed/pa29/narrow-zero-extension-encoding.t` retains byte/word
`zext` in MIR while manual disassembly proves `movzbl`/`movzwl` destinations;
it remains proposed because native bytes are not an active PA29 oracle.

Validation and frozen evidence:

- PA29: 183/183 assignment tests and 19/19 course tests;
- through PA29: 4,093/4,093;
- full report: 5,171/5,171;
- PA39 file audit: zero fatal findings;
- object: 3,762,128 to 3,760,048 bytes;
- `.text`: 1,031,422 to 1,029,350 bytes;
- `.gcc_except_table`: 73,434 to 73,422 bytes;
- `.eh_frame`: unchanged at 145,188 bytes; and
- three-block immutable ABBA paired deltas: wall -0.33%, user -0.54%, peak
  RSS +0.23%.  All six outputs per compiler were deterministic.

### 4.13 B4d1 result

`551e530f` folds an adjacent `lea address, [base+offset]` and
`load destination, [address+offset]` directly into the load when the address
register is overwritten without being read again.  A block-local proof looks
ahead at most five instructions and admits only write-only `mov`, `load`, and
`lea` definitions.  A call, store, atomic or EH operation, control transfer,
unknown instruction shape, read of the address register, missing bounded
overwrite, displacement overflow, or crossing between local and caller frame
offset namespaces rejects the fold.  Dispatch checks for `lea` before calling
the proof, so non-candidates pay only one opcode comparison.

The frozen MIR contained 3,253 adjacent address/load candidates.  The proof
accepted 1,195: 1,147 address overwrites followed the load immediately, 43
were two instructions later, three were three instructions later, and two
were four instructions later.  It rejected 1,919 at an opcode/shape barrier,
96 on an address-register read, and 43 without a bounded overwrite.  No case
needed the overflow or frame-namespace rejection, but both remain explicit
safety checks.

No existing checked-in LowIR fixture changed and no existing checked-in MIR
fixture changed.  This is a native-encoding-only `-O0` change.  The dumped MIR
for `proposed/pa29/dead-address-load-folding.t` deliberately retains the
adjacent `lea` and `load`; the generated program succeeds.  The reducer stays
proposed because PA29 has no standalone reference implementation and its
active harness does not inspect native instruction bytes, so behavior alone
would add no new correctness coverage.

The address proof is a separate 95-line implementation module.  Two existing
instruction encoders moved to the shared encoding module, leaving
`lowir_native_elf.cpp` at 3,000 lines and the PA39 audit with zero fatal
findings.  Validation and frozen evidence:

- PA29: 183/183 assignment tests and 19/19 course tests;
- through PA29: 4,093/4,093;
- PA29--PA38 report: 1,280/1,280;
- full report: 5,171/5,171;
- object: 3,760,048 to 3,756,128 bytes;
- `.text`: 1,029,350 to 1,025,456 bytes;
- `.gcc_except_table`: 73,422 to 73,406 bytes;
- `.eh_frame`: unchanged at 145,188 bytes;
- three-run internal encoder medians: 263.5 ms baseline and 264.4 ms
  candidate (+0.35%, below the run-to-run noise floor); and
- three-block immutable ABBA paired deltas: wall -0.33%, user -0.27%, peak
  RSS +0.09%.  All six outputs per compiler were deterministic.

### 4.14 B4d2 result

`014b7594` extends the same encoder-only proof to
`mov address, original_address` followed by
`load destination, [address+offset]`.  It emits the load through
`original_address` and omits the copy only when the copied address register is
overwritten without a read within the five-instruction window.  A self-copy
or a load that itself overwrites the copied address is also intrinsically
safe.  The B4d1 barriers remain unchanged.

The frozen MIR contained 574 adjacent copy/load candidates.  The proof
accepted 144: 141 overwrites immediately followed the load, one was two
instructions later, and two were three instructions later.  It rejected 428
at an opcode/shape barrier, one on an address-register read, and one without a
bounded overwrite.

No existing checked-in LowIR fixture changed and no existing checked-in MIR
fixture changed.  `proposed/pa29/dead-address-copy-load-folding.t` retains the
register copy and indirect load in its dumped MIR and its generated program
succeeds.  It remains proposed because the active PA29 lane does not inspect
native bytes and behavior alone duplicates existing indirect-load coverage.

The first implementation screen exposed an approximately 8 ms encoder cost
from moving the hot `setcc` primitive out of the ELF translation unit merely
to preserve the file-size limit.  Keeping that short primitive inline in the
shared encoding header removed the cost.  The planner is additionally called
only for an adjacent `lea`/`mov` plus `load`, reducing possible invocations on
the frozen MIR from 73,102 setup instructions to 3,827 pairs.

Validation and frozen evidence:

- PA29: 183/183 assignment tests and 19/19 course tests;
- through PA29: 4,093/4,093;
- full report: 5,171/5,171;
- PA39 file audit: zero fatal findings;
- object: 3,756,128 to 3,755,792 bytes;
- `.text`: 1,025,456 to 1,025,114 bytes;
- `.gcc_except_table`: unchanged at 73,406 bytes;
- `.eh_frame`: unchanged at 145,188 bytes;
- three-run internal encoder medians: 264.0 ms baseline and 264.8 ms
  candidate (+0.32%, below the run-to-run noise floor); and
- three-block immutable ABBA paired deltas: wall -0.17%, user -0.09%, peak
  RSS +0.12%.  All six outputs per compiler were deterministic.

### 4.15 B4d3 result

`2a9adc71` recognizes `mov address, base`,
`lea address, [address+offset]`, and
`load destination, [address+offset]` as one bounded chain.  It emits the load
directly from `base` with the checked sum of the two displacements when the
address register passes the existing death proof.  This is incremental to
B4d1: without B4d3 the encoder already removes the `lea`, while B4d3 also
removes the preceding copy.

The frozen MIR contained 142 exact chains.  The proof accepted 54: 52 address
overwrites immediately followed the load and two were two instructions later.
The other 88 reached an opcode/shape barrier before a safe overwrite.

No existing checked-in LowIR fixture changed and no existing checked-in MIR
fixture changed.  `proposed/pa29/dead-address-copy-index-load-folding.t`
retains the `mov`/`lea`/`load` chain in its dumped MIR and its generated
program succeeds.  It remains proposed because behavior duplicates active
coverage and PA29 has no native-byte oracle.

The initial implementation made `lowir_native_elf.cpp` 3,008 lines, which is a
fatal audit violation.  Fold planning and emission now live together in the
162-line address-folding module, scalar width ownership moved to the existing
data-layout module, and the ELF emitter is 2,978 lines.  A cheap inline shape
guard prevents an out-of-line planner call for unrelated instructions.

Validation and frozen evidence:

- PA29: 183/183 assignment tests and 19/19 course tests;
- through PA29: 4,093/4,093;
- full report: 5,171/5,171;
- PA39 file audit: zero fatal findings;
- object: 3,755,792 to 3,755,632 bytes;
- `.text`: 1,025,114 to 1,024,952 bytes;
- `.gcc_except_table`: unchanged at 73,406 bytes;
- `.eh_frame`: unchanged at 145,188 bytes;
- three-run internal encoder medians: 254.9 ms baseline and 251.2 ms
  candidate (-1.47%); and
- three-block immutable ABBA paired deltas: wall -0.33%, user 0.00%, peak RSS
  +0.28%.  All six outputs per compiler were deterministic.

### 4.16 B4d4 result

`9082687e` recognizes `mov copied_value, source` followed by a store from
`copied_value`.  It emits the store from `source` and omits the copy only when
the copied register is overwritten without a read within the bounded window.
If the store address itself uses `copied_value`, the address base is rewritten
to `source` as well.

This pattern must coexist with E5--E9 frame forwarding.  A frame store is
therefore rejected when the same frame slot is loaded before the copied
register's overwrite: the precomputed forwarding plan names the original
copied register, which no longer contains the value if the copy is omitted.
An overwrite before any later slot load already prevents delayed forwarding
and is safe.

The frozen MIR contained 2,111 adjacent copy/store candidates.  The proof
accepted 154: 104 overwrites immediately followed the store, 47 were two
instructions later, and three were three instructions later.  It rejected
1,344 on the explicit frame-forwarding barrier, 597 at an opcode/shape
barrier, and 16 without a bounded overwrite.

No existing checked-in LowIR fixture changed and no existing checked-in MIR
fixture changed.  `proposed/pa29/dead-copy-store-folding.t` retains
`mov r8, rax`, `store.i64 [rbx], r8`, and the following overwrite in its
dumped MIR; its generated program succeeds.  It remains proposed because the
runtime behavior is already covered and PA29 has no native-byte oracle.

Validation and frozen evidence:

- PA29: 183/183 assignment tests and 19/19 course tests;
- through PA29: 4,093/4,093;
- full report: 5,171/5,171;
- PA39 file audit: zero fatal findings;
- object: 3,755,632 to 3,755,232 bytes;
- `.text`: 1,024,952 to 1,024,546 bytes;
- `.gcc_except_table`: unchanged at 73,406 bytes;
- `.eh_frame`: unchanged at 145,188 bytes;
- three-run internal encoder medians: 260.2 ms baseline and 250.6 ms
  candidate (-3.69%); and
- three-block immutable ABBA paired deltas: wall -0.33%, user -0.81%, peak
  RSS +0.12%.  All six outputs per compiler were deterministic.

### 4.17 B4d5 result

`40500a27` recognizes `lea address, [base+offset]` followed by a store through
`address`.  It emits the store from `base` with the checked sum of the setup
and store displacements when `address` is overwritten without a read in the
bounded window.  A store whose value is the derived address is rejected, as
are displacement overflow and a transition between local and caller frame
offset namespaces.

The frozen MIR contained 684 adjacent address/store candidates.  The proof
accepted 259: 144 address overwrites immediately followed the store, 80 were
two instructions later, and 35 were three instructions later.  It rejected
424 at an opcode/shape barrier and one without a bounded overwrite.

No existing checked-in LowIR fixture changed and no existing checked-in MIR
fixture changed.  `proposed/pa29/dead-address-store-folding.t` retains an
accepted `lea`/`store` pair and its following overwrite in dumped MIR, and the
generated program succeeds.  It remains proposed because indexed-store
behavior is already covered and PA29 has no native-byte oracle.

Validation and frozen evidence:

- PA29: 183/183 assignment tests and 19/19 course tests;
- through PA29: 4,093/4,093;
- full report: 5,171/5,171;
- PA39 file audit: zero fatal findings;
- object: 3,755,232 to 3,754,304 bytes;
- `.text`: 1,024,546 to 1,023,620 bytes;
- `.gcc_except_table`: unchanged at 73,406 bytes;
- `.eh_frame`: unchanged at 145,188 bytes;
- three-run internal encoder medians: 252.2 ms baseline and 251.3 ms
  candidate (-0.33%); and
- three-block immutable ABBA paired deltas: wall +0.08%, user -0.09%, peak
  RSS +0.21%.  One candidate run encountered obvious intermittent host load
  at 7.47 seconds; the six-run medians remained 6.105 and 6.115 seconds.  All
  outputs per compiler were deterministic.

### 4.18 B4d6 result

`80327487` recognizes `mov address, base` followed by a store through
`address`.  It emits the store through `base` and omits the address copy when
the copied register is overwritten without a read in the bounded window.  If
the copied register is also the stored value, both store operands are
rewritten to `base`; no frozen candidate required that extension.

The frozen MIR contained 546 adjacent address-copy/store candidates.  The
proof accepted 170: 125 address overwrites immediately followed the store,
five were two instructions later, 37 were three instructions later, and three
were four instructions later.  It rejected 372 at an opcode/shape barrier and
four without a bounded overwrite.

No existing checked-in LowIR fixture changed and no existing checked-in MIR
fixture changed.  `proposed/pa29/dead-address-copy-store-folding.t` retains an
accepted pointer copy, indirect store, and immediate overwrite in dumped MIR;
its generated program succeeds.  It remains proposed because indirect-store
behavior is already covered and PA29 has no native-byte oracle.

Validation and frozen evidence:

- PA29: 183/183 assignment tests and 19/19 course tests;
- through PA29: 4,093/4,093;
- full report: 5,171/5,171;
- PA39 file audit: zero fatal findings;
- object: 3,754,304 to 3,753,760 bytes;
- `.text`: 1,023,620 to 1,023,074 bytes;
- `.gcc_except_table`: unchanged at 73,406 bytes;
- `.eh_frame`: unchanged at 145,188 bytes;
- three-run internal encoder medians: 250.8 ms baseline and 252.2 ms
  candidate (+0.56%, about 1.4 ms); and
- three-block immutable ABBA paired deltas: wall -0.57%, user -0.54%, peak
  RSS +0.09%.  All six outputs per compiler were deterministic.

### 4.19 B4d7 result

`53a6f6ea` recognizes `mov address, base; lea address,
[address+index]; store [address+offset], value`.  It combines the two checked
offsets, emits the store directly through `base`, and omits both address
instructions when the derived-address register is overwritten without a read
in the existing five-instruction window.  The stored value may not be that
address register.

The frozen MIR contained 192 exact candidates.  The proof accepted 84: 80
address overwrites immediately followed the store and four were two
instructions later.  It rejected 108 at an opcode or shape barrier.  The
implementation also consolidates the duplicated native/host fold dispatch
into the address-folding module; the hot loop classifies each sequence once
and passes the resulting enum to the out-of-line emitter.

No existing checked-in LowIR fixture changed and no existing checked-in MIR
fixture changed.  `proposed/pa29/dead-address-copy-index-store-folding.t`
retains the accepted `mov`/`lea`/`store` chain in dumped MIR and its generated
program succeeds.  It remains proposed because runtime indirect-store
behavior is already actively covered and PA29 has no native-byte oracle.

Validation and frozen evidence:

- PA29: 183/183 assignment tests and 19/19 course tests;
- through PA29: 4,093/4,093;
- full report: 5,171/5,171;
- PA39 file audit: zero fatal findings; consolidating dispatch leaves
  `lowir_native_elf.cpp` at 2,982 lines;
- object: 3,753,760 to 3,753,440 bytes;
- `.text`: 1,023,074 to 1,022,764 bytes;
- `.gcc_except_table`: unchanged at 73,406 bytes;
- `.eh_frame`: unchanged at 145,188 bytes;
- three-run internal encoder medians: 264.6 ms baseline and 259.4 ms
  candidate (-1.97%); and
- three-block immutable ABBA paired deltas: wall +0.57%, user +0.63%, peak
  RSS +0.07%.  The sub-50-ms differences changed sign between the internal
  encoder and end-to-end lanes and are treated as timing noise.  All six
  outputs per compiler were deterministic.

B4d is closed for baseline emission.  The remaining nearby patterns are
memory operations whose accesses cannot be deleted at `-O0` merely because
their loaded value or stored address is later overwritten: removing them
could erase a required fault or other observable access.  Wider alias-aware
load/store elimination belongs in an explicit optimization level, not in the
baseline encoder.

### 4.20 B5 result

`236f78e7` coalesces consecutive final-layout LSDA call-site ranges when their
landing-pad and action identities are equal.  Ordinary instructions and calls
typed `unwind=no` may lie in the covered gap.  Every potentially unwinding
call without that protection is recorded as an ordered barrier, so extending
a range can never give an unprotected call a handler.  Different landing pads
or action identities also prevent a merge.

The implementation is a single in-place linear scan over call sites and
barriers already ordered by machine emission; it performs no sort, allocation,
fixed-point iteration, rendered-text comparison, or whole-object rescan.  The
scan lives in the separate `lowir_native_lsda.cpp` owner rather than extending
the already-large native emitter.

No existing checked-in LowIR fixture changed and no existing checked-in MIR
fixture changed.  PA31 has no standalone reference binary, so the two new
representation checks remain under `proposed/pa31/`:

- `adjacent-lsda-call-site-coalescing.t` is an end-to-end destructor-unwind
  reducer whose four protected calls become two LSDA entries and whose second
  call still unwinds through the destructor; and
- `lsda-call-site-coalescing-unit.cpp` proves that a potentially throwing
  unprotected gap, different landing pad, or different action identity blocks
  coalescing, while an equal no-throw gap coalesces.

Existing PA31 behavior and inspection fixtures continue to protect typed-catch
action ordering and `_Unwind_Resume`.  Validation and frozen evidence:

- PA31 report: 22/22; PA31 assignment tests: 17/17; course and targeted tests:
  5/5;
- through PA31: 4,218/4,218; full report: 5,176/5,176;
- PA39 file audit: zero fatal findings;
- protected call sites: 5,672 input, 3,294 encoded, 2,378 coalesced;
- object: 3,753,440 to 3,726,504 bytes (-26,936);
- `.gcc_except_table`: 73,406 to 46,470 bytes (-26,936);
- `.text`: unchanged at 1,022,764 bytes with identical section SHA-256;
- `.eh_frame`: unchanged at 145,188 bytes with identical section SHA-256; and
- final three-block immutable ABBA medians: wall 6.105 to 6.125 seconds, user
  5.565 to 5.595 seconds, peak RSS 366,068 to 364,814 KiB.  Paired deltas are
  +0.82%, +0.72%, and -0.39%, respectively, and are timing-neutral under the
  3% gate.  One 8.93-second candidate outlier was retained.

### 4.21 C1 result

C1 required no new compiler change.  The requested frontend construction-time
interning is already part of the baseline:

- `CleanupDispatchCache` fingerprints and verifies the complete ordered action
  identity plus exception-cleanup context, then reuses the existing dispatch
  block for an exact state;
- action identity includes lifetime object, object/function binding, operand
  type, handler-exit state, and cleanup-region-exit state;
- cleanup chains above the bounded eight-action inline threshold are built by
  interning `(action identity, tail block)` nodes; and
- the PA26 architecture audit clears only occupied cache entries, so per-
  function reset is proportional to represented states rather than retained
  table capacity.

The core implementation dates to `c2b6fd68`; `e05062b1` added exception-context
identity and `8fd4193d` completed handler/cleanup-exit identity.  The active
PA26 `200-nested-short-circuit-temporary-cleanup.t` fixture exercises the path:
its telemetry reports eight probes, three cache hits, and five emitted entries,
while its checked-in LowIR and runtime behavior remain clean.  The guarded
local-static cleanup control records three distinct states and correctly gets
zero hits.

No existing checked-in LowIR fixture changed and no existing checked-in MIR
fixture changed in this run because no implementation was altered.  Attempting
to discover and merge additional arbitrary suffixes after construction would
be LowIR-visible optimizer work; it remains C2 at PA37 O1 rather than a second
baseline cache.  The full 5,176/5,176 report and fatal-clean PA39 file audit
from the immediately preceding B5 gate cover the unchanged implementation.

### 4.22 C2a result

`d3b9eca0` adds a bounded PA37 O1 transform that shares terminal `resume`
blocks inside one function.  It first counts blocks without allocating.  Only
functions with at least two one-instruction resume blocks build the typed
debug-location map and removal bitmap.  Equal blocks are redirected through
normal control-flow operands and EH target operands, then duplicate blocks are
removed.  The pass does not render or compare LowIR text, discover arbitrary
suffixes, iterate to a fixed point, or run at `-O0`.

The frozen O1 compile visits 22,188 blocks in 4,578 functions and removes
three resume blocks.  The pass itself takes about 1.585 ms.  Its deterministic
object changes are deliberately small:

| Metric | C1/B5 baseline | C2a | Delta |
| --- | ---: | ---: | ---: |
| object bytes | 3,619,144 | 3,619,024 | -120 |
| `.text` bytes | 932,780 | 932,744 | -36 |
| `.gcc_except_table` bytes | 45,601 | 45,597 | -4 |

The active PA37 O1 `340-share-terminal-resume.t` fixture has two distinct
cleanup predecessors and verifies that both target one retained resume block.
This is intentional optimized-LowIR coverage.  **No `-O0` output changed, no
existing checked-in LowIR fixture changed, and no existing checked-in MIR
fixture changed.**  Consequently there is no baseline fixture rewrite to
record; the only new exact layout is the new PA37 O1 fixture.

Two independent three-block immutable ABBA measurements are retained because
host peak RSS varied materially.  The first measured baseline/candidate wall
medians of 7.330/7.290 seconds and user medians of 6.680/6.635 seconds, with
paired deltas of -0.61% wall, -0.60% user, and +3.31% RSS.  The immediate
recheck measured 7.260/7.260 seconds wall and 6.605/6.625 seconds user, with
paired deltas of 0.00% wall, 0.00% user, and +1.36% RSS.  Neither sample shows
a compile-time regression; both RSS results are recorded rather than choosing
the lower one.

Validation is PA37 88/88, through PA37 5,151/5,151, full report
5,177/5,177, and a PA39 file audit with zero fatal findings.  Broader
bottom-up cleanup-tail sharing remains C2b and must receive its own fixture,
timing, and full-report gate.

### 4.23 C2b result

`9bf96710` extends the PA37 O1 cleanup owner with exact structural suffix
sharing.  It hash-conses each typed instruction together with the already-
interned suffix identity, so discovery is an expected-linear reverse scan and
hash collisions receive a full typed comparison.  The key covers types,
operands, call signatures and effects, EH selectors, and exact debug locations;
it never renders LowIR text.  A no-allocation block-count precheck excludes
functions that cannot contain a pair.

The accepted transform is intentionally narrower than the first prototype:

- blocks must be direct cleanup landing pads, contain no EH structure, and end
  in `resume`;
- the shared suffix must exactly equal a complete shorter landing-pad block,
  so the pass never creates a synthetic block or jumps into the middle of an
  EH region;
- a compact typed EH-state walk interns the active protected-region context,
  and every shared occurrence must have the same context; and
- each noncanonical landing pad remains as an ownership-preserving wrapper
  that jumps to the canonical pad.

The importance of the context condition was measured rather than assumed.  An
initial exact-text prototype changed one nested initializer-list cleanup in
`main` and failed the existing PA36
`700-hosted-unordered-set-constructor-runtime.t`: the native verifier reported
active EH states 1 and 0 converging at one landing pad.  The corrected pass
keeps that pair separate.  The new PA37 O1
`350-share-exact-cleanup-tail.t` fixture contains a positive shared destructor
suffix, a different-operand negative case, and this nested-EH-context negative
case.  Compiling its optimized LowIR through the native backend also succeeds.

Frozen O1 telemetry is:

| Metric | C2a baseline | C2b | Delta |
| --- | ---: | ---: | ---: |
| output LowIR instructions | 151,590 | 151,494 | -96 net |
| exact suffix groups shared | 0 | 50 | +50 |
| blocks rewritten | 0 | 83 | +83 |
| suffix instructions removed | 0 | 97 | -97 |
| `_Unwind_Resume` call sites | 1,619 | 1,537 | -82 |
| PLT32 call relocations | 29,772 | 29,593 | -179 |
| object bytes | 3,619,024 | 3,613,040 | -5,984 |
| `.text` bytes | 932,744 | 931,070 | -1,674 |
| `.gcc_except_table` bytes | 45,597 | 45,579 | -18 |
| `.eh_frame` bytes | 143,776 | 143,776 | 0 |

The net LowIR delta is one smaller than the tail-pass count because the EH
context correction retains one terminal resume block that C2a had previously
merged without proving compatible ownership.  On the final frozen run, the
context-aware terminal pass takes about 4.74 ms and exact tail sharing takes
about 10.42 ms.

The three-block immutable ABBA ran with host load1 falling from 6.64 to 3.57,
so only the interleaved paired result is used.  Baseline/candidate medians are
7.235/7.265 seconds wall, 6.590/6.610 seconds user, and 417,614/409,938 KiB
peak RSS.  Paired deltas are +0.28% wall, +0.30% user, and -2.42% RSS: timing-
neutral under the 3% gate.

PA37 passes 89/89, through PA37 passes 5,152/5,152, the PA37 debug lane passes
14/14, the full report passes 5,178/5,178, and the PA39 file audit has zero
fatal findings.  **O0 output is unchanged.  No existing checked-in LowIR
fixture changed and no existing checked-in MIR fixture changed.**  The only
new exact layout is the PA37 O1 fixture above.  Alpha-equivalent temporary
renaming and cross-context ownership remain C2c rather than being folded into
this proven bounded pass.

## 5. Execution plan

Each numbered implementation item is a separate compiler changeset.  Tests and
tracker updates may be committed with the change they protect, except that T1
is an independent tests-only changeset.

### Phase 0: establish tests and comparable timing

1. Add the E4--E9 reducers described above, using PA29's existing behavior and
   reference workflow rather than inventing a new harness.  Use PA31/PA32
   object inspection only where a stable property cannot be observed in PA29.
2. Record immutable baseline and candidate compiler paths and use the checked-in
   `scripts/run_ab_compile_benchmark.py`.  Never rebuild either compiler during
   its timing window.
3. Rebaseline `semantic_overload.cpp` at `-O0`, `-O1`, and `-O2` using three
   interleaved rounds each for cppgm++, GCC, and Clang.  Record wall, user, sys,
   peak RSS, object bytes, section sizes, defined function bindings, and object
   hashes.
4. Rebaseline `lowir_opt.cpp` with the host-built and self-hosted compilers at
   all three levels.  Retain phase telemetry, instruction/function/call counts,
   and a short CPU sample for later attribution.
5. Correct the PA39 jemalloc link ordering as a separate build-system
   changeset if the self-hosted binary still lacks a `NEEDED` entry while the
   host-built compiler has one.  Libraries must follow the link inputs (or be
   bracketed by an explicit linker retention option), and the result must be
   verified with `readelf`.  This changes the compiler runtime, not generated
   object semantics, and gets its own A/B timing and inception gate.

Timing jobs never run concurrently with compiler builds, test reports,
inception, sampling, or disassembly.

The historical under-15-second frozen goal is already met.  Fifteen seconds
remains an absolute regression alarm, while the tighter 3% paired timing rule
below is the normal acceptance gate.

### Phase 1: compact x86 memory displacements

Centralize displacement selection in the shared x86 memory encoder introduced
by `fb07fb77`:

- use no displacement when the value is zero and the ModRM/SIB base permits
  `mod=00`;
- use a sign-extended disp8 for values in `[-128, 127]`;
- otherwise use disp32;
- retain the required displacement for RBP/R13 in `mod=00` form;
- retain SIB for RSP/R12 while still selecting the shortest legal
  displacement;
- retain disp32 for no-base and RIP-relative forms; and
- emit the selected form directly so later per-function branch relaxation sees
  the true offsets.  Do not add a displacement-specific byte-deletion pass or
  whole-buffer rescan.

Add PA29 boundary coverage for `-129`, `-128`, `-1`, `0`, `1`, `127`, and
`128`, including base-register encodings that require special ModRM/SIB forms.
The reducer must execute loads and stores, not merely disassemble them.  Add a
stable inspection check only if the reference workflow agrees.

Acceptance requires zero existing LowIR/MIR fixture changes, all 1,274
PA29--PA38 report tests, a full report, deterministic object bytes, and an
incremental reduction close to the prototype after accounting for any changed
baseline.  Encoding time must remain linear per function.

### Phase 2: remove branches to the next encoded instruction

Extend the current linear branch compaction/layout step so an unconditional
local branch emits zero bytes when its resolved target is the next encoded
instruction.  This is separate from PA38 O1's MIR fallthrough cleanup: `-O0`
MIR remains unchanged, while final machine layout chooses the empty encoding.

The implementation must use the same one-pass candidate collection and
offset-translation machinery as E3P.  It must not erase bytes and rescan the
whole object for each branch.  Labels, relocations, LSDA protected ranges,
landing pads, line tables, and backward branches are explicit tests.  A PA29
runtime reducer covers conditional paths that join through an omitted branch;
PA31 covers an EH-bearing boundary; the existing PA38 O1 fallthrough test
continues to prove the MIR-level form.

The frozen structural check records eliminated branches and bytes.  The
measured upper bound is 9,399 branches and 18,798 bytes before earlier new
changes are applied.

### Phase 3: constant division and remainder selection

Treat constant division as target instruction selection at baseline, matching
what GCC already does at `-O0`.  Implement it in small changesets:

1. Unsigned division by a power of two becomes a logical shift; unsigned
   remainder becomes a mask.
2. Signed division by a positive power of two uses the required negative-value
   bias and arithmetic shift so the quotient truncates toward zero.  Signed
   remainder is reconstructed from `n - q*d`.
3. Add negative power-of-two divisors only after the positive signed cases are
   complete and tested.
4. Consider general invariant division using a proven multiply-high magic
   algorithm only after the power-of-two changes show material remaining
   benefit.  Keep precomputed constants local to the instruction; do not add a
   whole-function dataflow pass.

Tests cover i8/i16/i32/i64 and unsigned counterparts, zero, positive and
negative nonmultiples, extrema, and divisors `1`, `2`, `4`, large powers, and
`-1` where the language/LowIR operation is defined.  Do not turn an existing
hardware trap or undefined signed overflow case into a new observable
contract.  Quotient and remainder cases must verify both the numerical result
and surrounding register values.

The preferred implementation recognizes constant facts already available to
native selection and preserves MIR.  If recovering the constant requires a
narrow PA29 MIR selection change, run the existing fixtures first and record
every changed path in the tracker.  Broad LowIR movement is an immediate
deferral under section 4.1.

### Phase 4: small x86 instruction-selection changes

Implement and measure one pattern at a time.

1. Replace zero moves with a zeroing idiom only when a backward, block-local
   flag-liveness proof shows that clobbering flags is harmless.  The proof is a
   single linear pass over known flag definitions, uses, and barriers; an
   unknown instruction blocks the rewrite.
2. Replace compare-to-zero with `test` only for whitelisted widths and branch
   conditions whose x86 flags are equivalent.  Keep compare when AF or an
   unmodeled consumer could matter.
3. Use the narrowest valid zero-extension encoding from explicit operand-width
   facts.  Tests exercise high registers, partial-register semantics, and
   values with set upper bits.
4. Extend E5--E9 with one redundant address/load/store-copy shape at a time.
   Reuse the existing per-function use census and five-instruction bounded
   window.  Calls, stores, EH markers, volatile/atomic operations, aliasing
   memory, source redefinitions, and unknown instructions remain barriers.

No pattern may add a fixed-point scan.  Each records candidate count, accepted
count, bytes saved, encoder time, and the exact negative cases that justify its
barriers.

### Phase 5: baseline EH metadata compaction

Coalesce adjacent final LSDA call-site entries when they cover contiguous code
ranges and have identical landing-pad and action identities.  A gap may be
included only if it contains no instruction that can unwind.  Work from final
machine layout and typed EH roles, never source text or symbol spelling.

Use one linear scan over already ordered call sites.  PA31 inspection tests
cover:

- equal adjacent cleanup entries that coalesce;
- different landing pads or action chains that do not coalesce;
- a throwing gap that prevents coalescing;
- a no-throw gap that may coalesce; and
- preserved typed-catch/action ordering and `_Unwind_Resume` behavior.

Because PA31 has no standalone reference binary, new exact coalescing facts
remain proposed unless the upstream assignment fixtures already contain the
same case.  Existing PA31 active behavior and EH-fact tests still gate the
implementation.

This is valid at `-O0` because it is a canonical encoding of the same unwind
facts.  It must change neither LowIR nor MIR.

### Phase 6: cleanup sharing, with O0 triage first

First isolate repeated cleanup generation with reducers derived from the
largest frozen call-count gaps.  Distinguish two implementations:

1. A frontend lowering builder may intern an already-identical cleanup state
   by `(action identity, next cleanup identity, EH context/selector)`.  This is
   expected O(1) per action using compact typed identities and a per-function
   arena/map.  It is baseline-eligible only if it directly avoids creating
   duplicate nodes and causes narrow, reference-compatible LowIR changes.
2. A pass that discovers equal tails after LowIR construction belongs to PA37
   O1.  Hash-cons block suffixes bottom-up, share one terminal resume block per
   compatible EH context, and redirect predecessors.  Do not compare rendered
   instruction text or repeatedly compare whole suffix vectors.

Run the PA26 owner report immediately after the O0 prototype.  If LowIR fixture
movement is broad, record all counts, revert it, and mark C1 deferred.  Then
implement the PA37 O1 form with explicit optimized LowIR tests.  The existing
proposed PA26 constructor-unwind reducer remains evidence that two correct O0
layouts can differ; it is not authority to rewrite checked-in fixtures.

Acceptance records destructor and `_Unwind_Resume` call counts, cleanup block
counts, text, LSDA, `.eh_frame`, and compile time.  Sharing must not combine
different live-object sets, construction progress, handlers, selectors,
cleanup ownership, or resume destinations.

### Phase 7: typed demand and remaining definition audit

Do not infer deadness from GCC's symbol count.  Extend diagnostic telemetry to
classify every retained frozen definition by compact typed root/reason:

- externally visible strong definition;
- address or relocation use;
- direct call edge;
- vtable/RTTI/thunk/constructor/destructor lifecycle edge;
- EH cleanup/runtime edge;
- required weak/COMDAT ownership; or
- retained only by a conservative fallback.

The current post-force-inline native pass and its 683-function reduction are
the starting point.  Any conservative fallback bucket gets a reducer at the
earliest semantic owner before changing demand.  A true missing-demand fix is
baseline correctness.  A definition that becomes unreachable only because an
optional PA37 inline occurred is removed only for `-O1/-O2`, after that
inlining, using the existing typed reachability model.

The optional post-inline pass must retain strong externally visible symbols,
address-taken functions, aliases, object metadata roots, lifecycle variants,
EH actions, and all reachable weak definitions.  Its complexity is O(V + E)
over functions and typed references.  PA37 object-roundtrip tests and PA32
symbol inspection cover the difference between an unowned weak helper and an
externally observable weak definition.

### Phase 8: make PA37 O1 inlining effective and cheap

Reorder existing work rather than stacking extra whole-program passes:

1. Run the existing bounded simplify/DCE/CFG preparation once per function
   before computing inline summaries.
2. Build direct-call edges, SCCs, EH/no-unwind facts, simplified instruction
   costs, return shape, and address-taken/linkage facts once.
3. Process the acyclic SCC graph bottom-up.  Recursive SCCs remain protected by
   the existing recursion rule and growth budget.
4. Strip proven no-unwind EH scaffolding before callers test eligibility.
5. Batch small leaf and wrapper calls using cached summaries.  Permit a wrapper
   containing a call after its callee has already been processed, while keeping
   a hard per-caller growth cap.
6. Put only changed callers on a local simplify/DCE/CFG worklist after
   inlining.  Do not rerun all passes over untouched functions.
7. At the final native boundary, run the O1 weak-reachability closure described
   in Phase 7.

Add PA37 course reducers for:

- a callee that becomes inlineable only after local simplify/DCE;
- a wrapper whose safe EH markers disappear before eligibility;
- a three-function chain in adversarial source order;
- a small wrapper containing an already-processed direct call;
- multiple calls that hit the growth budget without quadratic copying;
- recursion/SCC rejection; and
- native object removal of a weak helper after its last call is inlined, while
  preserving address-taken and strong controls.

Record call visits, successful calls, budget skips, cloned instructions,
changed callers, post-inline function reachability, pass runs, and worklist
pushes.  These counters must demonstrate bounded work.  The initial target is
not a more permissive heuristic by itself; it is fewer self-host calls and less
code for equal or lower optimizer CPU time.

### Phase 9: profile-gated O2 work

Reprofile the self-hosted `lowir_opt.cpp` compile after the baseline and O1
phases.  Choose the next O2 changes from measured generated-code cost, in this
order:

1. Extend scalar slot promotion only where PA37 telemetry shows remaining
   nonescaping load/store traffic.  Use dominance and def/use worklists; never
   rescan the complete function after each promoted slot.
2. Add bounded value numbering, loop-invariant motion, or induction/strength
   reduction only when a sampled hot function and a reducer demonstrate the
   opportunity.  Each gets its own PA37 O2 test and monotonic work measure.
3. If spill/reload traffic remains dominant, add a PA38 O2 linear-scan or
   similarly near-linear allocation improvement.  Preserve PA38 O0/O1 MIR,
   update only O2 expectations through the reference workflow, and retain the
   late E5--E9 encoder proofs as safety nets.
4. Revisit trace layout and callee-save selection only after value placement;
   otherwise layout measurements are confounded by a changing instruction
   stream.

Do not introduce a distinct, more expensive O3 in this batch.  `-O2/-O3` stay
on the same bounded maximum level until level 2 is both effective and faster
than GCC to compile the comparison inputs.

## 6. Correctness and performance gates

### 6.1 Per changeset

For an owner PA N:

1. Run the smallest reducer directly while developing.
2. Generate any oracle only with the documented `ref-test-paN` path.
3. Run `make test-paN`.
4. Run `make test-report-through-paN`, as required by the assignment exit
   contract.
5. Run root `make test-report` with explicit owning and downstream PA names so
   the complete failure set is visible, for example:

   ```sh
   make test-report ACTIVE_TEST_REPORT_PAS='pa29 pa31 pa37 pa38'
   ```

6. Run the frozen size/count check and an interleaved immutable A/B timing
   screen.
7. Run the full root `make test-report` and require every test to pass before
   committing.
8. Run `perl scripts/cppgm_file_audit.pl --stage pa39 --paths dev/src` plus any
   other touched implementation path.  Separate files/source sets as needed
   until fatal findings are zero.
9. Update the live tracker with fixture paths, reference disposition, size,
   time, and decision.
10. Commit the single changeset and push it before beginning the next retained
   compiler change.

If a PA36, PA37, PA38, or inception failure exposes an earlier compiler defect,
the reducer is moved to the earliest assignment that owns the behavior before
the fix is accepted.

### 6.2 Timing acceptance

All timings use immutable compilers, the same input and flags, sequential
runs, the same CPU affinity where available, and three or more interleaved
rounds.  Record both wall and user time because the host is intermittently
loaded.  Discard an entire declared window, not an inconvenient individual
sample, when unrelated load makes it incomparable.

For each baseline backend changeset:

- deterministic output and structural savings are mandatory;
- a median user-time regression above 3% is a rejection unless a repeated
  interleaved block shows a larger end-to-end compiler improvement elsewhere;
- encoder/pass work counters must remain linear or near-linear; and
- `-O0` total compile time must remain below GCC in the same comparison window.

At each O1/O2 milestone, compare cppgm++, GCC, and Clang in alternating order
at the matching level.  The principal performance contract is total cppgm++
compile time, not an isolated optimizer timer: cppgm++ must remain faster than
GCC at `-O0` and become faster at `-O1` and `-O2`.  Also record incremental
optimizer cost relative to cppgm++ `-O0`, object size, and generated-code
runtime so an apparently fast optimizer cannot pass by doing no useful work.

Use inception only after the straightforward baseline batch, after the O1
batch, and at final completion unless a self-host correctness failure requires
an earlier run.  `make test-report` is the ordinary faster correctness gate.

### 6.3 Self-host and final gate

Before entering the self-build lane, the full root `make test-report` must be
clean.  Do not build or test anything else during a timed self/inception run.

The final sequence is:

1. clean and perform a timed from-scratch PA39 `cppgm++-self` build;
2. perform a timed clean 8-worker inception generation/compare and record peak
   RSS;
3. clean the inception objects;
4. perform a timed clean 32-worker inception generation/compare and record
   peak RSS;
5. require every inception object to match and the self/inception binaries to
   be byte-identical;
6. rerun the zero-fatal file audit; and
7. run the final frozen A/B/C `-O0/-O1/-O2` medians and `lowir_opt.cpp`
   self-host comparison with no overlapping load from builds.

The batch is complete only when the reports, audit, self build, both inception
lanes, fixture tracker, deterministic objects, and comparative timing tables
are all recorded in this document and every retained changeset is committed
and pushed.

## 7. Stop conditions

Stop and defer a candidate when any of the following occurs:

- broad `-O0` LowIR fixture movement;
- unexplained or reference-incompatible existing MIR fixture movement;
- a required oracle would have to be edited by hand;
- a whole-program or per-function pass lacks a stated complexity bound;
- a local rewrite requires repeated complete rescans;
- frozen output is nondeterministic;
- median compiler time regresses beyond the timing gate;
- a definition lacks an understood typed demand decision after the change;
- EH sharing changes live-object ownership, handler/action identity, or unwind
  behavior; or
- full `make test-report` or the fatal file audit is not clean.

Record deferred experiments in the tracker with enough evidence that they do
not need to be rediscovered.

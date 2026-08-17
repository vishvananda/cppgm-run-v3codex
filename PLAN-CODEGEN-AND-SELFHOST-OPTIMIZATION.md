# Plan: Baseline Code Generation and Optimized Self-Host Performance

Status: in progress; B1 and B2 complete

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

### 4.3 Live candidate tracker

Update this table immediately after each experiment, including reverted work.
Do not carry an unrecorded fixture-changing experiment into the next
candidate.

| ID | Candidate | Expected LowIR delta | Expected MIR delta | Initial status |
| --- | --- | ---: | ---: | --- |
| T1 | E4--E9 reducer backfill | 0 existing | 0 existing | Planned, tests only |
| P0 | Retain jemalloc in the PA39 self link | 0 | 0 | Planned build-system parity check |
| B1 | Compact memory displacements | 0 existing | 0 existing | **Landed** in `e46bde65`; deterministic object -157,352 bytes and text -156,330 bytes; behavior reference agrees while its unrelated MIR layout differs |
| B2 | Omit encoded jump to next instruction | 0 existing | 0 existing | **Landed** in `acf3d415`; 9,399 jumps and 18,798 text bytes removed with timing neutral |
| B3a | Unsigned power-of-two division/remainder | 0 existing | 0 existing | **Landed** in `65b62af8`; PA29 encoder peephole, active behavior reducer, frozen object byte-identical, timing neutral |
| B3b | Signed power-of-two division/remainder | 0 preferred | 0 preferred; list narrow movement if unavoidable | Planned after B3a |
| B3c | General constant division using multiply-high magic | 0 preferred | 0 preferred; list narrow movement if unavoidable | Deferred until B3a/B3b evidence |
| B4a | Flag-safe zero materialization | 0 | 0 | Planned |
| B4b | `cmp reg, 0` to `test reg, reg` | 0 | 0 | Planned |
| B4c | Narrow zero-extension encodings | 0 | 0 | Planned |
| B4d | Remaining bounded address/load/store folding | 0 | 0 | Planned one pattern at a time |
| B5 | Adjacent LSDA call-site coalescing | 0 | 0 | Planned |
| C1 | Direct cleanup-state suffix interning | Narrow only | 0 unless native consequences move it | Probe; defer on broad LowIR movement |
| C2 | General cleanup-tail and terminal-resume sharing | Intentional at O1 | Downstream only | Planned for PA37 O1 |
| D1 | Typed audit of remaining emitted definitions | 0 | 0 | Planned diagnosis |
| O1a | Simplify before inlining and bottom-up scheduling | Intentional at O1 | Downstream only | Planned |
| O1b | Post-optional-inline weak reachability | O1 native output only | Downstream only | Planned after O1a |
| O2a | Expanded slot/value promotion | Intentional at O2 | Downstream only | Profile-gated |
| O2b | Bounded improved register allocation | LowIR unchanged | Intentional at O2 | Profile-gated PA38 work |

For an accepted row, replace `Initial status` with the exact owner reports,
fixture counts and paths, frozen size/time delta, reference disposition,
commit, and final decision.  For a deferred row, record the same evidence and
the reason for deferral.

### 4.4 B1 result

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

### 4.5 B2 result

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

### 4.6 B3a result

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

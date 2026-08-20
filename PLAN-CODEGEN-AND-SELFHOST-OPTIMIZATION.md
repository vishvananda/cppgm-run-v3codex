# Plan: Baseline Code Generation and Optimized Self-Host Performance

Status: in progress; T1, P0, B1, B2, B3a, B3b, B3n, B3c, B4a, B4b, B4c, B4d1--B4d7, B5, B6, B7e, C1, C2a--C2b, D1, O1a, and R1--R3 complete; VP0--VP5 tracked in `PLAN-O0-VALUE-PLACEMENT.md`

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

References are generated only through the documented `ref-test` target.  All
retained regressions go in `cppgm.tests/course/paN/`.  A behavior-only test
belongs in the active course suite when it protects a real correctness
boundary; PA29 keeps reference MIR as an informational artifact without
grading its exact shape.  If the course adopts a representation that differs
from the pinned reference, update the public contract and authoritative
reference before activating an exact structural fixture.

PA26 and PA31 explicitly have no standalone reference binary.  For those
assignments, exact reference authority comes from an existing checked-in or
upstream assignment fixture.  New correctness coverage should use the active
course behavior lane unless an authoritative public layout/inspection oracle
exists; do not manufacture a `.ref` sidecar from our own output.

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
| E4 packed constant byte stores | Keep the existing runtime reducer; add an exact-shape reducer only if an authoritative inspection lane can prove it without binding register allocation | PA29 behavior course lane with informational MIR |
| E5 immediate 64-bit frame reload forwarding | Store is still observable while the reload result is consumed; include same-register and different-register forms | PA29 behavior course lane; exact representation only with an authoritative structural oracle |
| E6 single-use temporary-home elision | Adjacent single store/load positive case plus a second-use negative case | PA29 behavior course lane; exact frame omission only with an authoritative structural oracle |
| E7 one independent instruction | Positive load/LEA/move separators and call/store/source-redefinition barriers | PA29 behavior active; optimization-shape proof follows reference routing |
| E8 two-to-five independent instructions | Positive distances 2 and 5, distance 6 boundary, and a barrier in the window | PA29 behavior active; optimization-shape proof follows reference routing |
| E9 narrow reload forwarding | i8/i16/i32 cases that expose partial-register, zero-extension, and upper-bit behavior | PA29 behavior active; this is correctness coverage even without exact MIR enforcement |

One reducer should not be forced to prove multiple changesets when separate
small inputs make a regression attributable.  E4 is active behavior coverage:
execution proves the native folding is safe while its MIR remains
informational rather than an exact optimization oracle.

T1 landed in `ce76bd95`.  E4 is covered by the active PA29 behavior
`constant-byte-store-coalescing` witness because its only distinguishing
property is the native encoding.  Five PA29 course behavior tests cover
E5--E9:

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
| T1 | E4--E9 reducer backfill | 0 existing | 0 existing | **Landed** in `ce76bd95`; five PA29 behavior reducers reference-agree, E4's byte-shape witness is active behavior coverage with informational MIR, full report 5,176/5,176 |
| P0 | Retain jemalloc in the PA39 self link | 0 existing | 0 existing | **Landed** in `c6ee9be9`; exact-object A/B improves self-host frozen wall 1.25%, user 1.71%, and peak RSS 3.17%; 8-way inception matches; full report 5,176/5,176 |
| B1 | Compact memory displacements | 0 existing | 0 existing | **Landed** in `e46bde65`; deterministic object -157,352 bytes and text -156,330 bytes; behavior reference agrees while its unrelated MIR layout differs |
| B2 | Omit encoded jump to next instruction | 0 existing | 0 existing | **Landed** in `acf3d415`; 9,399 jumps and 18,798 text bytes removed with timing neutral |
| B3a | Unsigned power-of-two division/remainder | 0 existing | 0 existing | **Landed** in `65b62af8`; PA29 encoder peephole, active behavior reducer, frozen object byte-identical, timing neutral |
| B3b | Signed positive power-of-two division/remainder | 0 existing | 0 existing | **Landed** in `b37a6a93`; encoder peephole plus explicit-extension correction, active behavior reducers, frozen object -112 bytes, timing neutral |
| B3n | Signed negative power-of-two division/remainder | 0 existing | 0 existing | **Landed** in `e9cf0de9`; active behavior reducer, `-1` intentionally retained, frozen object byte-identical, timing neutral |
| B3c | General constant division using multiply-high magic | 0 preferred | 0 preferred; list narrow movement if unavoidable | **Landed** in `b7c61cc5` and `796de10c`; signed quotient/remainder and unsigned quotient/remainder are complete, no existing fixture movement, four active PA29 behavior/O2 reducers, frozen hardware divides 163 to 160, host-compiler wall -1.67%, and generated-self wall +0.64% (neutral) |
| B4a | Flag-safe zero materialization | 0 existing | 0 existing | **Landed** in `edd35810`; linear per-block flag liveness, active PA29 behavior reducer, frozen object -11,048 bytes, timing neutral |
| B4b | `cmp reg, 0` to `test reg, reg` | 0 existing | 0 existing | **Landed** in `fba50ba6`; width-aware native selection, active PA29 behavior reducer, frozen object -8,104 bytes, timing neutral |
| B4c | Narrow zero-extension encodings | 0 existing | 0 existing | **Landed** in `13fa0a10`; 32-bit `movzx` destinations and selective REX, active PA29 behavior reducer, frozen object -2,080 bytes, timing neutral |
| B4d1 | Fold a dead adjacent address calculation into its load | 0 existing | 0 existing | **Landed** in `551e530f`; 1,195 bounded proofs, active PA29 behavior reducer, frozen object -3,920 bytes, timing neutral |
| B4d2 | Fold a dead adjacent address-register copy into its load | 0 existing | 0 existing | **Landed** in `014b7594`; 144 bounded proofs, active PA29 behavior reducer, frozen object -336 bytes, timing neutral |
| B4d3 | Fold a dead copy/index/load chain | 0 existing | 0 existing | **Landed** in `2a9adc71`; 54 bounded proofs, active PA29 behavior reducer, frozen object -160 bytes, timing neutral |
| B4d4 | Fold a dead value copy into its store | 0 existing | 0 existing | **Landed** in `9082687e`; 154 bounded proofs after frame-forwarding barriers, active PA29 behavior reducer, frozen object -400 bytes, timing neutral |
| B4d5 | Fold a dead address calculation into its store | 0 existing | 0 existing | **Landed** in `40500a27`; 259 bounded proofs, active PA29 behavior reducer, frozen object -928 bytes, timing neutral |
| B4d6 | Fold a dead address-register copy into its store | 0 existing | 0 existing | **Landed** in `80327487`; 170 bounded proofs, active PA29 behavior reducer, frozen object -544 bytes, timing neutral |
| B4d7 | Fold a dead copied-and-indexed address into its store | 0 existing | 0 existing | **Landed** in `53a6f6ea`; 84 bounded proofs, active PA29 behavior reducer, frozen object -320 bytes, timing neutral; closes the safe baseline B4d inventory |
| B5 | Adjacent LSDA call-site coalescing | 0 existing | 0 existing | **Landed** in `236f78e7`; 5,672 protected calls become 3,294 LSDA entries, frozen object/LSDA -26,936 bytes, active PA31 end-to-end safety family, timing neutral |
| B6 | Put weak function bodies in real ELF COMDAT groups | 0 existing | 0 existing | **Landed** in `90368327`; actual weak code and relocation sections now share the function COMDAT, FDEs follow the selected code, a reference-agreeing PA32 reducer checks both membership facts, compiler `size` text -7,295,360 bytes, full report 5,186/5,186, zero-fatal audit, and clean object/final-binary inception comparison pass |
| B7 | Reduce residual copy, frame-home, and address-materialization traffic | 0 preferred | 0 at baseline; intentional only if assigned to PA38 | **Reprofiled after B6**: same-name functions account for about 2.25 MB of the 2.94 MB residual machine-text gap; `mov` and `lea` account for 2.20 MB of the parsed instruction-byte delta, led by register copies, stack homes/reloads, and materialized addresses; prove bounded zero-MIR cases first, then put representation-changing coalescing in PA38 |
| B7a | Omit a proved redundant 32-bit normalization | 0 existing | 0 existing | **Landed** in `0f01fa1a`; an immediately preceding non-forwarded 32-bit producer proves that the register's upper half is already zero, identical-source O0 self build machine text -41,856 bytes and 16,107 same-register moves, frozen object text -347 bytes, paired compile time neutral |
| B7b | Extend the proof through frame-load forwarding | 0 existing | 0 existing | **Landed** in `58aa9b65`; ordinary 32-bit frame loads and different-register forwarded reloads normalize their destination, while same-register forwarding retains the required operation; identical-source O0 self machine text -67,536 bytes and 26,719 same-register moves, frozen text -156 bytes, paired compile time neutral |
| B7c | Fuse register copy followed by 32-bit normalization | 0 existing | 0 existing | **Landed** in `9a929862`; a single 32-bit register move has the exact final value and flags of the adjacent 64-bit copy plus normalization; identical-source O0 self machine text -34,224 bytes and 11,721 same-register moves, frozen text -192 bytes, paired compile time neutral |
| B7d | Retain a direct parameter in an unclobbered incoming ABI register | 0 existing | 10 existing PA29 MIR fixtures | **Deferred**: a bounded clobber proof removed 325 MIR instructions and 2,391 text bytes from the O0 `lowir_opt.cpp` sample, and all affected programs remained correct, but it moved 2 strict, 7 structural, and 1 behavior MIR oracles without a PA29 reference workflow that authorizes regeneration |
| B7e | Fold a consumed transient R11 address setup | 0 existing | 0 existing | **Landed** in `10bdd7ad`; one linear backward use/definition plan per MIR block proves whether an adjacent R11 setup has another reader; O0 `lowir_opt.cpp` text -2,289 bytes and 607 stack LEAs, frozen text -2,626 bytes, timing neutral |
| B7f | Admit R10 as a persistent register in leaf functions with no R10 scratch operations | 0 existing | 14 existing PA29 MIR fixtures | **Deferred**: the corrected function-wide scratch proof preserved all PA29 behavior but moved 10 strict and 4 structural oracles for only 1,715 text bytes and about 600 push/pop instructions on O0 `lowir_opt.cpp`; this is too little return for baseline MIR movement |
| B7g | Coalesce local values only in single-block native encoding copies | 0 existing | 0 existing | **Landed** in `a5777d1e`; a linear forward value pass plus backward dead-definition pass removes 5,123 frozen x86 instructions and 14,586 text bytes, an exact nonserialized call-argument mask preserves ABI live-ins, all existing MIR is unchanged, and paired compile time is neutral |
| R1 | Reserve a reused incoming ABI argument register through its first call | 0 existing | 0 existing | **Landed** in `df01fb99`; active PA29 indirect-call behavior reducer, no existing fixture changed |
| R2 | Reject incoming-register forwarding after an earlier physical clobber | 0 existing | 0 existing | **Landed** in `df01fb99`; fixed 16-entry first-clobber table, active PA29 object-copy behavior reducer, no existing fixture changed |
| R3 | Keep `_Unwind_Resume` outside coalesced protected LSDA ranges | 0 existing | 0 existing | **Landed** in `f7946c1b`; active PA31 exactly-once `noexcept` cleanup reducer, no existing fixture changed |
| C1 | Direct cleanup-state suffix interning | Pre-existing LowIR shape | Pre-existing downstream shape | **Already landed** in the PA17/PA26 baseline (`c2b6fd68`, `e05062b1`, `8fd4193d`); exact action/context states and long `(action, tail)` chains are interned; 0 fixture changes in this run |
| C2a | Share terminal `resume` blocks | Intentional at O1 | Downstream only | **Landed** in `d3b9eca0`; 3 frozen blocks removed, object -120 bytes, PA37 88/88 and full report 5,177/5,177, timing neutral |
| C2b | Exact context-compatible cleanup-tail sharing | Intentional at O1 | Downstream only | **Landed** in `9bf96710`; 50 frozen groups, 97 LowIR instructions, 82 resume calls, and 5,984 object bytes removed; full report 5,178/5,178, timing neutral |
| C2c | Alpha-equivalent or cross-context cleanup-tail sharing | Potentially broad at O1 | Downstream only | Deferred: exact sharing captured the safe typed subset; cross-context ownership failed the PA36 reducer and alpha-renaming needs a separate SSA/liveness proof |
| D1 | Typed audit of remaining emitted definitions | 0 | 0 | **Landed** in `6bfef5ea`, corrected in `5517d984`; all 4,578 frozen functions classified, 74 conservative fallback roots and 78 internal-pruning candidates, exact object SHA preserved, full report 5,178/5,178, timing neutral |
| D2 | Prune unreachable internal native definitions | 0 required | Native function set changes at O0/O1/O2 | **Landed** in `a810c60e`: typed and textual object paths prune unreachable internal definitions while internal lifecycle definitions are retained as explicit object roots; active PA32 inspection reducer, full report 5,263/5,263 |
| O1a | Simplify before inlining and dependency scheduling | Intentional at O1 | Downstream only | **Landed** in `693e4357` and `919c2e55`: two reference-agreeing PA37 reducers, frozen object -1,144 bytes overall and byte-identical in the scheduling slice, five-block paired wall +0.41% and user +0.15%; full report 5,180/5,180, zero-fatal audit, and clean 8/32-worker inception lanes pass |
| O1b | Post-optional-inline weak reachability | O1 native output only | Downstream only | **Deferred** in `37ecea4e`: a per-object second closure removed contract-required weak/COMDAT definitions and failed 49 existing PA32/PA33 tests; the design evidence is retained in `PLAN-OBJECT-DEMAND.md` without a dormant test pending cross-object ownership |
| O2a | Expanded slot/value promotion | Intentional at O2 | Downstream only | Profile-gated |
| O2b | Bounded improved register allocation | LowIR unchanged | Intentional at O2 | Profile-gated PA38 work |

For an accepted row, replace `Initial status` with the exact owner reports,
fixture counts and paths, frozen size/time delta, reference disposition,
commit, and final decision.  For a deferred row, record the same evidence and
the reason for deferral.

### 4.3.1 Current `-O0` compiler-code baseline

R1--R3 were found while making the frozen source compile under a compiler that
was itself produced at `-O0` by `cppgm++`.  The reducers live at their earliest
owners:

- PA29 `forwarded-r8-indirect-call-target.t` proves that reusing incoming `r8`
  for the fifth call argument also reserves it against an indirect-call target;
- PA29 `promoted-rsi-after-object-copy.t` proves that an earlier physical
  `copyobj` clobber invalidates later forwarding from incoming `rsi`; and
- PA31 `340-unwind-resume-coalescing-barrier.t` proves that an explicit
  `noexcept` cleanup runs once when a loop call throws across a resume block.

The PA31 reducer hung before R3 because the resume PC selected the same cleanup
landing pad again.  GCC accepts and executes the source successfully.  No
existing checked-in LowIR fixture and no existing checked-in MIR fixture
changed for R1--R3.  The combined PA29/PA31 report passes 232/232, the full
report passes 5,183/5,183, and the PA39 file audit has zero fatal findings.
The shared register-clobber queries and unprotected-range construction were
separated into the existing analysis and LSDA modules; the touched monolithic
files are now 2,999 and 3,000 lines.

Fresh eight-worker builds then produced two otherwise matching `-O0` compiler
executables from the same corrected sources.  Both links retain jemalloc.

| Compiler executable | Build compiler | File bytes | `size` text bytes | Defined text/weak functions |
| --- | --- | ---: | ---: | ---: |
| host | GCC `-O0` | 13,693,688 | 8,451,033 | 36,926 |
| self | `cppgm++` `-O0` | 25,824,440 | 18,539,137 | 45,854 |

Two interleaved ABBA blocks compiled the frozen `semantic_overload.cpp` with
explicit `-O0`, sequentially and under a quiet CPU-pressure screen.  The four
samples per compiler were:

| Compiler executable | Wall samples (s) | Wall median | User median | Peak RSS median |
| --- | --- | ---: | ---: | ---: |
| GCC-built | 44.48, 44.60, 44.47, 44.79 | 44.540 s | 43.845 s | 364,660 KiB |
| cppgm++-built | 55.92, 55.91, 55.86, 55.89 | 55.900 s | 55.115 s | 368,746 KiB |

The paired cppgm++-built/GCC-built deltas are +25.37% wall, +25.67% user, and
+1.20% peak RSS.  This isolates a substantial baseline generated-code and
native-demand gap even with both optional optimization pipelines disabled; it
cannot be attributed only to PA37/PA38 inlining or optimization.

Each compiler's frozen output is internally deterministic.  Cross-compiler
bytes differ in one isolated standard-library function: the cppgm++-built
compiler retains one `std::__deque_buf_size(80)` call that the GCC-built
compiler folds to the constant 6.  The symbol sets are identical; the retained
call adds 14 `.text` bytes, one relocation, and 48 total object bytes
(3,727,048 versus 3,727,000).  This narrow valid selection difference is
recorded rather than presenting the host/self comparison as exact-object
parity, and is far too small to explain the compiler execution-time gap.

### 4.3.2 Current `-O0` compiler-size classification

A fresh matched-source build after B3c confirms that the large executable-size
gap is mostly two structural effects, not readonly data.  The relevant final
ELF sections are:

| Category | GCC-built compiler | cppgm++-built compiler | cppgm++ excess |
| --- | ---: | ---: | ---: |
| machine `.text` | 6,236,307 | 14,430,802 | 8,194,495 |
| `.eh_frame_hdr` + `.eh_frame` + LSDA | 1,419,968 | 4,079,634 | 2,659,666 |
| `.rodata` + `.data.rel.ro` + `.data` | 747,148 | 814,028 | 66,880 |

The data payload is therefore comparable in size, although cppgm++ currently
places most of it in writable `.data`.  Code and the unwind metadata that
describes that code dominate the gap.

The largest cause is immediately actionable.  In
`lowir_native_object_elf.cpp`, a weak definition receives an
`SHF_GROUP` `.cppgm.odr.N` marker, but its bytes and relocations stay in the
translation unit's monolithic `.text` and `.rela.text`.  GNU objects instead
place the actual `.text.<symbol>`, relocation section, and applicable LSDA in
the function's COMDAT group.  Our linker can select the winning weak symbol,
but the system linker cannot discard a losing cppgm++ body that is not a group
member.

Across the 157 objects in the compiler build, cppgm++ emits 74,870 weak body
ranges.  Cross-object equivalence grouping finds 51,540 redundant body
instances totaling 5,302,111 bytes.  The final cppgm++ executable also has
89,919 FDEs versus GCC's 29,812.  Discarding those bodies predicts a further
412,320-byte `.eh_frame_hdr` reduction and approximately 1,657,378 bytes of
`.eh_frame` reduction.  Real function COMDATs should therefore remove about
7.37 MB, or 72.6% of the 10,148,482-byte GNU `size` text gap, before any
instruction-level optimization.  This uses ordinary native ELF ownership and
does not require serialized cross-object compiler metadata.

After projecting the redundant bodies out, cppgm++ machine `.text` is about
9,128,691 bytes versus GCC's 6,236,307, leaving about 2.89 MB for later work.
A normalized disassembly attributes 2,156,578 bytes of the remaining
instruction-byte delta to `mov` and `lea`: register copies, frame homes and
reloads, and separately materialized addresses.  That is the next broad
target, but it is a family of dataflow and allocation improvements rather than
one safe deletion.  Repeated prologues and epilogues are visible too, but their
measured `push`/`pop` contribution is only about 0.3--0.4 MB.  B6 must land and
be reprofiled before selecting B7 slices or attributing the remaining extra
8,572 unique function bodies to demand versus code selection.

### 4.3.3 B6 result and post-COMDAT classification

`90368327` replaces the empty marker-section convention with real function
COMDAT ownership.  Weak function byte ranges are partitioned into
`.text.<signature>` sections, their relocation sections carry both
`SHF_GROUP` and `SHF_INFO_LINK`, and the weak definition is the group
signature.  Strong and local functions remain in the aggregate `.text`
section.  Cross-section references become ordinary ELF relocations.  Each FDE
continues to live in shared `.eh_frame`, but its relocation now names the
function's actual section, allowing the GNU linker to discard the FDE with a
losing COMDAT body.  LSDA remains aggregate and is a later, smaller target.

The earliest-owned active reducer is
`cppgm.tests/course/pa32/weak-function-body-comdat.t`.  Its two translation
units execute the selected inline definition and its object inspection checks
that both the function body and its relocation section belong to the
definition's COMDAT group.  GCC agrees.  Splitting text also exposed an old
same-section-fixup assumption through the existing PA33
`200-host-thread-local-wrapper-access.t`; exact section identity now decides
whether a fixup can be resolved locally, so the weak TLS wrapper receives the
required cross-section relocation.

The immediate pre-B6 and post-B6 `-O0` self binaries show the predicted result:

| Final compiler component | Pre-B6 | B6 | Change | GCC `-O0` |
| --- | ---: | ---: | ---: | ---: |
| file bytes | 25,886,080 | 18,657,216 | -7,228,864 | 13,693,896 |
| GNU `size` text | 18,600,825 | 11,305,465 | -7,295,360 | 8,452,343 |
| machine `.text` | 14,430,802 | 9,174,130 | -5,256,672 | 6,236,307 |
| `.eh_frame_hdr` | 719,364 | 309,860 | -409,504 | 238,508 |
| `.eh_frame` | 2,891,536 | 1,260,680 | -1,630,856 | 1,005,972 |
| LSDA | 468,734 | 470,406 | +1,672 | 175,488 |
| FDEs | 89,919 | 38,731 | -51,188 | 29,812 |

The FDE reduction is within 352 of the 51,540-body prediction.  The combined
code, header, and frame saving is 7,297,032 bytes; the small LSDA increase
makes the GNU `size` text saving exactly 7,295,360 bytes.  Machine text is now
47.1% above GCC rather than 131.4% above it, and GNU `size` text is 33.8%
above GCC rather than 120.1% above it.

The residual 2,937,823-byte machine-text gap is primarily code selection
inside the same source functions, not another duplicate-body effect.  Taking
the maximum body size for each demangled function name, common names account
for approximately 2,249,788 bytes of excess, while the net size of names found
only on one side is approximately 547,880 bytes.  The final compiler has
38,727 unique text addresses versus GCC's 29,809, so demand remains relevant,
but it is secondary to the per-function code-size gap.

A post-B6 disassembly attributes 2,203,845 bytes of the parsed instruction-byte
delta to plain `mov` and `lea` alone:

| Residual family | Excess bytes versus GCC |
| --- | ---: |
| register-to-register `mov` | 651,146 |
| stack `mov` | 674,672 |
| stack-address `lea` | 335,693 |
| other memory-address `lea` | 283,384 |
| other memory `mov` | 132,138 |
| immediate `mov` | 88,169 |
| RIP-relative `mov`/`lea` net | 38,643 |

`push` and `pop` add another 327,276 bytes of raw excess, reflecting both more
functions and much heavier use of `rbx`/`r12`--`r15`, but this is not the
majority target.  There are 59,020 apparent same-register 32-bit moves versus
706 in GCC.  They are writes such as `mov %ebx,%ebx`, which zero-extend the
upper half on x86-64; they are not unconditional no-ops.  A bounded producer
proof can remove those whose input is already normalized, but blind encoder
deletion would be incorrect.  B7 should therefore begin with proved copy and
normalization cases, followed by PA38 frame-home and address coalescing.

Validation is complete: PA32 is 135/135 with 13/13 course tests,
PA32--PA38 report selection is 963/963, the full report is 5,186/5,186, and the
file audit has zero fatal findings (23 inherited warnings).  A frozen
host-compiler ABBA comparison was neutral (8.986 s pre-B6 versus 8.971 s B6
median); the `-O0` self-compiler ABBA comparison was also neutral (76.138 s
versus 76.025 s median).  The clean eight-way optimized self build took
19.39 s wall with 307,704 KiB peak RSS.  The corresponding inception rebuild
took 4:15.20 wall with 314,324 KiB peak RSS, matched every object, and matched
the final `cppgm++` binary byte for byte.

### 4.3.4 B7a result

`0f01fa1a` removes a 32-bit `zext` from native encoding only when the
immediately preceding instruction provably wrote the same register with a
zero-extending 32-bit operation.  The bounded producers are non-frame 32-bit
loads, `movzx`, a prior 32-bit `zext` or `bswap`, and immediates that the
encoder emits with a 32-bit destination.  Frame loads are deliberately
excluded: frame reload forwarding can suppress the load, and a same-register
forward would then make the following normalization semantically necessary.
Textual MIR is unchanged.

The active PA29 behavior witness
`cppgm.tests/course/pa29/behavior/redundant-u32-normalization-encoding.t` retains both the global
`load.u32` and the `zext.i32` in MIR while native disassembly omits the
redundant `mov r32,r32`.  Runtime behavior is already actively covered and
PA29 has no native-byte oracle, so the reference MIR remains informational and
execution is the grading oracle.

For an exact codegen comparison, the compiler immediately before B7a and the
B7a compiler each compiled the same post-refactor source tree at `-O0`:

| Identical-source O0 self compiler | Before B7a | B7a | Change |
| --- | ---: | ---: | ---: |
| file bytes | 18,653,848 | 18,612,888 | -40,960 |
| GNU `size` text | 11,304,413 | 11,262,493 | -41,920 |
| machine `.text` | 9,172,818 | 9,130,962 | -41,856 |
| same-register 32-bit moves | 59,023 | 42,916 | -16,107 |
| `.eh_frame` | 1,260,792 | 1,260,792 | 0 |

The frozen max-optimization object falls by 347 GNU `size` text bytes.  Two
interleaved ABBA blocks measured 8.970 s versus 8.965 s wall medians; paired
candidate/baseline deltas were +0.78% wall, +0.86% user, and -0.44% peak RSS,
all within the 3% neutral gate under changing host load.  The clean eight-way
O0 self build took 17.53 s wall and 262,772 KiB peak RSS.  PA29 passes 36/36
strict, 59/59 structural, 88/88 behavior, and 28/28 course tests;
PA1--PA29 passes 4,102/4,102; the affected PA29--PA38 selection passes
1,295/1,295; and the full report passes 5,186/5,186.  The PA39 file audit has
zero fatal findings (25 warnings).  Its responsibility splits preserve the
existing statistics output while keeping both touched driver functions and
the native ELF orchestrator below fatal size limits.

### 4.3.5 B7b result

`58aa9b65` extends B7a to frame loads while accounting for the already-landed
frame-reload forwarding rules.  A real 32-bit load always zero-extends its
destination, and a forwarded load does too when it emits a 32-bit move between
different registers.  When forwarding eliminates a store/load pair whose
source and destination are the same register, however, no 32-bit instruction
is emitted; B7b detects that case and retains the normalization.  This is a
constant-time local query against the existing per-function forwarding plan.

Frame-forwarding analysis now lives in the responsibility-named
`lowir_native_frame_forwarding` module.  This reduces the native ELF
orchestrator from 2,978 to 2,838 lines rather than leaving it at the 3,000-line
fatal audit boundary.  The new module is present in both compiler and
`lowir2native` source sets.

The active PA29 behavior witness now contains a frame load separated from its store
by a call, as well as the original global load.  Its MIR retains each explicit
`zext.i32`; raw native disassembly omits the redundant instruction after the
real frame load, and the program exits zero.  Existing active PA29 narrow and
same-register frame-forwarding cases continue to cover the correctness
boundary.

Identical current sources compiled by the B7a and B7b compilers at `-O0`
measure:

| Identical-source O0 self compiler | B7a | B7b | Change |
| --- | ---: | ---: | ---: |
| file bytes | 18,611,848 | 18,542,216 | -69,632 |
| GNU `size` text | 11,260,589 | 11,192,909 | -67,680 |
| machine `.text` | 9,129,650 | 9,062,114 | -67,536 |
| same-register 32-bit moves | 42,916 | 16,197 | -26,719 |
| `.eh_frame` | 1,260,264 | 1,260,264 | 0 |

Both same-source eight-way builds took 17.95 s wall; B7b used 259,368 KiB
peak RSS versus 264,644 KiB for B7a.  The frozen object loses another 156 GNU
`size` text bytes.  A two-block immutable ABBA comparison measured 8.845 s
versus 8.810 s wall medians, with paired changes of -0.48% wall, -0.15% user,
and -0.22% RSS.  PA29 passes all four lanes, PA1--PA29 is 4,102/4,102,
PA29--PA38 is 1,295/1,295, the full report is 5,186/5,186, and the PA39 audit
has zero fatal findings (25 warnings).

### 4.3.6 B7c result

`9a929862` recognizes an adjacent 64-bit register copy followed by a 32-bit
normalization of the copy destination.  One 32-bit register move produces the
same final value, including zeroed upper bits, and neither form changes
condition flags.  The encoder consumes the pair in constant time and leaves
textual MIR unchanged.

The active PA29 behavior witness adds a `u32` function return.  Its dumped MIR retains
`mov r8, rax; zext.i32 r8`, while raw native disassembly contains the single
`mov %eax,%r8d` encoding and the program exits zero.  Existing active unsigned
call/extension behavior remains the runtime oracle.

Identical current sources compiled by the B7b and B7c compilers at `-O0`
measure:

| Identical-source O0 self compiler | B7b | B7c | Change |
| --- | ---: | ---: | ---: |
| file bytes | 18,546,464 | 18,509,600 | -36,864 |
| GNU `size` text | 11,193,869 | 11,159,621 | -34,248 |
| machine `.text` | 9,063,026 | 9,028,802 | -34,224 |
| same-register 32-bit moves | 16,198 | 4,477 | -11,721 |
| `.eh_frame` | 1,260,304 | 1,260,304 | 0 |

The same-source builds took 17.61 s and 17.49 s wall, respectively.  The
frozen object loses another 192 GNU `size` text bytes.  A timing block that
crossed a 19.4-to-9.4-second host-load transition was discarded; the stable
replacement block measured 9.180 s versus 9.090 s wall medians, with paired
changes of -0.98% wall, -0.12% user, and +1.79% RSS.  PA29 passes all four
lanes, PA1--PA29 is 4,102/4,102, PA29--PA38 is 1,295/1,295, the full report is
5,186/5,186, and the PA39 audit has zero fatal findings (25 warnings).

The remaining 4,477 apparent same-register 32-bit moves are no longer one
uniform local deletion opportunity.  Their leading predecessors are calls,
64-bit arithmetic, block boundaries, same-register forwarded reloads, and
sign-extending operations.  Each needs an ABI, flag-liveness, CFG, or source
width proof.  Further material copy reduction therefore moves to the planned
PA38 value-placement/register-allocation work rather than broadening the O0
encoder peephole.

### 4.3.7 Exact-current residual size classification

After B7c, both cppgm++ and GCC compiled the exact same current source tree at
`-O0`.  This removes the small source-drift caveat from the earlier B6
comparison:

| Final compiler component | cppgm++ | GCC | Delta |
| --- | ---: | ---: | ---: |
| file bytes | 18,509,600 | 13,801,280 | +4,708,320 |
| GNU `size` text | 11,159,621 | 8,504,783 | +2,654,838 |
| machine `.text` | 9,028,802 | 6,274,499 | +2,754,303 |
| `.eh_frame_hdr` | 309,748 | 240,764 | +68,984 |
| `.eh_frame` | 1,260,304 | 1,015,276 | +245,028 |
| LSDA | 470,234 | 176,032 | +294,202 |
| `.data` plus `.rodata` | 816,316 | 733,868 | +82,448 |
| `.symtab` plus `.strtab` | 6,525,273 | 5,269,402 | +1,255,871 |
| FDEs | 38,717 | 30,094 | +8,623 |
| defined code symbols | 46,241 | 37,271 | +8,970 |

The executable is therefore 34.1% larger on disk, GNU `size` text is 31.2%
larger, and machine text is 43.9% larger.  The 1.26 MB symbol-table/string-table
gap is non-allocating file content: stripping could reduce distribution size,
but would not improve generated code or loaded execution footprint.  The
0.61 MB unwind gap and extra FDEs remain a secondary definition/demand target.

The current normalized disassembly divides the machine-code gap as follows:

| Residual instruction family | Excess bytes versus GCC |
| --- | ---: |
| register-to-register `mov` | 488,224 |
| stack `mov` | 652,630 |
| stack-address `lea` | 327,299 |
| other memory-address `lea` | 283,785 |
| other memory `mov` | 130,917 |
| immediate `mov` | 86,042 |
| RIP-relative `mov`/`lea` | 38,413 |
| `push`/`pop` | 326,781 |

Plain `mov` and `lea` account for 2,007,310 bytes, or 72.9% of the machine
text gap.  Register copies, stack loads/stores, and stack-address formation
alone account for 1,468,153 bytes.  Adding `push`/`pop` brings these broad
value-placement and save/restore families to 84.7% of the gap.  These are
upper bounds, not all removable bytes, but they identify the dominant cause.

The excess is also widely distributed rather than concentrated in a few
compiler routines.  Maximum body size per demangled name attributes a net
2,129,582 excess bytes to 27,455 common names and 485,067 net bytes to names
present on only one side.  The ten largest positive common-name gaps total
161,243 bytes; the largest 100 total 623,062 bytes.  Consequently there is no
second COMDAT-sized local simplification.  The next majority opportunity is
the planned PA38 value coalescing/register-allocation work, which can attack
copy, home/reload, address-materialization, and callee-save traffic together.

### 4.3.8 B7d deferred incoming-parameter retention

A prototype retained a direct scalar parameter in its incoming ABI register
when the existing fixed-register clobber analysis proved that register intact
through the parameter's last use.  On the matched O0 `lowir_opt.cpp` sample it
removed 325 MIR instructions, reduced GNU text from 624,023 to 621,632 bytes,
and reduced functions preserving `r12`/`r13`/`r14`/`r15` by 52/12/101/20.
All changed tests still matched their implementation exit status, program exit
status, and stdout oracles.

The prototype nevertheless changed ten existing PA29 MIR fixtures:

- `pa29/tests/strict/100-object-abi-lowered.t`;
- `pa29/tests/strict/200-pass-by-value-lvalue.t`;
- `pa29/tests/structural/400-call-clobber-register-pressure.t`;
- `pa29/tests/structural/600-floating-short-circuit-branch.t`;
- `pa29/tests/structural/800-conditional-edge-liveness.t`;
- `pa29/tests/structural/800-forwarded-param-identity-live-across-call.t`;
- `pa29/tests/structural/800-slot-address-rematerialization.t`;
- `pa29/tests/structural/800-switch-call-case-liveness.t`;
- `pa29/tests/structural/800-xmm-live-across-integer-call.t`; and
- `pa29/tests/behavior/800-register-param-r8-home-clobber.t`.

PA29 has no standalone reference binary, and the two strict fixtures explicitly
require the original raw MIR.  The candidate was therefore reverted rather
than hand-editing oracles.  Its keep-going PA29 report was 203/213, with all ten
failures attributable to the listed MIR movement.  This remains a valid PA38
optional value-placement idea, but is not an acceptable baseline change under
the O0 fixture rule.

### 4.3.9 B7e transient-scratch address folding

`10bdd7ad` extends the existing native address-folding proof for R11, the fixed
transient setup register that is never assigned as a persistent `ValueFact`
home.  A backward plan records at every MIR boundary whether R11 will be read
before its next simple definition.  An adjacent setup/load or setup/store may
therefore fold when the consumed R11 value has no later reader, including at
block end.  A shared-address pair such as a 16-byte return retains its setup
because the first load is followed by a second R11 load.  Frame-store folds
retain the existing forwarding barrier.

The plan is constructed once per block in O(instructions + operands) time and
uses one byte per instruction.  Each encoder query is O(1); it replaces no MIR
and adds no rescan per candidate.  No existing checked-in LowIR or MIR fixture
changed.  `cppgm.tests/course/pa29/behavior/transient-scratch-address-folding.t` is the native-byte
witness: its one-eightbyte return may fold the R11 setup, while its
two-eightbyte return must retain one setup for both loads.  Existing active
object-return tests remain the behavioral oracle.

Measured evidence:

- O0 `lowir_opt.cpp` MIR: unchanged at 122,204 instructions;
- O0 `lowir_opt.cpp` object: 2,584,648 to 2,582,216 bytes and GNU text 624,023
  to 621,734 bytes;
- parsed O0 `lowir_opt.cpp` machine instructions: 140,583 to 139,809, with
  stack-address LEAs falling from 6,634 to 6,027;
- frozen explicit-O0 object: 4,516,264 to 4,513,368 bytes and GNU text
  1,214,586 to 1,211,960 bytes;
- three-block immutable ABBA: paired wall -0.39%, user +0.09%, and peak RSS
  -0.23%, with all six outputs per compiler deterministic;
- PA29: 183/183 assignment tests and 30/30 course tests;
- through PA29: 4,104/4,104;
- affected PA29/PA31/PA37/PA38 report: 353/353;
- full report: 5,188/5,188; and
- PA39 file audit: zero fatal findings and 23 inherited warnings.

### 4.3.10 B7f leaf-function R10 allocation

B7f prototyped adding R10 after R8/R9 in the persistent-value register pool,
but only in functions whose complete LowIR clobber inventory never uses R10 as
fixed scratch.  The first version exposed an important hidden encoder boundary:
floating-immediate materialization uses R10 below MIR, so the existing PA29
`800-f80-call-preserves-gpr-arguments` behavior test failed until floating and
conversion operations were included in the function-wide scratch inventory.
With that correction, every PA29 program had the expected behavior.

The resulting benefit was small.  On the same O0 `lowir_opt.cpp` sample, MIR
fell from 122,204 to 122,199 instructions, object size fell from 2,582,216 to
2,580,632 bytes, GNU text fell from 621,734 to 620,019 bytes, and disassembly
contained about 600 fewer push/pop instructions.  PA29 still reported 14 MIR
oracle changes: 10 strict fixtures and 4 structural fixtures.  Since PA29 has
no external reference workflow and the size return is much smaller than the
fixture surface, the prototype was reverted.  A future PA38 allocator may use
the result together with explicit MIR scratch definitions; it is not an O0
baseline change.

### 4.3.11 B7g single-block native encoding preparation

The first B7g prototype ran the existing full machine optimizer and frame
finalizer on an encoding-only copy of every O0 function.  It removed 33,893
frozen text bytes, including 7,849 `mov`, 309 `lea`, 1,117 `push`, and 1,374
`pop` instructions, and all 5,188 report tests passed.  Its CFG construction,
worklist, and whole-function copies nevertheless regressed the three-block
frozen wall median by 5.4%.  That version was rejected rather than accepting a
compile-time cost in the default lane.

`a5777d1e` narrows the preparation to single-block functions, where live-out
is empty and no CFG or fixed point is necessary.  One forward local-value pass
rewrites proved equivalent read operands, one backward register-liveness pass
removes dead definitions, and the existing frame finalizer drops callee-save
traffic made dead by those rewrites.  All work is linear in MIR instructions
and operands.  The streaming C++ compile path prepares the just-lowered
function in place; only callers that provide a persistent `MirProgram` clone
an eligible function before emission.

Calls now retain an exact GPR/XMM argument-register mask computed from the
existing SysV ABI plan.  The mask is deliberately internal and is not emitted
by the MIR serializer: conservative serialized MIR remains the PA29 contract,
while encoding-only liveness no longer keeps every possible argument register
alive at every call.  Variadic calls also record RAX.  Existing native
address-fold and constant-byte-store groups are marked before rewriting so the
general pass composes with, rather than dismantles, the smaller encoding
peepholes already landed in B4d and B7e.

No checked-in LowIR or MIR fixture changes.  The representation-only witness
`cppgm.tests/course/pa29/structural/single-block-call-argument-coalescing.t` retains
`mov r8,@value; mov rdi,r8` in its MIR dump but emits the global address
directly into RDI; active PA29 call-ABI tests remain the behavioral oracle.

Measured evidence:

- O0 `lowir_opt.cpp` object: 2,582,216 to 2,570,352 bytes and GNU text 621,734
  to 609,742 bytes, with its serialized MIR unchanged;
- frozen explicit-O0 object: 4,513,368 to 4,498,880 bytes and GNU text
  1,211,960 to 1,197,374 bytes;
- parsed frozen x86 instructions: 256,629 to 251,506, including `mov*`
  116,511 to 112,448, `lea` 28,089 to 28,052, `push` 11,117 to 10,720, and
  `pop` 16,629 to 16,232;
- all six frozen objects from each immutable compiler were internally
  deterministic;
- three-block immutable ABBA medians: wall 6.340 to 6.315 seconds (-0.39%),
  user 5.745 to 5.750 seconds (+0.09%), and peak RSS 364,852 to 365,242 KiB
  (+0.11%);
- PA29: 183/183 assignment tests and 30/30 course tests;
- through PA29: 4,104/4,104;
- full report: 5,188/5,188; and
- PA39 file audit: zero fatal findings and 23 inherited warnings.  The exact
  call-mask owner was separated into `lowir_native_abi.cpp`, leaving
  `lowir_native.cpp` at the 3,000-line boundary.

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

### 4.9.1 B3c signed-quotient result

`b7c61cc5` replaces signed non-power-of-two constant quotient sequences with
the standard multiply-high magic sequence in the native encoder.  The magic
calculation is bounded by integer width and recognition remains constant-time
per instruction.  Hardware division remains for signed remainder and all
non-power-of-two unsigned forms until their independent slices are proved.

No existing checked-in LowIR fixture changed and no existing checked-in MIR
fixture changed.  Two active PA29 behavior reducers were added:

- `signed-constant-division-magic.t` covers positive and negative divisors,
  both multiplier correction directions, narrow values, and 64-bit extrema;
- `optimized-constant-division-result-register.t` runs at `-O2` and proves
  that the selected fixed `rax` result remains live when optimized MIR also
  copies the quotient to another physical register.

The latter reducer was created from the first self-host failure: the initial
encoder rewrite computed only the copied destination and left a later fixed
`rax` consumer stale.  Computing in the selected fixed result register before
the final copy repaired the general contract rather than special-casing the
driver.  The reference accepts and executes both tests; neither test needs a
representation-specific MIR fixture.

Validation and measured evidence:

- PA29: 183/183 assignment tests and 28/28 course tests;
- through PA29: 4,102/4,102;
- affected PA29/PA31/PA37/PA38 report: 351/351;
- full report: 5,185/5,185;
- PA39 file audit: zero fatal findings;
- frozen hardware divides: 233 to 163, with 70 signed constant quotients
  removed;
- frozen object: 3,727,000 to 3,728,008 bytes and `.text`: 1,022,892 to
  1,023,904 bytes; the 1,012-byte text increase is retained because the
  generated self compiler improves materially;
- matched `-O3` self compiler text: 17,024,332 to 17,065,600 bytes, while
  hardware divides fall from 17,473 to 13,929;
- two-block self-compiler ABBA on the frozen explicit-`-O0` compile: paired
  wall -1.82%, user -1.86%, and peak RSS +0.41%.

A fresh diagnostic then built the entire compiler twice at explicit `-O0`,
once with GCC and once with `cppgm++`, and used those two executables for an
interleaved frozen explicit-`-O0` compile.  This deliberately disables the
PA37/PA38 optional optimization paths in both compiler executables:

| Compiler executable | File bytes | `size` text bytes | Defined text/weak functions | Frozen wall median | Frozen user median | Peak RSS median |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| GCC-built `-O0` | 13,693,896 | 8,452,343 | 36,928 | 46.980 s | 46.225 s | 364,052 KiB |
| cppgm++-built `-O0` | 25,886,080 | 18,600,825 | 45,856 | 56.880 s | 56.110 s | 368,376 KiB |

The paired cppgm++-built/GCC-built deltas are +23.04% wall, +23.27% user,
and +1.18% peak RSS.  The host was quiet at the start of each ABBA block, and
all four runs per executable succeeded and were internally deterministic.
This establishes a remaining baseline x86/native-demand gap, but it is much
smaller than the earlier optimized self-host gap: inlining and optional
optimization are not the whole problem, while baseline machine-code quality
does not explain a multi-fold slowdown by itself.

### 4.9.2 B3c remainder and unsigned result

`796de10c` completes B3c by replacing signed constant remainder and unsigned
constant quotient/remainder sequences with multiply-high division.  The
unsigned magic-number construction uses a fixed 64-step restoring division,
so it does not depend on a host `__int128` divide and adds no whole-function
analysis.  Remainders reuse the selected quotient and reconstruct
`dividend - quotient * divisor`.  Recognition also accepts the direct fixed
`rax`/`rdx` return left when PA38 removes the final result copy at `-O2`.

No existing checked-in LowIR fixture changed and no existing checked-in MIR
fixture changed.  Two additional active PA29 behavior reducers were added:

- `general-constant-remainder-and-unsigned-division.t` covers signed
  quotient/remainder and unsigned quotient/remainder at 8, 16, 32, and 64
  bits, including both unsigned magic forms and high divisors;
- `optimized-general-constant-division.t` runs at `-O2` and covers the direct
  fixed-result return exposed by optional propagation.

The reference accepts and executes both tests.  Their valid MIR differs from
our selected layout, so the active tests deliberately retain the behavioral
oracle without an exact MIR sidecar.  Disassembly of both reducers contains no
hardware divide instruction.  An independent exhaustive 8-bit and
1,010,000-case random/boundary 64-bit check also validated the unsigned magic
calculation.

Validation and measured evidence:

- PA29: 183/183 assignment tests and 30/30 course tests;
- through PA29: 4,104/4,104;
- affected PA29/PA31/PA37/PA38 report: 353/353;
- full report: 5,188/5,188;
- PA39 file audit: zero fatal findings and 23 inherited division warnings;
- frozen object: 4,516,184 to 4,516,264 bytes and GNU `.text`: 1,214,508 to
  1,214,586 bytes, while hardware divides fall from 163 to 160;
- immutable host-compiler ABBA: paired wall -1.67%, user -1.93%, and peak RSS
  +0.03%;
- matched generated-self compiler GNU text: 10,296,564 to 10,296,980 bytes,
  while hardware divides fall from 7,383 to 7,365; and
- two-block generated-self ABBA on the frozen explicit-`-O0` compile: paired
  wall +0.64%, user +0.59%, and peak RSS +0.02%.

All measured outputs were deterministic.  The generated-self timing is
neutral under the plan's 3% threshold, while this slice completes the
constant-division forms and removes the remaining matching hardware divides
without changing LowIR or MIR contracts.

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
`cppgm.tests/course/pa29/behavior/flag-safe-zero-materialization.t`: program behavior alone would
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
behavior and MIR gates.  `cppgm.tests/course/pa29/behavior/zero-compare-test-encoding.t` retains
`cmp ..., 0` in MIR while manual inspection proves u32 and i64 `test`
encodings.  The behavior lane executes the safety boundary without grading
native bytes or exact MIR.

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
gate.  `cppgm.tests/course/pa29/behavior/narrow-zero-extension-encoding.t` retains byte/word
`zext` in MIR while manual disassembly proves `movzbl`/`movzwl` destinations.
The behavior lane executes the safety boundary without treating native bytes
as a PA29 oracle.

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
for `cppgm.tests/course/pa29/behavior/dead-address-load-folding.t` deliberately retains the
adjacent `lea` and `load`; the generated program succeeds.  It is active in
the behavior lane because execution guards the fold's safety, while native
instruction bytes and exact MIR remain non-grading artifacts.

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
fixture changed.  `cppgm.tests/course/pa29/behavior/dead-address-copy-load-folding.t` retains the
register copy and indirect load in its dumped MIR and its generated program
succeeds.  It is active behavior coverage with informational MIR; native bytes
are not a grading oracle.

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
fixture changed.  `cppgm.tests/course/pa29/behavior/dead-address-copy-index-load-folding.t`
retains the `mov`/`lea`/`load` chain in its dumped MIR and its generated
program succeeds.  It is active behavior coverage with informational MIR;
PA29 does not grade native bytes.

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
fixture changed.  `cppgm.tests/course/pa29/behavior/dead-copy-store-folding.t` retains
`mov r8, rax`, `store.i64 [rbx], r8`, and the following overwrite in its
dumped MIR; its generated program succeeds.  It is active behavior coverage
with informational MIR and no native-byte oracle.

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
fixture changed.  `cppgm.tests/course/pa29/behavior/dead-address-store-folding.t` retains an
accepted `lea`/`store` pair and its following overwrite in dumped MIR, and the
generated program succeeds.  It is active behavior coverage with
informational MIR and no native-byte oracle.

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
fixture changed.  `cppgm.tests/course/pa29/behavior/dead-address-copy-store-folding.t` retains an
accepted pointer copy, indirect store, and immediate overwrite in dumped MIR;
its generated program succeeds.  It is active behavior coverage with
informational MIR and no native-byte oracle.

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
fixture changed.  `cppgm.tests/course/pa29/behavior/dead-address-copy-index-store-folding.t`
retains the accepted `mov`/`lea`/`store` chain in dumped MIR and its generated
program succeeds.  It is active behavior coverage with informational MIR and
no native-byte oracle.

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
fixture changed.  PA31 has no standalone reference binary, so the stable
course contract is end-to-end behavior rather than private LSDA layout.  The
active course family covers equal adjacent cleanup ranges, an unprotected
potentially throwing barrier, different landing continuations, and different
typed-catch actions.  The former private-header unit was deleted because its
`HostFunctionLayout` assertions were not a student-facing contract.

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

### 4.24 D1 result

`6bfef5ea` carries each semantic function-demand reason as a compact bit mask
to the typed LowIR symbol and makes the post-inline reachability walk retain a
reason mask per function.  Direct call targets, function addresses and global
relocations, lifecycle roots, EH/runtime roots, weak ownership, external
strong roots, and the existing conservative internal-root fallback are marked
without a rendered-name lookup.  The diagnostic reports one exclusive primary
bucket for every reachable function; only fallback names are printed, and only
under explicit `--stats`.

The first version incorrectly ORed stale pre-inline semantic demand reasons
into every later graph visit.  That made ordinary graph-reachable internal
functions appear semantically rooted and undercounted the conservative
fallback bucket as three.  `5517d984` corrects the exclusive classification
and, only under `--stats`, runs a second O(V + E) observation with the blanket
internal roots disabled.  Normal compilation still performs one reachability
walk and emits byte-identical output.

The 4,578 retained frozen O1 definitions classify exactly as follows:

| Primary retention reason | Functions |
| --- | ---: |
| externally visible strong definition | 13 |
| address or relocation use | 39 |
| direct-call closure | 4,445 |
| vtable/constructor/destructor/static lifecycle | 2 |
| EH cleanup/runtime | 1 |
| required weak/COMDAT ownership | 4 |
| conservative internal-root fallback | 74 |

With blanket internal roots disabled, the observational walk finds 78
unreachable internal definitions.  The difference between 74 fallback-primary
functions and 78 candidates is expected: four internal functions have another
primary retention reason in the emitted graph.  This correction reopens a
bounded internal-pruning experiment, but it still does not make GCC's symbol
count a deadness oracle.

The frozen object remains exactly 3,613,040 bytes with SHA-256
`5c647dc5...`, identical to C2b.  Three-block immutable ABBA medians are
7.265/7.265 seconds wall, 6.630/6.605 seconds user, and
417,242/418,370 KiB peak RSS.  Paired deltas are +0.14% wall, -0.45% user,
and -0.21% RSS, so the diagnostic bookkeeping is timing-neutral.

PA15 passes 113/113, through PA15 passes 1,163/1,163, the full report passes
5,178/5,178, and the PA39 file audit has zero fatal findings.  **O0, O1, and
O2 compiler output are byte-unchanged; no existing LowIR fixture changed and
no existing MIR fixture changed.**

### 4.25 D2 deferred result

The first typed prototype removed internal definitions in the same O(V + E)
closure that already removes weak definitions.  On the frozen O1 compile it
removed all 78 observed candidates: functions fell from 4,578 to 4,500 and
the object fell from 3,613,040 to 3,590,664 bytes (-22,376).  `.text` fell
2,632 bytes, `.gcc_except_table` 64 bytes, and `.eh_frame` 2,248 bytes.  This
is a useful upper bound, not an accepted optimization.

The PA32 whole report found one correctness regression in
`200-anon-namespace-special-member-symbols.t`: the anonymous `Analyzer` C2
base-constructor entry disappeared even though the ABI fixture requires C1,
C2, D1, and D2.  Marking recursively demanded base entries as ordinary
`object_output_root` repaired PA32, but changed 49 existing LowIR fixtures
across PA16--PA28 by rendering new `object_root=yes` metadata.  Per the
fixture policy, that broad LowIR movement was reverted rather than
rebaselined.

A narrower prototype used a spare bit in the existing typed demand mask, so
source `--emit-lowir` stayed unchanged.  It retained enough lifecycle entries
to pass PA1--PA32 (4,365/4,365), removed 16 net definitions and 4,144 object
bytes, and was timing-neutral in a three-block immutable ABBA (-0.62% paired
wall, -0.08% user, +0.29% peak RSS).  The full report then exposed three PA37
object-roundtrip failures: serialized LowIR intentionally does not contain
that private semantic bit, so compiling it could not reproduce the direct
object.  Adding serialized lifecycle metadata would return to the broad
LowIR-fixture change above.

The output-changing code was therefore reverted.  The landed D1 correction
keeps the frozen object exactly 3,613,040 bytes with SHA-256
`5c647dc5...`; PA37 object round trips pass 7/7, the full report passes
5,178/5,178, and the PA39 audit has zero fatal findings.  This records why the
first prototype was reverted.

Normalization follow-up `a810c60e` completed D2 without serialized metadata:
typed and textual object paths now prune unreachable internal definitions,
ordinary demand reasons and object roots retain demanded internals, and
internal lifecycle definitions are explicit object roots.  The active PA32
`unreachable-internal-function-pruning` inspection fixture covers the public
result, PA37 object round trips remain reproducible, and the full report passed
5,263/5,263.

**Fixture record:** the raw D2 experiments intentionally changed the native
function/MIR set at O0, O1, and O2, but no existing exact MIR fixture required
an update.  The attempted serialized-root repair changed 49 existing LowIR
fixtures and is the explicit reason for deferral.  The landed audit changes
neither existing LowIR nor existing MIR output.

### 4.26 O1a preparation result

`693e4357` moves the existing bounded simplify/DCE/CFG preparation before O1
inline summaries are built.  Functions whose original unsimplified size was
already within the old threshold retain that original instruction count as a
budget floor.  A formerly oversized function is newly eligible only when it
collapses to a call-free, single-block leaf of at most four instructions.  This
keeps preparation from turning dead source-level scaffolding into hundreds of
new medium-sized inline expansions.

The reference-agreeing PA37 reducer
`360-inline-after-callee-simplify.t` starts with a 42-instruction identity
callee.  The old ordering simplified its body only after rejecting the call;
the new ordering reduces it to one return and inlines it.  An unrestricted
prototype enabled 376 additional frozen inline sites, grew the object 17,504
bytes, and increased optimizer work, so it was tightened before acceptance.

On the accepted frozen O1 compile, inline calls change from 7,298 to 7,292,
final LowIR instructions from 151,494 to 151,453, and native call instructions
from 29,993 to 29,992.  The object changes from 3,613,040 to 3,611,896 bytes
(-1,144), with `.text` down 2,626 bytes, `.gcc_except_table` down 5 bytes, and
`.eh_frame` down 4 bytes.  The preparation work increases PA37 optimizer time
from 1.234 to 1.288 seconds in the instrumented run, but the immutable
three-block ABBA remains end-to-end neutral: baseline/candidate medians are
7.260/7.290 seconds wall, 6.620/6.680 seconds user, and 414,068/392,432 KiB
peak RSS; paired deltas are -0.07%, +0.68%, and -5.65% respectively.

PA37 through-report passes 5,153/5,153, all 14 PA37 debug and object-roundtrip
checks pass, the full report passes 5,179/5,179, and the PA39 audit has zero
fatal findings.  **O0 is unchanged.  No existing checked-in LowIR or MIR
fixture changes; the sole new exact O1 fixture agrees with the pinned
reference.**  Bottom-up scheduling and changed-caller worklist consolidation
remain part of O1a because they should recover the extra preparation work.

### 4.27 O1a no-unwind dependency result

`919c2e55` retains source order and records only calls that would otherwise be
inline candidates but are blocked by a tiny explicit-no-unwind callee's EH
markers.  Once that callee has inlined its own leaf, its EH markers and newly
unreachable landing blocks are removed, and only the recorded callers enter a
sparse reverse-edge worklist.  The original per-caller 128-instruction budget
is stored across revisits; a revisit cannot silently acquire a second budget.
Propagation is restricted to call-free single-block wrappers of at most four
instructions, and blocked records are collected only for no-unwind candidates
of at most eight pre-strip instructions.

The reference-agreeing `370-bottom-up-no-unwind-wrapper.t` places `main`
before an EH-wrapped identity wrapper and its leaf.  The old source-order pass
left `main` calling the wrapper; the worklist removes that call after the
wrapper becomes safe.  A global callee-first prototype was rejected because
it changed two existing PA37 fixtures.  An unrestricted reverse worklist was
also rejected: it raised frozen call visits from 30,028 to about 44,000,
performed 40 additional inlines without reducing final native calls, and grew
the object 416 bytes.

The accepted sparse form records zero blocked edges and performs zero revisits
on the frozen source, so its object is byte-identical to the O1a preparation
slice (SHA-256 `1bc0391d...`, 3,611,896 bytes).  Five-block immutable ABBA
medians are 7.305/7.330 seconds wall, 6.690/6.690 seconds user, and
382,148/386,980 KiB peak RSS.  Paired deltas are +0.41% wall, +0.15% user,
and -1.01% RSS; one visibly loaded candidate run is absorbed by the
interleaved block median.

PA1--PA37 passes 5,154/5,154, the full report passes 5,180/5,180, and the
PA39 audit has zero fatal findings.  **O0 is unchanged, and no existing LowIR
or MIR fixture changes.**  The rejected global schedule's two existing O1
fixture changes are recorded here and were reverted, not rebaselined.

### 4.28 O1b deferred result

The O1b prototype repeated the existing serialized weak-function reachability
closure after optional inlining and immediately before native emission.  The
narrow reducer's local call disappeared at O1/O2, and both the candidate and
the pinned reference invoked explicitly with `-O2` omitted the now-unowned
weak helper while O0 retained it.

The whole through-PA37 report rejected the generalization before benchmarking:
49 existing PA32/PA33 tests lost required weak/COMDAT definitions.  These are
intentional object-shape contracts, including duplicate template definitions,
symbol-spelling ownership, inherited constructors, and wrappers that must be
emitted even when their local call happens to inline.  The new reducer also
disagrees with the documented default-mode reference, which retains the weak
symbol; explicit `-O2` behavior is not authority to rewrite that fixture.

Per-object reachability cannot distinguish “no remaining local edge” from
“this object owns a definition that another object may select.”  Adding more
local demand bits would reproduce the serialization/ownership problem already
seen in D2.  The output-changing code and PA37 roundtrip placeholder were
reverted, and PA1--PA37 returned to 5,154/5,154.  The deferred reducer was
deleted during test normalization; its design evidence is retained in
`PLAN-OBJECT-DEMAND.md` without a dormant test.  **The experiment changed
native O1/O2/default-max function and MIR sets but did not alter serialized
LowIR; no existing fixture was updated.**  Revisit O1b only with a native
cross-object ownership model or a proven visibility class narrower than
ordinary weak/COMDAT linkage, adding a new active test when implementation
resumes.

### 4.29 O1 milestone self-host result

The post-O1 milestone entered the expensive lane only after a fresh root
`make test-report` passed 5,180/5,180.  The PA39 file audit then passed with
zero fatal findings and the same 23 advisory header-division warnings.

The canonical self-host build has a second, internal object-build job setting:
top-level `make -j8` alone leaves `INCEPTION_OBJECT_BUILD_JOBS` at the 32-CPU
default.  That diagnostic clean 32-worker self build took 17.89 seconds wall
and 312,156 KiB maximum RSS.  The comparable clean 8-worker result below sets
`INCEPTION_OBJECT_BUILD_JOBS=8` explicitly:

| Lane | Wall | User | System | Maximum RSS |
| --- | ---: | ---: | ---: | ---: |
| clean `cppgm++-self`, 8 object workers | 37.41 s | 253.75 s | 24.44 s | 326,648 KiB |
| clean inception compare, 8 workers | 268.92 s | 2,051.53 s | 56.21 s | 296,944 KiB |
| clean inception compare, 32 workers | 125.58 s | 3,423.96 s | 77.14 s | 292,712 KiB |

Only the inception-generation tree was removed between the 8- and 32-worker
runs, so both compiled against the exact same self binary.  Each lane produced
157/157 matching inception objects.  The self and inception binaries are both
24,304,360 bytes and byte-identical with SHA-256
`3819ec3c5be4c99609be424ebce37488a98b44265ab88d27c781116667bd597f`.
No test, build, disassembly, or benchmark workload overlapped any timed lane.

**Fixture record:** O1a changes only explicit PA37 optimized LowIR in its two
new reference-agreeing fixtures.  It changes no existing checked-in LowIR or
MIR fixture, and `-O0` is byte-unchanged.  The rejected broad scheduler's two
existing O1 fixture changes and D2's 49 existing LowIR fixture changes remain
documented as reverted/deferred experiments; they were not rebaselined.

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
require an authoritative upstream assignment fixture.  Otherwise, use active
end-to-end course behavior to gate safety without asserting private LSDA
layout.

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
implement the PA37 O1 form with explicit optimized LowIR tests.  The active
PA26 constructor-unwind behavior reducer demonstrates that two correct O0
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

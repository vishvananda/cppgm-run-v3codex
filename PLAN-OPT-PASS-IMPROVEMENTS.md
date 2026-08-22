# Plan: Optimization-Pass Improvements

Status: complete; every rank has a retained implementation or measured
disposition, the maximum frozen compile is below 15 seconds, and the exact
final tree is clean in both O0 and O3 32-worker inception lanes

Date: 2026-08-22

## Objective

Improve the code produced at `-O1`, `-O2`, and eventually a distinct `-O3`,
starting with the transformations that have the largest demonstrated value on
the frozen self-compile workload.  The generated compiler must become faster
without giving back cppgm++'s compile-time advantage or violating the
compile-time-first architecture in `spec.md`.

The primary workload is:

```text
~/cppgm-extended-pa39-source-layout/benchmarks/self_compile/stable/semantic_overload.cpp
```

This plan supersedes the unstarted broad O1/O2 recommendations in
`PLAN-CODEGEN-AND-SELFHOST-OPTIMIZATION.md`.  It does not reopen completed O0,
demand, or LowIR representation phases recorded by the earlier plans.  Rank 11
does extend the optimizer's treatment of LowIR EH regions because the final
R10 census identifies that eligibility boundary as the next measured
optimization constraint.  Its final generated-self profile also justifies one
targeted PA29 native-placement phase; that phase changes MIR and native code,
not serialized LowIR.

The required outcomes are:

- materially improve the generated `cppgm++-self`, with the function inliner
  and resulting dead-body removal addressed first;
- keep a matching-level frozen compile faster than GCC and Clang;
- preserve the no-option driver rule: absence of `-O` selects the maximum
  implemented level;
- keep optimizer-specific transformations out of `-O0`; PA29 backend
  correctness and direct-placement improvements may change MIR and native code
  at every level without changing serialized LowIR;
- keep each production pass linear or near-linear, bounded, function-local
  where possible, and based on typed compact identity;
- add every reducer to the active earliest-owning course suite, never a
  dormant `proposed` tree; and
- require a clean full report, zero-fatal file audit, and clean 32-worker
  self/inception lane before a phase is declared complete.

PGO, benchmark-specific recognition, host-compiler fallback, textual IR
transport, and unbounded fixed-point optimization are out of scope.

## Starting point

The measured source tree is commit `9f648884`.  The comparisons below use GCC
15.2 libstdc++ headers for all three compilers.  Clang is explicitly given
`-stdlib=libstdc++`; its include search was checked to start with the same GCC
15 standard-library directories as GCC.

```sh
INC=~/cppgm-extended-pa39-source-layout/dev/src
SRC=~/cppgm-extended-pa39-source-layout/benchmarks/self_compile/stable/semantic_overload.cpp

dev/cppgm++ -std=gnu++11 -O1 -I "$INC" -c "$SRC" -o cppgm-o1.o
g++          -std=gnu++11 -O1 -I "$INC" -c "$SRC" -o gcc-o1.o
clang++      -std=gnu++11 -O1 -stdlib=libstdc++ \
  -I "$INC" -c "$SRC" -o clang-o1.o
```

The same commands were repeated at `-O2` and `-O3`.  The cppgm++ `-O2` and
`-O3` objects are byte-identical because both flags currently select internal
level 2.

### Object and call-graph evidence

| Compiler | Level | Object bytes | `.text*` bytes | Decoded instructions | Defined function symbols | Weak functions | Static calls |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| cppgm++ | O1 | 3,047,896 | 671,678 | 166,687 | 5,528 | 4,491 | 17,213 |
| cppgm++ | O2 | 3,010,064 | 634,803 | 159,126 | 5,528 | 4,491 | 17,209 |
| cppgm++ | O3 | 3,010,064 | 634,803 | 159,126 | 5,528 | 4,491 | 17,209 |
| GCC | O1 | 963,296 | 417,966 | 89,993 | 452 | 283 | 9,934 |
| GCC | O2 | 939,368 | 388,579 | 83,941 | 506 | 194 | 9,303 |
| GCC | O3 | 933,056 | 388,963 | 84,047 | 463 | 181 | 9,410 |
| Clang | O1 | 909,272 | 358,519 | 82,014 | 424 | 280 | 8,780 |
| Clang | O2 | 909,048 | 363,045 | 82,792 | 411 | 270 | 8,911 |
| Clang | O3 | 931,464 | 384,703 | 87,467 | 397 | 258 | 9,162 |

The function-symbol counts include aliases.  Disassembly identifies 4,550
cppgm++ O1 code bodies, 373 GCC bodies, and 422 Clang bodies.  Exact
demangled-name matching gives the more useful split:

| O1 comparison | Common bodies | cppgm++ bytes in common bodies | Host bytes in common bodies | cppgm++-only bodies/bytes | Host-only bodies/bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| versus GCC | 348 | 320,726 | 369,066 | 4,202 / 350,478 | 25 / 48,900 |
| versus Clang | 380 | 331,285 | 326,314 | 4,170 / 339,919 | 42 / 32,205 |

Name matching is imperfect when a host compiler creates clones, but the
classification is decisive.  In common O1 bodies cppgm++ is only 4,971 bytes
larger than Clang and is 48,340 bytes smaller than GCC.  About 340--350 KiB of
cppgm++ text instead belongs to thousands of bodies the host compilers no
longer retain.  Backend instruction selection is therefore not the first O1
problem.  The inliner and the post-inline call graph are.

The same conclusion appears in selected call counts:

| O1 target family | cppgm++ | GCC | Clang |
| --- | ---: | ---: | ---: |
| `_Unwind_Resume` | 520 | 273 | 213 |
| `basic_string` | 4,424 | 886 | 1,018 |
| `shared_ptr` | 1,867 | 335 | 490 |
| `vector` | 2,629 | 410 | 366 |

The prior O0 comparison had roughly 4,980 GCC functions.  GCC's reduction to
452 bodies at O1 is consequently not an STL-implementation artifact: it is
mainly an inlining and post-inline emission result.  cppgm++ retains exactly
the same 5,528 function symbols at O1, O2, and O3.

### Current optimizer cost and behavior

The current PA37 optimizer first prepares every function, invokes the inliner
once, and revisits only rewritten callers.  The inliner builds recursion
information but expands ordinary nonrecursive functions in definition order.
It has a fixed 128-instruction caller budget, rejects most functions over 40
instructions, and its fast leaf path rejects any callee containing another
call.

Frozen telemetry is:

| Metric | O1 | O2 |
| --- | ---: | ---: |
| LowIR functions | 4,574 | 4,574 |
| input/output instructions | 144,398 / 122,625 | 144,398 / 106,734 |
| calls inlined | 7,106 | 7,106 |
| rewritten callers | 2,328 | 2,328 |
| inline budget skips | 1,789 | 1,792 |
| total PA37 time | 314.9 ms | 829.2 ms |
| inline time | 69.6 ms | 64.9 ms |
| slot-pass time | 66.9 ms | 558.4 ms |
| slot-promotion time | 0 | 466.3 ms |

O2 removes 15,891 additional LowIR instructions and 36,875 text bytes, but
only four additional calls and no additional functions.  Its current slot
promotion consumes 56% of PA37 time.  It allocates block states containing
dense value and slot vectors, so it is also the wrong foundation for more
global value work.

Single sequential timing screens were 4.93/5.47/5.62 seconds for cppgm++
O1/O2/O3, 16.06/22.69/23.28 for GCC, and 16.00/18.26/18.77 for Clang.  These
are not acceptance medians because host load varies.  They establish ample
compile-time headroom but do not authorize inefficient passes.  Retained
changes use the load-screened interleaved protocol below.

## What GCC and Clang add above local simplification

Optimization records were collected from the same source and headers.  Remark
systems count transformations differently, so the counts are directional,
not cross-compiler performance metrics.

Applied Clang remarks give the clearest hierarchy:

| Pass family | O1 | O2 | O3 |
| --- | ---: | ---: | ---: |
| inlining, including always-inline | 12,756 | 12,796 | 12,853 |
| LICM hoist/sink/promotion | 1,891 | 1,846 | 1,867 |
| GVN load PRE/elimination | 0 | 751 | 767 |
| SLP vectorization | 0 | 252 | 250 |
| argument promotion | 0 | 0 | 250 |
| loop unrolling/peeling | 3 | 93 | 95 |
| loop vectorization | 0 | 7 | 7 |
| dead-argument elimination | 10 | 10 | 14 |
| tail recursion/calls | 3 | 4 | 4 |

GCC reports about 13,042 applied inlines at O1 and 13,873/13,861 at O2/O3.
Its O2/O3 records additionally show roughly 1,053/950 basic-block SLP
successes, 523/392 applied remarks involving `.isra` parameter-specialized
clones, 40/53 loop-unroll successes, 2/11 loop-vector successes, and 31/22
interprocedural semantic-equality hits.  The `.isra` counts include later uses
of a clone and are evidence of the pass family, not a count of distinct
scalar-replacement decisions.

The evidence supports these conclusions:

1. Inlining is the dominant missing transformation at every optimized level.
2. LICM is the largest repeatedly observed non-inline family and is active in
   Clang already at O1.
3. Scalar replacement and memory/value redundancy elimination are the central
   O2 gap.  Our current no-phi slot pass is useful but both expensive and
   incomplete.
4. Unrolling and vectorization are real O2/O3 work, but they touch far fewer
   loops and can grow code.  Clang's frozen object grows from O1 to O2 and
   again to O3, so object size alone cannot justify them.
5. Tail recursion, jump threading, dead-argument elimination, function
   equality, and similar IPA work are secondary on this workload.

## Architecture required by `spec.md`

All phases below follow one shared implementation shape.

- Use `SymbolId`, `BlockId`, `ValueId`, `SlotId`, instruction ordinals, compact
  enums, and dense vectors.  Strings remain serialization and diagnostic data;
  they are not call-graph, expression, alias, loop, or liveness keys.
- Build translation-unit call-graph state once in O(functions + direct-call
  edges).  Function analyses remain function-local.
- Give each mutable function an analysis epoch and explicit invalidation bits
  for CFG, def/use, dominators, loops, aliases, and liveness.  A pass reuses a
  valid analysis and invalidates only facts its rewrite can change.
- Use dirty instruction, block, call-site, function, and loop worklists.
  Reconsider a consumer only when a dependency version changes.  Do not run a
  complete-program retry loop or rescan a function once per rewrite.
- Keep transformations deterministic with stable IDs and source ordinals, not
  ordered string maps in hot paths.
- Use function-local arenas or reusable scratch vectors.  Release large
  analysis state after the function unless an explicitly small summary is
  needed by the call graph.
- State a hard instruction-growth, clone, edge-insertion, and analysis-memory
  budget for every level.  Budget exhaustion conservatively skips work and is
  counted in telemetry.
- Keep the typed LowIR-to-MIR-to-ELF path authoritative.  If optimized object
  emission needs a fact after serialized LowIR, the fact must be serialized or
  derivable from LowIR; private frontend side metadata is forbidden.
- Instrumentation must be zero-output and near-zero-cost when disabled.  It
  records counts and nanoseconds, never rendered names on the production path.

Target asymptotic bounds are:

| Analysis or transform | Bound |
| --- | --- |
| call graph, reverse edges, SCCs | O(F + E) |
| one function's CFG, def/use, dominators, loop forest | O(I + E), or standard near-linear dominators |
| sparse slot SSA construction | O(slot accesses + inserted merge facts + relevant CFG edges) |
| value numbering | expected O(I), with typed hash keys; O(I log I) worst-case is acceptable |
| LICM | O(I + E) plus bounded alias checks |
| linear-scan placement | O(I log R), where the physical register set is fixed |
| local simplification after a rewrite | proportional to the dirty region, not the whole function |

Keep module ownership narrow.  `lowir_opt.cpp` remains the pass scheduler;
shared CFG/dominator/loop facts belong in a separate analysis module, call
graph and inlining policy/cloning should not accumulate in one oversized file,
and O2 placement belongs beside the native analysis/lowering modules rather
than in the ELF writer.  Add every new `dev/src/*.cpp` file to the applicable
tool lists in `dev/frontend_source_sets.mk`.  This split is part of the design,
not cleanup deferred until the file audit fails.

## Implemented Rank 0/1 result

Rank 1 is implemented without a special object-only LowIR.  Serialized
optimized LowIR and native-object emission use the same typed `LowirProgram`;
the native writer consumes the prepared program rather than a parallel IR or
frontend side channel.  The one missing source contract was explicit
`noinline`: PA13 now defines and round-trips `no_inline=yes` symbol metadata,
with a PA13 specification test and student-facing syntax/behavior
instructions.  Source GNU `noinline` attributes populate that field.  No
other LowIR field was needed.

The retained implementation provides:

- one dense-ID direct-call graph, Tarjan SCC classification, and stable
  callee-first component order in `lowir_inline_analysis`;
- bounded bottom-up cloning, a batched leaf-call path, explicit recursive,
  EH, size, `no_inline`, and per-caller budget rejections, plus targeted
  no-unwind caller revisits;
- post-inline use of the existing typed weak/internal reachability analysis,
  including counters for every retention reason, followed by dead-body
  pruning before native-object emission; and
- zero-output stats for graph size, SCCs, recursion, call visits and
  rejections, cloned instructions, rewritten callers, reachability, pruning,
  and pass time.  Production graph state uses compact integral IDs and dense
  vectors; rendered names remain outside the hot path.

The frozen-source A/B used immutable compiler binaries from baseline
`6df6bd42` and candidate `1085fcfb`, run under the same load screen.  The
paired census below uses one script and definition for both objects, which is
why its baseline symbol count differs slightly from the earlier exploratory
table.

| Metric | O1 baseline | O1 Rank 1 | Delta | O2 baseline | O2 Rank 1 | Delta |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| median compile wall | 4.98 s | 4.98 s | 0.0% | 5.49 s | 5.45 s | -0.7% |
| object bytes | 3,047,896 | 2,180,872 | -28.4% | 3,010,064 | 2,143,304 | -28.8% |
| `.text*` bytes | 671,678 | 616,160 | -8.3% | 634,803 | 580,448 | -8.6% |
| defined function symbols | 5,528 | 3,342 | -39.5% | 5,528 | 3,342 | -39.5% |
| static calls | 17,213 | 15,220 | -11.6% | 17,209 | 15,216 | -11.6% |

O1 peak RSS improved by 0.3%; O2 RSS was neutral.  Candidate O1 telemetry
reported 5,083 LowIR functions, 135,426 input and 106,082 output
instructions, 17,572 direct edges, 5,028 SCCs, 100 recursive functions,
5,707 inlined calls, 62,607 cloned instructions, 3,045 reachable functions,
and 2,038 pruned weak functions.  Inlining took 63.7 ms and the full optimizer
301.7 ms, versus 316 ms for the comparable baseline optimizer.  Rank 1 thus
materially closes the dominant body/call gap without consuming compile-time
or memory headroom.

The first clean optimized self build exposed an earlier PA29 x86-lowering
bug: when a scalar store source occupied `%rcx`, materializing a spilled
destination address into the same fixed scratch register destroyed the value.
The earliest active PA29 behavior reducer covers both ordinary and relaxed
atomic stores across an EH edge.  Native lowering now chooses `%rax` for the
address scratch only in that conflict; both registers were already modeled as
store clobbers, so the fix adds no allocation, map, or scan.  It does not
change LowIR and requires no PA13 contract addition.

Final validation at `d7d9e93b` is:

- PA29: 265/265; report through PA29: 4,183/4,183;
- full `make test-report`: 5,291/5,291;
- PA39 file audit: zero fatal findings (30 advisory warnings);
- clean 32-worker `cppgm++-self`: 17.89 s wall, 428.55 s user,
  43.20 s system, 229,164 KiB peak RSS; and
- separate 32-worker inception: 1:40.69 wall, 2,761.54 s user,
  69.08 s system, 227,464 KiB peak RSS, with all 191 objects and final
  `cppgm++-inception` matching.

Fixture movement in this phase is intentional.  PA13 gains the public
`no_inline` metadata fixture; PA37 gains bottom-up pruning, source-attribute,
and object-roundtrip coverage; PA29 gains the scratch-clobber behavior/MIR
reducer.  Existing PA28 virtual-base ABI references and PA32 linkage inputs
were corrected to their owning contracts, and the PA37 comparator gained
tests for the canonical/behavioral distinctions used by optimized fixtures.
No tests were placed in `proposed`.

## Rank 2 result

Rank 2 replaces promotion's block-by-all-values/all-slots state with sparse
slot bindings, epoch-indexed transfer scratch, and a packed meet.  It then
adds bounded iterated-dominance-frontier placement for typed scalar phis.  A
packed reverse-dependency graph rejects every phi transitively dependent on
an undefined incoming slot version in O(phis + incoming edges); this was
required by the frozen reducer and avoids a repeated whole-phi scan.

The public representation is the single PA13 `phi` instruction described in
the PA13 student contract and scaffold.  It contains typed `(BlockId,
Operand)` pairs, survives parse/serialize/canonical comparison, and is lowered
at PA29 as parallel predecessor-edge copies.  There is no hidden or
object-only LowIR.  PA29 handles cycles with one typed scratch and rewrites
phi predecessor labels when late force-inlining moves an outgoing edge.
PA37 owns phi placement, simplification, DCE, and scalar-slot promotion.

Active fixture movement is:

- PA13 grammar, validation, canonicalization, and student-facing syntax;
- PA29 ordinary/critical/backedge lowering, parallel-copy cycles, and
  force-inline predecessor correction; and
- PA37 join/loop promotion, pruned placement, forward simplification,
  incomplete-phi dependency rejection, EH exclusion, and object round trip.

The R2a staging requirement called for byte-identical output.  The sparse
commit `eae03449` missed that guard: removal of the obsolete dense-state
budget admitted one formerly skipped large function (`budget_skips` 1,264 to
1,263), reducing frozen output by 278 LowIR instructions, 1,256 object bytes,
and 1,220 `.text*` bytes.  No previously eligible function changed promotion
decisions.  This bounded output improvement is retained as part of Rank 2's
scalar-promotion result, but the missed intermediate guard is recorded rather
than described as exact-output work.

Load-screened ABBA at explicit `-O2`, using immutable Rank 1 (`71e6b7b4`) and
Rank 2 (`9c63791f`) binaries, produced:

| Metric | Rank 1 | Rank 2 | Delta |
| --- | ---: | ---: | ---: |
| median compile wall | 5.370 s | 5.040 s | -6.6% |
| median user time | 4.745 s | 4.560 s | -4.4% |
| paired peak RSS | 359,784 KiB | 361,342 KiB | +0.1% |
| optimizer time | 842.6 ms | 414.3 ms | -50.8% |
| slot-promotion time | 485.6 ms | 60.2 ms | -87.6% (8.1x) |
| optimized LowIR instructions | 95,861 | 91,224 | -4.8% |
| object bytes | 2,143,304 | 2,141,728 | -0.1% |
| `.text*` bytes | 580,448 | 578,860 | -0.3% |
| decoded instructions | 137,631 | 137,978 | +0.3% |

The final object retained the same 3,342 defined functions and linkage census
as Rank 1.  Six independently generated candidate objects were byte-identical.
Telemetry reports 3,735 eligible slots, 1,537 inserted phis with 3,214
incoming edges, 3,055 loads replaced by phi values, no phi-budget skips, and
8.03 MiB peak promotion scratch on the largest function.

The first final frozen compile exposed a transitive incomplete-phi bug in
`normalize_template_witness_source_location`: an omitted outer-loop phi was
still referenced by an emitted inner-loop phi.  The PA37
`440-incomplete-phi-dependency` reducer is defined on every executed path and
fails on the pre-fix commit by emitting a reference to missing `%t0`.
`9c63791f` supplies the packed dependency invalidation and the frozen object
then compiles successfully.

Final Rank 2 validation is:

- `make test-report`: 5,303/5,303;
- PA39 file audit: zero fatal findings (30 advisory warnings);
- clean 32-worker `cppgm++-self`: 18.24 s wall, 432.35 s user,
  41.14 s system, 230,424 KiB peak RSS;
- separate clean 32-worker inception: 1:41.31 wall, 2,816.62 s user,
  67.09 s system, 229,048 KiB peak RSS, with all 197 current objects and the
  final compiler matching byte-for-byte;
- the exact self compiler's frozen median: 27.84 s at `-O0` and 29.82 s at
  `-O3`; and
- the exact self compiler's `lowir_opt.cpp -O3` median: 16.79 s, with all
  three objects byte-identical.

The self/inception changes remain below the 3% rejection threshold.  The
generated-self frozen compile is still above the final 15-second goal, so
Ranks 3--7 remain necessary.

## Rank 3 result

Rank 3 adds one reusable natural-loop forest to the per-function analysis
epoch.  It derives headers and merged latches from dominator backedges, builds
membership and exits with dense epoch vectors, and derives immediate nesting
in time proportional to reported loop memberships.  A CFG mutation explicitly
invalidates the shared graph, dominator, frontier, and loop views.

At O1, LICM moves only nontrapping pure scalar/address instructions without an
explicit debug location and never crosses EH state.  Candidate dependencies
use a packed producer-to-user worklist.  A missing canonical preheader is
created only for a single critical entry edge, only when at least two root
invariants justify it, and under an eight-block per-function budget.  At O2,
typed direct global or nonescaping-slot loads are eligible only when the loop
has no same-object store or conservative call/atomic/unknown-memory clobber.
Simple `i64` induction facts additionally support inverted-exit
canonicalization, power-of-two multiply-to-shift rewriting, and deletion of a
finite loop whose body has no observable or trapping operation and whose
values do not escape the loop.

This work adds no LowIR field and no private object-only representation.  PA13
therefore does not move in this rank: PA37 consumes the public PA13 `phi`
instruction already added by Rank 2.  Active PA37 course reducers cover pure
LICM, nested and multiple-exit loops, bounded preheader creation, divide/EH/no-
preheader/debug negatives, distinct and aliasing globals, unknown-call and
atomic barriers, exit canonicalization, induction strength reduction,
unproven termination, and zero/one/multiple-trip dead-loop deletion.  The
existing `440-incomplete-phi-dependency` reference moves because invariant
global loads now reside before its loops.

The reducer pass found two correctness issues before commit.  Rewriting a
literal's numeric payload while retaining its input spelling serialized a
shift count of `8` instead of `3`; generated constants now clear presentation
spelling.  Inserting a preheader changed block indices while LICM still held
old definition indices; those dense facts are rebuilt immediately after the
CFG epoch changes, and generated preheaders serialize immediately before their
headers so PA13's definition-before-use rule remains true.  The separate PA37
debug object-roundtrip lane also exposed repeated `%dbg_*` presentation names
in existing O0 source LowIR.  Commit `85b4982e` gives every preserved debug
copy a unique deterministic temporary, making that serialized path parseable.

Load-screened alternating Rank 2/Rank 3 frozen O2 compiles produced:

| Metric | Rank 2 | Rank 3 | Delta |
| --- | ---: | ---: | ---: |
| median wall, six runs each | 5.065 s | 5.085 s | +0.4% |
| median user | 4.575 s | 4.610 s | +0.8% |
| median peak RSS | 361,708 KiB | 361,680 KiB | unchanged |
| optimizer time, instrumented sample | 418.4 ms | 420.7 ms | +0.6% |
| optimized LowIR instructions | 91,224 | 91,224 | unchanged |
| object bytes | 2,141,728 | 2,145,208 | +3,480 |
| `.text*` bytes | 578,860 | 582,314 | +3,454 |

The frozen input contains 437 natural loops and 445 backedges.  LICM examines
1,530 candidates and hoists 417 pure instructions in 1.90 ms; it finds no safe
memory load or counted-loop rewrite in this input.  The current backend grows
code for 27 sections and shrinks none after those moves, principally because
the moved values acquire longer live ranges.  This is measured placement debt,
not extra LowIR work, and is an explicit Rank 5 input.

A clean 32-worker self build took 18.84 s wall, 441.29 s user, 42.45 s system,
and 229,264 KiB peak RSS.  Alternating generated-self frozen O3 compiles were
29.815 s for Rank 2 and 30.275 s for Rank 3 (+1.5% wall and user), with peak RSS
within 0.4%.  The projected frozen-workload speedup therefore did not appear,
but both the host and generated-self changes remain below the plan's 3%
rejection threshold.  Rank 3 is retained for its bounded PA37 contract and the
shared loop facts needed by later ranks; Rank 5 must recover the measured
live-range/code-size loss.

Rank 3 validation is:

- `make test-report-through-pa37`: 5,280/5,280;
- `make test-report`: 5,309/5,309;
- PA37 debug-info and debug object-roundtrip lanes: 18/18;
- PA39 file audit: zero fatal findings (28 advisory warnings); and
- every repeated frozen object was byte-identical within its compiler variant.

## Rank 4 result

Rank 4 is implemented as four reviewable changesets.  Commit `50b2b037`
replaces the traversal-order-sensitive flat expression cache with a typed
dominance-scoped table.  One hash entry identifies each compact expression;
packed value stacks are pushed and restored at dominator-tree scope, so a
sibling block cannot hide an available ancestor.  The shared analysis epoch
now also caches dominator children.  The PA37 O1 reducer exercises the prior
sibling-overwrite miss.

Commit `c950139c` adds sparse memory value numbering at O2.  It derives compact
locations from existing typed `addr` and `index` instructions; no LowIR field,
private object form, or rendered name is involved.  Constant offsets and
access widths prove non-overlap for distinct projections.  Repeated unknown
pointer values use one conservative unknown-memory version.  Direct stores
update only overlapping known classes, while unknown stores, ordinary
read/write calls, atomics, and other memory operations update the unknown
version.  `readnone`/`readonly` calls and readonly globals retain only the
facts their public PA13 metadata permits.  Internal memory-version merges are
placed with the shared iterated dominance frontier and renamed in one
dominator traversal; they are analysis facts, not hidden serialized IR.
Functions with EH structure are conservatively skipped.

Commit `1a71f6b4` extracts the expression opcode/type/operand identity into a
single typed module shared by simplification and PRE.  Commit `8bb6be47` then
adds bounded O2 PRE using the public PA13 `phi`.  Fully redundant expressions
at ordinary joins become phis of predecessor values.  Partial insertion is
limited to nontrapping address or integer bitwise work on single-successor
predecessors whose operands are already available.  Critical-edge insertion,
loops, EH, and explicitly located instructions are skipped.  The hard limits
are 64 inserted expressions, 64 phis, 65,536 availability probes, and 32,768
input instructions per function; budget exhaustion never restarts the pass.

Active course coverage now includes direct, derived, distinct, overlapping,
readonly, unknown-pointer, call-effect, branch-merge, atomic, and EH memory
cases; full and partial expression redundancy; a critical-edge negative; an
EH negative; and a 65-candidate fixture proving that exactly 64 insertions are
accepted.  The PRE behavior reducer was also optimized, lowered through the
ordinary native path, and executed with exit status zero.  No PA13 edit is
needed because Rank 2's public `phi`, existing typed projections, global
storage, and call-effect metadata are sufficient.

On the frozen O2 compile, memory GVN builds 539 candidate classes and 391
internal merge versions, probes 1,331 loads, and removes 555 loads in 6.09 ms.
Subsequent cleanup reduces optimized LowIR from 91,224 to 90,542 instructions.
The frozen input offers 67 PRE candidates but none meet the deliberately
conservative insertion policy; PRE spends 5.31 ms and leaves that object
unchanged.  Relative to Rank 3, the final object falls from 2,145,208 to
2,143,000 bytes and `.text*` from 582,314 to 579,593 bytes; decoded
instructions fall by 616.  The small `.eh_frame` increase of 212 bytes and
push/pop increase are placement debt for Rank 5.

Load-screened alternating Rank 3/final-Rank-4 frozen O2 compiles produced:

| Metric | Rank 3 | Rank 4 | Paired delta |
| --- | ---: | ---: | ---: |
| median wall, six runs each | 5.050 s | 5.075 s | +0.2% |
| median user | 4.590 s | 4.610 s | +0.5% |
| median peak RSS | 359,874 KiB | 360,440 KiB | +0.1% |

Every repeated object was deterministic.  `make test-report` passes
5,315/5,315 and the PA39 file audit has zero fatal findings (28 advisory
warnings).  The final 32-worker self/inception lane remains intentionally
deferred until the later ranks have stopped changing the compiler binary.

## Ranked implementation plan

Rank 0 is a prerequisite and does not change output.  Ranks 1 through 7 are
ordered by expected value on the measured workload.  Do not begin a lower
rank until the preceding rank has either passed its gates or has a recorded
measurement showing that its projected value disappeared.

| Rank | Work | Level/owner | Reason for position |
| ---: | --- | --- | --- |
| 0 | Stable optimizer telemetry and reusable analysis epochs | PA37/PA38 infrastructure | Required to distinguish useful work from compile-time regressions |
| 1 | Bottom-up call-graph inlining plus immediate dead-body pruning | PA37 O1/O2/O3; object regression at PA32 if needed | About 340--350 KiB and 4,170--4,202 cppgm-only O1 bodies; 7,279--8,433 excess calls |
| 2 | Sparse slot SSA, cheaper promotion, and phi-capable scalar replacement | PA37 O2; PA13/PA29 surface only if `phi` is adopted | Existing promotion saves 36.9 KiB but costs 466 ms and cannot cross differing joins |
| 3 | Loop discovery, conservative LICM, and loop simplification | PA37 O1 pure operations; O2 memory/induction work | Clang reports about 1,850--1,900 successful LICM actions at every level |
| 4 | Dominator GVN, load elimination, and bounded PRE | PA37 O2 | Clang reports 751--767 successful load PRE/elimination actions |
| 5 | O2 MIR placement, live-range coalescing, and spill/call-cost improvement | PA38 O2 | Remaining movement is important, but common O1 bodies already nearly match Clang; remeasure after rank 1 |
| 6 | Interprocedural argument specialization, dead arguments, and safe body folding | PA37 O2/O3 | Host compilers use it, but the measured count and expected benefit are below inlining/value/loop work |
| 7 | Distinct O3 budgets: unrolling, SLP/SIMD, and selected loop vectorization | PA37/PA38 O3 | Real runtime opportunity, but lowest near-term confidence and highest code-growth/compile-cost risk |

### Rank 0: measurement and analysis ownership

Add or retain counters for:

- direct call edges, SCCs, recursive functions, call sites by eligibility
  failure, successful clones, cloned instructions, caller/program growth,
  newly unreachable bodies, and retained-body reason;
- CFG builds/reuses/invalidations, dominator and loop builds, sparse merge
  facts, promoted slots, blocked join loads, value-table probes, alias kills,
  and dirty worklist pushes;
- MIR value intervals, coalesces, spills by cause, call crossings, fixed-register
  conflicts, frame homes, register copies, and callee-save choices; and
- per-pass nanoseconds and peak transient bytes.

The disabled-stats object must be byte-identical to a stats-enabled object.
Telemetry names may be rendered only after compilation for the audit view.

Create one compact `FunctionAnalysis` owner instead of adding independent CFG
maps to every pass.  It should expose typed views and explicit invalidation;
it must not become a second IR or retain all per-function state for the whole
translation unit.

### Rank 1: production inliner and post-inline reachability

#### R1a. Bottom-up scheduling

Build one typed direct-call graph and reverse graph.  Compute SCCs and a stable
callee-first component order.  Recursive SCCs remain visible to policy rather
than causing the entire member function to be marked permanently ineligible;
calls within the SCC are rejected initially, while calls from outside the SCC
may still inline a bounded member.

Each function publishes a compact summary:

- original and current instruction/block count;
- direct call count and incoming direct-call count;
- return shape, slot/object-copy count, and cleanup/EH shape;
- explicit and inferred unwind/effect/return boundary facts;
- address/relocation use, linkage, object-root, alias, and no-inline facts; and
- summary version.

Eligibility and cost queries must be O(1) from this summary.  When a callee is
simplified or loses a call, enqueue only callers recorded on its reverse
edges.  The current special no-unwind revisit becomes an ordinary dependency
transition rather than a separate unordered-map path.

#### R1b. Profitability and bounded cloning

Use a size/change estimate, not a single source-size cutoff:

- honor force-inline and no-inline first;
- prioritize zero/negative-growth accessors, wrappers, trivial special
  members, and calls that become small after callee simplification;
- strongly prefer a sole remaining direct call when removal of the callee body
  and its EH/unwind metadata makes the whole-program result smaller;
- account for removed call setup, return shuffle, branch, frame, cleanup, and
  post-inline constant arguments;
- keep a per-call clone limit, per-caller growth limit, per-SCC depth limit,
  and translation-unit cloned-instruction limit; and
- use stricter code-neutral/negative-growth policy at O1, moderate profitable
  growth at O2, and a larger but still explicit budget at O3.

No rule may recognize STL spellings or demangled names.  EH compatibility is
proved from typed boundary and region facts.  Cloning appends unnamed typed
IDs in the object-only path; presentation names are created only when
serializable LowIR was requested.

After a clone, run simplify/DCE/CFG cleanup from the inserted instructions and
affected successor blocks using a dirty worklist.  Do not call the full
`prepare_for_inlining` pipeline on the whole caller after every call site.
Update call edges and incoming counts as instructions are inserted or removed.

#### R1c. Prune bodies whose last demand disappeared

Run the existing typed weak/internal reachability analysis after optional
inlining and local cleanup.  The first implementation must not invent a second
weak-linkage policy.  In the frozen source only four weak bodies are retained
by `object_output_root`; most weak bodies remain because live direct-call edges
still exist.

If a stronger inliner removes the last call but a body is still retained,
classify the exact typed reason before changing policy.  Explicit
instantiation definitions, address-taking relocations, lifecycle entries,
vtable/RTTI support, ABI aliases, and genuinely externally required strong
definitions remain roots.  If the existing `object_root` bit conflates a
source-mandated definition with an ordinary call demand, split that fact at
its semantic owner and serialize the distinction in LowIR.  Do not pass a
private emission flag from the frontend around PA37.

This ordering avoids repeating the rejected broad experiment that removed
weak definitions while calls and object contracts still required them.

#### R1 reducers and gate

PA37 O1 coverage must include adversarial definition order, a three-level
wrapper chain, a simplified callee that becomes eligible, a sole-use body that
becomes unreachable, multiple-use budget behavior, mutually recursive and
self-recursive negatives, address-taken and explicit-root negatives, object
returns, multi-return merge state, no-unwind EH wrappers, throwing/EH
negatives, and deterministic budget exhaustion.

Add driver behavior and object-roundtrip coverage for any body-pruning result.
If object binding or COMDAT membership changes, add the earliest PA32
inspection reducer as well.  Re-run the frozen body/call census before any
rank-2 work.  Rank 1 succeeds only if call count and retained bodies fall
materially and generated-self frozen compile time improves without a pass-cost
regression.

### Rank 2: sparse slot SSA and scalar replacement

#### R2a. Replace the dense block-state algorithm without changing output

The current `AbstractState` gives every block vectors sized by all values and
all slots.  Replace it with one CFG/dominator build and sparse per-slot
definitions:

1. collect direct slot accesses and escape facts in one instruction walk;
2. build definition blocks only for eligible slots;
3. place internal merge facts through a bounded iterated dominance-frontier
   worklist;
4. rename slot versions in one dominator-tree traversal with per-slot stacks;
5. collapse merge facts whose incoming versions are identical; and
6. rewrite only loads and stores belonging to proven promotable versions.

The first changeset must preserve exact O2 LowIR and object output while
reducing promotion time and transient bytes.  It is accepted only if the
frozen O2 slot-promotion time falls substantially from 466 ms; a target of at
least 2x is appropriate before building more passes on the analysis.

#### R2b. Quantify and then support differing control-flow joins

Count loads blocked only because predecessor versions differ.  If the count
and generated movement are material, add an explicit typed LowIR merge.
Ordinary hidden phi state is not allowed because optimized LowIR must survive
serialization and object round trip.

The narrow preferred public representation is a `phi` instruction containing
typed `(BlockId, Operand)` incoming pairs.  Before adopting it, complete a
surface audit covering:

- PA13 grammar, parser, serializer, validator, scaffold, and canonical form;
- PA29 LowIR-to-MIR edge-copy lowering and critical-edge handling;
- PA37 O2 generation, simplification, DCE, CFG repair, and object round trip;
- PA38 liveness/copy cleanup after edge copies; and
- debug locations and EH/landing edges.

If this audit shows block arguments are simpler across the existing model,
record that decision before implementation; do not support both forms.  A new
surface requires active course reducers at PA13 and PA29 plus the principal
PA37 O2 tests.  Student-facing main README sections describe only the current
syntax and required behavior.  Efficient dominance-frontier construction and
renaming advice belongs under `Design Notes (Non-Normative)`; migration
history remains here.

After scalar slots, extend scalar replacement only to small nonescaping
objects whose field/base projections are already typed and whose lifetime,
copy, and destructor behavior is completely represented.  Do not infer fields
by parsing names or offsets.

### Rank 3: loops and LICM

Build natural-loop membership from dominators and backedges once per valid
CFG epoch.  Record loop headers, latches, exits, nesting, and a canonical
preheader when it already exists.

At O1, hoist only nontrapping pure scalar computations and typed address
calculations whose operands dominate the loop and whose debug location remains
honest.  Do not hoist loads, calls, allocations, object operations, divides
that can trap, or anything across EH state.  Create a preheader only under a
small edge/block budget.

At O2, add:

- invariant direct-slot/global loads when a typed memory version proves no
  aliasing store or call clobber;
- loop deletion when the body has no observable effect and termination facts
  make deletion valid;
- simple induction-variable recognition, strength reduction, and compare
  simplification; and
- loop-exit canonicalization needed by later bounded unrolling.

Use one loop worklist.  Hoisting an instruction enqueues only its users and
parent loop; it must not restart every loop.  PA37 reducers cover nested loops,
multiple exits, zero/one-trip cases, calls, volatile/atomic access, aliasing
stores, EH, trapping arithmetic, and debug metadata.

### Rank 4: GVN, load elimination, and PRE

First consolidate the existing executable-edge propagation and cross-block
pure-expression reuse into a dominance-scoped typed value table.  An
`ExpressionKey` contains opcode enum, type ID/value, canonical operand IDs,
and operation flags.  It never contains rendered LowIR.

Model memory with compact version counters for proven alias classes:

- nonescaping direct slots;
- readonly globals;
- distinct typed aggregate projections when identity proves disjointness; and
- one conservative unknown-memory class.

A store increments only affected versions.  A call applies its typed
`readnone`/`readonly`/default effects.  Volatile, atomic, EH, and unknown
pointer operations are barriers as required.

Implement dominator GVN and redundant-load elimination first.  Add partial
redundancy elimination only after rank 2 supplies a public merge/edge-copy
mechanism.  PRE has explicit inserted-instruction and inserted-edge budgets;
it is skipped rather than iterated to a fixed point when the budget is
exhausted.

PA37 O2 tests cover same/different alias classes, call effects, branch joins,
loops, invalidation by stores, volatile/atomic barriers, EH, and deterministic
budget limits.

### Rank 5: MIR placement and coalescing

Repeat the common-body and movement census after ranks 1--4.  Proceed only on
movement that remains in shared live bodies; aggregate `mov` counts from
different retained function sets are not evidence.

Prefer improving the existing dense `FunctionFacts` and physical-location
planning before adding another public IR.  At O2, plan LowIR value intervals
and preferred locations per function, accounting for:

- ABI argument/result carriers and fixed-register arithmetic;
- call-clobber masks, EH edges, joins, backedges, and debug ranges;
- zero-cost copies and compatible parameter/source homes;
- callee-save cost versus a spill/reload pair; and
- address-mode folding and rematerializable constants.

A compact transient linear-scan table may assign final physical locations
before MIR emission.  The serialized MIR remains the complete physical MIR
consumed by the encoder; a private virtual-register MIR that bypasses the PA38
dump is not acceptable.  Allocation is O(I log R) with fixed target register
sets and explicit spill budgets.

Then run the existing PA38 local cleanup once with fresh liveness.  Add PA38
O2 structural, behavior, and debug-info reducers for call crossings, critical
edges, fixed-register conflicts, XMM values, EH, callee saves, and spill
fallback.  If only block-local copy selection changes, the earliest reducer
may be PA38 O1; whole-function allocation remains O2.

#### Rank 5 result

The retained implementation uses the existing dense, typed `FunctionFacts`
rather than introducing a virtual-register LowIR or another public contract.
O2 values that are live across an edge may retain their definition-time
physical GPR or XMM location when call clobbers, fixed-register uses, narrow
aliases, EH, and cyclic pressure prove that location safe.  A definition-time
frame fallback supports later acyclic pressure.  Cyclic intervals reserve one
register of headroom because changing an earlier emitted instruction's assumed
location after a backedge has been emitted would be unsound.  Call-crossing
GPRs use callee-saved registers; call-crossing XMM values and all EH functions
keep the conservative frame path.

The implementation maintains fixed-size register-indexed live-location lists,
allocates unnamed diagnostic/frame presentation only when needed, reuses safe
spill homes, and removes physical `mov reg, reg` and `fmov xmm, xmm` identities
in the ordinary PA38 cleanup pass.  Frame-home and location planning were
separated into real `.cpp` owners shared by `cppgm++` and `lowir2native`; the
main lowerer is 2,921 lines and the PA39 audit has no fatal size finding.  No
LowIR surface change, PA13 fixture, or student scaffold edit is needed.

The frozen O2 workload retains 853 edge values and removes 235 identity moves.
Relative to final Rank 4, the object falls from 2,143,000 to 2,142,080 bytes,
`.text*` from 579,593 to 577,716 bytes, and decoded instructions from 139,715
to 139,627.  Alternating immutable binaries gave 5.04 s versus 5.06 s median
wall, 4.58 s versus 4.61 s median user time, and 360,760 versus 360,660 KiB
median peak RSS: compile cost is neutral within host noise.

Active PA38 O1/O2 structural, behavior, and debug reducers cover identity
cleanup, integer and XMM edge retention, call crossings, EH rejection, spill
fallback, and cyclic pressure.  Existing PA38 debug references were refreshed
where already-active direct stores/call rematerialization and copy cleanup had
left stale MIR; the reference still records the resulting debug locations.
The full report passes 5,321/5,321 and the audit has zero fatal findings.  A
full report also found a pre-existing narrow signed frame-to-f80 encoder bug;
the PA29 behavior reducer and correction are retained in `3a7ccfeb` rather
than hiding it behind PA38 placement.

### Rank 6: interprocedural scalar and argument work

Use the rank-1 graph and summaries; do not build a second IPA graph.

- Propagate constants and readonly facts into internal or discardable weak
  callees when all direct calls agree.
- Remove dead parameters only from functions whose ABI is not externally
  observable.  Externally visible signatures keep their ABI even when local
  call setup can be simplified.
- Permit one bounded specialized clone when its estimated savings exceed clone
  cost and every original-body retention rule remains satisfied.
- Consider semantic body equality only for functions with identical typed ABI,
  linkage, EH, relocation, debug, and address-observability facts.

O2 uses code-neutral or shrinking specialization.  O3 may use a modest growth
budget.  The likely low-value tail-recursion and jump-threading cases may be
implemented here only when a reducer or profile identifies them; they are not
standalone priority phases.

#### Rank 6 result

The retained O2 pass reuses the Rank 1 `InlineCallGraph` and its dense
symbol-to-function table after inlining.  One linear call-site scan records
typed integer, floating, and global-address agreement in a packed
parameter-indexed array.  Non-address-observable internal functions are
rewritten in place; discardable nonrecursive weak functions use one bounded
internal clone while their observable definition and ABI remain untouched.
Rooted, addressed, lifecycle, alias, variadic, recursive, mismatched, and
`no_inline` candidates remain conservative.  Removing an argument never
removes its side-effecting producer.

The translation-unit limits are 256 clones and 8,192 cloned instructions.
Scratch storage is one dense parameter mask plus reusable value/parameter
vectors rather than one allocation per candidate.  On the frozen workload the
pass visits 13,402 direct calls and 55,917 candidate-body instructions,
classifies 1,370 candidates, and changes 66 functions.  It substitutes 67
operands, removes 68 parameters and 166 call arguments across 162 call sites,
and creates 66 clones containing 4,821 instructions.  No budget is exhausted;
peak accounted analysis scratch is 739,817 bytes and the measured pass takes
22.5 ms.

Relative to final Rank 5, the O2 object falls from 2,142,080 to 2,127,200
bytes, `.text*` from 577,716 to 575,238 bytes, decoded instructions from
137,901 to 137,352, and relocations from 19,950 to 19,883.  Defined symbol
count rises from 3,342 to 3,352 because some safe internal clones coexist with
still-demanded weak definitions; the total object and executable text both
shrink.  Three load-screened ABBA blocks give 5.150 versus 5.155 seconds median
wall, 4.650 versus 4.665 seconds median user, and 359,758 versus 359,830 KiB
median peak RSS: +0.29%, +0.22%, and +0.10%, respectively.  Stats-enabled and
ordinary objects are byte-identical, and O0/O1 output remains byte-identical
to Rank 5.

PA37 contains an exact O2 transformation fixture and an object-roundtrip
behavior fixture covering uniform and disagreeing arguments, a preserved
effectful producer, global addresses, clone-name collision, weak cloning, and
addressed/rooted/`no_inline` rejection.  Existing PA32 linkage fixtures caught
and prevented specialization of required `no_inline` COMDAT bodies.  The PA37
local report passes 116/116, through-PA37 passes 5,289/5,289, the full report
passes 5,323/5,323, the debug lane is clean, and the PA39 audit has zero fatal
findings.

Multiple specialized versions, semantic-body folding, tail recursion, and
jump threading were not retained.  Only five frozen parameters disagree
across calls, so multiversioning has little measured opportunity; body folding
would add linkage, relocation, EH, debug, and address-identity risk without a
demonstrated dominant body class.  These remain measured low-value candidates
rather than hidden Rank 6 work.

### Rank 7: a distinct O3

Keep O3 as an O2 alias until there is an accepted O3-only transformation.  Do
not add a cosmetic third pipeline.

The first O3 candidates are:

1. full unrolling of very small constant-trip loops;
2. bounded peeling/partial unrolling when it removes a branch or exposes
   vectorizable straight-line work;
3. SLP formation for adjacent independent scalar loads/operations/stores; and
4. selected loop vectorization only after typed dependence and alignment facts
   are sufficient.

Each candidate needs a runtime benchmark, because GCC/Clang O2/O3 show that
these passes may increase object size.  Cap unrolled instructions per loop,
per function, and per translation unit.  Vectorization must use typed SIMD MIR
operations and ordinary PA38 lowering; it may not inject opaque x86 byte
sequences below MIR.

When O3 becomes distinct, extend the PA37 and PA38 tools, help text, harnesses,
student READMEs/scaffolds, driver tests, object round trips, behavior tests, and
debug-info tests to accept and exercise O3.  The no-option driver then selects
that maximum level.  Until then it continues to select the current O2-backed
maximum.

#### Rank 7 result

The retained O3-only transformation is bounded full unrolling for canonical
constant-trip loops. It accepts signed and unsigned integer comparisons, an
exact trip count of at most four, one preheader/latch/exit, a single linear
body path, and no exceptional region. The planner proves induction range and
all outside replacements before mutation. It preserves body instruction and
debug-location order, uses fresh typed value IDs for clones, and maps every
loop-carried value to its final state. Zero-trip and inverted-exit loops use
the same path.

Growth is capped at 64 cloned instructions for a loop, one retained loop per
function, and 4,096 cloned instructions per translation unit. The pass reuses
the shared loop forest, keeps replacements in dense value-ID vectors, scans a
function once, and invalidates CFG analysis once after a retained rewrite. It
does not render or parse LowIR names and adds no hidden object-only data. The
existing PA13 typed LowIR operations express the complete result, so no PA13
syntax, fixture, or student-scaffold change is required.

PA37 now owns the O3 LowIR contract, exact signed/unsigned/zero-trip/side-
effect/budget reducers, 1x/2x/4x scaling fixtures, source-driver and debug
lanes, and source/serialized-LowIR object equivalence. PA38 owns structural,
behavior, and debug coverage for carrying that result through the ordinary
MIR pipeline. The tools and help accept internal level 3, PA38 applies its O2
machine rules at O3, and the ordinary no-option driver now selects O3.

On a no-inline synthetic kernel invoked 12 million times, O3 reduces three
consecutive generated-program runs from 0.36 seconds at O2 to 0.17 seconds at
O3 (about 53%) and reduces ELF text from 352 to 348 bytes. The pass reports
one unrolled loop, four iterations, 20 cloned instructions, five inspected
body instructions, 8,107 bytes of accounted peak scratch, and 34.9
microseconds. The 1x/2x/4x fixtures report exactly 1/2/4 loop visits,
4/8/16 iterations, 4/8/16 clones, and 1/2/4 body-instruction visits while
peak scratch remains 1,967 bytes.

The frozen source contains no loop meeting these deliberately narrow
constant-trip conditions: 442 loops are considered, none is unrolled, and
only 63 body instructions are inspected. Its O3 object therefore remains
byte-identical to Rank 6 O2 at 2,127,200 bytes. O0, O1, and O2 outputs are
also byte-identical to Rank 6. Three load-screened ABBA blocks give 5.160
versus 5.190 seconds median wall, 4.700 versus 4.715 seconds median user, and
360,774 versus 361,396 KiB peak RSS: +0.39%, +0.21%, and +0.04%,
respectively, all within the 3% gate.

Partial unrolling, peeling, SLP, and loop vectorization are not retained in
this rank. They have no demonstrated frozen opportunity, require broader
dependence or growth policy, and would add more compile work than the measured
bounded full-unroll result justifies. The full report passes 5,331/5,331, the
PA37/PA38 debug lane is clean, and the PA39 audit has zero fatal findings.

## Final generated-compiler correctives

The first exact generated-self gate after Rank 7 remained above the objective:
the frozen O0 compile took 30.15 seconds even though the host compiler completed
the same work in about five seconds. Profiling showed the generated compiler
spending most of its time in tiny STL accessors and object copies, rather than
in one expensive optimization pass.

Commit `a30c982f` adds bounded O2 small-object scalar replacement for complete,
nonescaping objects whose accesses prove one scalar type. It uses dense typed
slot/value facts and a union/find over complete copy edges; it does not infer
object structure from names or rendered LowIR. The same changeset lowers
1/2/4/8-byte residual `copyobj` operations as one scalar load/store pair in the
native backend. Active PA37 and PA29 coverage distinguishes complete objects
from escaped and partial objects. Only the pre-existing PA37
`525-pre-address-result-type` optimized fixture changes. The full report passed
5,338/5,338 with zero fatal audit findings. An exact 32-worker self build took
19.57 seconds wall, 458.95 seconds user, 44.64 seconds system, and 229,924 KiB
peak RSS; that compiler reduced the frozen O0 compile to 18.30 seconds.

The remaining corrective has two typed parts. PA13 now defines the function
role `unreachable`, and source lowering attaches it to the compiler intrinsic
together with the existing read-none, no-unwind, and no-return boundary facts.
An O1 CFG pass builds one dense symbol bitmap and removes a conditional edge
whose target begins with that operation. It allocates a block bitmap only for
functions that actually contain a marker block. Ordinary `noreturn` calls
without the role remain untouched. This public contract is covered in PA13,
the existing PA16 boundary fixture, and direct/source PA37 reducers.

At O3, one fresh linear call-graph build follows local scalar and CFG work. A
bounded late inlining wave accepts only single-block leaves, charges optimized
instruction count against a fresh 128-instruction caller budget, and cleans
only rewritten callers. A PA37 reducer proves that a 42-instruction complete
object-copy body remains a call at O2 after shrinking, but is inlined at O3.
On the frozen host compile this wave inlines 1,007 calls and clones 6,272
optimized instructions in 47.6 ms; unreachable-edge cleanup removes 74 edges
in 1.6 ms. The O3 object falls from 2,116,672 bytes before these two parts to
2,056,816 bytes, while a first host compile remains 5.16 seconds. Exact
generated-self and inception results are recorded below.

## Rank 8 final gate and matching-level matrix

The exact clean Rank 8 tree through commit `ad7a88d6` passes the full report at
5,346/5,346 and the PA39 file audit with zero fatal findings (31 warnings). A
fresh 32-worker O3 lane, timed as two separate commands against one explicit
object root, gives:

| Stage | Wall | Aggregate user | System | Peak RSS |
| --- | ---: | ---: | ---: | ---: |
| `cppgm++-self` | 19.27 s | 462.99 s | 43.68 s | 229,004 KiB |
| inception compare | 74.64 s | 2,014.92 s | 64.68 s | 230,788 KiB |
| combined | 93.91 s | 2,477.91 s | 108.36 s | 230,788 KiB |

All inception objects and the final compiler match. The apparent regression
from about 1:11 to about 1:30 was a stage-labeling error: the latter figure
included the roughly 19-second self build. The inception-only delta from the
remembered 71 seconds to 74.64 seconds is not evidence of a regression under
the intermittently loaded host; repeat only the stage-only measurement when a
candidate otherwise warrants another expensive inception run.

A separate fresh O0-generated 32-worker lane also passes every object and the
final comparison. Its combined self plus inception command takes 170.90
seconds wall, 4,759.18 seconds user, 132.80 seconds system, and 229,768 KiB
peak RSS. File modification times place about 19 seconds in self construction
and about 151 seconds in the inception work; future O0 evidence must use two
explicit timers so that approximation is not mistaken for a measured split.

Rank 9a replaces that approximate O0 split with two explicit timers against
one clean object root at commit `45b15f22`:

| O0 stage | Wall | Aggregate user | System | Peak RSS |
| --- | ---: | ---: | ---: | ---: |
| `cppgm++-self` | 19.49 s | 450.53 s | 43.48 s | 230,784 KiB |
| inception compare | 153.97 s | 4,292.13 s | 87.21 s | 228,688 KiB |
| combined | 173.46 s | 4,742.66 s | 130.69 s | 230,784 KiB |

Every O0 object and the final compiler match.  The 2.56-second combined wall
difference from the approximate 170.90-second Rank 8 lane is 1.5%, below the
3% rejection threshold and smaller than expected intermittent host-load
variation.  Aggregate CPU time falls by 16.52 seconds, so this is not evidence
of added compiler work.

Twelve immutable compilers were then built from the same source: GCC, Clang
with GCC's libstdc++ headers and library, and cppgm++ self output, each at
O0/O1/O2/O3. Three frozen compiles were interleaved by round under the same
host conditions. The frozen invocation intentionally has no `-O` flag, so it
exercises the compiler's required default maximum pipeline while the table
compares the quality of compiler executables built at matching levels.

| Executable producer | Build level | Median wall | Median user | Median system | Peak RSS | Frozen object |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| GCC | O0 | 37.16 s | 36.45 s | 0.63 s | 361,492 KiB | 2,056,816 B |
| Clang + libstdc++ | O0 | 30.75 s | 30.17 s | 0.57 s | 360,400 KiB | 2,057,056 B |
| cppgm++ self | O0 | 35.46 s | 34.91 s | 0.59 s | 360,768 KiB | 2,056,816 B |
| GCC | O1 | 6.01 s | 5.49 s | 0.47 s | 361,360 KiB | 2,056,816 B |
| Clang + libstdc++ | O1 | 5.71 s | 5.21 s | 0.49 s | 361,280 KiB | 2,057,056 B |
| cppgm++ self | O1 | 20.23 s | 19.60 s | 0.58 s | 362,396 KiB | 2,056,816 B |
| GCC | O2 | 5.61 s | 5.05 s | 0.48 s | 358,768 KiB | 2,056,816 B |
| Clang + libstdc++ | O2 | 5.61 s | 5.06 s | 0.50 s | 361,560 KiB | 2,057,056 B |
| cppgm++ self | O2 | 18.73 s | 18.18 s | 0.54 s | 362,908 KiB | 2,056,816 B |
| GCC | O3 | 5.21 s | 4.71 s | 0.50 s | 361,796 KiB | 2,056,816 B |
| Clang + libstdc++ | O3 | 5.41 s | 4.90 s | 0.50 s | 363,036 KiB | 2,057,056 B |
| cppgm++ self | O3 | 17.73 s | 17.12 s | 0.53 s | 361,708 KiB | 2,056,816 B |

GCC and cppgm++ produce byte-identical frozen objects at all four executable
build levels. Clang's object is deterministic and 240 bytes larger because of
the already understood host-configuration-family difference. The generated
O0 compiler beats GCC O0 but not Clang O0. At O1--O3 the generated compiler is
about 3.3--3.5 times slower than either host-built peer, so the under-15-second
objective remains open.

Flat `cpu-clock` profiles and symbol census identify two related gaps. The GCC
O1 compiler defines 4,822 functions (1,110 weak, 2,340 global, and 1,372
local), while the cppgm++ O1 compiler defines 36,857 (15,523 weak, 2,338
global, and 18,996 local). The cppgm++ profile spends time in separately
emitted trivial accessors such as `basic_string::size`,
`FixedQueue::operator[]`, `TypeTable::Get`, and `SyntaxArena::IsTag`. In
`Lexer::Peek`, GCC O1 emits 259 bytes and GCC O3 187 bytes; cppgm++ emits 617
and 624 bytes respectively. The generated body retains a 144-byte frame,
repeated address construction and loads, boolean materialization, and a call
to `FixedQueue::operator[]` where GCC operates directly on fields.

The existing O3 late wave rejects 13,277 encountered calls because the
optimized callee is not a single-block leaf. A bounded experiment admitted
small non-EH acyclic callees under the existing 40-instruction callee and
128-instruction caller limits. It exposed two correctness prerequisites:

1. Moving the caller tail into an inline continuation failed to retarget
   successor phi predecessor identities. PA37 now has an active O1 reducer,
   and shared typed edge repair is committed as `9fb2b10b` after a clean
   5,347/5,347 report and zero-fatal audit.
2. Even the unmodified `ad7a88d6` compiler can emit frozen O3 LowIR that fails
   an O0 parse/dump roundtrip: a `phi u8` receives a comparison temporary,
   although PA13 specifies every `cmp` result as canonical `i64`. The direct
   in-memory path masks this source-lowering type mismatch. Broader late
   inlining makes the mismatch reach additional CFG and native-lowering
   shapes, including a missing-temporary failure.

The broad inliner experiment is therefore rejected in its current form. Rank
9a first aligns every source-lowered comparison value with the existing PA13
contract, adds the earliest PA15 source reducer and PA37 serialized/object
roundtrip coverage, and measures fixture movement. Rank 9b then re-evaluates
small callful and multi-block late inlining with typed values, valid phi edges,
the existing hard growth budgets, a 1x/2x/4x work counter, and a new PA37 O3
contract fixture. Only after that gate should later scalar replacement and
copy propagation be expanded inside the newly inlined callers.

Rank 9a found 147 affected checked-in LowIR references across PA15--PA28. The
movement is the direct consequence of PA13's existing result contract:
comparison temporaries are `i64`, and source `bool` storage, arguments, and
returns now use explicit `i64` to `u8` conversions. The references were
regenerated in place through their owning PA targets. A new PA15 course
fixture fixes the earliest source-lowering requirement, and a PA37
object-roundtrip fixture exercises the serialized boundary at O0/O1/O2/O3.
The full frozen O3 LowIR is 6,836,572 bytes and survives an exact O0 parse/dump
roundtrip.

The implementation changes only compact `LowType` values at result creation;
it adds no text repair, string-keyed lookup, extra analysis set, or pass over
the program. Three interleaved old/new frozen compiles show no compile-time
regression. At O0 the old/new median is 4.73/4.72 seconds wall and 4.23/4.26
seconds user. At the default maximum it is 5.20/5.20 seconds wall and
4.71/4.69 seconds user. The corrected frozen objects grow from 2,999,352 to
3,000,784 bytes at O0 and from 2,056,816 to 2,061,016 bytes at the maximum;
this is the cost of making previously implicit narrow `bool` boundaries
explicit and remains a later typed code-generation simplification target.

Rank 9b first admitted every optimized nonrecursive, non-EH body of at most 40
instructions.  That broad policy inlined 1,895 late calls and cloned 20,156
instructions, but it was not profitable: although pruning reduced the frozen
object from 2,061,016 to 2,043,680 bytes, aggregate text grew from 567,619 to
618,634 bytes.  The generated compiler grew to 15,683,352 bytes and an
interleaved self-compiler A/B showed no runtime improvement.  Commit
`3eecc5f4` preserves the typed multi-block implementation and its phi-edge
reducer, but the unrestricted 40-instruction policy is not the retained
profitability endpoint.

The retained policy keeps the 40-instruction limit for a single-block,
single-return leaf and caps a callful or multi-block optimized body at six
instructions.  Compact leaf-shape facts are computed once per function and
updated only for rewritten functions; candidate call sites do not rescan
callee bodies.  A PA37 O3 fixture proves that the optimizer admits small
callful and multi-block bodies after local simplification, and a boundary
fixture proves that a seven-instruction callful body remains a call.  The
1x/2x/4x synthetic audit reports 2/4/8 direct edges, 2/4/8 late call visits,
2/4/8 late calls, and 5/10/20 cloned instructions.

The frozen cap sweep selected six by executable text rather than by total file
size:

| Complex-body cap | Object bytes | Aggregate text | Defined functions |
| ---: | ---: | ---: | ---: |
| leaf-only baseline | 2,061,016 | 567,619 | 3,256 |
| 4 | 2,031,744 | 565,648 | 3,176 |
| 6 | 2,025,496 | 565,513 | 3,161 |
| 7 | 2,024,928 | 567,321 | 3,154 |
| 8 | 2,022,600 | 567,948 | 3,147 |

At cap six the late wave inlines 1,283 calls, clones 7,248 instructions, and
changes 448 callers.  One measured run spends 51.6 ms in that wave and 537.4
ms in the full optimizer.  Three exact same-source interleaved self-compiler
A/B rounds reduce median frozen compile wall time from 17.71 to 17.42 seconds
and average wall time from 17.70 to 17.43 seconds.  Median aggregate user work
falls from 17.16 to 16.92 seconds.  The generated compiler shrinks from
14,673,336 to 14,539,192 bytes.  A second A/B against the retained older matrix
binary is neutral at 17.53 seconds median for both sides, so neither host-load
sample indicates a regression.

The final Rank 9b tree passes 5,352/5,352 report tests and the PA39 file audit
with zero fatal findings.  A fresh 32-worker O3 self build takes 19.11 seconds
wall, 459.05 seconds aggregate user, 42.41 seconds system, and 231,868 KiB peak
RSS.  Its separate 32-worker inception compare takes 73.42 seconds wall,
1,965.06 seconds aggregate user, 62.02 seconds system, and 228,816 KiB peak
RSS.  All 209 objects and the final compiler match.  The retained policy is
committed as `276a5c5d`.

Rank 10a removes an over-conservative early-inliner condition that rejected
every object-named callee inside an EH region.  Object identity is a linkage
fact, not an unwind effect.  The existing compact `no_unwind_` analysis already
combines the serialized public boundary with typed body inference, so the
inliner can safely admit an object-named callee exactly when that analysis
proves it cannot unwind.  Callees that may unwind, contain their own EH, or are
called from a landing handler remain rejected.  This is one deleted branch in
the candidate check and adds no lookup, allocation, or program scan.

Two older fixtures exposed by the stronger policy now state their actual
contracts.  The PA31 reactive-spill input calls a separately compiled opaque
helper, because GCC and Clang both prove the former same-translation-unit
helper nonthrowing and omit its LSDA even at O0.  The PA33 ABI-tag publication
input uses explicit class-template instantiation plus escaped member-function
addresses, because GCC and Clang may omit ordinary inline/template bodies once
all local calls disappear.  Existing LowIR object roots and address references
then retain exactly the definitions the source makes observable; no object-only
LowIR or new metadata field is required.

On the frozen O1 compile, Rank 10a reduces the object from 2,173,384 to
2,007,640 bytes, aggregate text from 614,863 to 597,171 bytes, `.eh_frame`
from 82,056 to 71,496 bytes, and the measured weak/local function population
by 391.  Six-run interleaved explicit-O1 timing is neutral: baseline/candidate
medians are 4.979/4.984 seconds (+0.10%).  The focused PA31/PA33/PA37 report
passes 264/264 and the full report passes 5,354/5,354; the file audit has zero
fatal findings and 29 advisory warnings.

Rank 10b separates ordinary internal call reachability from independent object
publication.  The PA30 typed adapter formerly promoted every nonzero semantic
demand mask on an internal function to `object_root=yes`; evaluated-use and
retained-call demand therefore survived even after the optimizer removed the
corresponding typed call edge.  The adapter now excludes only those two call
reasons.  Address, lifecycle, vtable, static-lifecycle, EH, explicit
instantiation, ABI, and explicit source roots remain permanent.  This is one
constant mask test per adapted symbol; the optimizer continues to use its
existing dense symbol table and typed reachability walk.

Six existing exact O0 LowIR fixtures in PA15, PA17, PA22, and PA25 lose only
the obsolete `object_root=yes` metadata and were regenerated in place through
their documented local reference selection.  Seven PA31--PA36 object-symbol
fixtures that intended to inspect internal mangling now use GNU
`noinline,used`, matching Clang O3 emission rather than depending on an
optimizer retaining an otherwise unobservable body.  That exposed and fixed
lambda-declarator function-control propagation; a PA37 driver reducer checks
`no_inline=yes` on the synthesized call operator, and a second reducer checks
that a call-only internal definition disappears after inlining.

Against Rank 10a, the frozen O1 object falls from 2,007,640 to 1,908,944 bytes,
aggregate text from 597,171 to 585,993 bytes, `.eh_frame` from 71,496 to
62,696 bytes, and measured local definitions from 1,024 to 712.  Weak and
strong counts are unchanged.  Six-run interleaved explicit-O1 timing remains
neutral at 4.990/4.992 seconds (+0.04%).  The full report passes 5,356/5,356
and the file audit has zero fatal findings and 29 advisory warnings.

Rank 10c adds the bounded GCC-style `-finline-functions-called-once` class that
was still absent from ordinary O1.  The existing CSR call-graph construction
marks non-call observation while it scans typed operands and structured object
data.  A weak or internal definition is transferable only when its reverse
edge range contains exactly one call and no address, relocation, alias,
lifecycle, object-root, or other independent use exists.  This adds one byte
per function and no string key, rendered symbol, or second program walk.

The definition-removing class has independent limits of 160 instructions per
body, 320 per caller, and 10,240 per translation unit.  The greater of the
original and simplified body counts is charged.  Exhausting this budget falls
back to the ordinary 128-instruction policy when that policy would have
accepted the call, so the new class cannot suppress an old inline.  A retained
transfer moves instruction-owned payloads through the existing typed renamer
and releases the old body immediately; the final reachability pass remains the
single authority that removes the now-unreferenced definition.

The retained cap is a profitability boundary rather than a backend-safety
workaround.  A clean post-commit reproduction audit compiled the individual
`lowir_loop_simplify.cpp` source and the frozen source at O1, O2, and O3 with
caps 192 and 256 without register exhaustion.  The earlier failure came from
an intermediate broad-policy experiment and is not a current backend blocker.
With the original 10,240-instruction translation-unit budget, cap 256 admits
only 21 more candidates but lets early larger bodies displace 58 smaller
inlines: the object changes by only -88 bytes and one more definition remains.
That source-order budget interaction is handled as a separate policy question.
Ownership transfer is byte-identical to the initial clone-and-release
prototype on the frozen object.

A synthetic 1x/2x/4x audit reports 1/2/4 direct edges, call visits, eligible
single-call candidates, transferred calls, and discarded bodies, with
42/84/168 transferred instructions.  Thus the added analysis and retained
work remain linear in typed operands, call edges, and transferred body size.

Against Rank 10b, the frozen O1 object falls from 1,908,944 to 1,885,744 bytes
and measured definitions fall from 2,645 to 2,589.  `.eh_frame` falls from
62,696 to 60,864 bytes.  Aggregate text rises by 1,551 bytes, from 585,993 to
587,544, because the generated caller shape is not always as compact as the
separate body; the generated compiler's runtime nevertheless improves.  Its
O1-built binary file falls by 6,552 bytes while loaded text rises by 220,240
bytes.  Three alternating exact-self A/B frozen compiles give these medians:

| Frozen input level | Rank 10b wall/user | Rank 10c wall/user | Change |
| --- | ---: | ---: | ---: |
| O0 | 17.21 / 16.66 s | 16.77 / 16.22 s | -2.6% / -2.6% |
| O1 | 18.34 / 17.80 s | 17.98 / 17.44 s | -2.0% / -2.0% |

Three alternating host-built compiler runs at O1 also improve from 5.04 to
5.00 seconds median wall and from 4.57 to 4.52 seconds median user. Releasing
the 453 transferred frozen bodies before the later per-function schedule
offsets the added graph fact and profitability checks.

The separately timed clean O1 self builds were 19.24 and 26.24 seconds wall,
but CPU utilization differed by 655 percentage points; their aggregate user
times were 460.52 and 468.47 seconds.  The alternating single-process A/B is
the acceptance signal on this intermittently loaded host.  PA37 reducers cover
the transferred body, multiple direct calls, address observation, and the
160-instruction boundary.

Rank 10d makes the existing bounded optimized-body wave part of O1 and O2 as
well as O3.  This is one late wave after the transformations selected by the
level, not one wave per inherited level.  It rebuilds the typed call graph
once and retains the established limits: 40 instructions for a call-free
single-block leaf, six for a callful or multi-block body, and 128 cloned
instructions per caller.  The telemetry and helper names are level-neutral so
the implementation does not retain a false O3-only contract.

Two existing PA37 fixtures move.  The O1 growth-budget fixture now demonstrates
that three calls become eligible only after the callee folds to one addition;
the remaining calls still prove deterministic budget exhaustion.  The O2 IPA
fixture now demonstrates that a constant-specialized weak body can be inlined
and pruned after specialization.  PA38 has no movement.  The student-facing
contract describes the selected level's single late wave and its exact limits.

Against Rank 10c, frozen O1 performs 1,007 additional inlines and clones 8,872
optimized instructions.  The object falls from 1,885,744 to 1,824,328 bytes,
defined functions from 2,589 to 2,424, and `.eh_frame` from 60,864 to 56,588
bytes.  Aggregate text rises from 587,544 to 598,690 bytes.  The O1-generated
compiler has 1,564 fewer measured function symbols; its loaded text rises from
8,464,124 to 8,659,986 bytes.  Three alternating exact-self frozen O1 runs
improve median wall time from 17.99 to 17.66 seconds and median user time from
17.42 to 17.09 seconds, both about 1.9%.  Peak RSS is neutral to slightly lower.
The clean O1 self build takes 19.61 seconds wall, 459.79 seconds aggregate user,
43.64 seconds system, and 229,928 KiB peak RSS.  The full report passes
5,358/5,358 before that self build.

Rank 10e removes the source-size anomaly in the called-once translation-unit
budget.  The minimum remains 10,240 instructions, but a larger input may
transfer at most one original translation unit of instruction payload.  The
existing per-function summary loop accumulates that input count with saturating
`size_t` arithmetic, so the policy adds no scan, symbol lookup, string key, or
asymptotic work.  The 160-instruction body and 320-instruction caller limits
remain unchanged.

On frozen O1 the effective 135,662-instruction budget transfers 2,014 of 2,015
eligible single-call bodies and charges 41,602 instructions.  Against Rank 10d,
the object falls from 1,824,328 to 1,786,440 bytes, `.eh_frame` from 56,588 to
53,340 bytes, and measured definitions from 2,424 to 2,321.  Aggregate text
rises from 598,690 to 603,829 bytes.  The generated compiler file falls from
13,160,440 to 13,054,328 bytes while loaded text rises from 8,659,986 to
8,687,766 bytes.

Three alternating exact O1-self frozen compiles are neutral within host noise:
baseline/candidate median wall is 17.48/17.55 seconds and median user is
16.95/17.04 seconds.  The clean candidate self build takes 19.06 seconds wall,
458.20 seconds aggregate user, 43.22 seconds system, and 229,340 KiB peak RSS.
No PA37 or PA38 fixture moves; the full report remains 5,358/5,358.

Rank 10f adds a stats-only census after final reachability pruning.  It rebuilds
the compact typed CSR graph only when a `Stats` sink is present and records
discardable definitions in a seven-by-eleven dense matrix.  The direct-use
buckets are 1, 2, 3, 4, 5--8, 9--16, and 17 or more; instruction buckets are
0--1, 2, 3, 4, 5--6, 7--8, 9--12, 13--20, 21--40, 41--160, and 161 or more.
Definition, call, and body-instruction totals share the same fixed indexing.
Ordinary compilation performs no census work and carries no persistent graph.

The first frozen O1 census finds 1,162 discardable retained definitions with
4,134 live direct calls and 76,460 body instructions.  The decisive population
is not initially multi-use: after dead callers disappear, 656 definitions have
exactly one live call, and 600 of those contain at most 160 instructions.  The
initial and late inliners classified them against graphs that still contained
the subsequently pruned callers.  This establishes post-pruning called-once
cascading as the next policy step before any positive-growth multi-use policy.
The remaining population includes 210 leaf, 299 EH-bearing, 80 recursive, and
317 explicit-no-inline definitions; these categories overlap.

Rank 10g consumes the population established by that census.  After the first
reachability prune it builds one compact typed CSR call graph and runs only the
definition-removing single-call policy.  The graph is processed in callee-first
order, so a transferred callee can become part of a transferred caller without
an unbounded whole-program fixed point.  A dense byte marks only callers of
single-use discardable definitions; unrelated functions are skipped before an
instruction scan.  The wave retains the 160-instruction body,
320-instruction caller, and proportional translation-unit limits, uses the
already optimized body counts, prepares only rewritten callers, and performs
one final reachability prune.

On the frozen O1 input the post-prune graph has 10,401 direct edges.  The wave
transfers 59 bodies containing 2,935 instructions into 38 callers, exhausts no
budget, and reduces measured definitions from 2,321 to 2,257.  The object falls
from 1,786,440 to 1,768,584 bytes and `.eh_frame` from 53,340 to 51,632 bytes;
aggregate text rises from 603,829 to 609,202 bytes.  The final census falls to
1,103 discardable definitions, 4,075 calls, and 75,972 body instructions.

Three serial alternating exact-O1-self frozen compiles remain within the
declared host-noise gate.  Baseline/candidate median wall is 17.48/17.78
seconds (+1.7%), median user time is 16.93/17.27 seconds (+2.0%), and RSS is
neutral.  The clean candidate O1 self build takes 20.11 seconds wall, 462.13
seconds aggregate user, 43.55 seconds system, and 228,468 KiB peak RSS.

The first candidate self build exposed an earlier PA29 backend defect rather
than an inliner limit: a wide comparison always required a fresh scalar result
GPR, even when its only consumer was a branch, and its comparison-as-value
path had no frame fallback.  PA29 now lowers a sole-use `i128` comparison as
direct high-word decisions plus an unsigned low-word tie-break.  A retained
Boolean result spills to a typed temporary home when the preserved GPR pool is
full.  The active PA29 course suite includes a structural direct-branch oracle,
all ten wide predicates, and a five-live-parameter pressure reducer that the
prior compiler rejected.  These new references were generated through the
documented local `REF_TEST_APP=../dev/lowir2native` path because the pinned
bundle predates this backend correction.  The full report passes 5,362/5,362
and the PA39 file audit has zero fatal findings.  The audit also caused the
duplicate `lowiropt` telemetry printer to be replaced by the existing shared
driver report module.

### Rank 10h: close the EH eligibility boundary before changing scheduling

Keep the current EH correction separate from convergence work so a broader
schedule cannot hide an eligibility or native-lowering defect.  A call inside
an active caller EH region does not require its callee to be nonthrowing when
the callee has no EH control instructions of its own.  Ordinary potentially
throwing calls cloned from that body remain between the caller's existing
`eh_try`/`eh_cleanup` and `eh_end` markers and inherit the caller's landing
destination.  Landing blocks and callees containing `eh_*`, `throw`,
`exception`, `exception_selector`, or `resume` remain ineligible.

The retained change moves 96 post-prune bodies containing 5,465 instructions.
On an exact same-source comparison against Rank 10g, the frozen O1 object falls
from 2,065,008 to 2,026,024 bytes (-38,984), measured definitions fall by 135,
and `.eh_frame` falls by 3,956 bytes.  Aggregate text grows by 10,868 bytes and
LSDA by 167 bytes, so the win is definition and unwind-metadata removal rather
than a local instruction-count reduction.  Total inlined calls rise by 362,
including 95 more calls in the late wave and 22 more in the post-prune wave;
the retained discardable population falls by 184 definitions and 190 calls.

The first self-host attempt exposed an independent PA29 scalar atomic-load
pressure failure; the corrective reducer requires an atomic result live across
a call to use a typed frame home when all preserved GPRs are occupied.  The
from-scratch 32-job O1 self build then completes in 19.49 seconds wall, 463.69
seconds aggregate user, 60.03 seconds system, and 228,696 KiB peak RSS.  Three
host-built ABBA blocks are neutral (candidate/baseline median wall -0.36%, user
+0.20%, RSS -0.91%).  The exact generated-self raw medians are 19.175/19.345
seconds wall (+0.9%), 18.600/18.745 seconds user (+0.8%), and 398,558/398,010
KiB RSS (-0.1%).  One final baseline sample was externally loaded, so the
block-paired result is not used as evidence; both uncontaminated raw medians
remain inside the 3% noise gate.  The focused PA29/PA37/PA38 report passes
458/458, the full report passes 5,365/5,365, and the audit has zero fatal
findings and 32 warnings.

### Rank 10i: incremental cleanup-coupled inliner convergence

Rank 10g proved that a callee-first order can transfer an already eligible
acyclic chain without a whole-program fixed point.  It does not make the
current three fixed graph snapshots a convergence algorithm.  In the initial,
late, and post-prune waves, a body inlined into a callee is not locally cleaned
before every parent evaluates the callee's new cost and shape.  A deeply
layered wrapper in which each level becomes eligible only after its child is
inlined and simplified can therefore expose only part of the chain.

The extended reference compiler demonstrates the missing behavior, but not
the desired implementation architecture.  It repeatedly inlines within a
function, runs cleanup after batches of eight successful calls, and performs
up to four complete O1 program rounds.  It also uses much broader policy
limits: an ordinary small body may contain 24 instructions, a proven
nonthrowing body 32, and a preferred-local body 40; caller limits are normally
768 instructions and 96 blocks.  The current cppgm++ late wave limits a
callful or multi-block body to six instructions and gives an ordinary caller
128 cloned instructions.  Reference parity therefore cannot be evaluated by
adding a four-round loop while retaining the current policy.

A serialized-path scheduling experiment applies the current O1 optimizer to
its own result.  It is deliberately not an acceptance benchmark because every
application reparses the program and resets original-size and growth budgets.
It shows both a real convergence gap and the poor economics of naive retries:

| O1 applications | Retained definitions | Textual calls | Serialized LowIR bytes |
| ---: | ---: | ---: | ---: |
| 1 | 1,882 | 15,886 | 9,291,051 |
| 2 | 1,777 | 15,801 | 10,122,216 |
| 3 | 1,754 | 15,745 | 10,564,784 |
| 4 | 1,742 | 15,723 | 10,777,689 |

The additional rounds remove 105, then 23, then 12 definitions while growing
the serialized program by 16%.  The latest prototype census still contains
1,100 discardable definitions and 4,704 calls.  Of those definitions, 505
have one live call and 595 have multiple calls; only 212 have the current
strict leaf shape.  The overlapping blocked populations include 368
EH-bearing, 343 explicit-no-inline, and 89 recursive definitions.  Scheduling
is therefore a material problem, but repeated called-once waves alone cannot
close the GCC/Clang gap.

#### R10i-a. Publish cleaned nonrecursive bodies in one graph traversal

The reducer changed the required scheduler design.  In the current ordinary
policy, recursive targets are rejected and every direct edge cloned into a
caller points to a descendant of the cloned callee.  The existing CSR
callee-first order has therefore already visited every possible target before
it visits the caller.  A versioned reverse-caller queue would duplicate that
order without exposing another acyclic candidate.

Retain the existing graph and dense cached instruction-count, leaf/callful,
and EH-shape arrays.  For each nonrecursive function in stable callee-first
order:

1. evaluate calls from those O(1) facts under the existing monotonic six/128
   policy;
2. perform the bounded inline batch;
3. if the body changed, immediately run local simplification, effect-aware
   DCE, and CFG cleanup on that function; and
4. republish its dense shape facts before advancing to its callers.

The cleanup is reached through a direct non-owning function pointer and typed
caller context, not `std::function`, strings, or a heap-allocated pass object.
Each ordinary function is expanded once.  A changed function receives at most
one additional full local cleanup in the common acyclic path, so this remains
proportional to input IR plus cloned instructions.  The existing candidate
checks make one-block and no-simplification bodies cheap.  A finer inserted-
instruction worklist would require new def-use maintenance but produced no
frozen output benefit at the retained policy, so defer it until a profile
shows repeated local cleanup rather than inlining policy is material.

The PA37 reducer places both parents before an eight-level wrapper chain.  The
old pass leaves two calls to the top wrapper because its newly constant dead
branch is not removed before the parents are considered.  The retained pass
inlines both parents in the same traversal and removes all unreachable calls.
The 1x/2x/4x ladder is exactly linear in work: functions 11/22/44, input
instructions 109/218/436, late call visits 6/12/24, late calls 6/12/24,
simplifier and DCE runs 43/86/172, and CFG runs 24/48/96.  Frozen O1 output is
byte-identical under six/128.  Two host-built ABBA blocks measure candidate
wall +0.58%, user +0.64%, and RSS +0.73%, all within the 3% noise gate.
Two exact-self ABBA blocks measure candidate median wall 19.315 versus 19.530
seconds (-1.1%), user 18.750 versus 18.945 seconds (-1.0%), and RSS +0.6%; the
paired medians are wall -2.53%, user -2.36%, and RSS +1.01%.  A first clean
self build ran during host contention at 21.03 seconds wall and 501.00 seconds
user; the immediate clean repeat is 19.31 seconds wall, 465.31 seconds user,
43.90 seconds system, and 228,472 KiB peak RSS, matching the R10h range.  The
full report passes 5,366/5,366 and the audit has zero fatal findings and 32
warnings.

A reverse-caller worklist remains appropriate only when a later phase changes
incoming-use eligibility, or after the separately bounded recursive snapshot.
At that point maintain exact typed edge counts and enqueue only affected
callers; do not add a generic fixed-point loop to this ordinary acyclic path.

#### R10i-b. Select broader O1 profitability after convergence works

Do not copy the reference's 24/32/40 and 768/1,024 limits blindly.  First use
the converged scheduler with the retained six/128 policy as an output-neutral
schedule baseline.  Then sweep callful/multi-block caps of 8, 12, 18, and 24
and caller growth budgets chosen from 192, 320, 512, and 768.  Record successful
calls by body shape, cloned and cleanup-removed instructions, retained
definitions, calls, executable text, EH metadata, and generated-self frozen
runtime.  O1 selects the smallest policy that materially improves generated
compiler runtime without exceeding the compile-time/RSS gate; O2/O3 may use
larger budgets only when separately measured.

Profitability must recognize typed shape, call/return setup removed, constant
arguments exposed, last-use body removal, and post-inline cleanup.  It must not
recognize STL or demangled spellings.  The earlier unrestricted 40-instruction
experiment remains evidence that body-count reduction alone can grow text and
fail to improve runtime.

The measured diagonal sweep rejects every broader general policy.  All three
generated-self samples per point are deterministic:

| Late body/caller cap | Frozen object | Frozen text | Definitions | O1-self compiler | Frozen wall median | Frozen user median |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 6 / 128 | 2,026,024 B | 528,724 B | 2,395 | 13,131,784 B | 19.84 s | 19.25 s |
| 8 / 192 | 2,021,944 B | 533,540 B | 2,373 | 13,177,760 B | 20.14 s | 19.50 s |
| 12 / 320 | 2,023,936 B | 545,014 B | 2,347 | 13,324,504 B | 19.73 s | 19.16 s |
| 18 / 512 | 2,031,880 B | 561,894 B | 2,304 | 13,522,576 B | 21.70 s | 21.01 s |

The 24/768 point is rejected before self-hosting: it produces a 2,148,064-byte
object with 660,142 bytes of text and raises the one-run host compile from
5.79/5.19 seconds wall/user to 6.58/5.96 seconds.  The 8/192 point saves only
4,080 object bytes while slowing the generated compiler by 1.5%.  The 12/320
runtime movement is noise, while frozen text grows by 16,290 bytes and the
compiler by 192,720 bytes.  The larger points are clearly worse.  Restore
6/128 exactly.  A future broader policy needs a measured typed benefit model;
raising size and budget caps alone is not an optimization.

#### R10i-c. Admit recursive members once without recursive expansion

The current test `call_graph_.recursive[target]` rejects every call to a
recursive SCC, including calls from outside that SCC.  Merely changing it to
reject same-SCC edges is unsafe: after cloning a recursive body, a rescan of
the external caller could treat the cloned backedge as another external call
and expand forever.

Keep recursive targets out of the main convergence queue initially.  After
the nonrecursive queue is stable and SCCs have been refreshed once, take a
stable snapshot of eligible calls whose caller and callee are in different
SCCs and whose callee SCC is recursive.  Process only those snapshotted sites
under a separate clone and growth budget; calls introduced by their bodies are
not added to the snapshot.  Calls within the recursive SCC therefore remain
ordinary calls.  Clean the changed callers, then resume only the nonrecursive
dirty queue so wrappers above the newly exposed body may converge without
ever reconsidering a recursive target.  This supplies a precise one-expansion
boundary without serialized flags, per-instruction hidden metadata, or an
arbitrary retry cap.

If the frozen census and generated-self profile show no material opportunity
from these 89 overlapping recursive definitions, record that disposition and
retain the conservative rejection instead of adding the tail phase.

The typed late-wave census rejects this phase before implementation.  The
frozen graph contains 188 calls within recursive components and 207 calls
from other components into recursive targets.  Applying every existing late
eligibility rule except the recursive-target rejection leaves only eight call
sites, seven callers, and three target definitions, for a total upper bound of
46 cloned instructions.  This is too small to move the generated compiler's
runtime materially, while the stable-site snapshot, a second graph update,
and wrapper propagation would add production machinery to every optimizing
compile.  Keep all recursive targets rejected.  The diagnostic was an
out-of-band typed census and was removed after measurement, so ordinary
builds gain no scan, counter, string lookup, or storage.

#### R10i-d. Preserve frame-address identity through native phi placement

The first final R10i inception attempt exposed a PA29 backend defect rather
than an inliner-policy failure.  R10h made the valid optimized body of
`lowir_interprocedural_specialization.cpp` large enough to carry the address
of a local semantic `Function` object through a representation-preserving
copy and a `phi ptr`.  LowIR still named the address value correctly.  Native
parallel-copy lowering instead saw only an `OP_FRAME` machine location and
loaded the first eight object bytes as the pointer passed to
`specialize_function`, producing a generated-compiler SIGSEGV at O2/O3.
Restoring the pre-R10h EH restriction avoided the shape but did not correct
the backend, so the profitable EH policy remains retained.

The PA29 fix attaches one compact Boolean address-value fact to each transient
phi move.  Address sources are not equal to scalar values stored at the same
frame location, do not create false parallel-copy dependencies, and are
rematerialized with `lea` before being placed in the ordinary pointer phi
home.  There is no string key, rendered operand comparison, new program scan,
or serialized side channel.  The lowerer-facing phi adapter moved from the
3,009-line `lowir_native.cpp` into the existing phi-lowering module; the main
lowerer is now 2,938 lines and the audit remains below its fatal boundary.

The active PA29 behavioral reducer takes both incoming edges and records the
informational MIR.  The old compiler exits with signal 11 and shows
`load.ptr` from the local object; the retained reference exits successfully
and shows `lea` on each edge.  PA29's normative requirement and non-normative
Design Notes state the value distinction without maintainer history.  The
reference was generated through the documented local
`REF_TEST_APP=../dev/lowir2native` path because it is a newly corrected backend
contract.

The exact retained tree passes PA29/PA37/PA38 at 460/460, the full report at
5,367/5,367, and the PA37/PA38 debug lanes.  The PA39 audit has zero fatal
findings and 32 advisory warnings.  A clean 32-worker O3 self build takes
19.10 seconds wall, 467.79 seconds aggregate user, 42.96 seconds system, and
230,828 KiB peak RSS.  The separate clean inception comparison takes 69.76
seconds wall, 1,864.78 seconds aggregate user, 61.44 seconds system, and
231,596 KiB peak RSS; all 209 objects and the 12,482,168-byte final compiler
match byte-for-byte.

Three interleaved compiles with that exact self binary give these medians:

| Diagnostic | Wall | User | System | Peak RSS | Object bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| frozen explicit O0 | 14.96 s | 14.42 s | 0.55 s | 363,076 KiB | 2,965,936 |
| frozen default maximum | 16.74 s | 16.22 s | 0.51 s | 359,628 KiB | 1,659,296 |
| `lowir_opt.cpp` explicit O0 | 8.66 s | 8.40 s | 0.25 s | 177,252 KiB | 1,373,792 |
| `lowir_opt.cpp` default maximum | 9.35 s | 9.09 s | 0.23 s | 182,680 KiB | 712,376 |

Every three-run object set is deterministic.  O0 now crosses below 15 seconds,
but the maximum pipeline remains 1.74 seconds above the objective.  The final
typed census contains 934 discardable definitions, 3,904 calls, and 75,963
instructions; overlapping populations include 193 leaf, 299 EH-bearing, 80
recursive, and 317 explicit-no-inline definitions.  No remaining multi-use
leaf has nonpositive static growth under the existing estimator.  A next
inlining class therefore needs measured runtime benefit, not another global
cap increase.

#### R10i reducers and acceptance order

PA37 O1 owns the scheduling contract.  Add exact course reducers for an
eight-or-more-level wrapper chain, adversarial definition order, a child whose
inline-plus-cleanup transition makes each parent eligible, multiple parents
receiving one summary notification, deterministic budget exhaustion, and the
1x/2x/4x work ladder.  Recursive coverage includes self and mutually recursive
same-SCC negatives, one external-to-SCC expansion whose cloned recursive call
remains, and a nonrecursive wrapper above that expansion.  Existing fixture
movement is updated in place and recorded; PA13 changes are unnecessary
because no LowIR syntax or semantic fact is added.  Add PA38 coverage only if
the resulting MIR exposes a new machine-level behavior or failure.

Implement and validate the phases separately:

1. finish and commit Rank 10h after its PA29/PA37/PA38 and full-report gates;
2. land R10i-a with the current six/128 policy and prove work scaling;
3. retain one R10i-b policy point only after exact generated-self A/B timing;
4. evaluate and either retain or explicitly reject R10i-c; and
5. rerun the retained census before selecting any further inlining class.

Every retained changeset runs its owning reports and the full
`make test-report` before self-hosting.  Use alternating immutable exact-O1-self
frozen compiles for the primary runtime signal.  The final retained inliner
then requires the zero-fatal audit, a clean timed self build, and the separate
timed 32-worker inception comparison with peak RSS.

### Rank 11: remove the EH eligibility and value-harvesting wall

`INLINER-FEEDBACK.md` adds a call-site-weighted census to the definition census
above.  The central result is actionable: 6,861 of 13,410 remaining static O3
calls are constructor/destructor calls, while the largest O3 rejection classes
are callee EH (4,487), landing blocks (3,592), and `no_inline` (2,337).  Memory
GVN and PRE skip 610 EH-bearing functions, and native edge-register placement
also rejects an entire function when it contains any EH structure.  This
explains why the R10i-b cap sweep moved definitions without improving the
generated compiler: the sweep did not change the eligibility or harvesting
walls around the dominant call population.

The basic-string case corrects the feedback's ordering.  The GCC 15 libstdc++
header does not attach `_GLIBCXX_NOEXCEPT` or a GNU `nothrow` attribute to
`~basic_string`; it relies on the C++11 rule that a destructor without an
explicit exception specification has the specification implied by its bases
and members.  The same-header probe
`static_assert(noexcept(value.~basic_string()), "")` passes GCC and Clang but
fails cppgm++.  Our O0 LowIR consequently omits `unwind=no` from
`~basic_string`, even though it correctly preserves `throw()` as `unwind=no`
on `_M_destroy` and the explicit `noexcept` on `operator delete`.

The host pipelines expose two further, distinct steps.  Clang O0 marks the
destructor `nounwind` and retains a terminate landing around `_M_dispose`;
Clang O1 inlines the chain and reduces it to the local-buffer check plus
`operator delete`.  GCC's initial GIMPLE likewise contains
`eh_must_not_throw`, but its O0 EH pass already proves the `_M_dispose` chain
nonthrowing and removes that handler; GCC O1 then inlines the chain completely.
Manually adding only `unwind=no` to our probe lets the current O1 strip remove
the destructor cleanup and one call, but `_M_dispose` remains opaque under the
six-instruction late cap.  Therefore the next work is not one magic host pass:
it is (1) the missing standard exception specification, (2) preservation of
the source inline hint, and (3) the existing body no-unwind/cleanup machinery,
in that order.

There is no frozen-source GNU `nothrow` shortcut hiding this case.  The exact
preprocessed frozen input contains 160 `always_inline` and three `noinline`
attributes, both already represented by `force_inline`/`no_inline`, but no
`nothrow` attribute.  It does contain 35 `pure`, 18 `const`, and 25 `cold`
attributes.  Those deserve a separate effects/profitability audit below, but
none occurs on the `basic_string` destructor chain.  No phase may recognize a
`std` name, ABI spelling, or libstdc++ header path.

Two safety qualifications from the first review still apply.  The existing
`no_unwind_` bitmap contains useful callee facts but cannot itself prove an
EH-bearing function nonthrowing because `infer_no_unwind` deliberately marks
that function unsafe.  A residual region-removal phase needs an explicit
region-local proof followed by publication of the newly EH-free function fact.
Also, a call in a landing block must be proven not to unwind; merely containing
no EH control instruction is insufficient while another exception is active.

#### R11a. Implement implicit destructor exception specifications

Fix the semantic fact before changing the optimizer.  A user-provided as well
as an implicitly declared destructor with no `noexcept`-specifier receives the
exception specification implied by the destructors of its potentially
constructed base and member subobjects; the destructor body does not determine
that public specification.  An exception escaping a resulting nonthrowing
destructor reaches the existing terminate boundary.

Generalize the current deferred defaulted-destructor machinery rather than add
a late lowering guess.  Mark every destructor without an explicit exception
specification as a completed-class fact.  Extract a non-mutating typed
subobject exception-specification computation from
`CompleteDefaultedDestructor`; keep deletion and triviality changes exclusive
to genuinely defaulted destructors.  Reuse `FunctionIsNonthrowing` and its
existing deferred/in-progress/succeeded state so class templates and nested
subobjects are evaluated once on demand.  Include potentially constructed
virtual bases and arrays, and conservatively diagnose a real recursive
exception-specification dependency.  Publish the canonical
`BindingRecord::nonthrowing` and terminate boundary before LowIR lowering, so
the existing `unwind=no` field carries the fact with no new IR syntax.

The earliest semantic/lifecycle owner gets exact tests for user-provided
destructors with (a) only nonthrowing scalar/subobject destruction and (b) a
`noexcept(false)` member or base.  PA18 covers virtual override compatibility,
PA19 covers dependent class-template subobjects, PA21 covers `noexcept(...)`
and the minimal basic-string assertion, and PA26 covers a body call/throw that
must terminate rather than unwind through the caller.  PA37 adds a source
driver reducer proving the corrected boundary reaches optimized LowIR.  Locate
the earliest changed assignment with through-PA reports and regenerate all
intentional boundary/EH fixture movement in place; this semantic correction is
expected to move broad O0 LowIR and must be committed separately.  Student
READMEs state the exception-specification and terminate requirements only.

Before moving on, rerun the frozen destructor census: total definitions,
definitions with `unwind=no`, EH-bearing definitions, static destructor calls,
and terminate/`_Unwind_Resume` sites.  The available source-LowIR census has
706 destructor definitions, 446 without `unwind=no`; 278 of 482 `std` destructor
definitions lack the boundary, including `basic_string`, `shared_ptr`,
containers, and pairs.  Treat these numbers as an opportunity estimate, not a
license to mark every destructor nonthrowing; the typed subobject rule decides.

Implementation result (2026-08-21): every destructor with an omitted
exception-specification now defers to one cached completed-class computation.
The computation walks typed nonvirtual bases, virtual bases, and members;
defaulted destructors alone retain the separate deletion/triviality mutation.
Out-of-class definitions, class-template instantiations, and explicit member
specializations reuse the canonical result.  A typed `terminate` runtime role
was added at the existing PA13 role boundary so standalone native emission can
materialize the corrected terminate edge without recognizing a symbol name.

Active reducers now live in PA13, PA16, PA19, PA20, PA21, PA28, and PA35.  One
standard-invalid PA30 definition was corrected to repeat its declared
`noexcept(false)`, and two existing PA26/PA37 references moved for the newly
visible boundary.  The full report is 5,374/5,374 with zero fatal audit issues.
Against the retained pre-R11 host-built compiler, frozen O1 object size moved
from 1,733,112 to 1,655,944 bytes, `.text*` from 619,577 to 596,226 bytes, and
defined symbols from 2,976 to 2,886.  Three consecutive host-built frozen
medians are 4.79 seconds at O0 and 5.27 seconds at maximum optimization; the
final generated-self O0/O3 and inception lanes remain the combined Rank 11
gate.

#### R11b. Preserve the C++ inline hint across LowIR

The semantic model already records `inline_function` for explicit `inline`,
in-class definitions (including `~basic_string` and `_M_dispose`), and template
ODR/emission decisions, but LowIR currently discards that fact.  Add compact
`inline_hint=yes` function metadata at the PA13 boundary.  It is a bounded
profitability hint, distinct from mandatory GNU `always_inline`
(`force_inline`), prohibitive GNU `noinline`, ELF `prefer_local`, weak binding,
and object retention.  Carry one Boolean through the typed PA15 model,
PA30/object adapter, parser/serializer, and optimizer; do not recover it from
symbol spelling or linkage.

PA13 owns syntax, parse/dump/roundtrip, scaffold, and student instructions.
PA15 owns explicit free-function `inline` lowering; the first class assignment
owns the implicit in-class-definition producer; PA19 covers template
specializations; and PA37 O1 owns positive, negative, size-budget, and
1x/2x/4x optimizer behavior.  Source LowIR gains the field broadly, but the
early relaxed comparator treats optional policy metadata as non-layout; add
targeted owning references rather than churn unrelated source fixtures.  The
direct source-object path and serialized LowIR object path must remain
byte-identical.

Start inlining policy at the retained six/128 baseline.  Measure a separate
bounded inline-hint body cap one value at a time, charging the same original-
versus-cleaned cost and caller growth budget; do not make the hint an unlimited
force-inline path.  The basic-string acceptance case requires callee-first
cleanup to expose `_M_destroy`/`operator delete`, remove calls through
`~basic_string`, `_M_dispose`, and `_M_is_local` when profitable, and finally
prune the weak bodies and D1/D2 aliases when no observable use remains.  Compare
the resulting probe object and frozen call family directly with GCC and Clang.

Implementation result (2026-08-21): `BindingRecord::inline_function` now
reaches serialized LowIR as one typed `inline_hint` Boolean and survives the
PA30 compiler-object round trip.  PA13 owns its function-only syntax and
scaffold field; PA15, PA16, and PA19 reducers cover explicit free functions,
in-class definitions, and function-template specializations.  PA37 covers the
source path, `no_inline` precedence, the size boundary, and byte-identical
direct-versus-serialized objects at O0 through O3 and in the debug lane.

The measured 12-instruction hinted late cap was rejected: it removed 19
additional definitions but grew the frozen O1 object by 5,760 bytes and
`.text*` by 9,471 bytes.  The narrow next-step cap of seven is retained.  It
uses the existing cached typed shape and 128-instruction caller budget, removes
five additional definitions, changes the object by -592 bytes and `.text*` by
+384 bytes, and leaves the three-run host-built O1 wall median at 5.18 seconds.
The basic-string destructor and `_M_dispose` call counts are not reduced yet;
their residual EH/landing eligibility, rather than missing inline preference,
is now the next gate.  The full report is 5,382/5,382, the PA37 debug/object
lane is clean, and the file audit has zero fatal findings.

#### R11c. Strip provably dead protected regions in callee-first order

Replace the whole-function gate in `strip_explicit_no_unwind_eh` with a typed,
region-granular proof.  Seed the existing dense per-symbol no-unwind facts from
explicit boundaries and declarations.  As the R10i callee-first traversal
visits a function, propagate its EH stack through the CFG, assign compact IDs
to protected regions, and mark every active region unsafe when it contains an
operation that can transfer to a handler.  Direct calls to already proven
no-unwind targets are safe; indirect calls, unresolved or may-unwind direct
calls, `throw`, and `resume` are unsafe.  A live nested region or cleanup that
can resume into an outer region also keeps the outer region live.  Ambiguous
EH state at a CFG merge is a conservative rejection, not a guessed context.

Remove only a matched `eh_try`/`eh_cleanup` and `eh_end` pair whose complete
protected region is proven unable to unwind.  Then run the existing local CFG
cleanup so its now-unreferenced landing blocks disappear, recompute the compact
EH/leaf/instruction summaries, and publish a newly inferred no-unwind function
fact before its callers are visited.  This makes a destructor-cleanup cascade
converge in one ordinary callee-first traversal rather than with a translation-
unit fixed point.  Recursive SCCs remain conservative unless every member can
be proven without assuming another member's result.

Implementation result (2026-08-21): each inliner wave now assigns dense region
IDs only inside EH-bearing functions, propagates one compact active-stack fact
over normal CFG edges, and removes independently proven-safe regions.  An
ambiguous stack merge rejects that function.  Retained EH markers contribute
their landing edges to reachability, so deleting a safe sibling cannot delete
an unsafe handler.  When the last region disappears, an EH-free body whose
remaining typed calls are nonthrowing publishes that fact immediately for
later callers in the same callee-first traversal.

Two active PA37 reducers cover mixed safe/unsafe sibling regions and
callee-to-caller no-unwind publication; six earlier PA37 references moved only
by newly dead markers/handlers.  The frozen O1 pass analyzes 8,296 regions
across its bounded waves, removes 1,361, publishes 112 facts, and encounters no
ambiguous functions.  Versus R11b, the object shrinks 115,568 bytes, `.text*`
shrinks 29,705 bytes, definitions fall by 220, and host-built O1 wall improves
from 5.18 to 5.12 seconds.  `_Unwind_Resume` relocations fall from 412 to 250;
basic-string destructor calls fall 934 to 841 and shared-pointer destructor
calls 652 to 592.  The full report is 5,384/5,384, PA37 debug/object tests are
clean, and the file audit has zero fatal findings.

The implementation should extract or share the existing typed EH-state
propagation in `lowir_cleanup_o1.cpp`, rather than add a second string-keyed or
block-order approximation.  Use dense block/region arrays and compact
persistent stack nodes bounded by EH pushes; allocate scratch once per
function and release it after cleanup.  Work is O(blocks + instructions + CFG
edges + removed blocks), with each region published at most once.  Add stats
only to the existing optional `Stats` sink: regions considered/removed,
protected instructions visited, conflict rejections, and cascading functions
made EH-free.  Ordinary compiles gain no symbol spellings, demangling, or
call-site maps.

First remeasure the existing whole-function strip after R11a/b: the corrected
destructor boundary plus already inferred no-unwind callees may remove most of
the basic-string and standard-library population without a new region
analysis.  Retain the region-granular implementation below only for a material
residual population in functions whose whole body cannot be stripped.

PA37 O1 owns the transformation contract and student-facing requirement.  Add
exact course reducers for a destructor-shaped cleanup around a direct inferred
no-unwind chain, nested removable regions, a region spanning blocks,
adversarial definition order, and a multi-level cascade.  Negative reducers
retain regions containing a may-unwind direct call, an indirect call, a
`throw`, a `resume`, a live nested cleanup, and a conflicting incoming EH
state.  A 1x/2x/4x stats ladder must establish linear visits and publications.
No PA13 syntax or scaffold change is required.  Add a PA37 source/behavior
case using ordinary C++ destruction so the optimizer is not validated only on
handwritten LowIR; update changed optimized fixtures in place.

#### R11d. Separate lifecycle retention from inlining policy

Audit and then remove the host-object rule in `pa15_lowering_abi.cpp` that sets
`no_inline` on every constructor/destructor base entry.  ABI publication and
definition retention must remain expressed by `object_output_root`, lifecycle
role, object aliases, and reachability.  `no_inline` must mean only the explicit
source/control policy defined by PA13 and PA37.  Do not add replacement
metadata unless an object inspection proves that an existing typed retention
fact is insufficient.

This phase begins with a lifecycle matrix: complete/base constructor and
destructor entries, weak/internal binding, shared and distinct C1/C2/D1/D2
bodies, virtual-base entries, aliases, address use, explicit `inline`, GNU
`always_inline`, and GNU `noinline`.  Verify both the earlier PA15 force-inline
path and the PA37 optimizer, because a synthesized base entry can currently
carry `force_inline=yes` and `no_inline=yes` simultaneously.  Before retaining
the deletion, inspect symbols, aliases, COMDAT groups, and relocations and prove
that every externally required base entry remains emitted.

The first source-level lifecycle assignment whose output changes owns the
exact reducer and any fixture regeneration; current evidence points to PA17,
but the prototype must establish the earliest affected PA with
`make test-report-through-paN` rather than assuming it.  PA32/PA33 gain or
extend object inspection for base-entry publication and alias retention, and
PA37 gains positive inlining plus explicit-`noinline` negative coverage.  The
PA13 metadata syntax and scaffold do not change.  Student-facing text changes
only where the owning assignment currently requires the synthesized
`no_inline`; implementation history and the linkage diagnosis stay here.

Implementation result (2026-08-21): lifecycle base-entry identity is one
transient typed Boolean in the PA15 symbol record rather than serialized
policy.  The initial typed reachability walk promotes only base entries reached
from an object-emission root to the existing `object_root` fact; the transient
Boolean then dies at the adapter boundary.  This preserves a required C2/D2
definition after later call inlining without turning every synthesized entry
into a root.  Textual LowIR emission runs the same object-root projection so a
direct object and an object rebuilt from serialized LowIR remain identical.
The hot native-object path adds no graph pass: it reuses the reachability walk
that already prunes weak/internal definitions.  The non-pruning text path pays
one additional object-reachability walk solely to serialize the same contract.

PA28 owns the exact source metadata reducer, PA30's existing foreign-relocation
case proves that a weak C2 remains linkable, PA32 now checks C2 publication
rather than requiring an optimizer-visible call instruction, and PA37 covers
positive inlining plus direct/serialized object identity.  The frozen O1
result versus R11c is 1,571,664 object bytes (+31,880), 564,959 `.text*` bytes
(-1,946), and 2,833 defined symbols (+172).  Ordinary and late inlines rise
from 8,807/1,435 to 9,345/1,619 while `no_inline` rejections fall from 2,913 to
1,058.  Three host-built wall runs are 5.21, 5.18, and 5.16 seconds (5.18-second
median); the extra retained ABI bodies explain the file-size increase, while
their call sites are now eligible for the following landing-block phase.

#### R11e. Inline proven-no-unwind callees from landing blocks

After R11a--d shrink the population, relax the landing rejection only
when the target has no EH control instructions *and* its explicit or inferred
fact proves it cannot unwind.  The ordinary size, recursion, signature,
variadic, `no_inline`, and growth checks still apply.  Keep potentially
throwing callees and every callee containing `eh_*`, `throw`, `exception`,
`exception_selector`, or `resume` as calls.  This admits empty and small
cleanup destructors without requiring true EH-region grafting.

PA37 O1 exact reducers cover an empty destructor-like callee, a small inferred
no-unwind chain, and multiple landing callers.  Negative cases cover a
potentially throwing direct call, indirect call, direct `throw`, an EH-bearing
callee, and an explicit `no_inline`.  Behavioral cases must exercise both the
ordinary cleanup path and an exception already in flight so terminate/resume
semantics cannot be accidentally weakened.

Implementation result (2026-08-21): the existing dense landing bitmap now
admits a call only when the callee's cached symbol fact is no-unwind and its
cached body summary is EH-free.  No new graph, map, string key, or retry loop is
introduced; all ordinary policy, cost, recursion, and growth checks run first.
Three PA37 LowIR reducers cover explicit and inferred no-unwind helpers across
multiple landings plus the complete unsafe matrix.  A PA37 source reducer shows
the actual cleanup destructor body substituted into the landing block, and a
PA30 runtime reducer executes it while an integer exception is already in
flight.  One existing PA37 incoming-EH-edge fixture moved to the newly legal
shape.

Against R11d, frozen O1 falls from 1,571,664 to 1,541,816 object bytes
(-29,848), from 564,959 to 563,436 `.text*` bytes (-1,523), and from 2,833 to
2,728 defined symbols.  Ordinary/late inlines rise from 9,345/1,619 to
9,988/1,873; landing rejections fall from 4,714 to the 12 genuinely unsafe
sites.  `_Unwind_Resume` remains 250, while basic-string destructor calls fall
from 868 to 676 and shared-pointer destructor calls from 594 to 585.  Three
host-built wall runs are 5.12, 5.13, and 5.06 seconds (5.12-second median).

#### R11f. Remeasure effects and eligibility before expanding value passes

On the exact retained R11a--e tree, repeat the frozen O1/O2/O3 census before
changing another pass.  Record static calls by typed target category and call
frequency, rejection counts, EH-bearing definitions, `_Unwind_Resume` calls,
LSDA/`.eh_frame`/text, retained bodies, and the existing MIR movement buckets.
The optional production counters use fixed numeric categories; demangled
function-family names remain an out-of-band object/profile report.  Also
profile the generated compiler so a frequent static call is not automatically
treated as a hot runtime call.

At the same checkpoint, audit the preprocessed GNU function attributes against
semantic and LowIR facts.  `always_inline` and `noinline` are already covered.
If the 35 `pure` or 18 `const` declarations have material live call traffic,
map them through the existing typed effects contract (`readonly` and
`readnone`, respectively) with PA33 source tests and PA37 DCE/GVN positives and
side-effect negatives.  Validate the mapping against GCC attribute semantics;
do not infer effects merely because the current body looks pure.  Treat `cold`
as a profitability hint only if a profile shows value, and do not add `leaf` or
`artificial` to production LowIR without a consumer.  A GNU `nothrow` extension
can map to the existing unwind fact when implemented for compatibility, but it
has zero occurrences in this frozen C++ input and is not on the critical path.

This measurement decides whether the remaining whole-function EH exclusions
are still high value.  It also corrects the R10i-b conclusion narrowly: the
measured 8/192 through 24/768 policies remain rejected on the old eligibility
tree, but their result is not evidence about profitability after R11.

Implementation result (2026-08-21): the exact preprocessed frozen input has 53
relevant declarations (35 `pure`, 18 `const`), but only 26 plainly attributable
static calls survive in the O1 object: nine calls each to the red-black-tree
increment and decrement helpers, four to `std::uncaught_exception`, and four to
`atoi`.  The `basic_string` destructor and `_M_dispose` chain is not annotated;
its remaining gap is therefore inlining, constant propagation, scalar
replacement, and dead cleanup work rather than a missing GNU effect tag.

The source facts were nevertheless retained because the implementation is
small and general.  A single ordered byte enum on the canonical semantic
binding maps GNU `pure` to LowIR `readonly` and GNU `const` to `readnone`;
redeclarations and template patterns merge the stronger fact without a string
map.  Attribute collection now scans each relevant syntax owner once, and O1
DCE accepts an unused `readonly`, no-unwind, returning call just as it accepts
`readnone`.  PA33 source coverage exercises ordinary and template attributes;
PA37 source and exact LowIR reducers cover propagation, direct/indirect DCE,
ordinary side effects, and may-unwind retention.

The generated frozen O1 object remains byte-identical to R11e at 1,541,816
bytes (563,436 `.text*` bytes, 2,728 definitions).  Three O1 host-built runs are
5.23, 5.15, and 5.13 seconds (5.15-second median); the one O2 structural check
also retains the prior 1,473,216-byte object, 520,510 text bytes, and 301
whole-function EH skips.  Serialized LowIR contains 1,432 definitions, 291 of
them EH-bearing, so region-aware value harvesting remains justified for R11g.

#### R11g. Make value harvesting region-aware where evidence remains

If R11f still shows a material EH-skipped population, replace whole-function
exclusion one pass at a time.  Memory GVN comes first, then PRE, because the
former directly attacks redundant loads/stores introduced by inlining and is
easier to bound.  Reuse the shared compact EH-context analysis and treat every
region transition, may-unwind call, landing entry, cleanup, `throw`, and
`resume` as a value-state barrier.  Transform only ordinary blocks/edges whose
incoming EH state is unique and equal; never propagate a memory or expression
fact across an exceptional edge.  Retain the current function-wide fallback
when state is conflicting or the existing instruction/probe budgets are
exhausted.

These remain O2/O3 passes initially.  Moving them into O1 is a separate policy
decision requiring matching-level compile-time and generated-runtime evidence;
it is not bundled into removal of the EH guard.  PA37 O2 owns exact positive,
barrier, nested-region, conflict, budget, behavior, and 1x/2x/4x work tests.
Existing PA37 O1 EH fixtures remain negatives for transformations not promised
at O1.

For native edge-register placement, replace `function_has_eh` only if the
remeasured movement census justifies it.  An edge is eligible when its exact
source and target have one equal ordinary EH state and it is not an exception,
landing, cleanup, or resume edge; all other edges use the current frame
fallback.  Reuse compact block/edge facts and keep allocation near-linear.
PA38 O2 owns MIR structure, behavior, pressure/fallback, debug, and scaling
coverage.  This backend subphase is committed separately from LowIR GVN/PRE so
fixture movement and timing are attributable.

Memory-GVN implementation result (2026-08-21): a shared typed EH-context
module assigns compact marker/parent states over ordinary CFG edges and rejects
the function when unequal stacks meet.  Handler entries, EH instructions, and
calls that may unwind feed one sparse barrier version included in every memory
key.  This invalidates all locations in constant work per boundary instead of
clearing a per-location table or walking a string-keyed set.  The existing
PA37 `480-memory-value-numbering-barriers` fixture now eliminates the adjacent
load inside one protected state; its atomic case remains unchanged.  New PA37
fixture 481 makes the newly legal protected/landing eliminations and the
transition, may-unwind, and conflicting-merge negatives explicit.

On the frozen O2 compile, all 301 EH-bearing functions are analyzed, 72 with
conflicting incoming stacks conservatively retain the old whole-function
fallback.  Eliminated loads rise from 575 to 645, while the object falls from
1,473,216 to 1,472,920 bytes and `.text*` from 520,510 to 520,192 bytes.  The
pass rises from about 5.2 ms to 9.9 ms; total one-run wall time was 5.44 seconds
under variable host load.  The full 5,396-test report passes and the file audit
has zero fatal findings.

The separately prototyped PRE extension is rejected.  It admitted 301 EH
functions (72 conflict fallbacks), but found no partial redundancy in the
frozen source.  Instead, it converted 280 fully available expressions into
join phis, growing the object from 1,472,920 to 1,480,480 bytes and `.text*`
from 520,192 to 527,801 bytes while doubling PRE time from about 5.0 to 10.0
ms.  The exact equal-region/handler reducer proved the context rule itself,
but there is no production or course-fixture change to retain for a rejected
optimization.  Any future PRE revisit must first avoid profitable full
expressions and demonstrate partial-redundancy evidence independently.

Native edge-register placement is rejected at the same checkpoint.  Removing
the EH-wide gate as an upper-bound experiment raises planned retained values
from 591 to 1,150, but grows the object from 1,472,920 to 1,478,728 bytes and
`.text*` from 520,192 to 525,930 bytes.  Definition-time fallback stores and
the extra register pressure outweigh saved reloads before any state-analysis
cost is added.  Therefore the measured population does not justify building
a region-aware placement analysis; the existing conservative EH guard stays.

#### R11h. Re-run profitability selection after eligibility and harvesting

Only after R11a--g are measured, repeat the inliner cap experiment.  Sweep one
variable at a time from the retained 6-instruction/128-growth baseline instead
of another diagonal sweep: body shape/cap first, then per-caller growth, then
the separate single-call limits if its population changed.  Score candidates
by call/return work removed, constant arguments exposed, cleanup removal,
last-use body deletion, and post-harvest instruction count.  Keep the smallest
policy with a material exact generated-self runtime win; do not recognize STL
or mangled spellings.

The primary performance gate is the exact O3-built self compiler compiling the
frozen source at the maximum level, because that is the under-15-second goal.
Retain O1 and O2 matching-level measurements to prevent a higher-level policy
from silently changing a lower level.  Use three-run interleaved medians and
ABBA follow-up under comparable host load, never concurrent builds.  Record
compiler/object/text/EH size, pass time, total wall/user/system, and peak RSS.
A change inside the established 3% noise band needs a deterministic structural
benefit; a compile-time or RSS regression beyond it needs a larger generated-
runtime win and an explicit ledger decision.

Implementation checkpoint (2026-08-21): the 20-instruction/320-caller-budget
probe exposed an optimizer correctness bug and was not treated as an
ineligibility reason.  Straight-line CFG cleanup could merge an earlier target
into a later predecessor, moving target definitions after uses in intervening
serialized blocks.  The PA37 `501-forward-only-block-merge` reducer makes the
old optimizer emit an undefined `%value`; reparsing that output at `-O0`
rejects it.  Cleanup now admits only forward-presentation merges with one O(1)
block-index comparison.  The original aggressive frozen O1 compile then
completed at 1,628,080 object bytes, 692,846 `.text*` bytes, and 5.22 seconds
wall on the host-built compiler.  The retained 7/128 policy passes the full
5,397-test report and the PA39 audit has zero fatal findings.  Profitability
selection remains separate from this required correctness fix.

The same probe exposed an independent PA29 native-placement defect in the
generated self compiler.  On a wide scalar boundary, the constrained-pressure
path reserved only `r8` and `r9`; it could therefore allocate a temporary over
a still-live `rdi` or `rsi` parameter.  The generated
`GeneratedTokenCollector` constructor overwrote `this` while materializing a
vtable address and the self compiler crashed in `Lexer::StartTokenSpelling`.
The `wide-live-parameter-temporary-pressure` behavioral reducer preserves the
public native-placement contract without requiring exact MIR comparison.
Placement now reserves every allocator-managed live incoming register through
the existing dense typed use count, including at a wide boundary.  This adds
one O(1) branch per incoming register parameter and no string identity or
additional scan.  The full report passes 5,398/5,398 and the PA39 audit remains
zero-fatal.  A clean corrected 32-worker O3 self build took 18.32 seconds and
230,256 KiB peak RSS; that compiler's interleaved frozen-source medians are
14.38 seconds at `-O0` and 16.12 seconds at the default maximum level, at about
370 MiB peak RSS.  The 11,850,352-byte compiler emits a 1,473,328-byte maximum
object with 520,609 `.text*` bytes, 44,140 `.eh_frame` bytes, 18,256 LSDA bytes,
and 2,668 defined symbols; its `-O0` object is 3,027,632 bytes with 770,778
`.text*` bytes and 5,835 definitions.  These figures are the exact-self R11h
policy baseline.

The 12-instruction probe exposed a second independent PA29 native correctness
bug before profitability measurement.  An immediately returned division used
the direct `rax`/`rdx` setup whenever its fixed-register ordering was safe, but
that setup emits register-only `MI_MOV` instructions and also admitted a
frame-resident dividend.  The resulting `mov rax, [frame]` reached an encoder
that correctly rejects memory operands for `MI_MOV`.  Direct setup now accepts
only a register or immediate dividend; all memory-shaped dividends reuse the
ordinary typed materialization path.  This is one O(1) operand-kind check and
adds no analysis.  The PA29 `frame-dividend-direct-return` reducer forces the
dividend into a frame home across an EH call: it fails on the old predicate
with `unsupported native move operand`, then compiles and runs after the fix.
PA29 passes 280/280, the report through PA29 passes 4,217/4,217, and the full
report passes 5,399/5,399.  Cap-12 profitability remains a separate decision.

After that correction, cap 12 exposed a third independent PA29 defect.  Native
address folding replaced `lea` plus a typed narrow load with the encoder's raw
load operation.  A folded `u8` load therefore wrote only the low byte of a
register and retained stale high bits, while the ordinary typed load path
zero-extended it.  Folded loads now use the shared typed scalar-memory helper.
The PA29 `folded-narrow-load-normalization` behavioral reducer forces the
address fold while retaining stale bits in the destination; it exits 1 with
the old backend and 0 with the correction.  PA29 passes 281/281, through PA29
passes 4,218/4,218, the full report passes 5,400/5,400, and the PA39 audit is
zero-fatal.  The correction is `c60198ac`.

The corrected cap-12 result rejects a larger raw-body limit.  Its clean self
compiler is 12,035,520 bytes versus 11,850,352 bytes at cap 7.  In three
interleaved ABBA blocks on the frozen maximum compile, cap 12 changes median
wall/user/RSS from 17.610 s / 17.000 s / 399,010 KiB to 17.650 s / 17.080 s /
398,582 KiB.  The paired deltas are +0.142% wall, +0.324% user, and +0.002%
RSS, while the frozen object grows 14,160 bytes.  The larger compiler and
output buy no generated-runtime improvement, so cap 7 is restored.

Clang's same-libstdc++ optimization record explains why copying its numerical
threshold is not an appropriate replacement.  At O2, Clang first inlines the
small leaves and `_M_destroy` into `_M_dispose`, then inlines `_M_dispose` into
`~basic_string`, and finally inlines that expanded destructor into callers.
It does not retain separate shallow and expanded definitions.  Its call-site
analyzer nevertheless scores `_M_dispose` at 15 and the destructor at 15
against a normal threshold of 225 because it predicts simplification, dead
blocks, cheap loads/stores, argument setup, and the avoided call.  The emitted
Clang O1/O2/O3 objects have only two relocations that take the destructor's
address and no `_M_dispose` relocation; the nested chain has been folded to
the ultimate allocation/deallocation work.  LLVM's CGSCC inliner appends new
calls to a bounded worklist and uses cost multipliers/history to control SCC
growth, rather than retaining two IR bodies.

Our raw LowIR count cannot yet approximate that post-inline cost: cap 12
clones frame/slot traffic that later passes do not remove.  A bounded shallow-
wrapper experiment tested whether preserving one small shape was an adequate
approximation without copying two bodies.  It retained a single-block,
inline-hinted wrapper with exactly one nested call and at least 64 direct uses,
inlined that shallow shape under a separate 512-instruction caller budget, and
deferred the cloned nested call until the next bounded wave.  This removed all
711 frozen-object destructor relocations, but mainly changed the next layer's
`_M_dispose` relocations from 995 to 1,774.  It did not expose the expanded
dispose body and its ultimate deallocation call.

The corrected exact-self compiler grew 15,104 bytes, from 11,850,352 to
11,865,456 bytes.  The frozen object became only 568 bytes smaller, from
1,733,560 to 1,732,992 bytes.  Three interleaved ABBA blocks changed median
wall/user/RSS from 17.615 s / 17.050 s / 398,568 KiB to 17.690 s / 17.070 s /
396,240 KiB; paired candidate deltas were +0.142% wall, +0.176% user, and
-0.846% RSS.  PA37 passed 161/161, the report through PA37 passed 5,364/5,364,
the full report passed 5,401/5,401, and the audit was zero-fatal, so correctness
was not the limitation.  The implementation and its proposed PA37 fixture were
removed because flattening only one call layer adds policy and compiler size
without improving generated runtime.

The next inliner experiment should instead approximate the feature that made
Clang's fully expanded body cheap: a bounded post-simplification cost, not an
alternative stored body.  Compute a compact per-function cost summary during
the existing bottom-up traversal, with inexpensive address calculations,
loads/stores, returns, and removable parameter/return plumbing weighted below
control flow and surviving calls.  At a call site, substitute constants and
direct slot/address arguments into that summary, identify provably dead
successors, and charge only the reachable weighted work plus a separate hard
raw-clone budget.  A direct-call benefit must be subtracted from the weighted
cost so chains such as `_M_is_local` -> `_M_destroy` -> `_M_dispose` can become
profitable while allocation/deallocation remains a shared external call.

Keep this analysis allocation-free per call site: dense vectors are prepared
once per function, a reusable scratch generation array holds substituted facts,
and branch reachability is a bounded walk over the callee's existing blocks.
The raw clone budget remains the size-safety backstop, recursion and EH rules
remain unchanged, and no STL names or ABI-specific patterns are recognized.
First validate the scoring on synthetic PA37 chains whose raw instruction
counts are equal but whose surviving call/control-flow costs differ.  Then
require the frozen destructor chain to reach the deallocation call, an exact-
self runtime win in interleaved A/B timing, bounded 1x/2x/4x optimizer work,
the full report, the file audit, and 32-way inception before retaining it.

Implementation result (2026-08-22): a general weighted-cost prototype was
not retained.  Restricting it to the destructor-shaped conditional cleanup
still grew frozen `.text*` from 715,765 to 828,594 bytes and the exact self
compiler from 11,850,352 to 12,099,072 bytes.  Although it removed every
`_M_dispose` relocation, three ABBA blocks regressed median wall and user time
by 2.7% and 2.8%, respectively.  The full 5,400-test report passed, so this was
a profitability rejection rather than a correctness failure.  The weighted
policy and its unused counters were removed.

The cheaper retained approach canonicalizes the body before applying the
existing policy.  O1 now removes a zero-element pointer `index` and bypasses
the strict empty-diamond shape that materializes an integer `phi`, immediately
compares it, and branches.  The diamond pass rejects EH targets, observable
arms, shared intermediate values, and all other phi shapes.  It first checks
for the exact three-instruction merge, then uses dense value-use counts and
the existing typed CFG; no strings, per-site maps, or fixed-point retry are
introduced.  O2 memory GVN now preserves an unknown pointer's identity through
an exact copy or an equal-input phi, which lets the existing dominance walk
remove the cleanup body's cross-block reload.  Address-forming indexes still
receive a distinct identity.

These transformations reduce the serialized `_M_dispose` body from fourteen
instructions and six blocks to eight instructions and three blocks.  Raising
the hinted raw cap from seven to eight then removes all 1,002 frozen
`_M_dispose` call relocations, but grows host-built frozen text from 716,660 to
767,064 bytes and the exact self compiler from 12,073,336 to 12,241,200 bytes.
The isolated three-way experiment on the then-live source gives median
wall/user/RSS of 17.59 s / 17.03 s / 398,492 KiB for the original baseline,
17.22 s / 16.63 s / 398,716 KiB for canonicalization at cap seven, and
17.36 s / 16.79 s / 398,852 KiB at cap eight.  It is valid for that A/B/C
decision but is not the pinned frozen-workload acceptance timing.  Cap eight
is rejected again: canonicalization supplies the measured improvement, while
broad cleanup-body cloning gives part of it back and causes the size growth.

The retained cap-seven host object is 1,710,984 bytes versus 1,733,560 bytes
at baseline; the `size(1)` text figure is nearly flat at 716,660 versus
715,765 bytes.  The live-source experiment shows a 2.1% wall and 2.3% user
improvement.  PA37 owns three new positive/negative reducers, and seven
existing PA37 references move in place where zero projections or the newly
marginal lifecycle inline shape change.

Acceptance at `c0526298` is a clean 5,403/5,403 full report and a PA39 file
audit with zero fatal findings and 30 advisory warnings.  The separated CFG
module keeps `lowir_opt.cpp` below the 3,000-line audit limit.  A clean
32-worker O3 self build took 18.92 seconds wall, 464.54 seconds user,
43.10 seconds system, and 231,164 KiB peak RSS; the compiler is 12,082,192
bytes.  The following separate 32-worker inception took 64.71 seconds wall,
1,743.04 seconds user, 59.54 seconds system, and 230,252 KiB peak RSS.  All 211
objects and the final `cppgm++-inception` binary match the self-host baseline.

On the pinned stable `semantic_overload.cpp`, that immutable compiler's first
three consecutive maximum-level runs are 15.73, 15.71, and 15.85 seconds
(15.73-second median).  Later interleaved runs place it at 15.945 and 15.625
seconds under different host load.  Those are the correct R11h acceptance
baseline; the live-source 17.22-second result above is not used for the
under-15 decision.

#### R11i. Defer true EH grafting

Inlining a callee with genuinely live EH regions requires remapping nested
handler state, landing blocks, cleanup continuations, exception values, and
resume ownership.  Implement that only if R11f/h leave a large, profiled-hot
population that cannot be reached by dead-region removal.  It is not a default
follow-up to R11e.  The source exception-specification correction is R11a and
must not be replaced by optimizer inference.  True grafting remains a PA37
optimizer feature and adds no private LowIR side channel.

The residual census does not justify this complexity.  The hottest remaining
work is frontend execution in the generated compiler, while R11c/e already
remove or cross the dead/nonthrowing EH regions that can be handled without
grafting.  R11i is therefore complete with a measured deferred disposition;
no private object-only LowIR or EH metadata is added.

#### R11j. Fold branches known from their sole incoming edge

The generated-self profile still contains repeated tests of the same selector
on a successor reached by one arm of an earlier branch.  In that successor the
selector is already known.  O1 now replaces the second branch with a jump when
the block has exactly one ordinary predecessor, both branches use the same
typed value, the incoming edge identifies the value, the block is not an EH
target, and removing the untaken edge needs no `phi` repair.

The implementation first performs a cheap dense `ValueId` candidate scan so
the usual function avoids graph construction.  A candidate function builds
the existing typed CFG once and visits each block once.  There is no retry
loop, string identity, or per-edge map.  The exact PA37 O1
`504-edge-known-branch` reducer owns the transformation and the PA37 normative
contract states the edge and `phi` restrictions.  Commit `6d32d926` passes the
then-current 5,404-test report; the final combined output and timing are
recorded below.

#### R11k. Retain storage-only derived addresses through native selection

The final generated-self disassembly showed that indexed addresses were still
materialized in registers even when every consumer was a load, store, further
index, or bulk-memory address operand.  PA29 now classifies this property once
during the existing linear use analysis with dense per-`ValueId` bytes.  The
native value fact retains a typed base/index/scale/displacement operand across
nonadjacent instructions and CFG edges only while its carrier registers remain
valid.  A consumer that observes the pointer or crosses a carrier clobber
materializes the address.  Serialized LowIR and its canonical comparison do
not change.

The first generated-self probe exposed two real correctness boundaries and
they were fixed rather than used as rejection reasons.  A deferred base must
be materialized with address semantics, not loaded as the scalar stored at
that address (`84784d8f`).  A retained address based on a forwarded parameter
must use the parameter's stable selected home even when its original incoming
register happens not to be the carrier that the interval check sees
(`49cb90a5`).  The structural and behavioral PA29
`storage-only-derived-address-placement` reducer was expanded at each boundary.
The initial implementation is `50032b61`; one existing PA29 MIR reference
moves because the now-public direct placement is the intended contract.

The hot path uses the existing dense use facts, `MirOperand`, precomputed
clobber masks, and O(1) selected-home lookups.  It adds no string set, textual
operand parsing, per-consumer rescan, or broad LowIR rewrite.  The PA29
normative section describes observable address placement; implementation
advice remains in its non-normative Design Notes.

#### R11l. Preserve forwarded parameters in extended calls

The clean O3 inception lane passed after R11k, but the required explicit-O0
lane exposed a generated compiler crash.  The ordinary call-argument builder
already replaced a promoted parameter-slot load with its stable selected
parameter home.  The extended ABI builder, used when a call also carries an
object or wide argument, instead reread the clobbered incoming register after
a bulk copy.  This is a backend lifetime bug, not an O0 ineligibility.

Commit `ecb22eaa` applies the same O(1) typed `forwarded_parameter` lookup to
the extended GPR move set.  The PA29 structural/behavioral
`forwarded-parameter-extended-call-placement` reducer forces an intervening
bulk copy and verifies that the scalar arrives intact.  No call signature,
LowIR field, extra analysis, or string-keyed state is introduced.

#### R11 final performance and inception gate

The exact final O3-built self compiler at `ecb22eaa` is 11,828,888 bytes.  On
the pinned frozen source, three ABBA blocks against the immutable `c0526298`
compiler give:

| Compiler | Runs | Median wall | Median user | Median RSS | Frozen object | `.text*` |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| R11h `c0526298` | 6 | 15.625 s | 15.130 s | 369,800 KiB | 1,452,400 B | 581,841 B |
| final `ecb22eaa` | 6 | 14.925 s | 14.395 s | 368,342 KiB | 1,442,144 B | 571,569 B |

The paired candidate deltas are -4.59% wall, -4.78% user, and -0.39% peak
RSS.  One candidate invocation was perturbed to 19.25 seconds by transient
host load; both clean blocks remained 14.79--15.06 seconds and the six-run
median remains below the 15-second objective.  The final object is 10,256
bytes smaller, including 10,272 fewer text bytes, with unchanged 34,296-byte
data.

At explicit O0, three final runs are 13.31, 13.24, and 13.27 seconds
(13.27-second median, 12.72-second median user).  Their objects are
byte-identical at 3,025,760 bytes with 916,882 text bytes, down 1,872 object
bytes and 1,911 text bytes from the immutable R11h compiler.  A no-option
compile is byte-identical to explicit O3, confirming that absence of `-O`
still selects the maximum pipeline.  As a slow-object diagnostic,
`lowir_opt.cpp` takes 7.65 seconds at O0 and 8.14 seconds at O3; optimization
reduces its object text from 487,511 to 256,731 bytes.

The final clean 32-worker lanes, each with self construction timed separately
from inception, are:

| Lane | Stage | Wall | Aggregate user | System | Peak RSS |
| --- | --- | ---: | ---: | ---: | ---: |
| O0 | `cppgm++-self` | 20.00 s | 454.49 s | 43.96 s | 231,724 KiB |
| O0 | inception compare | 158.67 s | 4,343.03 s | 89.86 s | 231,508 KiB |
| O3 | `cppgm++-self` | 18.42 s | 461.90 s | 42.73 s | 232,248 KiB |
| O3 | inception compare | 59.05 s | 1,600.89 s | 57.05 s | 230,620 KiB |

Both lanes match all 211 objects and the final compiler byte-for-byte.  The
final implementation tree passes 5,406/5,406 report tests and the PA39 file
audit has zero fatal findings and 33 advisory warnings.

#### R11 acceptance order

Implement and commit the semantic correction (R11a) and LowIR inline hint
(R11b) independently, with their earliest through-PA reports and full
`make test-report` clean.  Measure the basic-string reducer and frozen census
before deciding whether the residual region analysis (R11c), lifecycle-policy
split (R11d), and landing relaxation (R11e) are still material; retain each in
its own changeset.  R11f is then a measurement/effects checkpoint and may
reject any or all of R11g.  Commit memory GVN, PRE, and native placement
separately when retained.  R11h follows only the retained eligibility and
harvesting phases.  R11i records the measured decision not to add EH grafting.
R11j follows the retained canonical cleanup because it consumes the same typed
CFG.  R11k is a separately reviewable PA29 backend phase, with generated-self
correctness boundaries fixed and committed before measurement.  R11l is the
earliest-owned reducer and corrective found only by the explicit-O0 lane.  The
final O0 and O3 inception gates cover their combined generated code.

At the end of R11, run the zero-fatal audit, a clean timed 32-worker O3 self
build, and a separate clean timed 32-worker inception comparison with peak RSS
and byte-for-byte object/final-binary comparison.  Also run a clean explicit
O0 32-worker inception after any source-lowering or lifecycle metadata change,
because O3 inception can hide unoptimized generated-code defects.  The phase
is not complete until the full root `make test-report` passes on the exact
ledger commit.  Repeat that report and the zero-fatal audit after updating this
document.

## Fixture and public-contract policy

Optimization changes are expected to move optimized LowIR and MIR.  They are
not a reason to hide tests or preserve stale layout.

- LowIR transformation reducers belong in `cppgm.tests/course/pa37/` and the
  PA37 O1/O2/O3 lane matching the feature.
- Machine transformation reducers belong in `cppgm.tests/course/pa38/`, with
  structural MIR, behavior, and debug-info coverage as appropriate.
- A new LowIR operation also needs the earliest PA13 syntax/roundtrip and PA29
  consumer coverage.  Object binding/COMDAT changes need PA32 inspection.
- If a failure appears in PA36 or earlier, add the reducer to that earliest
  owning assignment before fixing the later pipeline.
- Completely new tests go directly into active course fixtures.  Existing
  fixtures whose intended optimized shape changes are regenerated in place
  through the documented reference target.  Do not create `proposed`.
- Use the pinned reference wrappers through `make ref-test-paN`.  A local
  binary may be selected only through a documented test/ref option; do not
  update the external binary bundle merely to evaluate a local candidate.
- Record every intentionally changed checked-in LowIR and MIR fixture, its
  owning contract, and whether comparison is exact, canonical/structural, or
  behavioral in the phase ledger.
- Student-facing normative sections say what students must implement now.
  Complexity suggestions go in `Design Notes (Non-Normative)`.  Old layouts,
  benchmark history, experiment outcomes, and maintainer instructions remain
  in this plan.

## Per-changeset validation

Prototype on one reducer and the frozen object first.  A retained logical
phase then uses this order.

### 1. Focused correctness

Run the owning local test, then collect the whole PA37/PA38 failure set at
once:

```sh
make test-pa37
make test-pa38
make test-report ACTIVE_TEST_REPORT_PAS='pa37 pa38'
make test-debuginfo DEBUGINFO_TEST_PAS='pa37 pa38'
```

For a PA37-only phase, the assignment exit gate includes:

```sh
make test-report-through-pa37
```

For PA38 or a cross-layer phase, it includes:

```sh
make test-report-through-pa38
```

Run any earlier owning PA introduced by the change as both `make test-paN` and
`make test-report-through-paN`.  `make test` alone is not an exit signal
because it stops before reporting the complete failure set.

### 2. Deterministic output and code shape

Compile the frozen source twice at each affected level and require identical
objects within one compiler build.  Record object/text/COMDAT/LSDA/EH sizes,
relocations, bodies by binding, calls, decoded instructions, and the largest
same-function deltas with `scripts/report_elf_code_shape.py`.

At rank 1, body count and call count are primary.  At ranks 2--4, LowIR
instructions, source-slot movement, and call counts are primary.  At rank 5,
same-function movement and spills are primary.  At rank 7, generated-program
runtime is primary; code growth must stay inside the stated budget.

### 3. Compile-time A/B

Build immutable baseline and candidate compiler copies before timing.  Do not
build or test concurrently with the measurements.  Use
`scripts/run_ab_compile_benchmark.py` with at least three load-screened ABBA
blocks at every affected level.  Record wall, user, and peak RSS medians plus
paired deltas.  The frozen extended-tree `validate_perf_regression.py` is a
BSD-time hardware-counter gate: on this Linux host its `/usr/bin/time -lp`
invocation is unsupported and exits before compiling.  Do not treat that as a
candidate failure or edit the frozen checkout.  Here the local GNU-time A/B
runner, deterministic output/code-shape census, and inception peak-RSS gate
provide the portable signals.  The default no-flag command is still compiled
separately to exercise maximum-level routing.

Reject or redesign a phase when:

- median user or wall time regresses by more than 3% without a separately
  approved, materially larger generated-program win;
- peak RSS regresses by more than 3%;
- optimizer work grows superlinearly on a synthetic size ladder;
- matching-level cppgm++ compile time is no longer faster than both GCC and
  Clang; or
- output improvement comes from an untyped/string-keyed hot path, repeated
  whole-function scan, hidden IR, or unbounded retry loop.

For inliner and O3 growth policies, add 1x/2x/4x synthetic call-chain or loop
fixtures and record instructions visited, worklist pushes, clone count, and
elapsed pass time.  Doubling input must not produce an unexplained quadratic
step.

### 4. Full regression gate before self-hosting

The exact candidate commit must pass:

```sh
make test-report
perl scripts/cppgm_file_audit.pl --stage pa39
```

The audit must report zero fatal findings.  Split implementation files before
crossing a fatal limit; do not evade the audit with include-as-code fragments.
No self/inception timing begins while the full report is incomplete or while
another build is running.

### 5. Clean 32-worker self and inception gate

Use a fresh explicit object root so only the intended tree is removed or
retained.  Time the self build separately from inception and record wall,
aggregate user/system, and peak RSS:

```sh
RUN_ROOT=/tmp/v3codex-opt-<commit>-j32

/usr/bin/time -v make -C pa39 -j32 cppgm++-self \
  INCEPTION_OBJ_ROOT_BASE="$RUN_ROOT" \
  INCEPTION_BUILD_JOBS=32

/usr/bin/time -v make -C pa39 -j32 compare-cppgm++-inception \
  INCEPTION_OBJ_ROOT_BASE="$RUN_ROOT" \
  INCEPTION_BUILD_JOBS=32
```

The second command must compare all 211 current objects and the final compiler.  A
warm or partially inherited object tree is not a clean timing.  Keep the self
tree between the two commands so the inception measurement does not include a
second self build; remove only the explicitly named run root when the evidence
has been saved.

After building `cppgm++-self`, benchmark that exact binary on the frozen source
at explicit O0 and the maximum level.  O0 isolates the quality of the generated
compiler binary; maximum level includes the cost of the new optimization
passes.  Also retain the slow `lowir_opt.cpp` object compile as a diagnostic
because it directly exercises the optimizer implementation.  The frozen
compile remains subject to the established under-15-second goal.

Rerun `make test-report` and the zero-fatal audit on the final ledger commit if
the plan document or fixtures changed after the timed commit.

## Commit discipline and ledger

One rank may contain multiple experiments, but one retained changeset must be
reviewable as implementation + reducers + public documentation/scaffold +
fixture migration + ledger evidence.  Commit and push each retained changeset
before starting the next.  Rejected prototypes are reverted without rewriting
unrelated user changes, and their reason is recorded here when it affects the
ranking.

Fill one row for every retained or rejected phase:

| Phase | Intended result | Fixture/contract movement | Frozen output delta | Compile-time/RSS delta | Full report/audit | 32-way self/inception | Status/commit |
| --- | --- | --- | --- | --- | --- | --- | --- |
| R0 | typed telemetry and analysis ownership | stats only; PA37 comparator unit coverage | stats disabled preserves ordinary output | included in Rank 1: O1 RSS -0.3%, O2 neutral | 5,291/5,291; zero fatal | self 17.89 s / inception 1:40.69, both 32-way | Rank 1 telemetry complete, `1085fcfb`; reusable CFG epochs remain before R2 |
| R1a | bottom-up deterministic inliner | PA37 O1 bottom-up/pruning and source-attribute fixtures; PA13 `no_inline` contract | O1 text -8.3%, calls -11.6% | O1 wall neutral; optimizer 301.7 ms vs 316 ms | 5,291/5,291; zero fatal | self 17.89 s / inception 1:40.69, all matches | complete, `1085fcfb` + `d7d9e93b` corrective reducer |
| R1b | bounded profitability and localized cloning/cleanup | PA37 O1/O2; existing optimized refs regenerated in place | O2 text -8.6%; object -28.8% | O2 wall -0.7%, RSS neutral | 5,291/5,291; zero fatal | same clean lane | complete for Rank 1; remeasure policy after R2, `1085fcfb` |
| R1c | post-inline weak/internal pruning audit | PA37 object roundtrip; PA32 owning linkage inputs | defined functions -39.5%; 2,038 weak bodies pruned at O1 | included above | 5,291/5,291; zero fatal | same clean lane | complete, `1085fcfb` |
| R2a | sparse promotion state | no new contract; no checked fixture movement | -278 LowIR instructions, object -1,256 B after one old dense-budget skip was removed; intermediate exact-output guard missed and recorded | promotion 485.6 ms to 49.5 ms (9.8x); 1.33 MiB peak sparse scratch | included in final Rank 2 gate | included in final Rank 2 lane | retained with staged-guard discrepancy, `eae03449`; telemetry `9de9d17f` |
| R2b | public phi-capable scalar replacement | PA13 syntax/scaffold; PA29 lowering/correctives; PA37 O2 and object roundtrip | vs Rank 1: LowIR -4.8%, object -0.1%, text -0.3%; functions unchanged | wall -6.6%, user -4.4%, RSS +0.1%; optimizer -50.8%, promotion 8.1x faster | 5,303/5,303; zero fatal | self 18.24 s / inception 1:41.31; all 197 objects and final binary match | complete, `84b4c184`, `01aebb78`, `9f0538be`, corrective `9c63791f` |
| R3 | LICM and loop simplification | PA37 O1/O2 | active course reducers | +0.6% optimizer sample | +3,480 B object | 5,309/5,309 plus debug lane | complete; frozen benefit absent, +1.5% generated-self remains below gate |
| R4 | GVN/load elimination/PRE | PA37 O1/O2 typed GVN, memory, PRE, budget, EH, and behavior reducers; no PA13 movement | -682 LowIR instructions, -2,208 B object, -2,721 B text, -616 decoded instructions vs R3 | wall +0.2%, user +0.5%, RSS +0.1%; memory 6.09 ms, PRE 5.31 ms | 5,315/5,315; zero fatal | final lane deferred until compiler stops changing | complete, `50b2b037`, `c950139c`, `1a71f6b4`, `8bb6be47` |
| R5 | MIR placement/coalescing | PA38 O1/O2 structure, behavior, and debug; PA29 narrow x87 corrective | object -920 B, text -1,877 B, decoded -88; 853 edge retains and 235 identity moves | wall +0.4%, user +0.7%, RSS -0.03%; overlapping host-noise range | 5,321/5,321; zero fatal | final lane deferred until compiler stops changing | complete, corrective `3a7ccfeb`, Rank 5 `a04e5900` |
| R6 | IPA argument/scalar work | PA37 O2 exact transform and object behavior; existing PA32 linkage negatives | object -14,880 B, text -2,478 B, decoded -549, relocations -67; +10 defined symbols | wall +0.29%, user +0.22%, RSS +0.10%; IPA 22.5 ms and 0.74 MiB scratch | 5,323/5,323; PA37 debug clean; zero fatal | final lane deferred until compiler stops changing | complete, `41f679cc` |
| R7 | bounded full unrolling; distinct O3 | PA37 exact/source/object/debug O3; PA38 structural/behavior/debug O3; help and READMEs; no PA13 change | frozen O3 unchanged at 2,127,200 B; synthetic runtime -53%, text 352 to 348 B | wall +0.39%, user +0.21%, RSS +0.04%; 1x/2x/4x visits linear | 5,331/5,331; PA37/PA38 debug clean; zero fatal | final lane pending | implementation complete, `3bc622b7` |
| R8a | complete small-object scalar replacement and scalar residual copies | PA37 O2 exact/object; one existing optimized fixture moved; PA29 Design Notes, no MIR change | frozen O3 2,116,672 B; generated-self frozen O0 30.15 to 18.30 s | host compile neutral; bounded dense facts and union/find | 5,338/5,338; zero fatal | self 19.57 s / inception pending | complete, `a30c982f` |
| R8b | typed unreachable-edge cleanup and bounded late O3 leaf inlining | PA13 role/scaffold/ownership tests; PA16 existing producer fixture; PA37 O1 direct/source and O3 late reducer | frozen O3 2,116,672 to 2,056,816 B; 1,007 late calls removed | unreachable 1.6 ms; late inline 47.6 ms; host wall 5.16 s | 5,346/5,346; zero fatal | self 19.27 s / inception 74.64 s, timed separately; all matches | complete, `9816f4f6`; generated-code correctives `93467e1c`, `f5fe92e1`, `ad7a88d6` |
| R8c | preserve phi predecessor identity when inlining moves a terminal edge | PA37 O1 exact reducer; PA37 normative inlining contract | no intended frozen change while O3 remains leaf-only | typed block IDs; work bounded by the fixed 128-instruction caller budget | 5,347/5,347; zero fatal | not rerun for output-neutral prerequisite | complete, `9fb2b10b` |
| R9a | canonical source-lowered `cmp` value identity | PA15 normative contract and exact course reducer; PA37 object roundtrip; 147 PA15--PA28 references regenerated in place | frozen O0 +1,432 B; maximum +4,200 B; 6,836,572-byte O3 LowIR exact parse/dump roundtrip | old/new median O0 wall 4.73/4.72 s, user 4.23/4.26 s; maximum wall 5.20/5.20 s, user 4.71/4.69 s; direct compact types, no new pass or text identity | 5,349/5,349; zero fatal, 31 warnings | clean O0: self 19.49 s / inception 153.97 s, timed separately; all matches | complete, `45b15f22` |
| R9b | bounded late inlining of small callful/multi-block optimized callees | PA37 O1 phi-edge reducer; O3 optimized-shape and seven-instruction boundary fixtures; 1x/2x/4x counters | vs leaf-only: object -35,520 B, text -2,106 B, defined functions -95 | exact self A/B median 17.71 to 17.42 s; late wave 51.6 ms; compact cached shape facts | 5,352/5,352; zero fatal | self 19.11 s / inception 73.42 s; 209 objects and final compiler match | complete, broad implementation `3eecc5f4`, retained cap `276a5c5d` |
| R10a | admit object-named proven-no-unwind callees inside EH regions | PA37 O1 exact + object-roundtrip reducers; PA31 opaque throwing-boundary fixture; PA33 explicit publication roots | O1 object -165,744 B, text -17,692 B, `.eh_frame` -10,560 B, 391 fewer measured function definitions | six-run explicit-O1 A/B median 4.979 to 4.984 s (+0.10%); no new analysis or storage | 5,354/5,354; zero fatal, 29 warnings | final combined lane pending | complete, `8fffcbca` |
| R10b | stop treating ordinary calls to internal functions as permanent object roots | six PA15/17/22/25 exact refs lose only stale root metadata; seven PA31--PA36 symbol fixtures gain explicit retention; PA37 lambda-control and pruning reducers | vs R10a: object -98,696 B, text -11,178 B, `.eh_frame` -8,800 B, 312 fewer measured local definitions | six-run explicit-O1 A/B median 4.990 to 4.992 s (+0.04%); one constant mask test per symbol | 5,356/5,356; zero fatal, 29 warnings | final combined lane pending | complete, `134f0c17` |
| R10c | bounded definition-removing single-call inlining with immediate body release | PA37 O1 positive/address/multiple-use reducer and 160-instruction boundary; normative limits and typed Design Notes | vs R10b: object -23,200 B, text +1,551 B, `.eh_frame` -1,832 B, 56 fewer measured definitions | exact O1-built-self frozen medians: O0 17.21 to 16.77 s, O1 18.34 to 17.98 s; host O1 5.04 to 5.00 s; typed scan adds one byte/function; ownership transfer avoids instruction payload copies | 5,358/5,358; zero fatal, 31 warnings | O1 self build succeeds; final O3 32-way lane pending | complete, `0d39bea1` |
| R10d | run the bounded optimized-body wave at every optimizing level | two existing PA37 O1/O2 exact refs move; PA37 level contract and Design Notes; no PA38 movement | vs R10c O1: object -61,416 B, text +11,146 B, `.eh_frame` -4,276 B, 165 fewer definitions; 1,007 calls inlined | exact O1-self A/B wall 17.99 to 17.66 s, user 17.42 to 17.09 s; one typed graph rebuild and 60.1 ms measured late wave | 5,358/5,358; PA37 debug clean; zero fatal, 31 warnings | O1 self 19.61 s / 459.79 user / 229,928 KiB; final 32-way lane pending | complete, `0fd38172` |
| R10e | proportional called-once translation-unit budget | PA37 normative limit and typed Design Notes; no fixture movement | vs R10d O1: object -37,888 B, text +5,139 B, `.eh_frame` -3,248 B, 103 fewer definitions; 2,014 called-once bodies transferred | exact O1-self A/B wall 17.48 to 17.55 s, user 16.95 to 17.04 s; budget accumulated in the existing dense summary loop | 5,358/5,358; zero fatal, 31 warnings | O1 self 19.06 s / 458.20 user / 229,340 KiB; final 32-way lane pending | complete, `40c0325b` |
| R10f | stats-only post-pruning retained-call census | no output fixture movement; fixed typed bucket contract recorded in this plan | 1,162 discardable definitions / 4,134 calls / 76,460 instructions; 656 become single-use after pruning, 600 at most 160 instructions | one compact CSR rebuild only with a `Stats` sink; fixed dense counters, no strings | PA37/PA38 178/178; zero fatal, 29 warnings | diagnostic only | complete, `02025dcf` |
| R10g | one post-prune definition-removing cascade plus wide-compare corrective | PA37 exact cascade and normative contract; PA29 structural, predicate-behavior, and pressure reducers; PA29 Design Note | vs R10e/f O1: object -17,856 B, text +5,373 B, `.eh_frame` -1,708 B, 64 fewer definitions; 59 bodies transferred | exact O1-self A/B wall 17.48 to 17.78 s (+1.7%), user 16.93 to 17.27 s (+2.0%); one compact CSR graph and dense caller byte; no fixed point | 5,362/5,362; zero fatal, 32 warnings | O1 self 20.11 s / 462.13 user / 228,468 KiB; final 32-way lane pending | complete, backend `2e6be885`, inliner `b53bbde4` |
| R10h | admit EH-control-free callees inside an active caller EH region; atomic-load pressure corrective | PA37 O1 inherited-EH and direct-throw exact reducers; PA29 atomic-load behavior/MIR pressure reducer; PA29/PA37 normative and Design Notes | vs R10g: object -38,984 B, text +10,868 B, `.eh_frame` -3,956 B, LSDA +167 B, 135 fewer definitions; 96 post-prune bodies / 5,465 instructions | host A/B wall -0.36%, user +0.20%, RSS -0.91%; exact-self raw medians wall +0.9%, user +0.8%, RSS -0.1%; no new graph or string-keyed state | 5,365/5,365; zero fatal, 32 warnings | O1 self 19.49 s / 463.69 user / 228,696 KiB; final combined 32-way inception pending | complete, backend `6e4c9fb8`, inliner `a8062b96` |
| R10i-a | cleanup-coupled nonrecursive convergence in one callee-first traversal | PA37 O1 eight-level cleanup-transition reducer with adversarial definition order and two parents; existing deterministic budget fixture retained; no PA13 change | frozen O1 byte-identical at six/128; reducer removes two retained wrapper calls and all newly unreachable calls | host A/B wall +0.58%, user +0.64%, RSS +0.73%; exact-self wall -1.1%, user -1.0%, RSS +0.6%; 1x/2x/4x work counters exactly linear; no queue/string state | 5,366/5,366; zero fatal, 32 warnings | repeat O1 self 19.31 s / 465.31 user / 228,472 KiB; final combined 32-way inception pending | complete, `96aad279` |
| R10i-b | broader measured O1 profitability | no fixture or contract movement because no broader point is retained | 8/192: object -4,080 B but text +4,816 B; 12/320: object -2,088 B but text +16,290 B; 18/512 and 24/768 grow object and text | exact-self medians vs 6/128: 8/192 +1.5%, 12/320 -0.6% noise, 18/512 +9.4%; 24/768 host compile +13.6% wall | R10i-a baseline remains 5,366/5,366, zero fatal | four exact O1 self compilers built; no retained policy, so no inception | complete, all broader points rejected; exact 6/128 restored |
| R10i-c | bounded external-to-recursive-SCC tail | no fixture or contract movement because the measured opportunity does not justify a new policy | 207 external-to-recursive calls narrow to eight eligible sites / three targets / at most 46 cloned instructions | rejected before production implementation; avoids a stable-site snapshot, graph update, and propagation scan for a negligible upper bound | R10i-a baseline remains 5,366/5,366, zero fatal | diagnostic only; final combined lane covers the retained inliner | complete, rejected by typed frozen census; conservative recursive rejection retained |
| R10i-d | preserve frame-address value identity through native phi transfers | active PA29 behavioral reducer with informational MIR; PA29 normative and Design Notes; no LowIR change | corrected generated compiler; frozen self medians O0 14.96 s / maximum 16.74 s; deterministic objects 2,965,936 / 1,659,296 B | one transient Boolean per phi move, no scan or string identity; phi adapter separated and main lowerer reduced to 2,938 lines | 5,367/5,367; PA37/PA38 debug clean; zero fatal, 32 warnings | O3 self 19.10 s / inception 69.76 s; all 209 objects and final binary match | corrective complete, `a8420b92`; under-15 maximum objective remains open |
| R11a | standard implicit destructor exception specifications and terminate boundary | PA13/16/19/20/21/28/35 active reducers; PA26/30/37 corrective movement | O1 object -77,168 B, `.text*` -23,351 B, defined symbols -90 against retained pre-R11 host compiler | one cached typed completed-class walk; typed terminate role; no string identity | 5,374/5,374; zero fatal, 32 warnings | final combined O0/O3 self/inception lane required | implementation complete; host-built frozen medians O0 4.79 s / maximum 5.27 s |
| R11b | preserve bounded C++ inline preference as `inline_hint` | PA13 syntax/scaffold; PA15/16/19 producers; PA37 policy/source/object/debug | cap 12 rejected; retained cap 7: object -592 B, `.text*` +384 B, definitions -5; basic-string calls unchanged | one typed Boolean and cached shape test; existing 128 caller budget; no strings | 5,382/5,382; zero fatal, 32 warnings | final combined self/inception lane required; PA37 object/debug clean | implementation complete; host-built frozen O1 median 5.18 s; proceed to residual EH gate |
| R11c | residual region-granular dead-EH stripping and no-unwind publication | PA37 O1 mixed-region/publication reducers; six exact refs moved; debug/object clean | object -115,568 B, `.text*` -29,705 B, definitions -220, resume 412 to 250 | dense typed region/stack facts per EH function; bounded callee-first waves; no strings | 5,384/5,384; zero fatal, 32 warnings | final combined self/inception lane required | implementation complete; host O1 median 5.12 s; 1,361/8,296 regions removed and 112 facts published |
| R11d | separate lifecycle publication/retention from explicit `no_inline` | PA28 exact policy; PA30 foreign relocation; PA32 publication; PA37 source/object roundtrip | vs R11c: object +31,880 B, text -1,946 B, +172 definitions; ordinary/late inlines +538/+184 | one transient typed Boolean; reuse native reachability; object-root fact serialized | 5,387/5,387; PA37 object/debug clean; zero fatal | final combined O0/O3 lane required | implementation complete; host O1 median 5.18 s |
| R11e | proven-no-unwind landing-block inlining | PA37 exact/source; PA30 exception-in-flight runtime | vs R11d: object -29,848 B, text -1,523 B, -105 definitions; basic-string destructor calls -192 | existing dense landing/no-unwind/EH summaries and budgets | 5,392/5,392; PA37 debug clean; zero fatal | final combined lane required | implementation complete; host O1 median 5.12 s |
| R11f | post-eligibility weighted census, profile, and GNU effects audit | PA33 source; PA37 source/DCE positives and negatives | 53 declarations but 26 attributable live calls; 291/1,432 serialized definitions EH-bearing; O1 byte-identical | compact canonical effects enum; single-pass attribute collection | 5,395/5,395; zero fatal | diagnostic | complete; GNU `nothrow` absent and `basic_string` cleanup unannotated |
| R11g | region-aware memory GVN, PRE, and native edge placement when justified | PA37 O2 memory EH positive/barrier/conflict; no rejected-prototype fixtures retained | memory: +70 eliminated loads, -296 B object, -318 B text; PRE +7,560 B object; placement +5,808 B object | shared compact EH states only for retained memory GVN; rejected prototypes removed | 5,396/5,396; zero fatal | required for each retained subphase | complete: memory retained; PRE and placement rejected |
| R11h | post-harvest inliner profitability sweep; PA29 correctives; cleanup-body canonicalization | PA29 behavior/MIR and normative/Design Notes; PA37 O1/O2 positive/negative reducers and seven moved references | cap 12, shallow wrapper, weighted cost, and cap 8 rejected; retained cap-7 live-source experiment -2.1% wall/-2.3% user; pinned stable acceptance baseline 15.73 s | exact typed CFG/use arrays and pointer facts; no strings or retry loop; Boolean CFG work separated from scheduler | 5,403/5,403; zero fatal, 30 warnings | O3 self 18.92 s / 231,164 KiB; 32-way inception 64.71 s / 230,252 KiB; 211 objects and final binary match | complete, `c0526298`; pinned under-15 objective still open at this checkpoint |
| R11i | true EH-region grafting | no fixture or contract movement because the residual profile does not justify the feature | dead and no-unwind regions already handled; genuine live-EH population is not the measured hot constraint | avoids nested handler remapping, extra metadata, and an otherwise unjustified pass | covered by retained R11 report/audit | diagnostic disposition; final combined lane covers retained work | complete, deferred by R11f/h census and profile |
| R11j | fold a branch whose selector is known from its sole incoming edge | PA37 O1 exact reducer and normative edge/phi limits | included in final combined -10,256 B object/-10,272 B text | cheap dense candidate scan; one existing typed CFG and one block walk only for candidates | 5,404/5,404 at retention; final 5,406/5,406, zero fatal | included in final O0/O3 lanes | complete, `6d32d926` |
| R11k | retain storage-only derived addresses as typed MIR operands | PA29 structural/behavior reducer expanded across two correctness boundaries; one existing MIR reference moves; normative and Design Notes updated | combined R11j/k final maximum object 1,452,400 to 1,442,144 B; text 581,841 to 571,569 B | dense byte facts and precomputed clobbers; no string keys, rescans, or LowIR movement; final ABBA -4.59% wall/-4.78% user/-0.39% RSS | 5,405/5,405 after placement; final 5,406/5,406, zero fatal | O3 self 18.42 s; inception 59.05 s; 211 objects/final match | complete, `50032b61`, correctives `84784d8f` and `49cb90a5` |
| R11l | preserve forwarded scalar parameters in extended ABI calls | PA29 structural/behavior reducer; PA29 call-lifetime contract and Design Note | final O0 object 3,027,632 to 3,025,760 B; text 918,793 to 916,882 B | one O(1) typed selected-home lookup per affected GPR argument; no new analysis | 5,406/5,406; zero fatal, 33 warnings | O0 self 20.00 s / inception 158.67 s; O3 self 18.42 s / inception 59.05 s; all 211 objects/final match | corrective complete, `ecb22eaa`; final maximum median 14.925 s |

## Completion criteria

This plan is complete only when:

- every rank has a retained implementation or a measured disposition;
- the rank-1 body/call census explains and materially closes the dominant O1
  gap before backend work is undertaken;
- O2 promotion no longer uses block-by-all-values/all-slots dense state and
  new analyses satisfy the declared complexity bounds;
- every new LowIR or MIR fact is public, typed, round-trippable, documented at
  its earliest owner, and covered by active course tests;
- the no-option maximum, explicit O0/O1/O2/O3 routing, serialized LowIR object
  path, MIR dump path, debug metadata, EH, and cross-object behavior pass;
- cppgm++ retains its matching-level compile-time lead over GCC and Clang and
  the maximum-level frozen compile remains under 15 seconds;
- the exact final tree passes PA37, PA38, their debug lanes, every earlier
  owning PA, the full `make test-report`, and the PA39 file audit with zero
  fatal findings;
- a clean 32-worker `cppgm++-self` build and separate clean 32-worker inception
  compare are timed with peak RSS, all current objects match, and the final
  compiler matches byte-for-byte; and
- each retained changeset and the completed ledger are committed and pushed.

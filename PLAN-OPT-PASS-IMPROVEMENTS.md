# Plan: Optimization-Pass Improvements

Status: active; Ranks 1--8 are implemented and gated; the matching-level
matrix and Rank 9 correctness analysis are active

Date: 2026-08-21

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
EH, demand, LowIR/MIR representation, or code-shape phases recorded by the
earlier plans.

The required outcomes are:

- materially improve the generated `cppgm++-self`, with the function inliner
  and resulting dead-body removal addressed first;
- keep a matching-level frozen compile faster than GCC and Clang;
- preserve the no-option driver rule: absence of `-O` selects the maximum
  implemented level;
- keep `-O0` unchanged by this plan;
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
paired deltas.  Also run the frozen extended-tree
`validate_perf_regression.py` check, whose default no-flag command exercises
the maximum optimization level and gates on retired instructions and memory.

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

The second command must compare all 191 objects and the final compiler.  A
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
| R9a | canonical source-lowered `cmp` value identity | PA15 normative contract and exact course reducer; PA37 object roundtrip; 147 PA15--PA28 references regenerated in place | frozen O0 +1,432 B; maximum +4,200 B; 6,836,572-byte O3 LowIR exact parse/dump roundtrip | old/new median O0 wall 4.73/4.72 s, user 4.23/4.26 s; maximum wall 5.20/5.20 s, user 4.71/4.69 s; direct compact types, no new pass or text identity | 5,349/5,349; zero fatal, 31 warnings | pending required clean O0 lane | implementation and fixture gate complete; commit pending |
| R9b | bounded late inlining of small callful/multi-block optimized callees | PA37 O3 exact/behavior/work-counter fixtures | pending | retain 40-instruction callee and 128-instruction caller caps; pending measured policy | pending | pending | blocked on R9a |

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

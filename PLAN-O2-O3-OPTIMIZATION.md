# Plan: O2/O3 Generated-Compiler Optimization

Status: executing

Date: 2026-08-29

## Objective

Make compilers produced by cppgm++ at `-O2` and `-O3` at least as fast as the
same-revision compiler produced at `-O1`, then target a 20% or greater O3
improvement.  The comparison is generated-compiler throughput, not merely the
time spent inside the optimizer and not a diagonal that changes producer and
workload levels at once.

The primary hard floor is:

- an O2- or O3-produced self compiler must not be slower than the O1-produced
  self compiler on fixed O1, O2, or O3 full-compiler workloads; and
- its same-level self/GCC code-quality ratio must not be worse than the O1
  control ratio.

The stretch target is:

- on the full O3 compiler workload, `self-O3 / self-O1 <= 0.80`; and
- after normalizing each producer against the same-revision GCC-built
  producer, the O3/O1 ratio improvement is also at least 20%.

The raw stretch target is about 25.9 seconds from the current 32.365-second
`self-O1 -> O3 workload` median.  Holding the current GCC controls fixed, the
normalized 20% target is about 24.3 seconds.  These seconds are orientation,
not permanent constants: every accepted phase uses same-revision controls.

This work may retune or reorganize the existing O2/O3 passes and may add new
bounded transformations when measurement justifies them.  It must not weaken
the PA37 or PA38 assignment contracts, add benchmark-specific recognition,
hide frontend facts behind the serialized LowIR or MIR boundaries, or make
optimization the no-option default.  Omitting `-O` continues to mean `-O0`.

## Current evidence

The starting revision is `fca27131`.  The completed 32-way matrix used the
full current 219-object cppgm++ workload, three order-rotated lanes per cell,
and six position-balanced lanes for the close O3-workload comparison.

Use this notation throughout the work:

- `S_ab`: a self-hosted cppgm++ produced at `-Oa`, compiling the workload at
  `-Ob`;
- `G_ab`: the same source built into cppgm++ by GCC at `-Oa`, compiling the
  workload at `-Ob`;
- `C_ab`: the corresponding Clang-built control;
- `P_b(a:1) = S_ab / S_1b`: the raw producer-quality change at a fixed
  workload; and
- `N_b(a:1) = (S_ab / G_ab) / (S_1b / G_1b)`: the same change normalized by
  what GCC gained or lost from its producer build level.

The measured wall/aggregate-CPU medians are:

| Producer | O1 workload | O3 workload |
| --- | ---: | ---: |
| `S_1b` | 31.250 / 905.400 s | 32.365 / 910.690 s |
| `S_3b` | 33.080 / 926.500 s | 32.525 / 924.710 s |
| `G_1b` | 20.960 / 589.670 s | 20.960 / 590.460 s |
| `G_3b` | 18.430 / 502.220 s | 19.640 / 547.370 s |

Consequently:

| Fixed workload | Raw self O3/O1 | O1 self/GCC | O3 self/GCC | Normalized O3/O1 |
| --- | ---: | ---: | ---: | ---: |
| O1 wall | 1.058560x | 1.490935x | 1.794900x | 1.203875x |
| O1 CPU | 1.023305x | 1.535435x | 1.844809x | 1.201489x |
| O3 wall | 1.004944x | 1.544132x | 1.656059x | 1.072486x |
| O3 CPU | 1.015395x | 1.542340x | 1.689369x | 1.095329x |

Thus O3 is materially worse on the O1 workload and mildly worse on the O3
workload.  The normalized loss is larger: about 20% on the O1 workload and
7--10% on the O3 workload.  The current combined post-O1 pipeline is
detrimental, but this matrix does not say which pass or interaction is at
fault.

At the current post-D.loop-priority-inline checkpoint, the inversion remains
removed and O3 now has a meaningful raw lead.  Three order-rotated all-32
lanes per cell give these producer medians:

| Producer | O1 workload | O3 workload |
| --- | ---: | ---: |
| `S_1b` | 31.540 / 905.060 s | 31.790 / 907.720 s |
| `S_2b` | 31.650 / 903.420 s | 31.440 / 905.000 s |
| `S_3b` | 30.220 / 856.760 s | 30.120 / 858.110 s |
| `G_1b` | 21.040 / 591.560 s | 21.710 / 592.660 s |
| `G_2b` | 18.620 / 520.530 s | 18.820 / 523.500 s |
| `G_3b` | 18.470 / 500.330 s | 17.940 / 498.970 s |

On the fixed O1 workload, raw O2/O1 is 1.003488x wall and 0.998188x CPU;
raw O3/O1 is 0.958148x and 0.946633x.  The corresponding normalized ratios
are 1.133909x/1.134398x for O2 and 1.091470x/1.119242x for O3.  On the fixed
O3 workload, raw O2/O1 is 0.988990x/0.997003x and raw O3/O1 is
0.947468x/0.945347x.  Normalized O2/O1 is 1.140860x/1.128718x, while
normalized O3/O1 is 1.146573x/1.122851x.  Thus the raw floor is met on O3;
O2 wall remains a close-result extension item on the O1 workload.  O3 is now
about 5.5% faster than O1 rather than merely equal, but approximately 12--15%
of same-source normalized producer-quality gap remains, and roughly fifteen
percentage points remain to the raw 20% stretch target.

Every lane reproduced the same 219-object manifest at its fixed workload.
The O1 and O3 manifest hashes were respectively
`23faea475b40bd6a0980f9167b9225f547e2b5153aaf45ae6c6578f2bee08281`
and `7d75a1956c5c0b4d059fa578a34868e39bd2d5310e05ea074dd7af241f6b9838`;
the final compiler hashes were
`2d4483245bd508e0f6242bf9717e2a6ede53bb2b7bbcf1b7f6fdf7cb1017583f`
and `e4670c488cd40ceec44dcffbbc0a2a115db1c427360ff341175e234e78b84975`.
The fixed matrix work root was removed after every exact lane, and no stale
benchmark or profiler process remained.

## What changes above O1 today

The attribution work must preserve the separation between the LowIR pipeline
and the native pipeline.

### LowIR O2 additions

The current O2 path adds or changes these operations relative to O1:

1. interprocedural constant/argument specialization, currently using the call
   graph constructed before the ordinary inlining wave;
2. cross-block single-store slot forwarding;
3. counted-loop simplification and strength reduction;
4. loop-invariant load motion in addition to the O1 invariant work;
5. memory GVN in the ordinary body pass, whereas O1 deliberately delays its
   cross-block load reuse until after the inlining and pruning waves;
6. partial redundancy elimination; and
7. all downstream effects those rewrites have on late-inline eligibility,
   pruning, cleanup, and LowIR-to-MIR live ranges.

The early-versus-late memory-GVN difference is a first-class interference
hypothesis.  The current O1 code explicitly records that early load reuse can
change the size model and turn local savings into caller growth.  O2 currently
runs the pass in that earlier position.

The specialization graph lifetime is a second first-class audit item.  The
graph is built before ordinary inlining, then reused after the program has
changed.  The plan must determine whether this is merely conservative or
whether stale use/body summaries lead to poor clone and argument decisions.

### LowIR O3 addition

O3 adds bounded full unrolling of eligible constant-trip loops.  It otherwise
uses the O2 LowIR work and the common late-inline/pruning tail.  A historical
frozen-TU census found no eligible loop, but the current complete compiler
source must be censused again.  O3 unrolling must not be blamed for an O2
regression without that evidence.

### Native O2 additions

The native O2 path adds these relevant differences:

1. broad whole-function planned residency for values, joins, backedges, and
   call-crossing intervals;
2. trace-based block layout;
3. unconditional final frame recomputation after machine cleanup; and
4. secondary changes in spills, frame homes, callee-save pressure, branch
   fallthrough, and frameless eligibility caused by those decisions.

The planner is available in restricted forms at O1, so attribution must dose
specific placement classes rather than label all register placement "O2".
The framed/frameless interaction must also be crossed explicitly: an
optimization that helps framed functions but harms the frameless population
is destructive interference, not a net O2 win.

### Native O3 addition

There is no O3-only MIR rewrite.  PA38 specifies that O3 carries PA37's O3
LowIR through the O2 machine path.  Therefore any O3-only difference is caused
by the LowIR unroller or by how its result interacts with the common O2 native
pipeline.

## Measurement model

### Separate code quality from optimization cost

Every experiment reports two different quantities:

1. **Producer quality:** compilers produced at different levels compile one
   identical fixed-level workload.  `S_21` versus `S_11`, for example,
   isolates the quality of the generated compiler code because both execute
   the O1 pipeline.
2. **Requested-level cost:** one immutable producer compiles the same source
   set at O1, O2, and O3.  This measures the extra optimizer work and any
   source/output-size effects of the requested level.

Neither quantity may be inferred from the diagonal `S_11` versus `S_33`.

### Primary and secondary oracles

The primary fast timing oracle is the complete 32-way O1 compiler workload.
It is representative, finishes much faster than Cachegrind, and isolates
producer code quality.  An O3 workload is added for every milestone and for a
candidate whose pass cost or O3-only output is relevant.

Use the following escalation:

1. one hot translation unit plus optimizer and MIR censuses for immediate
   rejection;
2. three balanced pairs of the 32-way O1 workload for an ordinary candidate;
3. six balanced pairs for a result within 1%;
4. twelve pairs when a boundary decision remains within noise;
5. the O3 workload and GCC/Clang controls at a retained milestone; and
6. hardware `perf stat`/sampling when permitted, or one shortlisted
   Cachegrind run when deterministic dynamic attribution is needed.

Do not use QEMU software-mode counters as the ordinary oracle.  They measure
an emulator as well as the compiler and are unlikely to beat the native
32-way timing path.  Do not start Cachegrind until checking for an existing
`valgrind`, `cachegrind`, or `callgrind` process.  Stop only a process owned by
the recorded experiment and exact PID; never kill an unrelated user process.

The retained deterministic attribution oracle is one O1 compile of the hot
`preprocessor.cpp` translation unit under Cachegrind.  It completes in about
one minute with the self producer, preserves an exact output hash, and is used
only after the native hot-TU screen has shortlisted a candidate.  On the
current sources, self O1 and O3 execute 5,196,654,174 and 5,181,114,513
instructions respectively, only a 0.299% reduction.  Same-source GCC O1 and
O3 execute 2,952,710,945 and 2,424,753,931 instructions, a 17.88% reduction.
GCC's change is distributed across whitespace scanning, decoding, range and
identifier checks, literal scanning, vector growth, UTF-8 append, and macro
operator checks; it is broad interprocedural simplification, not one isolated
backend peephole.

For each timing window:

- use immutable compiler binaries and identical source, include, and link
  inputs;
- use `-j32` and `INCEPTION_BUILD_JOBS=32` for compiler and inception builds;
- balance run order and record wall, aggregate CPU, and maximum RSS;
- record medians, paired ratios, and all samples, not only favorable samples;
- extend close results rather than discarding individual outliers;
- reject a whole declared window if host load invalidates it;
- record producer, object-manifest, and final-binary hashes; and
- remove the exact run root after evidence is saved.

### Acceptance arithmetic

A retained phase must improve the appropriate same-revision normalized ratio.
An absolute self-time increase may still be a relative win if GCC or Clang
moves more, but it does not by itself satisfy the final raw-speed target.

For a close result, require both:

- median paired wall and aggregate-CPU ratios in the intended direction; and
- the upper uncertainty/noise allowance to remain at or below 1.01x for the
  hard no-regression floor.

The final floor is stricter than the per-candidate screen: O2/O1 and O3/O1
medians must be at or below 1.00x on the fixed workloads.  The stretch exit is
`P_3(3:1) <= 0.80` and `N_3(3:1) <= 0.80`.

## Phase A: baseline, coverage, and experiment harness

### A0. Reproduce and complete the matrix

Before changing optimizer policy:

1. build self-, GCC-, and Clang-produced compilers at O1, O2, and O3 from the
   exact same source revision;
2. fill the missing O2 producer row;
3. run the complete O1/O2/O3 producer-by-workload matrix for self and GCC;
4. run Clang at least on all diagonals and on the O1 and O3 fixed workloads;
5. repeat the frozen compile with GCC-O3- and Clang-O3-built cppgm++ at
   requested O0, O1, O2, and O3, including the previously requested
   GCC-O3-producer/O0-workload best-case lane; and
6. record executable text/data/bss, per-object hashes, final hashes, wall,
   aggregate CPU, and RSS.

The O2 row tells us whether the first regression is O1-to-O2 or O2-to-O3.
Until it exists, no O3-only implementation begins.

### A1. Audit active student-facing coverage

Build a table mapping every retained O2/O3 behavior to:

- its high-level PA37 or PA38 README description;
- at least one positive structural or behavioral reducer;
- at least one negative safety guard where the proof can fail;
- its serialized LowIR or MIR round-trip coverage where relevant; and
- its debug-info lane where it can move located instructions.

The existing suites already contain focused O2 coverage for slot promotion,
LICM, counted loops, memory GVN, PRE, specialization, small-object promotion,
native trace layout, planned residency, EH guards, and O3 unrolling.  This
phase verifies that those tests express the documented property rather than
the current implementation's entire text.

Exact matching on program content is not an acceptable definition of a
feature.  New or backfilled tests must let a student implement from the README:

- PA37 tests inspect relationships such as an eligible load, loop, parameter,
  or call disappearing while guarded twins remain and the result behaves the
  same;
- PA38 tests inspect canonical MIR relationships or generated behavior, not a
  particular register spelling or full executable image;
- native encoding changes use the earliest PA2X structural or behavioral
  surface that owns the encoding; and
- diagnostic tests require fields and a positive event, never exact timing or
  unrelated whole-program counts.

Complete `.ref` and `.cmir` files remain compatibility fixtures.  Any required
fixture update uses the documented reference workflow and is explained; it is
not substituted for a focused feature predicate.

### A2. Add an experiment-only split and dose harness

Use one temporary, typed pipeline configuration to separate:

- LowIR level from native level; and
- individual O2/O3 pass families from their shipped level presets.

The harness must prove that its default O1, O2, and O3 endpoints are byte-exact
with the ordinary driver before any ablation is trusted.  Use the same harness
binary to generate every variant so option-parsing and source-layout changes
are common across the dose set.

This is maintainer experiment machinery, not a new student or LowIR contract.
Remove it before landing unless a small internal pass-pipeline abstraction is
independently justified, code-shape neutral at the default presets, covered by
ordinary routing tests, and passes the file audit.  Do not retain hidden
environment-variable behavior or undocumented public flags.

The initial layer cross is:

| LowIR dose | Native dose | Question |
| --- | --- | --- |
| O1 | O1 | endpoint control |
| O2 | O1 | net LowIR O2 effect |
| O1 | O2 | net native O2 effect |
| O2 | O2 | interaction and ordinary O2 endpoint |
| O3 | O1 | O3 LowIR effect without O2 native work |
| O3 | O2 | ordinary O3 endpoint |

If the combined result is worse than both isolated results, the first work is
interaction repair, not a new optimization.

## Phase B: locate the first harmful increment

Run both cumulative forward doses and leave-one-out ablations.  A pass that
looks neutral alone can still be harmful through ordering, inlining, live
ranges, frame state, or layout.

### B1. LowIR dose order

Measure these increments separately and cumulatively:

1. interprocedural specialization;
2. single-store cross-block forwarding;
3. counted-loop simplification;
4. O2 load LICM;
5. memory GVN in its current early position;
6. PRE;
7. the resulting late-inline/prune/cleanup decisions; and
8. O3 full unrolling.

For each dose record:

- LowIR instructions, functions, calls, clones, inlines, and pruned bodies;
- inserted phis/expressions and removed loads/stores;
- pass CPU, visits, worklist events, scratch bytes, and budget skips;
- final MIR instructions, frame operands, moves, spills, saved registers, and
  frameless function count;
- object text, relocations, function count, and unwind/LSDA size; and
- fixed-workload producer performance.

Diff the changed functions and rank their contribution using sampled runtime
or deterministic dynamic instructions.  Static instruction reduction alone
is not a win when it lengthens live ranges or adds hot frame traffic.

### B2. Pass-order interference

Cross the highest-risk orderings explicitly:

- early versus post-inline memory GVN;
- specialization before versus after rebuilding the post-inline call graph;
- LICM before versus after memory GVN;
- PRE enabled versus disabled around late-inline costing; and
- cleanup before versus after each changed-caller inlining decision.

The first expected experiment is to give O2 the same delayed load-reuse
discipline that protects O1's inlining size model.  Retain it only if the O2
contract still fires on its focused reducers and the complete compiler
improves.

### B3. Native dose order

Ablate these native choices independently:

1. broad call-free residency;
2. call-crossing callee-saved residency;
3. join and backedge residency;
4. trace layout;
5. final frame recomputation and save pruning; and
6. final frame-pointer omission.

Then run the required destructive-interference crosses:

- O1 versus O2 LowIR with the same native placement;
- framed versus frameless policy with each placement class;
- trace layout on versus off for framed and frameless functions; and
- PRE/LICM on versus off with O2 placement on versus off.

Classify every affected function by calls, loops, EH, frame operands,
callee-save count, spill count, and frameless outcome.  If a placement rule
helps framed code but harms frameless code, gate it on the final proven frame
state or redesign its cost model.  Do not retain a policy based on the average
of two opposing populations.

### B4. Regression exit

Phase B is complete when:

- the first harmful increment and any material interaction are named by
  repeated measurements;
- `S_21/S_11`, `S_31/S_11`, `S_22/S_12`, and `S_33/S_13` are no greater than
  1.00x at the median and satisfy the close-result rule;
- the corresponding normalized ratios are no worse than O1; and
- all contract-required passes still demonstrate their documented positive
  behavior.

A required PA37/PA38 transformation that is globally harmful is not silently
deleted.  Narrow its eligibility with a general profitability or pressure
proof, change its order, or retain the required conservative positive class
while rejecting the harmful class with a student-facing negative test.

## Phase C: turn O2 into a positive level

After removing the inversion, use the B-phase residual profile to select the
largest measured O2 opportunity.  Do not begin every candidate.

Candidate order is:

1. repair post-transform live ranges and spills introduced by PRE, LICM, or
   specialization;
2. improve the bounded whole-function residency planner with live-range
   splitting or pressure-aware placement for the measured hot class;
3. make post-inline cleanup and pruning consume the final O2 body/call graph
   without a whole-program fixed point;
4. fold profitable address/memory operations in MIR or x86 selection where a
   hot disassembly and an earlier PA2X reducer show the gap; and
5. revisit branch/block layout only if counters show front-end or branch loss
   after movement is under control.

Each candidate must state a general proof, a bounded cost model, and the
population it affects before implementation.  Prefer transformations that
reduce dynamic instructions, frame traffic, and calls together.  Reject
changes that merely shrink LowIR while increasing final spills, saves, or hot
branches.

Phase C's target is a stable `P_b(2:1) < 0.95` on at least the O1 and O2 full
workloads with no O3 regression.  This intermediate 5% target is not the final
20% O3 target; it demonstrates that O2 is a useful base rather than a renamed
O1.

## Phase D: build a meaningfully stronger O3

Current O3 has only bounded full unrolling, so removing the inversion is
unlikely to reach 20% by itself.  Select O3 work from the refreshed dynamic
profile, in this order:

1. an O3-specific, pressure-aware inlining dose for hot/hinted or
   definition-removing call classes, followed immediately by the existing
   scalar/object cleanup and reachability pruning;
2. merged-region allocation improvements required for that inlining dose to
   reduce movement rather than amplify it;
3. broader loop unrolling or peeling only when the current compiler source
   contains a measured eligible hot population and typed dependence proofs
   keep growth bounded; and
4. SLP or other SIMD formation only after a real compiler hot loop, behavior
   benchmark, typed MIR representation, and PA37/PA38 contract justify the
   implementation cost.

Inlining is first because prior GCC ablation evidence found it dominant on
compiler code, while mature O1-to-O3 controls gain substantially.  However,
old rejected inline doses are not accepted on reputation: re-evaluate them
under the corrected same-revision self/GCC metric and the current post-inline
pipeline.  Dynamic instructions, movement, calls, text growth, optimizer
cost, and full-workload time all gate the dose.

For an O3 growth transform:

- use explicit per-site, per-function, and translation-unit budgets;
- keep work linear or near-linear and report budget exhaustion;
- cap text growth against its measured dynamic benefit;
- require 1x/2x/4x work-scaling reducers;
- keep O2 output unchanged; and
- prove behavior through serialized LowIR and MIR, debug, and native lanes.

Phase D first clears `P_3(3:1) <= 0.95`, then iterates on the largest remaining
profiled cost until the 0.80 raw and normalized stretch targets are reached or
the ledger contains measured dispositions for the candidates capable of that
scale.  A missed stretch goal is recorded honestly; it is not converted into
a weaker post-hoc metric.

## Correctness and verification cadence

### Every experimental measurement

- confirm the repository revision and compiler hashes;
- check for stale benchmark, Valgrind, Cachegrind, QEMU, and build processes;
- use one explicitly named run root;
- require the expected object census and deterministic manifests; and
- remove only plan-owned scratch after recording results.

### Every retained implementation increment

For the earliest owning PA N:

1. run the focused positive and negative reducers;
2. run `make test-paN`;
3. run `make test-report-through-paN`;
4. run affected PA37/PA38 structural, behavior, round-trip, and debug lanes;
5. compile the frozen input twice and require deterministic output;
6. run the three-pair 32-way O1 producer-quality screen;
7. run `perl scripts/cppgm_file_audit.pl --stage pa39` and require zero fatal
   findings;
8. update this plan's ledger;
9. commit the coherent increment; and
10. push it before beginning the next retained compiler change.

Do not combine unrelated correctives with an optimization landing.  If a
self-build exposes a latent earlier bug, add the reducer to the earliest
owning PA, land and push that corrective separately, then resume the dose.

### Periodic larger gate

After every two retained increments, after either LowIR/native boundary moves,
and at the end of each phase:

```sh
make test-report
make test-debuginfo DEBUGINFO_TEST_PAS='pa37 pa38'
perl scripts/cppgm_file_audit.pl --stage pa39
```

Then run the six-pair 32-way O1 workload and the O3 workload against
same-revision GCC controls.  Add Clang at every phase boundary.  All current
tests must pass; do not hardcode a historical test count.

### Self-host and inception milestone

After the full report and audit are clean, use a fresh root and 32 workers:

```sh
RUN_ROOT=/dev/shm/v3-o23-<phase>-<commit>

/usr/bin/time -v make -C pa39 -j32 cppgm++-self \
  INCEPTION_OBJ_ROOT_BASE="$RUN_ROOT" \
  INCEPTION_BUILD_JOBS=32

/usr/bin/time -v make -C pa39 -j32 compare-cppgm++-inception \
  INCEPTION_OBJ_ROOT_BASE="$RUN_ROOT" \
  INCEPTION_BUILD_JOBS=32
```

Time self construction separately from inception.  Require every current
object and the final compiler to match.  Run explicit O1, O2, and O3 milestone
lanes when their output changes; run the final O0 lane to prove the default and
baseline path remain correct.  Check available `/dev/shm`, `/tmp`, and user
quota before starting and delete the exact plan-owned root after recording
hashes and evidence.

## Retention and stop rules

Retain an increment only when:

- its documented structural/behavioral property is covered at the earliest
  owning assignment;
- its output is deterministic and the LowIR/MIR replay boundaries remain
  sufficient;
- its work and memory growth are bounded and measured;
- the corrected normalized metric improves, with no unexplained raw slowdown;
- O0 and O1 do not regress outside the declared noise allowance;
- the through-target report, affected debug/round-trip lanes, and zero-fatal
  file audit pass; and
- the increment is committed and pushed with its ledger evidence.

Stop, redesign, or reject a candidate when:

- it recognizes compiler filenames, symbols, source text, or test content;
- it needs hidden frontend or native side data absent from serialized IR;
- its feature test requires exact program-content matching rather than a
  documented property;
- it repeatedly rescans a whole function or program without a bounded
  worklist argument;
- it improves LowIR counts while worsening final dynamic work, movement, or
  normalized full-workload time;
- it helps framed layout but harms frameless layout without a valid gate;
- it moves O0 output for an optimizer-only feature;
- it leaves stale processes or unbounded scratch data; or
- it cannot pass the full report, inception comparison, or file audit.

Rejected prototypes are restored completely.  Record their measured output,
time, and reason, but do not keep dormant flags, exact-match tests, or unused
analysis structures.

## Final acceptance matrix

The final revision must record:

1. the complete self/GCC producer-by-workload O1/O2/O3 matrix;
2. Clang O1/O2/O3 producer controls on O1 and O3 workloads;
3. frozen O0/O1/O2/O3 compiles using fully optimized GCC-, Clang-, and
   self-produced cppgm++ binaries;
4. raw `P_b` and normalized `N_b` ratios for wall and aggregate CPU;
5. object/text/function/call/MIR/movement/spill/frame/callee-save censuses;
6. optimizer time, visits, worklist, scratch, and budget telemetry;
7. deterministic object manifests and final compiler hashes;
8. a clean full report, affected debug and round-trip lanes, and zero-fatal
   PA39 file audit; and
9. clean 32-way O0/O1/O2/O3 self/inception evidence, with all objects and
   final compilers exact.

The hard exit is no O2/O3 inversion in raw or normalized measurements.  The
performance exit sought by this plan is at least 20% O3 improvement over the
same-revision O1-produced compiler on the O3 workload, both raw and normalized
against GCC.

## Execution ledger

### A0/A2 baseline and first native attribution

The fresh O2 row used the complete 219-object source set and three rotated
32-way lanes per cell.  `S_21` measured 32.67 seconds wall / 925.83 aggregate
CPU and `G_21` measured 19.59 / 525.39.  On the O3 workload, `S_23` measured
32.31 / 924.43 and `G_23` measured 19.02 / 523.17.  Every fixed-level final
compiler matched its peer.  Relative to the recorded O1 row, most of the raw
regression begins at O2, and the normalized loss is larger because GCC gains
substantially at O2.

An experiment-only split-level driver reproduced the ordinary O1 and O2
endpoints byte-for-byte, then produced these O1-workload medians:

| LowIR/native dose | Wall | Aggregate CPU |
| --- | ---: | ---: |
| O1/O1 | 31.96 s | 908.00 s |
| O2/O1 | 32.48 s | 913.25 s |
| O1/O2 | 33.12 s | 931.36 s |
| O2/O2 | 33.06 s | 930.38 s |

The first harmful layer was therefore native O2.  The experiment-only flags
were removed after the endpoint and dose builds; no private public interface
was retained.

### B3.1 conditional-fallthrough interference

The native O2 delta reduced to `trace_layout`; the placement policy itself is
common at O1 and O2.  An ELF census of the O1/O2-native cross found that trace
layout added 69,621 decoded instruction bytes and 17,824 unconditional jumps,
while paired conditional counts merely inverted.  Layout followed an
unconditional predecessor into a block that was already the natural successor
of a conditional block.  When both conditional successors had been placed,
the later `clean_branches` pass could no longer remove the conditional block's
trailing jump.

The retained repair limits unconditional trace following to functions with no
conditional branch.  PA38 now states that boundary and a focused structural
control proves both the positive unconditional-only reorder and the negative
conditional-fallthrough guard.  The one placement-pinned compatibility fixture
was updated only for the intended order.  The corrected O2 producer is 73,012
text bytes smaller than the pre-change O2 producer.

Three direct old/new O1-workload pairs had medians of 32.25 / 928.19 seconds
and 32.46 / 919.66 seconds wall/aggregate CPU: wall remained inside the 1%
close-result band while CPU improved 0.92%.  On the O3 workload, three pairs
improved the medians from 31.94 / 925.51 to 31.91 / 910.76 seconds, with
deterministic old and new outputs and 73,316 fewer text bytes in the corrected
result.  Exact-source O1 controls show that the raw O2 floor is now close but
not yet closed; the current two-lane O1/O2 means are 31.44 / 906.04 versus
31.90 / 909.55.  Exact-source GCC O1/O2 controls confirm the larger remaining
normalized deficit because GCC improves from 21.47 / 592.39 to a median
18.65 / 520.66.  Further O2 generated-code work is therefore still required.

The through-PA38 report passed 5,471/5,471 and the PA39 file audit had zero
fatal findings.  A periodic debug gate exposed stale fixtures from earlier
frame-policy and residency improvements; after checking that debug locations
were unchanged, the canonical compatibility fixtures were refreshed and the
complete PA38 debug lane passed 11/11 in corrective commit `bf0da1a9`.

### B3.2 reset completed deferred-carrier register lifetimes

The whole-function placement census exposed a lifetime-ownership error rather
than ordinary register pressure.  A deferred address marked its physical
carrier register as non-releasable, but the mark survived after the address
and carrier were dead and the pool had assigned that register to an unrelated
value.  The new value consequently inherited the old carrier's release veto.
In the hot macro-processor `Run` function this stale state blocked one cyclic
region grant and kept an unrelated short-lived value resident well past its
last use.

The retained repair clears a carrier mark only when O2/O3 begins a new value
lifetime in that register and the live-location index proves that the prior
lifetime is over.  The stale-bit test comes before the live-index query, so the
ordinary value-update path performs neither the query nor a table write.  A
broader prototype that queried and rewrote the carrier table for every
register assignment produced the same useful code but regressed the six-pair
screen by 0.11% aggregate CPU; it was narrowed before retention.

On the hot function, MIR instructions fell from 2,753 to 2,686, scalar loads
and stores from 453/433 to 408/388, and cyclic-region residencies rose from one
to two while busy failures fell from four to three.  The complete O3-produced
compiler lost 34,640 `.text` bytes (7,890,434 to 7,855,794).  Six balanced
32-way O1-workload pairs, all with the exact final hash
`81676d5b65301728a48a7e4a47d2c88435f91ed0b593412dd3f5ded1f02d171e`,
improved median paired wall time by 0.68% and aggregate CPU by 0.41%.

The same-source GCC-O3 control moved 0.11% in the slower aggregate-CPU
direction across three balanced pairs, giving an incremental normalized gain
of about 0.53%.  Its shorter wall samples were substantially noisier and are
not used to inflate the claim.  PA38 now documents carrier lifetime ownership,
and a structural/behavioral control checks reuse using relationships between
arbitrary chosen registers rather than exact register names or complete MIR.
The through-PA38 report passed 5,471/5,471, PA38 debug passed 11/11, and the
PA39 audit had zero fatal findings.  Fresh 32-way inception comparisons were
exact: O2 self/inception both hashed
`7af023ee8a4933b4971df7de0980282876e05c5cf4415542689bd679cb43e4b1`,
and O3 self/inception both hashed
`341c8feca143281ed92f11980178e034d5bccc1d27f7fe36e58cffafb9d38ff0`.

### B2.1 defer O2/O3 memory GVN until after inlining

The first pass-order cross confirmed the O1 pipeline's warning: running memory
GVN before the inlining waves made an initially oversized shared callee look
cheap enough to duplicate into both callers.  The retained ordering runs O2
and O3 whole-program load reuse after all inline and reachability-pruning
waves, then optimizes the retained body.  O1 keeps its existing additional
restriction for inline-hinted functions.

PA37 now documents the ordering property.  Its new structural and behavioral
control begins with 19 cross-block loads in a shared callee, proves that O2
retains both calls before reducing the retained callee to one load, and proves
that an intervening store still blocks reuse.  Restoring the old ordering
causes this control to fail because the two calls disappear, so the test
distinguishes the documented property rather than merely accepting both
pipelines.

The corrected O2 producer is 33,646 text bytes smaller.  Three balanced
old/new O1-workload pairs improved median wall time from 33.00 to 31.81
seconds (3.61%) and aggregate CPU from 912.35 to 908.44 seconds (0.43%).
Same-source GCC-O2 controls moved much less; the corresponding two-lane means
improved about 1.75% in wall time and 0.13% in CPU, so the normalized direction
also favors the change.

Requested-level cost and generated-code quality moved differently at O3.
Building the O3 result became 1.54% slower in median wall time and 0.64% slower
in CPU across six balanced pairs, but the emitted compiler lost 33,186 text
bytes.  When those immutable old/new O3 compilers ran the fixed O1 workload,
the three-pair medians improved from 32.50 / 915.35 to 31.75 / 911.14 seconds
wall/CPU, a 2.31% / 0.46% producer-quality gain.  This is retained because the
plan's objective is generated-compiler throughput; the one-time optimizer
cost remains recorded rather than being conflated with that result.

The through-PA38 report passed 5,471/5,471, the complete PA37 debug lane and
PA38 debug lane passed, and the PA39 file audit had zero fatal findings.  Fresh
32-way O2 and O3 inception builds were exact: the O2 self/inception pair both
hashed `ca0363112686d9577c4847886fbc34a9897f6eb0875e6143a5aea390e7b2c3e3`,
and the O3 pair both hashed
`5b6b32fe586fbe1059ffcdbb7c25ace1c0fdb5f79ea255d1ff47320877f3e65f`.

### B1.1 bound weak-target specialization by clone payoff

The specialization ablation exposed a link-time amplification that per-object
sizes concealed.  Broad O2 weak-target specialization turns each translation
unit's coalescible weak definition into a uniquely named internal clone.  The
ordinary weak definitions can be coalesced by the linker, but every surviving
private clone is retained.  The pre-change O2 compiler contained 162 such
symbols totaling 82,891 symbol bytes; disabling all specialization reduced
final text by 44,629 bytes even though many individual objects grew.

Internal-target argument propagation is useful and remains unchanged.  Weak
specialization is now restricted to the generally profitable class where a
uniform scalar parameter directly controls a branch or switch.  That gives
subsequent cleanup an entire control-flow region to discard and makes the
private clone small enough to inline away.  A uniform value used only as data
does not justify a clone that may survive cross-translation-unit weak-symbol
coalescing.

The retained O2 compiler has no surviving `__o2spec` symbol and is 46,397 text
bytes smaller than the preceding checkpoint.  Three balanced old/new
O1-workload pairs produced identical final hashes and improved median wall
time from 32.76 to 31.91 seconds (2.59%) and aggregate CPU from 916.50 to
912.03 seconds (0.49%).  Two same-source GCC-O2 control pairs moved from
19.375 / 523.285 to 18.98 / 521.965 seconds in mean wall/CPU, so the
source-normalized direction remains favorable by about 0.56% wall and 0.24%
CPU.  The O3 compiler is likewise 46,365 text bytes smaller and has no
surviving private clone.  Its first timing window was discarded in full after
two contiguous baseline lanes suffered abnormal host load; no O3 timing claim
is made from that window.

PA37 now describes both the positive control-flow proof and the negative
data-only boundary.  A new structural and behavioral control proves that the
control-flow case specializes, that two data-only weak calls retain their
observable two-argument ABI, and that execution is unchanged.  Restoring the
broad pre-change policy makes the negative guard fail.  The PA37 suite passed
188/188 including the property, its complete debug and object-round-trip lane
passed, the through-PA38 report passed 5,471/5,471, and the PA39 file audit
remained at zero fatal findings.  Fresh 32-way inception comparisons matched
every object and the final compilers: O2 self/inception both hashed
`5c1403afd2413ea646d100ff972832aaa78917cd9962a6ab2bd1817386326982`,
and O3 self/inception both hashed
`b59f30c02cb640220a953d4f506781b6636246b9f340f2fddc406c4af30c3044`.

### Rejected generated-code follow-ups

An O3-only repeated-constant specialization dose targeted the dominant
`Peek(0)` family seen in the self-O3 profile.  It redirected 57 calls and an
additional inverse-predicate cleanup reduced the resulting clone, but the
valid exact-output A/B moved only from 32.01 / 906.01 to 31.92 / 905.26 seconds
wall/aggregate CPU.  A candidate profile still attributed about 8.37% to the
generic and specialized `Peek` bodies together, versus 8.56% in the baseline.
The GCC advantage is therefore not explained by constant cloning alone.  The
dose and its provisional tests were removed.

A second dose split weakly aligned 33-through-64-byte object copies into MIR
chunks no larger than 32 bytes, allowing direct unaligned loads and stores in
place of `rep movsb`.  It reduced the hot `Token` move-constructor sample share
from approximately 3.6% to 3.0%, and the focused PA38 property failed under a
pre-change policy.  The complete six-pair, position-balanced O1 workload told
the opposite end-to-end story: the paired candidate/baseline median was
1.0305x wall and 1.0038x aggregate CPU, with five of six CPU pairs regressing.
All twelve final outputs were byte-exact at
`ff2a0f31c8402e7d1acc081f1a08cdc3dd558c5610abe37e63f6a89549904756`.
The candidate did pass the 5,471-test through-PA38 report, the complete PA38
debug lane, and exact O2/O3 inception comparisons; the corresponding O2 and
O3 hashes were
`e41f8efe295ed58f006bd4d84275a0e89730eea33bd63c252b4542da3ccfa0b4`
and `ef6ecdebb5a2ca83242b4f54763b70a15622d16fb2757c73ef6a9973809d5456`.
Those correctness results do not override the measured throughput regression,
so the implementation, student-facing text, and test were removed together.

An O2/O3-only Boolean-phi dose then threaded constant acyclic choices and
narrow loop-local choices while leaving the existing O1 `i64` loop rule
byte-exact.  It removed the materialized Boolean and reload from hot string
and token move paths, reduced the O3-produced compiler by 5,036 text bytes,
and improved the exact macro-processor screen by about 0.3%.  The complete
32-way screen did not generalize: candidate wall improved from 32.40 to 32.08
seconds, but aggregate CPU regressed from 906.59 to 913.11 seconds (0.72%).
Both final outputs hashed
`91b5b94ba7379e7176aba9e8e038d9618c0c6ded3e244c2524d1b6b6b18b3495`.
The dose was restored before adding a student-facing contract.

The documented `hint-late-cap=96` inlining dose was also rechecked against the
current O3 producer rather than carrying forward the older O1 result.  It cut
the remaining `Peek` call sites from 100 to 44, but expanded `.text` from
8,625,444 to 9,453,438 bytes.  An exact 32-way ABBA macro-processor screen
regressed from 1.220/1.175 seconds median wall/user to 1.280/1.240 seconds,
or 5.35% wall and 5.53% user.  This rejects indiscriminate late inlining even
under the newer O3-native pipeline; the next inlining attempt must address the
merged region's location pressure rather than merely raising the size cap.

A narrower O3 ordinary-inliner dose admitted only hinted 41-through-72
instruction callees whose actual arguments were all direct and included an
integer literal.  It reduced direct `Peek` calls from 96 to 50 and removed
237 million dynamic instructions from `Peek` in the hot Cachegrind compile,
but total instructions increased from 5,181,114,513 to 5,346,449,839
(3.19%).  The enlarged callers displaced smaller profitable inlines: dynamic
work rose by about 204 million instructions in string equality, 182 million
in `Take`, 83 million in `ScanPunctuator`, and 48 million in
`StartTokenSpelling`.  Native six-pair hot-TU time regressed 4.87% wall and
4.84% user.  The dose was removed.

A follow-up protected ordinary inlining by placing the same literal-call
class in a separate O3 wave after pruning.  At a 288-instruction translation-
unit dose, the existing small inlines remained intact and hot Cachegrind work
fell 0.50%, but native wall/user time rose 1.54%/1.62%.  At a 768-instruction
dose, Cachegrind work fell 1.26% and `Peek` calls fell from 96 to 73, while the
tokenizer object grew 39%, the hot `Run` body grew from 2,686 to 3,890 MIR
instructions, and native wall/user time rose 1.79%/1.34%.  This independently
confirms the P31 guarded-partial-inline grid: fewer calls or simulated
instructions do not pay for merged-body instruction-cache and frame pressure.
No further broad or partial inlining dose should be attempted without a new
region-allocation result.

The whole-compiler mixed-constant census then examined all 217 optimized
shared-source translation units.  Repeated groups are not confined to
`Peek`: material populations occur in parser expectations and expression
modes, semantic flags, lowering instruction kinds, temporary-frame modes,
serialization widths, and native encoding modes.  Any next specialization
prototype is therefore source-agnostic and O3-only: at most one bounded clone
per internal target, only a sufficiently repeated scalar group, and only when
substitution makes control flow removable.  It must keep unspecialized calls
on the original body, run normal cleanup before costing, and pass the hot
native screen before a student-facing contract is added.

The post-inline call-graph rebuild cross was also tested and rejected.  It
changed no workload object except `pipeline.o` itself, whose extra analysis
call added 56 text bytes.  Specialization rescans current calls and uses the
pre-inline graph only for the still-valid symbol/function map and conservative
recursion flag, so rebuilding has no present compiler-workload benefit.

### D.group-late bounded mixed-group specialization

The retained follow-up differs from the rejected `C.peek0` prototype in both
placement and policy.  It runs at O3 after the ordinary and post-prune inline
waves, considers the source-diverse populations found by `D.group-census`, and
never folds a target merely because one source-visible constant is common.
For an internal, nonobservable, nonrecursive fixed-arity target of at most 128
instructions, it selects at most one integer parameter/value group with at
least eight matching calls and at least one unlike call.  A cleaned generic
copy is compared with the cleaned specialized copy, and the group is accepted
only when the removed work across its matching calls pays for the complete
clone.  The census tracks at most 64 groups per target; the translation-unit
budgets are 24 clones and 1,536 cloned instructions.  Unspecialized calls stay
on the original definition.

The profitable compiler instance is the repeated zero-mode tokenizer path:
69 calls move to a one-parameter clone, while the unlike calls remain generic.
Final O3 cleanup combines this with a general predecessor-edge implication
rule: equality, inequality, and unsigned zero/one boundary edges may establish
an integer equality for a downstream `eq`/`ne` branch with one ordinary
predecessor.  The generic body remains 9 blocks / 57 LowIR instructions; its
specialized clone is 6 blocks / 43 instructions, and their native symbol sizes
are 315 and 219 bytes.  The tokenizer object grows by only 113 text bytes.
The final O3 compiler grows from 8,590,756 to 8,619,172 text bytes because it
also contains the new optimizer implementation.

The deterministic hot Cachegrind compile falls from 5,181,114,513 to
5,076,525,416 instructions (2.019%) with exact output.  On the full fixed O1
workload, six position-balanced 32-way self pairs move unpaired median wall
from 31.620 to 31.480 seconds (0.44%) and aggregate CPU from 902.935 to
897.660 seconds (0.58%); paired medians are 0.998094x wall and 0.994004x CPU.
The six-pair same-source GCC control is 1.001921x on paired CPU, giving a
normalized self result of 0.992099x, a 0.79% improvement.  Wall-time noise is
recorded as inconclusive: GCC's paired and unpaired estimators disagree in
direction, and the paired normalized wall ratio is 1.005274x.  All twelve
self results and all twelve GCC-control results produce the same final binary
hash, `fb6e82d42a8394299b3fc3abc724fc5ed653deb75f6d1bbe00ee2408e1875469`.

PA37 documents the general rule and owns a structural/behavioral property
that keeps O1/O2 generic, dynamically identifies the O3 clone without naming
it, proves the clone is smaller and the unlike call survives, verifies the
edge implication, and executes the serialized optimized LowIR through the
native path.  PA37 passed 188/188, PA38 passed 45/45, the through-PA38 report
passed 5,471/5,471, complete PA37/PA38 debug and object-round-trip lanes
passed, and the PA39 file audit had zero fatal findings.  Fresh 32-way O3
inception matched every object; self and inception both hash
`76e2cc0c198eb0f3d7521ad27c9f569ada72124dd5122f8d55b688173a8bba0e`.

### D.loop-shared late shared-loop inlining rejected

The GCC O1-to-O3 Cachegrind delta made the tokenizer's range search look like
the next large opportunity: GCC removes the generic helper, while self retains
an 82-byte `IsInRanges` body with three callee-save pairs and attributes about
100 million hot-TU instructions to it.  The existing inliner rejects that
22-instruction call-free body solely because it is a shared ordinary loop.

A source-independent O3-only probe gave otherwise-qualified shared loops a
separate late wave after every ordinary inline, pruning, and specialization
wave.  The separate budget preserved the already-profitable acyclic inlines.
On the tokenizer it removed the generic range helper and its five remaining
calls, but duplicated the search into three callers.  Tokenizer text grew
29,982 -> 30,676 bytes; `IsIdentifierBody` grew 185 -> 251 bytes,
`ScanIdentifierSuffix` 443 -> 605 bytes, and `Lexer::Run` 11,418 -> 11,993
bytes.

The first six-position full O1-workload window was discarded because its
opening baseline lane suffered an extreme scheduler tail.  Every output in
that window was nevertheless exact.  A fresh reverse-order three-pair window
also produced one identical final hash,
`cdbe8f335f683f833197aa9ee09e9ad835e25a5ad9b51d76dc3c53c27cbccb76`.
Its medians moved baseline/candidate wall from 31.48 to 32.37 seconds
(+2.83%) and aggregate CPU from 899.40 to 909.02 seconds (+1.07%).  Removing
the call boundary therefore does not pay for the duplicated loop state under
the current allocator.  The implementation was removed before student-facing
documentation or tests; this closes small shared-loop inlining unless a later
region-allocation change materially changes the premise.

### D.address-group readonly-address specialization rejected

The next GCC comparison showed four constant-propagated `IsOperator` variants,
while self retained 48 calls to one 150-instruction generic body.  A bounded
O3-only extension of grouped specialization therefore canonicalized direct
addresses of readonly structured byte globals, specialized the largest
repeated address group, folded byte loads from its initializer, and pruned
constant control while repairing surviving phi inputs.  It redirected 12
calls to a 73-instruction / 393-byte clone while preserving the 150-instruction
/ 893-byte generic body for unlike calls.  The macro-processor object grew 350
text bytes and the complete O3 producer grew 18,544 text bytes, primarily from
the additional analysis and cleanup machinery.

Both the native screen and full workload were output-exact.  Cachegrind on the
macro-processor compile fell from 15,012,364,660 to 14,990,891,458
instructions, a real but small 0.143% reduction.  Native single-TU CPU was
effectively flat at 3.3775 versus 3.3825 seconds.  Three position-balanced
32-way full-workload samples per producer then moved aggregate CPU from
898.48 to 898.77 seconds (+0.033%) and mean wall time from 31.23 to 31.72
seconds (+1.56%).  Every full result had the same final hash,
`c86a99d244de27e8b0c4fbda19471093f0accdbbed612513e1f91e88fd82e356`.
The local instruction saving does not pay for the producer footprint, so the
implementation was removed before adding a student-facing contract or test.
Do not retry readonly-address grouping without a substantially cheaper reuse
of existing scalar and CFG analyses.

### D.call-plan planned call-result grants retained

The next native census found that the cyclic-region planner had proved five
profitable spans in the tokenizer's `Lexer::Run`, but only two survived to
MIR.  Two independent reactive policies defeated the other grants.  An
ordinary earlier call result selected a call-preserved register without
consulting future whole-function plan spans, and the six-argument pressure
path assigned a frame home to a cyclic call result before the completed
arguments were retired.  The missed results then remained in the frame across
later calls and repeated branch comparisons.

At O2 and above, initial scalar call-result allocation preserves the existing
caller-/callee-saved order and plan-hold fallback, except that it rejects a
register whose future cyclic-region span overlaps the result lifetime.  A
proven cyclic result is not sent through eager six-argument frame homing;
after the call, its completed arguments are consumed before the ordinary
planned allocator runs.  Failure still takes the established frame fallback.
O1 uses the original allocation path and its tokenizer object is byte-identical
before and after this dose.  A broader prototype was narrowed after it added
callee-saved traffic to an unrelated EH fixture; both affected legacy fixtures
are byte-compatible under the retained policy.

On the tokenizer, planned cyclic grants rise from zero to five and busy-plan
misses fall from three to zero.  `Lexer::Run` falls from 2,632 to 2,451 MIR
instructions, scalar loads/stores from 408/388 to 319/311, and frame homes
from 14 to 11.  The tokenizer loses 234 `.text` bytes.  The retained helper
cost leaves the complete O3 producer effectively flat at +292 text bytes
(8,619,172 to 8,619,464).  The helper remains with the function-lowering
state it queries; adjacent formatting was compacted to keep `function.cpp` at
the 3,000-line audit limit.  Rebuilding recovered the exact accepted self and
GCC producer hashes, so that formatting change is code-generation inert.

Six position-balanced 32-way O1-workload pairs are binary-exact and all six
favor the candidate in aggregate CPU.  Mean CPU moves from 899.990 to 896.422
seconds (0.40% faster); wall is effectively flat at 31.477 versus 31.510
seconds (+0.11%).  The matching GCC-O3 source control moves CPU from 498.745
to 497.685 seconds (0.21% faster), so the normalized CPU ratio is 0.998157x, a
0.18% relative improvement.  On the O3 workload, five of six pairs favor the
candidate: mean CPU falls from 902.297 to 899.572 seconds (0.30%) and wall
from 31.852 to 31.398 seconds (1.42%).  Each side is deterministic within its
expected O3 binary hash.

PA38 documents the source-independent rule and owns a behavioral structural
control with two cases: an earlier call result live across a future cyclic
span, and a cyclic result from a six-register-argument call.  It checks only
the presence or absence of the result frame home and the relevant call
relationships, permits every physical-register choice, runs the program, and
uses O1 as a negative level-isolation control.  A temporary pre-change build
retained both frame homes; the candidate removes both.

The checkpoint gates are clean.  PA37 passes 188/188, PA38 passes 45/45, and
the report through PA38 passes 5,471/5,471.  The PA37/PA38 debug and object
round-trip lanes pass, and the PA39 file audit has zero fatal findings.  Two
fresh compiles of frozen `semantic_overload.cpp` at each of O2 and O3 produce
the same object at SHA-256 `9030c008ceec078ad5083a662d7eefbe4a5745656d45530d51a6d641e3f18796`;
O2 and O3 are also mutually exact on this source.  Independent all-32 O2 and
O3 inception lanes match every object and the final compiler.  Their final
hashes are `86d0f8a00f53cc31ed1e08b62d3b330dd355cc464ccbdb1ef2f162946793a30e`
at O2 and `4faa64e562aee329b12475a506240169f4cf1170274dd3e3139012ee127c2f4c`
at O3.  The full two-generation gates take 51.29 and 49.93 seconds wall,
respectively, with explicit 32-way outer, build, and object concurrency.

### D.epilogue shared-return layout rejected

The current tokenizer profile exposed 24 complete copies of `Lexer::Run`'s
six-pop epilogue, while GCC and Clang use one shared return sequence. The
existing native epilogue planner already supports exact byte-costed sharing,
but every optimized level had disabled it. Re-enabling that policy at O2/O3
reduced `Run` from 2,418 to 2,257 native instructions, its physical epilogues
from 24 to one, tokenizer text by 1,006 bytes, and complete O3-producer text by
94,192 bytes.

That static saving did not improve throughput. The broad form raised the two-
lane O1-workload user time about 1.5%. Sharing only functions with at least 16
returns reduced producer text by 17,928 bytes but raised user time about
0.26%. Excluding the exceptionally return-heavy instruction encoder and
limiting the policy to 16--64 returns was nearly flat on O1 (+0.11% user), but
the O3 workload regressed about 0.36% user, 0.32% aggregate CPU, and 1.5% wall.
The duplicate return bytes are cheaper than the extra taken branches in this
workload. Every form was removed before contract or test movement.

### D.copy-pair staged adjacent scalar-copy fusion rejected

The next MIR prototype recognized two adjacent 64-bit loads staged in a
unique predecessor and their two adjacent stores in the successor. With
conservative CFG, liveness, register, debug-range, and intervening-operation
guards, it replaced them by one exact 16-byte vector `copy_bytes` before the
first store, preserving the original load-before-store semantics. It reduced
the specialized `Peek(0)` clone by three MIR instructions and eight native
bytes, reduced the generic `Peek` by eight bytes, and reduced tokenizer text
by 22 bytes.

Despite the local improvement, the generated O3 compiler grew 8,744 text
bytes from the new analysis. More importantly, twelve position-balanced hot-
TU lanes were exact but the candidate was slower in every position: mean wall
moved 0.9733 to 1.0133 seconds and user time 0.9283 to 0.9667 seconds, about a
4.1% loss. Inspection showed that shrinking several early functions shifted
the much hotter translation and physical cursors onto less favorable entry
addresses. The rewrite was removed. This result motivated testing layout
stability directly rather than rejecting every locally useful shrink.

### D.align64 bounded O3 function-entry alignment retained

The native object path previously aligned every function to only two bytes.
The relocatable writer then repacked ordinary and weak text at that same
minimum, so even an encoder-side alignment attempt would have been discarded.
The retained O3 policy records a 16-byte entry-alignment request when the
final optimized function contains at least 64 MIR instructions. Smaller O3
functions and every function at O2 and below retain two-byte alignment. Both
executable emission and relocatable ordinary/COMDAT partitioning consume the
same MIR fact.

In the compiler workload, 3,772 of 4,043 native functions at least 256 bytes
long become 16-byte aligned, versus 628 incidentally aligned before the dose.
Complete O3-producer text grows 8,619,464 to 8,644,712 bytes (+25,248, 0.29%).
The O1-workload three-pair all-32 screen is exact in every lane: mean wall
falls 32.02 to 31.28 seconds (2.31%) and aggregate CPU 852.56 to 845.26
seconds (0.86%). A four-pair same-source GCC control is effectively flat in
CPU (+0.09%), making the normalized self CPU result about 0.9905x, a 0.95%
relative improvement.

The O3 workload required a four-pair extension because one baseline lane had
a scheduler tail. The position-balanced medians fall 31.89 to 31.80 seconds
wall (0.30%) and 862.80 to 853.78 seconds CPU (1.05%). A first GCC O3 window
was rejected in full after one 588-CPU-second host event; the fresh balanced
window is flat in CPU (+0.06%), so normalized self CPU improves about 1.11%.
Baseline and the timed candidate O3 outputs are each deterministic at hashes
`1e22afd0783bb22065dc8dedf28255946a4389b9296447b2a1840b61fad55851`
and `dfcd0aad2dcd0e1314f06715c9909048f7ff0f3653c4212595864c06ee01c871`.
The final sparse MIR serialization cleanup adds only 32 text bytes to the
producer and produces hash
`c8ca94b36224b126c0dd163f670ee5da3cd327898af4253821788b97d36a572b`.

PA38 documents the size-only rule. Its focused property dynamically counts
the final MIR rather than matching fixture text, checks O3/O2 isolation and a
small-function guard, executes both native programs, and replays the LowIR
through the compile-only driver to verify the ELF function address after text
partitioning. The PA38 suite passes 45/45 and the report through PA38 passes
5,471/5,471. The PA37/PA38 debug and native object-roundtrip gates pass, and
the PA39 file audit reports zero fatal errors. Two fresh frozen
`semantic_overload.cpp` compiles are deterministic within each level at
`a56db05037d0fd27d1597f6c1775b723b17e9b447b34b908c09de622cde4a985`
for O2 and
`88665587f98a62da5017e1098681a3871d9807f0c5e117603c19cabd889d7aff`
for O3; their intentional difference is the O3-only layout policy. Explicit
all-32 O2 and O3 inception pass every object comparison and the final
executable comparison in 49.63 and 50.68 seconds. The self/inception hashes
are exact at
`83eb0b6e2e19b00a5faa883d7d556ef403fcf7621acc2cec3d3a339708793762`
for O2 and
`c8ca94b36224b126c0dd163f670ee5da3cd327898af4253821788b97d36a572b`
for O3.

### D.copy-pair-align aligned re-evaluation rejected

The staged adjacent-copy prototype was reconstructed on top of the retained
large-function alignment, with a stronger alias guard: the vector copy may
cross only nonvolatile stores proven disjoint within the same destination
base. It again replaces the two 64-bit loads and two stores by one exact
16-byte copy in both generic and specialized `Peek`, saving three MIR
instructions in each. The candidate producer grows 8,940 text bytes from the
analysis itself.

Alignment reduces but does not remove the layout penalty. Every dominant hot
entry remains 16-byte aligned, but shrinking the early functions still moves
`PhysicalCursor::Next` by one 16-byte slot. In a twelve-lane hot-TU screen,
mean wall moves 1.208 to 1.222 seconds (+1.1%) and user time 1.155 to 1.173
seconds (+1.6%). Each side is deterministic within its expected object hash.
The candidate is therefore rejected and fully removed again: the local MIR
win remains real, but 16-byte entry alignment is not a sufficiently strong
layout oracle to make it a throughput win.

### D.hint-clone loop forwarding re-evaluation rejected

The retained grouped-specialization pass creates its O3 clones after the main
loop pipeline. The dominant `Peek(0)` clone consequently did not receive the
loop-carried store/load forwarding already applied to its inline-hinted
generic source. A narrow prototype let that O3 clone inherit the source
function's optimization hint and ordered final predecessor-edge cleanup before
loop forwarding. In the hot clone this replaced the queue-size reload on the
second loop test with the value just stored on the backedge. The number of
preserved registers remained three; the complete producer grew by only 32 text
bytes.

The extra carried live range cost more than the eliminated load. In a twelve-
lane position-balanced hot-TU screen, deterministic baseline and candidate
objects hashed `a8dda7edde98adfb744adec02a56c0abd8b4137b47643efe77af670921d3f4cd`
and `164d315acfe7e6e87e3c42095891adaeae3c3eb0bdcdc34daea4d84c0e827ef8`.
Mean wall time moved from 1.210 to 1.237 seconds (+2.2%), and user time from
1.157 to 1.183 seconds (+2.3%). The implementation and pass-order change were
removed before contract or test movement. This is another direct example of
an instruction-count improvement losing to register-lifetime and layout cost.

### D.loop-thread backedge-condition threading rejected

A stronger follow-up matched GCC's specialized `Peek(0)` control shape without
carrying the queue size around the loop. After ordinary final edge cleanup, a
general O3 rule distributed a private loop-header `phi`/compare/branch onto
each unconditional incoming edge when the targets had no phis and every CFG,
dominance, use-count, and EH guard held. The entry kept its initial load and
test, while the latch tested the value it had just stored. A paired native
rule then reused the flags from an immediately preceding `add 1` across scalar
stores for zero/equality branches. Together they removed the latch reload,
header jump, and repeated test without adding a live range or changing the
clone's three preserved registers and 219-byte native size.

The dynamic saving is real but too small for the implementation. The combined
optimizer grew the complete O3 producer by 10,416 text bytes. Twenty-four
position-balanced hot-TU lanes were deterministic at baseline object hash
`a8dda7edde98adfb744adec02a56c0abd8b4137b47643efe77af670921d3f4cd`
and candidate hash
`c01dff2344d86a105a4e1cdadf4f03bee9243b12b41a3f450dc7ccd4a0b9028e`.
Mean wall time moved only from 1.204 to 1.202 seconds (-0.21%), while user time
was exactly flat at 1.154 seconds. The LowIR and MIR additions were removed
before student-facing contract or test movement; this does not materially
advance the 20% O3 objective.

### D.reuse-plan destructive-reuse reservation guard rejected

The post-alignment profile and function census localized the remaining cursor
gap more precisely. `PhysicalCursor::Next` has no frame homes or spills, and
`TranslationCursor::Next` has only six frame homes; their excess is primarily
copies, calls, and missed interprocedural collapse rather than classical spill
traffic. A current `hint-late-cap=96` dose still made `Lexer::Run` lose its one
remaining cyclic-region grant: destructive result reuse can inherit a
preserved register without passing through the allocator's future-span check.

A narrow O2/O3 prototype rejected destructive reuse only when the new result's
lifetime overlapped a future cyclic-result plan. It was byte-inert for the
tokenizer under the shipped policy and restored the cyclic grant under the
hinted dose. In that dose, `Run` fell from 2,627 to 2,403 MIR instructions;
scalar loads/stores fell from 616/516 to 513/383. The result was nevertheless
still 4.5--5% slower than the accepted compiler on a 36-lane balanced hot-TU
screen, so broad hinted inlining remains rejected after the new allocation
cross.

The guard alone changed 15 generated compiler objects and reduced complete O3
compiler text by 3,872 bytes. Three position-balanced all-32 O1-workload pairs
were output-exact at
`89de66702dec1fb2bee0a0a4aab9f54be3e43dc59ac6ef37e859d0048cec9a11`.
Baseline/guard mean wall was 31.330/30.993 seconds, but that apparent gain came
from one baseline scheduler tail. Mean aggregate CPU was 894.120/893.000
seconds and the paired median CPU ratio was about 1.0001x, effectively flat in
the wrong direction. The implementation was removed before contract or test
movement. This closes destructive-reuse reservation as an independent change
and confirms that preserving one cyclic grant is insufficient to make broad
merged-body inlining profitable.

### D.align-sweep stronger function-entry alignment rejected

The locally smaller adjacent-copy prototype had shown that 16-byte function
alignment still lets a small early size change move hotter functions by a
whole entry slot. A direct policy sweep therefore rebuilt the accepted O3
compiler with 32- and 64-byte alignment for the same final functions of at
least 64 MIR instructions. The 32-byte producer added 28,320 text bytes over
the accepted compiler; the 64-byte producer added 84,192 text bytes. Both
compiled the hot preprocessing TU to the accepted exact object hash
`d9dbe51e6b5848711ff72e7b27fef7f5bd638a352db0ea19c20efbc2ae74a41e`.

Neither stronger alignment was a stable throughput win. In a 36-lane
three-way window, mean CPU per compile was 1.7092 seconds at 16 bytes, 1.7283
at 32 bytes, and 1.6992 at 64 bytes. A cleaner 24-lane reversed window was
1.3638, 1.4638, and 1.4000 seconds respectively. Thus 32 bytes regressed in
both windows, while 64 bytes swung from 0.6% faster to 2.7% slower and paid a
1.06% producer-text increase. Stronger padding is not the missing layout
oracle. The shipped 16-byte policy was restored before documentation or test
movement.

### D.hint49 move-constructor attribution dose rejected

The post-alignment flat profile attributed 310,430,637 instructions to the
preprocessor's `Token(Token&&)`, versus 53,554,903 in the same-source GCC
compiler. The optimized constructor contains 48 LowIR instructions, exactly
at the shipped late hinted nonleaf cap. Raising only `hint-late-cap` from 48
to 49 inlined 36 of its 37 retained calls in `macro_processor.cpp`; optimized
LowIR grew from 54,338 to 55,810 lines and the complete O3 producer added
11,600 text bytes. Static direct calls in the linked compiler likewise fell
from 37 to one.

The apparent flat-profile gap was attribution rather than avoidable work. An
output-exact candidate Cachegrind run retired 5,033,575,400 instructions,
only 3,219,302 (0.064%) fewer than the existing 5,036,794,702-instruction
baseline. The former constructor cost moved chiefly into `AddSourceToken`,
`vector<Token>::push_back`, and `Lexer::Run`. Native 24-lane hot screens were
also contradictory: candidate CPU was 1.1% faster in the first balanced
window and 4.2% slower in its reversed repeat. The dose was removed without
contract or test movement. Future profile work must use inclusive call paths
or whole-program deltas before treating an out-of-line C++ helper as a large
target.

### D.group4 lower-frequency constant groups rejected

The retained O3 grouped specializer was widened only by lowering its minimum
matching-call threshold from eight to four. Across the complete compiler this
found exactly one additional general case, in semantic template result
identity. Its specialized native clone was 11 bytes, the target object shrank
by five bytes, and total producer text remained exactly 7,907,026 bytes; the
existing tokenizer clone was unchanged. Thus the dose did not add optimizer
text or broad clone growth.

The exact-output 24-lane hot-TU screen nevertheless regressed from 1.3525 to
1.4458 seconds mean CPU per compile, about 6.9%. A five-byte early object
change can still move later linked functions despite the retained 16-byte
large-function alignment, so the local semantic simplification is not a
stable whole-compiler win. The eight-call threshold was restored before
contract or test movement.

### D.final-slots final dead-slot sweep rejected

The tokenizer's final optimized LowIR still contained repeated scalar setup
slots from inlined `std::string::push_back` bodies. Some apparent uses had
disappeared only after the late load-reuse and edge-cleanup waves, after the
ordinary slot cleanup. A narrow prototype reran the existing zero-load slot
remover at the very end, only for callers changed by inlining.

The prototype found the intended structural residue: tokenizer output fell by
34 LowIR instructions and six additional functions reported a dead-slot
change. It did not change the relevant native work. `AppendUTF8` remained
exactly 495 MIR instructions and 1,965 native bytes, with the same 80 scalar
loads and 70 scalar stores; the complete tokenizer object remained 28,741
text bytes. Native lowering already ignores these unread scalar setup slots,
so another whole-function slot scan would duplicate work without improving
generated code. The sweep was removed before contract or test movement. A
useful follow-up must eliminate the address/store/load shuttles that survive
into MIR, not merely canonicalize dead LowIR storage.

### D.const-ref constant reference-slot recovery rejected

A stronger source-independent prototype recognized a scalar local whose
address is used only by exact, nonvolatile loads and rewrote those loads back
to direct slot loads before ordinary scalar promotion. The broad form removed
more LowIR but lengthened nonconstant values across calls; in `AppendUTF8` it
created six spills and two frame homes. The narrowed form admitted only slots
whose direct stores are all integer or floating constants, which are freely
rematerializable and cannot create that live-range cost.

The narrowed property removed the ten zero-reference shuttles in
`AppendUTF8`: the function fell from 495 to 465 MIR instructions, scalar loads
from 80 to 70, native size from 1,965 to 1,846 bytes, and acquired no spill or
frame home. The tokenizer lost 128 text bytes. Applied at every optimized
level, however, the O3-produced compiler was 3,056 text bytes smaller but the
24-lane O1 hot-TU screen regressed 1.56% mean CPU (0.9608 to 0.9758 seconds).

To separate requested-level scan cost from O3 producer quality, a second form
ran the rewrite only for requested O3. Its O3-produced compiler remained
2,368 text bytes smaller, and its fixed O1 output was byte-exact with the
accepted compiler, so the recognition scan was completely dormant in the
timed workload. The 24-lane balanced repeat still regressed from 0.9642 to
0.9808 seconds mean CPU (+1.73%) and from 0.9700 to 0.9875 seconds wall
(+1.80%). The smaller O3 producer's changed code/layout is therefore itself
detrimental; both forms were removed before contract or test movement.

### D.repeat-stable-query repeated read-only query reuse retained

Inclusive call-path profiling corrected the next target.  The retained
specialized `Peek(0)` clone was called 29,501,413 times by `Lexer::Run` and
accounted for 1,242,645,360 inclusive Cachegrind instructions, while the
same-source GCC compiler called its corresponding constant-propagated helper
only 7,314,471 times.  This was repeated dynamic query work, unlike the
previous move-constructor flat-profile attribution.

The retained O3-only LowIR rule proves a query property from structure rather
than a function name or fixture body.  A candidate callee must enter a
two-successor guard, have an acyclic, nonvolatile, side-effect-free fast arm
ending in its sole normal return, and allow its slow arm to return normally
only after revisiting the guard.  Synthetic continuations after known
`noreturn` calls are ignored when identifying normal returns.  Caller
availability is keyed by the callee and exact typed SSA argument tuple, merged
across the CFG, and invalidated by stores, atomics, object copies, zeroing,
volatile operations, or other calls.  EH callers are excluded.  The analysis
admits at most 128 signatures per caller and has an explicit update budget.
The first call establishes the available result and a subsequent proven call
is replaced by a copy before the existing cleanup pipeline runs.

On the tokenizer this removes 31 of 69 static calls to the specialized query,
reduces `Lexer::Run` from 2,451 to 2,139 MIR instructions, cuts call copies
from 286 to 182 and scalar stores from 311 to 308, and shrinks tokenizer text
from 29,812 to 28,148 bytes without adding a spill.  O1 and O2 output hashes
remain exact.  The bounded analysis recognizes one callee, four signatures,
and 31 reuses with no budget skip and a 65,337-byte peak accounting estimate.
An output-exact Callgrind comparison falls from 5,007,552,472 to
4,781,819,644 retired instructions (-4.51%); clone calls fall from 29,501,413
to 18,094,496 and their inclusive cost falls from 1.243 billion to 1.060
billion instructions.

The final position-balanced 12-lane hot screen improves paired median wall
time 3.36% and user time 3.52%.  Three all-32 ABBA blocks over the complete
fixed O1 workload are output-exact at manifest hash
`b4f90fba3d0a3833dfd935d0bd09fff3f48898fdf6eaec1ad953645cc8226b93`
and final hash
`2844c3cb0f85f45a30d9b60f4baaae2904dc34e5f3c955bbb6b76181552df22a`.
Their paired median ratios are 0.985355x wall and 0.979360x aggregate CPU.
The equivalent requested-O3 workload has paired median ratios 0.989897x wall
and 0.980850x CPU.  A same-source GCC-O3 producer control is effectively flat
at 0.991355x wall and 1.002334x CPU; dividing block by block gives a normalized
paired median of 0.993947x wall and 0.982454x CPU.  The retained decision is
therefore supported by a roughly 1.75% normalized CPU gain, not merely by a
raw self-host timing movement.

PA37 now describes the structural query contract and owns a student-buildable
property control covering the normal and cold-`noreturn` positive shapes plus
store, ordinary-call, changed-argument, and volatile negative barriers.  The
checker verifies O1/O2 isolation, O3 structure, bounded stats, LowIR replay,
and native behavior without exact whole-program matching.  There is no new
PA38 optimization contract: the existing PA37 control drives the changed
LowIR through native compilation, and the unchanged PA38 surface is covered
by its full suite.  PA37 passes 188/188, PA38 passes 45/45, the report through
PA38 passes 5,471/5,471, and the PA37/PA38 debug and object-round-trip lanes
pass.  The PA39 audit has zero fatal findings.  Two frozen O2 and O3 compiles
remain deterministic at their prior hashes, and a fresh explicit all-32 O3
inception run matches every object and the final executable in 31.30 seconds.
The final O3 producer hash is
`d61d9ec8610c9fb5cd5890a181aac3348294d1a66e45e84e12f6f37a98f398c3`.

The residual 18,094,496 clone calls remain the strongest measured opportunity,
but a follow-up must attribute which calls are separated by genuine mutation,
by conservatively classified read-only calls, or by unrelated stores.  It
must not broaden availability based on the `Peek` name or source layout.

### D.loop-priority-inline bounded loop-call inlining retained

The post-query profile exposed a different interprocedural gap.  The
self-built tokenizer spent about 685 million Cachegrind instructions in
`TranslationCursor::Next` plus `PhysicalCursor::Next`, while same-source GCC
spent about 445 million in its combined translation path.  GCC had expanded
the hot physical-cursor call inside the translation loop while retaining the
callable physical body for a second cold site.  Broad hinted inlining and the
earlier shared-loop policy were already measured losses; this opportunity was
instead one large, repeated body with distinguishable loop and non-loop uses.

The retained O3-only rule is source-independent.  It considers an internal or
weak, fixed-arity, nonrecursive `inline_hint` body with no EH instructions and
an optimized size from 256 through 512 LowIR instructions.  A caller must
have no EH region and exactly two direct calls to that body, with matching
argument counts, exactly one call in a natural loop, and exactly one outside
all natural loops.  Eligible pairs are ordered by decreasing body size and
then stable function order.  At most one pair per translation unit is
selected, and only its unique loop call is expanded.  The ordinary full
inliner, cleanup, and post-inline slot promotion do the rewrite; the non-loop
call and shared definition remain.  This excludes the arbitrary two-hot-site
case and bounds growth to one clone of at most 512 instructions.

Productionizing the prototype also removed avoidable analysis.  The selector
sorts compact candidate pairs, constructs a loop forest only until it finds
the first valid pair, records the selected caller's loop-block bitmap once,
and makes the generic inliner visit only that caller.  Dedicated statistics
report pairs considered, candidates selected, calls expanded, cloned
instructions, owned scratch, and elapsed time.  On the tokenizer it considers
and selects one pair, clones one 396-instruction placement-time body, reduces
the two direct physical-cursor calls in the translation caller to one, and
produces byte-identical optimized LowIR to the screened prototype.  The
tokenizer object grows 2,800 text bytes; after the cheaper selector and final
stats support, the complete O3 producer grows 30,320 text bytes from
8,672,128 to 8,702,448 bytes, substantially less than the prototype's
59,020-byte growth.

The final hot object is exact at SHA-256
`d9dbe51e6b5848711ff72e7b27fef7f5bd638a352db0ea19c20efbc2ae74a41e`.
Callgrind falls from 4,781,920,534 to 4,662,903,717 instructions, a 2.49%
reduction.  On the fixed O1 full-compiler workload, three position-balanced
pairs are exact at manifest
`d11de18177221bf591045e1ac7ea7e02ded58501b10c869c4d164a1658c799ab`
and final hash
`2d4483245bd508e0f6242bf9717e2a6ede53bb2b7bbcf1b7f6fdf7cb1017583f`.
Mean self wall falls from 30.820 to 30.080 seconds (-2.40%) and aggregate CPU
from 873.453 to 857.043 seconds (-1.88%); paired medians improve 2.87% and
1.93%, respectively.  The matching final-source GCC control changes only
-0.33% wall and -0.24% CPU by means.  Mean normalization is therefore
0.9792x wall and 0.9835x CPU, while position-by-position paired normalization
has medians 0.9868x and 0.9843x.

The requested-O3 workload was extended to twelve lanes because the first
wall window was close.  Across six pairs, old and new producers each reproduce
their own manifest and final hash.  Mean wall is 30.900 versus 31.012 seconds
(+0.36%) and paired-median wall is +0.52%, both inside the 1% no-regression
allowance.  Mean aggregate CPU falls from 876.883 to 862.517 seconds (-1.64%)
and every pair favors the new producer.  The corresponding GCC requested-O3
control gets 0.48% slower in mean CPU and about 1.48% slower in paired-median
wall.  The mean-normalized self/GCC result is consequently about 0.9834x wall
and 0.9789x CPU; the ratio of paired medians is about 0.9905x and 0.9784x.
This is a relative code-quality win even though raw O3 wall is nearly flat.

PA37 documents the bounded structural rule and owns a property control with
two eligible loop/non-loop pairs, a no-loop pair, and a two-loop-call pair.
The checker verifies O1/O2 isolation, exactly one complete O3 expansion,
retention of the shared call and body, bounded stats, LowIR replay, and native
behavior without exact whole-program matching.  No PA38 contract is added:
the change is entirely serialized LowIR, and the PA37 property compiles and
runs that output through the native path.  PA37 passes 188/188, PA38 passes
45/45, and report-through-PA38 passes 5,471/5,471.  PA37/PA38 debug and
round-trip lanes pass; the LowIR audit remains 124/99; and the PA38 file audit
has zero fatal findings and the same 32 warnings.  Frozen O2 is exact at its
prior hash, frozen O3 is internally exact at
`bd835d64eca1f2b874b84326b9c3f64af3fd3beba6991e2acad3c023ed2aaaaa`,
and explicit all-32 O3 inception matches every object and the final compiler
in 47.82 seconds.  The final self O3 producer and inception hash is
`e4670c488cd40ceec44dcffbbc0a2a115db1c427360ff341175e234e78b84975`.

### D.group-fast-sibling grouped wrapper and sibling return rejected

The next prototype coupled two source-independent O3 transformations.  A
LowIR rule retained the bounded read-only fast-return prefix of one newly
created grouped scalar clone and moved its complete body to a private slow
clone.  A native rule converted an eligible direct scalar call immediately
followed by its return into a sibling jump when the caller had no stack
arguments, frame state, callee saves, dynamic stack, debug homes, or host EH.
The latter rule made the grouped wrapper's sole slow call cheaper, so the two
doses were screened both separately and together.  Provisional PA37 and PA38
property controls checked level isolation, structural eligibility and negative
shapes, serialized replay, native encoding, bounded statistics, and behavior
without matching fixture or generated-symbol text.

The coupling produced a genuine but unrepresentative tokenizer result.  Six
hot-TU ABBA blocks improved paired wall/user medians by 0.66%/0.69%, and an
output-exact Callgrind run fell from 4,662,903,717 to 4,613,564,880
instructions (-1.058%).  The complete fixed O1 workload instead moved by
+1.035% wall and +0.169% aggregate CPU across six valid ABBA blocks.  Its
matching GCC source/layout control moved +0.411%/-0.123%, so the normalized
result was approximately +0.621% wall and +0.292% CPU.  Requested O3 moved
-1.147%/-0.105% raw; GCC moved -0.055%/+0.196%, yielding about
-1.093%/-0.300% normalized.  Thus the bundle helped only the active O3
workload and failed the intended direction on the primary fixed-O1 producer
oracle.

Isolation explained the interaction.  Grouped fast-wrapper splitting alone
regressed one complete O3 ABBA block by 0.594% wall and 0.614% CPU despite its
hot instruction saving.  The first sibling-return prototype was flat on O1
(three-block medians +0.300% wall/+0.006% CPU) and modestly favorable on O3
(-0.327%/-0.127%).  After removing the failed LowIR half and rebuilding from
the exact prospective final source, however, sibling return was only
-0.163%/+0.106% on O1 and its first O3 screen reversed to +1.641% wall and
+0.370% CPU.  Every fixed-level output was deterministic, and the provisional
PA37 188/188, PA38 45/45, 5,471/5,471 cumulative report, and zero-fatal audits
were clean before removal.  The final-source reversal demonstrates entry and
link-layout sensitivity rather than a robust improvement.  Both transforms,
their statistics, and their provisional student contracts were removed.

### D.repeat-readonly-call read-only call-barrier relaxation rejected

The retained repeat-stable-query pass still had 18,094,496 dynamic calls to
the specialized query.  A source-independent follow-up classified an internal
function as call-free and read-only only when every instruction was already
accepted by the repeat-path structural proof.  Direct calls to such functions
then preserved the caller's available query results.  Stores, atomics, object
copies, zeroing, volatile operations, indirect calls, and direct calls with
any nested call or other unclassified instruction remained barriers.

On the tokenizer this recovered four additional query results, reducing the
remaining static specialized calls from 38 to 34.  Optimized LowIR lost eight
lines and tokenizer text fell 48 bytes.  The complete candidate O3 producer
grew 1,348 text bytes from the summary and lookup machinery.  An output-exact
Callgrind run fell from 4,662,903,717 to 4,650,176,064 instructions, a real
0.273% reduction.

The complete fixed-O1 producer screen did not support retaining the rule.
Three valid self ABBA blocks had paired median candidate/baseline ratios of
1.000331x wall and 1.000497x aggregate CPU.  Three same-source GCC ABBA blocks
had paired medians of 1.006683x and 1.001020x, respectively, making the
median-ratio normalization about 0.99369x wall but only 0.99948x CPU.  Thus
raw self throughput moved slightly in the wrong direction and the normalized
CPU benefit was only about 0.05%.  Two additional GCC blocks were discarded
in full: one opened with an isolated extreme scheduler tail, and the other's
final lane coincided with an independently observed one-core repository scan.
Both were replaced rather than partially sampled.  Every valid lane reproduced
the same 219-object manifest and final compiler.

The primary raw screen therefore fails the plan's intended-direction rule,
and the deterministic saving is too small to justify a new analysis contract.
The implementation was removed without changing PA37 documentation or tests.
Do not retry call-barrier relaxation without a substantially larger dynamic
population or a proof that reuses an already-retained summary at near-zero
producer cost.

### D.block-local-save block-local callee-save recoloring retained

The accepted tokenizer profile exposed a native cost below the LowIR call
structure.  Its hot grouped query clone was 219 bytes and preserved `rbx`,
`r12`, and `r13`, even though complete MIR liveness showed that every `r13`
range began and ended within one block.  Online physical placement had reused
one callee-saved color for several disjoint local ranges, causing every call to
pay a function-wide save and restore for values that never crossed an edge.

The retained O3-only rule runs after complete MIR liveness.  It considers at
most the five SysV callee-saved GPR colors.  A source color is eligible only
when no debug range uses it, every occurrence is explicit, no live range
reaches a block boundary, and every block containing it has a conflict-free
destination.  Blocks may independently use a caller-saved color or merge into
an already-used callee-saved color.  The rewrite is all-or-nothing across the
source color.  Liveness is recomputed after each success; eliminated sources
cannot become destinations, and callee-saved destination anchors cannot later
be eliminated.  These monotonic masks prevent the recoloring cycles found in
the first prototype and bound convergence by the five source colors.  O1 and
O2 retain their existing whole-function recoloring policy.

The final hot clone is 203 bytes and preserves two registers.  The complete O3
producer shrinks from 8,702,448 to 8,699,524 text bytes, a 2,924-byte saving.
The frozen large semantic-analysis object is deterministic at O2 and remains
byte-identical between old and new producers at
`b5e47d8f2682c301351581c47228eace71549bcb4b52f0c26322e4f97908aadf`.
At O3 the old and new deterministic hashes are
`762d84fec4163717b3e019e5ef04965f2b8bc3fe4a88d3abf6b673609dc54b7c`
and
`6432ef9d40c5c6567fb2af4b482e83b170e567c10c44f94d0e4eedc3efdd6e2d`;
the candidate object is 55 text bytes smaller.  The hot O1 tokenizer object is
byte-identical at
`d9dbe51e6b5848711ff72e7b27fef7f5bd638a352db0ea19c20efbc2ae74a41e`,
while Callgrind falls from 4,662,903,717 to 4,582,516,636 instructions, a
1.7247% reduction.

On the fixed requested-O1 producer workload, six position-balanced blocks all
favor the candidate in aggregate CPU.  Mean wall is 30.203 versus 30.248
seconds (+0.15%) and paired-median wall is +0.31%, both within the 1% raw
no-regression allowance.  Mean CPU falls from 855.518 to 850.642 seconds
(-0.57%) and the paired median improves 0.49%.  The same-source GCC control
moves -0.20% wall and +0.13% CPU by means, with paired medians of -0.29% and
+0.08%.  Mean normalization is therefore about 1.0035x wall and 0.9931x CPU;
paired-median normalization is about 1.0060x and 0.9943x.  The wall result is
scheduler-sensitive but remains below the hard floor; every self CPU block,
the normalized CPU result, and deterministic attribution favor retention.

Requested O3 is stronger and includes the new proof's own compile-time cost.
Six valid self blocks improve mean wall from 30.412 to 29.976 seconds (-1.44%)
and mean CPU from 858.572 to 852.233 seconds (-0.74%); paired medians improve
1.81% and 0.75%.  Two self blocks were discarded whole and replaced after an
unrelated repository scan and an unrelated GCC build were observed during
their windows.  Five uncontaminated GCC O3 blocks move +0.22% wall and +0.19%
CPU by means, with paired medians of -0.14% and +0.18%.  Repeated attempts at
a sixth control block were discarded or stopped when the recurring external
jobs reappeared.  The resulting mean-normalized O3 improvement is about 1.65%
wall and 0.93% CPU; paired-median normalization is about 1.67% and 0.93%.
The final same-source O3 wall ratio is approximately 1.648x, so the 1.5x phase
goal remains open.

PA38 owns the change because serialized LowIR is unchanged.  Its existing
call-free recoloring fixture now compares O1/O2 and O3 preservation
properties, proves a dynamically identified source color disappears without
requiring a particular replacement register, retains a call-crossing negative
control, executes all generated programs, and checks level-isolated bounded
statistics.  On the fixture O3 reports 10 candidates, two eliminated colors,
and six rewritten blocks; O2 reports zero for each field.  PA37 therefore
needs no new contract or test.

The PA38 suite passes 45/45, the full report passes 5,471/5,471, PA37/PA38
debug and object-round-trip lanes pass, and the PA39 audit has zero fatal
findings with the unchanged 32 warnings.  Explicit all-32 inception gates
match every object and final compiler at O2 and O3.  O2 self/inception is exact
at
`83d76ec00738e03f5ad6ae48003d44a956e51342a2be305662518998e7535075`;
O3 is exact at
`2e39c2458ba0b8ed3afbbaca028bf64c653c8897eb4119fa1009d763c96df643`.

### D.medium-copy-chunks O2+ medium fixed-copy selection retained

The accepted profile still attributed 310,429,155 exclusive Callgrind
instructions to the hot preprocessing-token move constructor.  Its final
61-byte fixed tail used `rep movsb`, while the same-source GCC producer used
explicit vector and scalar transfers.  The earlier `C.copy61` prototype had
already proved that replacing this operation could make a representative
workload slower, so the rule was re-evaluated rather than inferred from that
one function.  The intervening accepted native layout, placement, and
recoloring changes materially changed the result: the old screen had five of
six CPU pairs regress, whereas the final screen below has all fixed-O1 pairs
and eleven of twelve requested-O3 pairs improve.

The retained rule is O2 and above rather than the prototype's broader O1+
policy.  One linear MIR scan selects direct chunks only for a fixed
`copy_bytes` larger than 32 and no larger than 64 bytes whose proven alignment
is below eight.  The selection is an explicit serialized MIR encoding fact,
so MIR dumping and object emission use the same decision.  Existing small or
naturally aligned direct copies are unchanged; O0, O1, dynamic copies, and
copies above 64 bytes retain their compact policy.  The scan visits every MIR
instruction at most once, owns no proportional scratch state, and reports one
bounded rewrite statistic per selected operation.  Restricting the rule to
O2+ both preserves the O1 reference producer and avoids an otherwise useless
scan when an O3-produced compiler is asked to emit O1 code.

The final O3 producer grows from 8,699,524 to 8,706,232 text bytes.  The hot
move constructor grows from 215 to 258 bytes but replaces the fixed 61-byte
`rep movsb` with three unaligned vector pairs and scalar tail chunks.  The O1
hot object remains exact at
`d9dbe51e6b5848711ff72e7b27fef7f5bd638a352db0ea19c20efbc2ae74a41e`,
while Callgrind falls from 4,582,516,636 to 4,447,750,256 instructions, a
2.9409% reduction.

On the complete fixed-O1 producer workload, all six position-matched CPU
pairs favor the candidate and both producers emit the same 219-object
manifest and final compiler.  Mean wall falls from 30.108 to 29.652 seconds
(-1.52%) and aggregate CPU from 850.067 to 843.138 seconds (-0.82%);
paired-median wall and CPU improve 0.85% and 0.72%.  The matching GCC control
moves +0.38% wall and +0.02% CPU by mean.  Mean normalization is therefore
0.9811x wall and 0.9916x CPU; the ratio of paired medians is approximately
0.9915x and 0.9924x.

Requested O3 required twelve self pairs after the first six had favorable CPU
but an ambiguous wall median.  The full window has eleven of twelve CPU pairs
favor the candidate.  Mean wall falls from 30.316 to 30.155 seconds (-0.53%)
and aggregate CPU from 854.383 to 848.730 seconds (-0.66%); paired medians
improve 0.35% and 0.61%.  Six same-source GCC pairs move -0.12% wall and
+0.08% CPU by mean, giving mean-normalized ratios of 0.9959x wall and 0.9925x
CPU.  Paired-median normalization is approximately 0.9984x and 0.9934x.  The
candidate's same-window O3 self/GCC wall ratio is still about 1.648x, so this
is a retained incremental improvement rather than completion of the 1.5x
goal.

PA38 documents the O2+ selection and owns a student-buildable property with a
weakly aligned medium positive case plus small and oversized controls.  It
checks O0/O1 isolation, O2/O3 serialized structure, bounded statistics,
per-function native encoding through the compile-only driver, and behavior
without fixing scratch registers or complete output.  The earlier PA29 O0
weak-medium-copy property remains clean.  PA38 passes 45/45 and the through-
PA38 report passes 5,471/5,471.  PA37/PA38 debug and object-round-trip lanes
pass; the PA39 file audit has zero fatal findings and the unchanged 32
warnings.  Explicit all-32 inception comparisons match every object and final
compiler: O2 is exact at
`e33c5bdfcd895b1cf94c6a8035d335771f51c69bfef5f9ee5514e8a625cbf2af`,
and O3 is exact at
`bd2fa645d3ca089f7cab634d37c5287cd34af5d8173e25d918717ffb74db3e95`.

### Rejected O3 trivial Boolean-diamond dose

The residual profile motivated a narrower recheck of the earlier acyclic
Boolean-phi rejection.  The dose was limited to O3 and to an exact two-arm
diamond: both arms contain only a jump to the merge, have the same sole
predecessor, contribute opposite constant `u8` Boolean values to a one-use
phi, and the phi immediately controls a branch.  O1 retained its original
LowIR and byte-exact output; O3 bypassed the arms and phi and branched on the
original condition.

The dose removed the hot token move constructor's spilled Boolean temporary,
reduced that function from 258 to 230 bytes, and reduced the complete O3
producer by 11,956 text bytes.  On the fixed O1 hot TU, output remained exact
and Callgrind fell from 4,447,750,256 to 4,410,495,809 instructions (-0.84%).
This local evidence did not survive the representative builds.  The complete
O1 workload was CPU-flat (candidate/baseline 1.00065x by mean and 1.00001x by
paired median) while wall was 1.0104x by mean.  The complete requested-O3
workload regressed to 1.00149x mean CPU and 1.00195x paired-median CPU; wall
was 1.00373x by mean.  Every O1 lane produced the same 219-object manifest and
final compiler, and each O3 side was internally deterministic.  The
prototype was restored without student-contract or test movement.  This is
another case where a real hot-path instruction reduction is insufficient in
the presence of whole-program layout and build-wide optimizer costs.

### Rejected multi-return stable-query dose

The remaining generic lookahead body was excluded from repeat-stable query
reuse solely because it has both its ordinary indexed return and a pure early
EOF return.  A source-independent prototype accepted multiple pure return
paths while still rejecting any path that could return after a memory effect
without first revisiting the query guard.  This recognized four stable
functions instead of one and removed six repeated generic lookahead calls.
`Lexer::Run` lost 42 bytes, the tokenizer object lost 32 text bytes, and the
complete producer grew only 4,204 text bytes.

Those six calls are cold.  The O1 hot object remained exact, native timing was
flat, and Callgrind moved only from 4,447,750,256 to 4,447,598,892
instructions: 151,364 instructions or 0.0034%.  The proof extension was
restored before a full-build gate or student-contract movement.  Multiple
pure returns are valid but not a material continuation of the retained query
reuse on this workload.

### Rejected innermost-loop helper-inline dose

Instruction-position profiling attributed the largest remaining grouped-query
site precisely: one call in `Lexer::Run` executes 3,623,141 times and is the
only call to its helper in a seven-block innermost natural loop.  A bounded
O3-only prototype selected one 32--64-instruction internal scalar helper with
at least 16 direct uses, at least 16 uses in one caller, and exactly one call
inside an innermost loop of at most eight blocks.  It then allowed that one
structurally selected call to override the ordinary shared-loop-body inline
guard.  On the tokenizer this selected the 42-instruction grouped query, which
has 38 static uses and 28 in `Lexer::Run`, and removed exactly the profiled hot
call.  Subsequent cleanup also eliminated several redundant cursor-index
reloads.  O1 requested output remained byte-identical.

The expansion was nevertheless harmful.  The tokenizer object grew 96 text
bytes and the complete O3 producer grew 5,876 text bytes.  Three sub-second
hot-TU ABBA blocks were output-exact but averaged about +2.7% wall and +2.9%
user time for the candidate.  More decisively, an output-exact Callgrind run
increased from 4,447,750,256 to 4,450,070,595 instructions, or 2,320,339
instructions (+0.0522%).  Inlining duplicates the helper's cursor-fill loop
and exception path inside an already large caller; the removed call overhead
does not repay the resulting control-flow, allocation, and layout effects.
The prototype was restored without student-contract or test movement.  Do not
retry shared loop-shaped helper inlining merely from call frequency; require
a callee whose body becomes materially simpler under the call-site facts.

### Rejected acyclic selection/compare cascade threading

The accepted `AppendUTF8` profile exposed ten repeated `std::string::push_back`
capacity selections.  Each tests whether the string uses local storage,
materializes that Boolean through a two-arm phi, selects either constant
capacity 15 or a loaded heap capacity through a second phi, and only then
compares the new size.  A bounded O3 prototype first threaded the scalar
capacity phi into two incoming compares while preserving the intervening local
stores.  That form reduced `AppendUTF8` from 495 to 485 MIR instructions and
from 1,965 to 1,789 bytes, but Callgrind improved only 0.0890% because the
preceding Boolean frame shuttle remained.

The complete dose therefore required the scalar selection to be immediately
fed by an exact constant-Boolean diamond and collapsed both selections as one
structural operation.  It rejected loop-carried, exceptional, phi-target,
volatile, nonlocal-store, multi-use, and non-two-input shapes.  On the tokenizer
it removed ten `u8` and ten `i64` phis, reduced optimized LowIR by 70
instructions, reduced `AppendUTF8` to 405 MIR instructions and 1,509 bytes,
and reduced the tokenizer object's text by 448 bytes.  The complete O3 producer
nevertheless grew 7,108 text bytes.  O1 and O2 requested LowIR remained
byte-exact.  The output-exact hot Callgrind workload fell from 4,447,750,256 to
4,422,404,305 instructions, a real 25,345,951-instruction or 0.5699% saving.

That local result did not become a material representative O3 win.  Across six
32-way O1 pairs, every raw CPU pair favored the candidate: mean CPU improved
0.80% and mean wall was flat.  The same-source GCC control improved 0.34% CPU
and 1.38% wall, leaving mean normalization at 0.99533x CPU but 1.01380x wall;
paired-median normalization was 0.99632x CPU and 1.02422x wall.  Requested O3
was smaller still: mean CPU improved only 0.17%, paired-median CPU improved
0.10%, mean wall regressed 0.73%, and three of six CPU pairs regressed.  Both
sides were deterministic and the candidate reproduced its own O3 compiler,
so this is a performance rejection rather than a correctness failure.  The
prototype was fully removed without README or property movement.  Future work
should not retry this cascade alone; it needs a broader string-append
simplification capable of approaching GCC's much smaller complete body.

### Rejected remainder-by-one/address-boundary dose

The accepted O1/O2/O3 tokenizer LowIR retained nine signed or unsigned
remainders by one, including six dynamically executed divides in
`TranslationCursor::Next`.  A scalar-only fold removed all nine and reduced
that function from 947 to 886 MIR instructions and from 4,297 to 4,156 native
bytes.  It nevertheless changed the zero-index address use shape, gained two
callee-saved registers plus two common-path address calculations, and added
about 47.6 million instructions to `Next`.  Hot Callgrind consequently
regressed 1.071%.

A broader attempt to rematerialize the affected addresses exposed a contract
problem rather than a safe optimization: `ValueFact::deferred_address`
currently represents both a semantic address and a postponed memory load, and
the ownership of counted base/index dependencies is tied to that distinction.
Propagating it through arbitrary aliases can therefore load where an address
is required or consume a shared dependency too early.  That prototype and all
of its diagnostic code were fully removed.  A narrower form preserved the
zero array projection as an address-class boundary and borrowed only a stable
zero-displacement deferred representation for its identity copy.  This
removed the common-path address calculation while avoiding the broader
contract change.

The final narrow candidate still removed all nine remainders and all six
native divides.  `TranslationCursor::Next` fell to 881 MIR instructions and
4,132 bytes, the tokenizer lost 208 text bytes, and the complete producer grew
1,832 text bytes.  Its exact-output hot Callgrind result was effectively flat:
4,448,033,704 versus 4,447,750,256 instructions (+0.0064%), with `Next`
itself improving by 60,718 instructions.  Across the complete 32-way O1
workload self CPU improved 0.18% while same-source GCC improved 0.03%, for an
approximately 0.15% normalized CPU gain.  Requested O3 self CPU was flat
(-0.002%) while same-source GCC improved 0.25%, so the normalized O3 ratio
regressed approximately 0.25%; native hot timing also regressed about 0.9%.
The candidate was restored without README or property movement.  The cold
divide removal does not justify the new native representation contract or its
unfavorable O3 result.

### Residual tokenizer attribution and rejected availability/inline probes

A fresh same-source GCC-O3 producer was rebuilt from the accepted revision so
that the residual comparison did not rely on an older source or layout.  On
the exact hot tokenizer workload it executes 2,423,258,043 Callgrind
instructions, versus 4,447,750,256 for the accepted self-O3 producer.  The
new GCC count differs from the prior control by only about 125,000
instructions, so the residual ranking is stable.  The largest self/GCC
exclusive-instruction gaps are the tokenizer `Run` body (+172.55 million),
the translation cursor `Next` body (+120.19 million), the grouped query versus
GCC's constant-propagated `Peek` (+272.91 million), UTF-8 append (+110.66
million), and the token move constructor (+127.78 million).  Together these
five regions explain about 804 million instructions, or roughly 40% of the
remaining 2.024-billion-instruction gap.  The next doses therefore remain
focused on general CFG/value-placement improvements in these regions rather
than a new broad whole-program pass.

Two repeat-stable-query extensions failed at the static proof/census stage.
First, preserving an available query across stores to proven nonescaping
caller-local slots changed neither optimized tokenizer LowIR nor the final
object: the remaining useful barriers are predominantly calls, not local
stores.  Second, permitting a second same-parameter constant group, including
a reduced four-call secondary floor, produced no additional clone that passed
the existing cleaned-body payoff test.  Both probes were fully removed; they
do not justify contract or test movement.

A third source-independent O3 probe targeted the hot identifier predicate
without duplicating its inner range-search loop directly.  It selected an
acyclic 16--64-instruction wrapper with exactly two direct uses and one nested
loop-shaped call, then inlined the wrapper at its one call inside an
innermost loop of the largest caller.  This removed the 3,623,141-execution
`IsIdentifierBody` call in `Lexer::Run`, but grew optimized LowIR from 8,403
to 8,461 lines, grew `Run` from 9,578 to 9,775 native bytes, grew the tokenizer
object by 208 text bytes, and grew the complete O3 producer by 6,704 text
bytes.  Sixteen output-exact hot lanes regressed by about 3% wall and user
time.  The prototype was removed before Callgrind, README, or test movement.
The out-of-line predicate itself remains a better target: its 185-byte self
body materializes Boolean joins through frame slots, whereas GCC threads the
choices into returns after inlining the predicate.

### D.terminal-phi-return O3 terminal return threading retained

The residual comparison above identified a narrower form of GCC's control-
flow advantage that does not require inlining the identifier predicate.  At
the end of O3, the retained dose moves a two-input scalar phi and up to three
one-use `convert`, `cmp`, or `unary` operations from a terminal return join
onto its two direct incoming edges.  It also moves a two-input `u8` or `i64`
phi used directly by a branch when at least one successor is a direct return.
The proof rejects shared values, longer or non-linear scalar chains, secondary
temporary operands, indirect incoming edges, loops and backedges, exceptional
regions, and branch successors needing phi repair.  Cleanup is bounded to
four successful rounds per function and is isolated from O1 and O2.

PA37 documents that source-independent contract and owns a property fixture
with both eligible forms, shared-value and long-chain guards, O1/O2 isolation,
bounded statistics, serialized-LowIR replay through the compiler driver, and
behavior.  The checker tests structural properties rather than complete
program text, exact generated names, or a fixed compiler output.

The fixed O1 hot object remains exact at
`d9dbe51e6b5848711ff72e7b27fef7f5bd638a352db0ea19c20efbc2ae74a41e`.
The retained O3 producer reduces its Callgrind instruction count from
4,447,750,256 to 4,388,177,694 (-1.3394%).  The tokenizer object loses 84 text
bytes, and the hot out-of-line identifier predicate falls from 185 to 109
bytes with its frame eliminated.  The clean fixed-point producer is
`5157235e785e72b59d6b1e5c77bfdb3bb7ddd6f735e0d8c36febe03e4941a826`
with 8,715,932 text bytes, 9,700 bytes above the accepted producer.

Three clean all-32 ABBA blocks over the complete requested-O1 workload are
exact at one 219-object manifest and final compiler.  Mean wall falls from
30.625 to 30.138 seconds (-1.59%) and aggregate CPU from 850.617 to 841.057
seconds (-1.12%); paired medians improve 2.03% and 1.11%, with all three CPU
blocks favorable.  The valid same-source GCC control moves +1.06% wall and
-0.14% CPU, so mean normalization is 0.9737x wall and 0.9902x CPU; paired-
median normalization is 0.9642x and 0.9898x.

At requested O3, each producer is internally deterministic: the accepted
producer emits final hash
`278bcf604f412b836c7e33174567da7f19ad0da5119c4c43068c2927cbf3a8d3`,
while the retained producer emits
`5157235e785e72b59d6b1e5c77bfdb3bb7ddd6f735e0d8c36febe03e4941a826`.
Mean wall falls from 30.228 to 30.030 seconds (-0.66%) and aggregate CPU from
849.245 to 843.133 seconds (-0.72%).  The paired medians improve 0.45% wall
and 0.75% CPU, and every CPU block is favorable.  The same-source GCC control
moves +1.04% wall and +0.04% CPU, giving mean-normalized ratios of 0.9832x
wall and 0.9924x CPU and paired-median ratios of 0.9907x and 0.9928x.

The final inception gate also exposed why fresh roots are mandatory.  An
incrementally reused producer tree predated the added `Stats` fields, while
the self-host compiler had accepted `-MMD` without creating the requested
dependency files.  Its stale entry object therefore differed from a clean
inception object.  All timings from that producer were discarded.  A clean
generation built by the accepted compiler produced the expected G1 hash;
that compiler produced the G2 hash above; and a clean G3 build matched every
G2 object and the final compiler.  This is one expected bootstrap generation
for an optimization that changes the compiler's own O3 output, followed by an
exact fixed point, not an ignored inception mismatch.

PA37 passes 188/188, PA38 passes 45/45, and the through-PA38 report passes
5,471/5,471.  PA37/PA38 debug and object-round-trip lanes pass, and the PA39
file audit has zero fatal findings.  The retained decision uses only the clean
fixed-point producer and the clean G2/G3 all-32 inception comparison.

### Rejected whole-color leaf recoloring and the remaining frame gap

The next native probe asked whether the hot token move constructor's five
callee-saved registers were merely an online-allocation accident.  An O3-only
prototype tried to recolor each complete callee-saved physical color to any
caller-saved register proven conflict-free by completed MIR liveness.  This is
the narrowest possible extension of the retained whole-function recoloring
proof and required no hidden LowIR or frontend facts.

The proof could eliminate only `r13` from the hot constructor.  The function
kept its frame pointer, four saves, and temporary frame slots, shrinking from
258 to 253 bytes.  Across the compiler the new choices instead grew the clean
O3 producer by 2,096 text bytes.  A four-block, 16-run output-exact hot-TU ABBA
screen measured candidate/baseline paired ratios of 1.00835x wall and 1.01171x
user time.  The prototype was therefore removed before README, property,
Callgrind, or full-workload movement.

This rejects whole-color recoloring as the explanation for the remaining
self/GCC gap.  GCC's hot constructor is frameless and save-free; reaching that
shape requires a general live-range split or earlier placement repair that can
eliminate the constructor's frame temporaries and redistribute multiple
cross-block ranges, not another whole-register substitution.  Any such dose
must first demonstrate that it improves the complete framed and frameless
populations rather than this one symbol.

An immediately following static probe tested constant-index rematerialization
from incoming pointer parameters.  It removed two pointer homes from the hot
constructor but did not reduce its 258 native bytes, five saved registers, or
frame, while growing the hot source object by about 1.9 KiB.  The prototype was
removed without timing or contract movement: eliminating address homes alone
does not repair the live ranges that force the frame.

### D.composite-copy-preserve O3 parameter-carrier preservation retained

The useful placement boundary was the composite move itself.  At O3, a fixed
copy of 1--64 bytes directly between two incoming parameter addresses may use
the reserved encoder scratch registers instead of destroying the incoming
address carriers.  If that same function later discards the result of the
recognized memory-copy builtin, the dynamic copy receives explicit MIR
operands and preserves the incoming carriers around `rep movsb`.  Fixed copies
through derived addresses, functions having only a dynamic copy, used builtin
results, and O0/O1/O2 retain their previous conservative forms.  The dynamic
form records which operands are direct frame/global addresses so object
encoding does not reconstruct a call or lose address semantics.

The first broad prototype was intentionally rejected as a correctness failure,
not hidden by a source change.  It marked every small fixed copy as preserving
the string-operation pointers even when address setup had already destroyed
them; its G2 compiler crashed in `TypeTable::Intern`.  A corrected but still
broad probe also changed unrelated copy bodies: the hot `std::string` copy
constructor alone added 2.765 million Callgrind instructions.  Restricting the
proof to direct parameter copies and enabling explicit dynamic preservation
only in the same composite-move function removes that offset.  The unrelated
string constructor is within 30 instructions of the accepted profile.

The retained token move constructor falls from 258 to 234 native bytes, from
five to four saved registers, and from a 64-byte to a 48-byte frame.  Its two
parameter evacuations and pointer frame homes disappear.  The final fixed copy
through derived addresses remains independent: it keeps its existing
`encoding=direct_chunks` fact without receiving the preservation fact.  The
complete O3 producer grows 5,904 ELF `.text` bytes, from 7,975,634 to
7,981,538.

On the fixed hot compile, output remains exact at
`d9dbe51e6b5848711ff72e7b27fef7f5bd638a352db0ea19c20efbc2ae74a41e`.
Callgrind falls from 4,388,177,694 to 4,377,937,136 instructions (-0.23337%);
the token move itself falls from 181,339,590 to 171,181,377.  The same-source
GCC control moves from 2,423,258,043 to 2,423,432,711 (+0.00721%), giving a
normalized instruction ratio of 0.997594x (-0.24056%).  Four hot ABBA blocks
improve paired-median wall/user time by 1.66%/0.88%.

Three rotated, clean 32-way blocks over the complete requested-O3 workload
produce deterministic per-side compilers.  Mean wall falls from 29.563 to
29.358 seconds (-0.69%) and aggregate CPU from 841.720 to 836.298 seconds
(-0.64%); paired-median wall/CPU ratios are 0.99372x and 0.99348x.  A final
balanced requested-O1 block is byte-exact at one 219-object manifest and
compiler; wall falls from 29.770 to 28.755 seconds (-3.41%) and aggregate CPU
from 839.715 to 830.825 seconds (-1.06%).

PA38 documents the source-independent rule and owns a LowIR property fixture.
The checker validates O3-only fixed and dynamic preservation, reduced
preserved-register pressure, a direct frame-address operand, dynamic-only and
used-result negative controls, driver replay, and behavior.  It names fixture
roles but does not compare complete MIR, exact physical registers, executable
bytes, or program-specific compiler output.

PA29 passes 291/291, PA38 passes 45/45, and the report through PA38 passes
5,471/5,471.  PA37/PA38 debug and object-round-trip lanes pass, and the PA39
file audit has zero fatal findings.  Fresh all-32 O3 G1/G2/G3 object trees and
final compilers are exact at
`659de0c5ba8799ccf96c273fb3aeb35b3c85e64c67f7641fa1425baa9ce87ace`;
fresh O2 G1/G2 is exact at
`58424d56c9c3a64101514eac5f68265f4ac3d11814fdfa1b85d87d7379d25ca9`.

### D.adjacent-integer-normalizations O2+ MIR cleanup retained

The next retained native dose removes integer representation work whose proof
is already local in final MIR.  At O2 and O3, a narrow integer load followed
immediately on the same virtual register by a same-width sign or zero
normalization selects the corresponding signed or unsigned load.  Two
adjacent normalizations on the same register are also combined when the later
width subsumes the earlier one, when their signedness agrees, or when the
first operation zero-extends and therefore proves the value nonnegative.
The pass is deliberately block-local and adjacent.  It does not cross an
instruction or remove a narrow sign extension followed by a wider zero
extension, since that chain can change the represented value.  Removed source
locations are retained on the surviving instruction.  O0 and O1 do not run
the pass.

This is a PA38 native-MIR contract rather than a PA37 LowIR addition.  The
PA38 README describes the source-independent proof, and its new course
control checks signedness selection, both safe chain forms, the value-changing
and intervening-instruction guards, O0/O1 isolation, debug-location survival,
driver replay, native encoding, and generated behavior.  The checker matches
roles and structural relationships with register-agnostic expressions; it
does not compare a complete MIR dump, compiler output, exact register names,
or object bytes.  PA37 remains unchanged because the serialized LowIR surface
is unchanged.

On the fixed hot preprocessing TU, final MIR falls from 6,480 to 6,419
instructions and object `.text` falls from 29,749 to 29,605 bytes.  The O3
producer falls from 7,981,538 to 7,972,210 `.text` bytes (-9,328), while its
fixed requested-O1 output remains exact at
`d9dbe51e6b5848711ff72e7b27fef7f5bd638a352db0ea19c20efbc2ae74a41e`.
Callgrind falls from 4,377,937,136 to 4,341,389,715 instructions (-0.8348%).
The common-path same-source GCC control changes only +0.0051%, so the
instruction result is also approximately -0.84% after normalization.

Six clean, balanced 32-way blocks over the complete requested-O1 workload
produce one exact 219-object manifest and final compiler per side.  Self mean
wall/CPU ratios are 0.98344x/0.99420x; paired-median ratios are
0.98213x/0.99346x.  The same-source GCC mean ratios are
0.99813x/0.99956x and paired medians are 0.99379x/0.99832x.  The resulting
normalized gain is 1.17% wall and 0.49% CPU by paired median, or 1.47% wall
and 0.54% CPU by means.  Thus the improvement is not merely work added to the
GCC-built control, and both raw self metrics remain favorable.

A dedicated reporting counter was tested and rejected separately.  Although
it did not affect the generated hot object, it added 160 producer text bytes
and introduced consecutive slow tails in the representative 32-way workload,
including a block with about 1.17% more aggregate CPU.  Removing that counter
restored the exact pre-counter GCC seed and the measured retained fixed point.
The general rewrite count plus structural and behavioral property checks are
sufficient observability without paying that compiler-layout cost.

The final counter-free gates pass PA29 291/291, PA38 45/45, and the report
through PA38 5,471/5,471.  PA37/PA38 debug and object-round-trip lanes pass,
and the PA39 file audit remains at zero fatal findings and the established 32
warnings.
Two fresh frozen compiles reproduce the exact hot-object hash above.  Fresh
all-32 O2 and O3 inception trees each match all 219 objects and their final
compiler.  Under the fixed measurement roots, O2 is exact at
`cb26b76270761c53eca9cfcf3e5ecd851b43ea17a8c23701c850a40bc27b50fa`
and O3 is exact at
`0ec21c5fb19205f3726953dae144256cc33e36de926cee09435ce17566de12e2`;
their complete self/inception gates take 49.37 and 48.46 seconds respectively
with 32 build and object workers.

### Rejected late transient-swap staging elimination

A late O2+ LowIR prototype recognized three adjacent equal-size object copies
in a complete-object swap.  It removed the first copy only when its source was
a distinct, complete, write-only local slot and removed that slot's address
plumbing and initialization at the same time.  The general proof handled the
same-address destination/incoming case, rejected partial, escaped, observed,
volatile, nonlocal, size-mismatched, alignment-mismatched, and incomplete
three-copy forms, and did not depend on compiler symbols or source text.

The first in-place implementation exposed a prototype correctness bug before
any source fixture was changed.  Its compactor moved a retained instruction
onto itself before the first deletion; self-move cleared vector-valued call
arguments.  In the generated ELF writer this changed
`_M_initialize_map(this, 0)` into a zero-argument call and made the candidate
compiler consume excessive memory.  Guarding the move with
`kept != other_index` restored the exact 219-object workload and final
compiler.  A retained version would have required a PA37 property control
proving that an unrelated preceding call keeps all of its arguments, in
addition to the positive and negative swap proofs.  Because the corrected
optimization was rejected, neither a new contract nor that implementation-
specific regression surface was added to the student assignment.

The corrected lean form removed a transient 80-byte slot, its zero stores,
and one full copy from both the macro swap and ELF-writer move-construction
population.  The macro body fell 115 native bytes.  The self O3 producer grew
9,072 `.text` bytes, however.  Its fixed O1 hot output remained exact at
`d9dbe51e6b5848711ff72e7b27fef7f5bd638a352db0ea19c20efbc2ae74a41e`,
and Callgrind fell from 4,341,389,715 to 4,312,247,681 instructions
(-0.6710%).

Three lean self ABBA blocks over the complete 32-way requested-O1 workload
were exact.  Mean wall/aggregate-CPU ratios were 0.99800x/0.99947x; paired
medians were about 0.99777x/0.99824x.  Three same-source GCC `-O3` producer
blocks were also exact at manifest
`dfd53e92d5e503a61b2a5c5a6ab7767d5a947c871fb97a3c4555d6ded7fd1a85`
and final hash
`3c9e3f6bc8ee5b17bd3308bdc46aba3d88e6f6dfce0885adce68851e58e4f476`.
Their mean wall/CPU ratios were 1.00357x/0.99740x, with paired medians
1.00165x/0.99816x.  Normalized wall therefore improved about 0.56% by means
and 0.39% by paired median, but normalized CPU regressed 0.21% by means and
was effectively flat at 1.00008x by paired median.  The initial corrected
form with redundant cleanup was separately flat in six self samples; the
leaner pipeline call did not turn the deterministic instruction saving into
a corroborated CPU improvement.  The complete prototype was removed under
the close-result rule before README, property-test, or compatibility-fixture
movement.

Fill one row for every retained or rejected dose.

| Phase/dose | Hypothesis | README/test movement | LowIR/MIR/object delta | Raw and normalized timing | Report/audit/inception | Decision/commit |
| --- | --- | --- | --- | --- | --- | --- |
| A0 | fill O2 row and refresh controls | none | O2 final hashes exact per fixed workload | O2 regression begins before O3; normalized loss larger | process/storage checks clean | diagnostic complete |
| A1 | verify every retained O2/O3 contract has property coverage | backfill only where absent | pending | output-neutral | pending | pending |
| A2 | split LowIR/native and named pass doses | experiment-only harness removed | O1/O2 endpoints byte-exact | native O2 is first harmful layer | focused builds exact | complete |
| B1 | locate first harmful LowIR increment | existing contracts plus needed negative guard | pending | pending | pending | pending |
| B2.1 | move O2/O3 memory GVN after all inline/prune waves | PA37 README plus positive ordering and store-barrier property control | O2 producer -33,646 text bytes; emitted O3 compiler -33,186 text bytes | O1-workload producer quality -3.61% wall/-0.43% CPU; O3 producer quality -2.31%/-0.46%; O3 requested cost +1.54%/+0.64% | 5,471/5,471; PA37/38 debug clean; zero-fatal audit; O2/O3 inception exact | retained in this checkpoint |
| B1.1 | prevent weak specialization clones from defeating link coalescing | PA37 README plus positive control-flow and negative data-only property control | O2 -46,397 text bytes and 162/82,891 surviving clone symbols/bytes removed; O3 -46,365 text bytes | O1-workload -2.59% wall/-0.49% CPU; normalized direction about -0.56%/-0.24%; invalid O3 window discarded | 5,471/5,471; PA37 debug/round-trip clean; zero-fatal audit; O2/O3 inception exact | retained in this checkpoint |
| B2.graph | rebuild specialization graph after ordinary inline | none | no workload object changes; `pipeline.o` +56 text bytes | static rejection | experiment removed | rejected |
| B3.1 | prevent trace layout from displacing conditional fallthrough | PA38 README plus positive/negative structural control | O2 text -73,012 bytes; O3-workload result -73,316 text bytes | O1-workload CPU -0.92%; O3-workload CPU -1.59%; normalized floor still open | 5,471/5,471; debug 11/11; zero-fatal audit | retained in this checkpoint |
| B3.2 | prevent a completed deferred-address carrier from pinning a later register lifetime | PA38 README plus register-agnostic structural/behavioral control | hot MIR -67 instructions and -45/-45 scalar loads/stores; O3 producer -34,640 text bytes | O1 workload -0.68% wall/-0.41% CPU; normalized CPU direction about -0.53% | 5,471/5,471; debug 11/11; zero-fatal audit; O2/O3 inception exact | retained in this checkpoint |
| C.peek0 | specialize the dominant repeated `Peek(0)` call family | provisional property removed | 57 calls redirected; combined `Peek` profile 8.56% to 8.37% | one exact pair -0.28% wall/-0.08% CPU, below useful signal | candidate restored | rejected |
| C.copy61 | replace weak small-object `rep movsb` with direct chunks | provisional PA38 README/property removed | hot move share about 3.6% to 3.0% | six-pair median +3.05% wall/+0.38% CPU | 5,471/5,471; debug clean; O2/O3 inception exact | rejected and restored |
| C.boolphi | thread O2/O3 narrow and acyclic constant Boolean phis | none; rejected at screen | O3 producer -5,036 text bytes; O1 workload byte-exact | hot TU about -0.3%; full CPU +0.72% | focused shape and exact-output checks | rejected and restored |
| D.hint96 | recheck broad hinted late inlining under O3 native optimization | none; rejected at screen | `Peek` calls 100 to 44; O3 producer +827,994 text bytes | hot TU +5.35% wall/+5.53% user | exact-output ABBA screen | rejected; no source change |
| D.const72 | admit only hinted 41--72 instruction calls with all-direct actuals and an integer literal | none; rejected at screen | `Peek` calls 96 to 50; hot dynamic instructions +3.19%; `Run` loads/stores and frame bindings increased | six-pair hot TU +4.87% wall/+4.84% user | exact output; candidate removed | rejected; displaced smaller profitable inlines |
| D.literal-supp | put literal-call inlining in a separate post-prune budget | none; rejected at screen | dose 288: hot Ir -0.50%; dose 768: Ir -1.26%, tokenizer text +39%, `Run` MIR 2,686 to 3,890 | dose 288 +1.54%/+1.62% wall/user; dose 768 +1.79%/+1.34% | exact output; both candidates removed | rejected; merged-region pressure despite fewer instructions |
| D.group-census | find source-diverse constant groups that can delete control flow without broad inlining | none; diagnostic only | 217 TUs; populations in parser, semantic, lowering, serialization, and native paths | no timing claim | immutable O3 LowIR census | supports one bounded O3 prototype |
| D.group-late | clone one profitable repeated integer-constant group after all inline waves and consume its edge equality | PA37 README plus level-isolation, structural, replay, and behavior property | hot Ir -2.019%; 69 calls redirected; clone 43 vs generic 57 LowIR instructions; O3 producer +28,416 text bytes | self -0.44% wall/-0.58% CPU; same-source GCC paired CPU +0.19%; normalized CPU -0.79%; wall normalization inconclusive | PA37 188/188; PA38 45/45; 5,471/5,471; debug/round-trip clean; zero-fatal audit; O3 inception exact | retained in this checkpoint |
| D.loop-shared | revisit qualified shared loop bodies in a separate final O3 inline wave | none; rejected before contract movement | range helper and five calls removed; tokenizer +694 text bytes; three hot callers enlarged | clean repeat +2.83% wall/+1.07% CPU | exact six-lane output; candidate removed | rejected; call removal did not pay for duplicated loop state |
| D.address-group | specialize repeated direct readonly byte-string addresses and fold clone-local byte control | none; rejected before contract movement | 12 calls redirected; clone 73/393 vs generic 150/893 LowIR instructions/native bytes; O3 producer +18,544 text bytes; hot Ir -0.143% | hot CPU +0.15%; full 32-way CPU +0.033%, wall +1.56% | exact hot and six-lane full output; candidate removed | rejected; local saving did not pay for machinery/footprint |
| D.call-plan | keep reactive call results out of future cyclic spans and grant a cyclic call result after its completed arguments retire | PA38 README plus O2/O1 register-agnostic structural/behavioral control | `Run` -181 MIR, scalar loads/stores -89/-77, frame homes -3; tokenizer -234 `.text`; O3 producer +292 text bytes; O1 object exact; unrelated EH/branch fixtures exact | O1 workload +0.11% wall/-0.40% CPU; GCC CPU -0.21%; normalized CPU -0.18%; O3 workload -1.42% wall/-0.30% CPU, five of six pairs favorable | PA37 188/188; PA38 45/45; 5,471/5,471; debug/round-trip clean; zero-fatal audit; frozen O2/O3 exact; all-32 O2/O3 inception exact | retained in this checkpoint |
| D.epilogue | share repeated optimized return sequences, with broad and bounded variants | none; rejected before contract movement | broad: `Run` -161 native instructions and -23 physical epilogues; O3 producer -94,192 text bytes; bounded variants -17,928/-16,120 bytes | broad O1 user +1.5%; return-bounded +0.26%; bounded/excluded O3 CPU +0.32% and wall +1.5% | exact-output all-32 screens; all variants removed | rejected; taken return branches cost more than duplicate bytes |
| D.copy-pair | fuse staged adjacent 64-bit predecessor loads/successor stores into one vector copy | none; rejected before contract movement | clone -3 MIR/-8 bytes; generic -8 bytes; tokenizer -22 text bytes; producer +8,744 text bytes | twelve-lane hot TU +4.1% wall/user, every candidate lane slower | exact hot outputs; candidate removed | rejected; exposed entry-address sensitivity |
| D.align64 | align large final O3 MIR functions and preserve the request through object text partitioning | PA38 README plus dynamic MIR-count, level-isolation, behavior, driver-replay, and ELF-symbol property | 3,772/4,043 >=256-byte functions aligned; O3 producer +25,248 text bytes (+0.29%) | O1 workload -2.31% wall/-0.86% CPU; GCC CPU +0.09%; normalized CPU -0.95%; O3 workload -0.30% wall/-1.05% CPU; GCC CPU +0.06%; normalized CPU -1.11% | PA38 45/45; 5,471/5,471; PA37/38 debug/round-trip clean; zero-fatal audit; all-32 O2/O3 inception exact | retained in this checkpoint |
| D.copy-pair-align | re-evaluate staged scalar-copy fusion on the aligned baseline with a stronger alias guard | none; rejected at screen | generic and clone each -3 MIR; producer +8,940 text bytes; hot entries remain aligned | twelve-lane hot wall +1.1%, user +1.6% | deterministic output; candidate removed | rejected; 16-byte alignment reduces but does not eliminate layout sensitivity |
| D.hint-clone | apply existing loop-carried store/load forwarding to late O3 grouped clones | none; rejected at screen | specialized `Peek(0)` backedge reload removed; preserved-register count unchanged; producer +32 text bytes | twelve-lane hot wall +2.2%, user +2.3% | deterministic baseline/candidate objects; candidate removed | rejected; carried live range costs more than the reload |
| D.loop-thread | thread a private loop-carried compare onto incoming edges and reuse `add 1` flags across latch stores | none; rejected at screen | latch reload/jump/test removed; clone remains 219 bytes and preserves three registers; producer +10,416 text bytes | 24-lane hot wall -0.21%, user exactly flat | deterministic baseline/candidate objects; candidate removed | rejected; real dynamic saving is too small for the implementation |
| D.reuse-plan | keep destructive result reuse out of a future cyclic-result reservation, then recheck broad hinted inlining | none; rejected before contract movement | shipped tokenizer exact; guard-only O3 producer -3,872 text bytes; guarded hint96 `Run` 2,627 to 2,403 MIR and scalar loads/stores 616/516 to 513/383 | guarded hint96 hot +4.5--5%; guard-only three-pair mean CPU -0.13% but paired median 1.0001x, wall confounded by one baseline tail | exact 36-lane hot output and six full-workload outputs; candidate removed | rejected; restored grant does not overcome merged-body cost and guard alone is flat |
| D.align-sweep | test 32/64-byte entry alignment as a stronger layout oracle for local O3 rewrites | none; rejected before contract movement | 32-byte producer +28,320 text bytes; 64-byte producer +84,192; hot object exact | 32-byte CPU +1.1% then +7.3%; 64-byte CPU -0.6% then +2.7% | 36-lane and reversed 24-lane output-exact screens; shipped 16-byte policy restored | rejected; stronger padding is unstable and not worth its text cost |
| D.hint49 | inline the 48-instruction token move constructor whose flat profile appeared 256.9M instructions behind GCC | none; rejected before contract movement | direct calls 37 to 1; producer +11,600 text bytes; hot object exact | Cachegrind Ir -0.064%; native CPU -1.1% then +4.2% | exact static, Cachegrind, and 48 native outputs; override-only dose removed | rejected; flat constructor cost merely moved into callers |
| D.group4 | admit four-call integer-constant groups under the retained static-payoff rule | none; rejected before contract movement | one 11-byte semantic clone; target object -5 bytes; producer text exact | 24-lane hot CPU +6.9% | exact hot outputs; eight-call threshold restored | rejected; tiny early size change is layout-negative |
| D.final-slots | revisit zero-load scalar slots after final inline/load/edge cleanup | none; rejected before contract movement | tokenizer -34 LowIR instructions, but `AppendUTF8` remains 495 MIR/1,965 bytes and tokenizer remains 28,741 text bytes | static rejection; added scan has no generated-code benefit | focused LowIR/MIR/object census; prototype removed | rejected; native lowering already omits the residue |
| D.const-ref | recover exact loads through addresses of constant scalar locals before promotion | none; rejected before contract movement | `AppendUTF8` 495 to 465 MIR, 80 to 70 scalar loads, 1,965 to 1,846 bytes, no spills; tokenizer -128 text bytes; O3 producer -2,368 bytes | all-level form CPU +1.56%; O3-only form with exact O1 output CPU +1.73%, wall +1.80% | two deterministic 24-lane screens; both forms removed | rejected; real local win produces a slower compiler even without runtime scan cost |
| D.repeat-stable-query | reuse a structurally proven repeat-stable internal query result for the same SSA argument tuple | PA37 README plus positive/negative structural, level-isolation, stats, replay, and behavior property | 31 calls removed; `Run` 2,451 to 2,139 MIR; tokenizer -1,664 text bytes; hot Ir -4.51%; O1/O2 exact | hot wall/user -3.36%/-3.52%; O1 workload CPU -2.06%; O3 workload CPU -1.92%; GCC-normalized CPU -1.75% | PA37 188/188; PA38 45/45; 5,471/5,471; debug/round-trip clean; zero-fatal audit; frozen exact; all-32 O3 inception exact | retained in this checkpoint |
| D.loop-priority-inline | expand one bounded inline-preferred body at its unique loop call while retaining its paired non-loop call | PA37 README plus structural, level-isolation, bounded-stats, replay, and behavior property | one 396-instruction body cloned; hot cursor call 2 to 1; tokenizer +2,800 text bytes; producer +30,320; hot Ir -2.49%; O1/O2 isolated | O1 workload -2.40% wall/-1.88% CPU, normalized -2.08%/-1.65%; O3 workload +0.36% wall/-1.64% CPU within wall allowance, normalized -1.66%/-2.11% | PA37 188/188; PA38 45/45; 5,471/5,471; debug/round-trip clean; LowIR/file audits clean; frozen deterministic; all-32 O3 inception exact | retained in this checkpoint |
| D.group-header-inline | inline the one outer-loop-header call to a late grouped clone after repeat-stable-call reuse has consumed the original call equivalences | none; rejected before contract movement | moving the dose before repeat-stable reuse destroyed that reuse and did not reduce retained calls; the correctly ordered dose reduced `Lexer::Run` grouped-clone calls 28 to 27, grew the tokenizer object 48 text bytes, and grew the complete O3 producer 6,372 text bytes | three balanced hot-TU ABBA blocks were exact but candidate/baseline was +0.89% wall and +0.92% user | valid serializable LowIR and exact native output in the ordered screen; prototype and its transient invalid pre-reuse ordering removed | rejected; a real hot call removal lost to duplicated body/layout cost |
| D.group-fast-sibling | split one grouped clone into a bounded fast wrapper plus complete slow clone, then tail-transfer eligible scalar call/return pairs | provisional PA37/PA38 structural, level-isolation, replay, encoding, stats, and behavior properties removed | combined hot Ir -1.058%; split-only full O3 CPU +0.614%; final sibling-only producer +3,288 text bytes | combined O1 +1.035% wall/+0.169% CPU and normalized +0.621%/+0.292%; combined O3 normalized -1.093%/-0.300%; final sibling-only O3 screen +1.641%/+0.370% | provisional PA37 188/188, PA38 45/45, 5,471/5,471, and audits clean; all implementation and contract movement removed | rejected; hot-only synergy and final layout sensitivity fail representative gates |
| D.repeat-readonly-call | preserve repeat-stable query availability across direct internal call-free read-only helpers | none; rejected before contract movement | four more query reuses; tokenizer -48 text bytes; producer +1,348; hot Ir -0.273% | self O1 paired median +0.033% wall/+0.050% CPU; GCC +0.668%/+0.102%; normalized -0.631%/-0.052% | exact 219-object manifest and final output in all valid self/GCC lanes; two contaminated GCC blocks rejected whole and replaced | rejected; raw throughput is not in the intended direction and normalized CPU gain is negligible |
| D.block-local-save | eliminate a callee-saved color whose disjoint ranges are all confined to individual blocks | PA38 README plus O2/O3 level-isolation, structural, bounded-stats, and behavioral properties; PA37 unchanged | hot clone 219 to 203 bytes and three to two saves; producer -2,924 text bytes; hot Ir -1.7247%; frozen O2 exact and O3 -55 text bytes | O1 workload CPU -0.57%, normalized -0.69%, wall within +0.60%; O3 workload -1.44% wall/-0.74% CPU, normalized -1.65%/-0.93% | 45/45; 5,471/5,471 full report; debug/round-trip clean; zero-fatal audit; all-32 O2/O3 inception exact | retained in this checkpoint |
| D.medium-copy-chunks | select direct chunks for weakly aligned fixed 33--64-byte copies at O2+ | PA38 README plus O0/O1 isolation, O2/O3 structure/stats/encoding/replay/behavior property; PA29 O0 guard retained | hot constructor 215 to 258 bytes and loses fixed-tail `rep movsb`; producer +6,708 text; hot Ir -2.9409%; O1 requested output exact | O1 workload -1.52% wall/-0.82% CPU, normalized -1.89%/-0.84%; O3 workload -0.53%/-0.66%, normalized -0.41%/-0.75% | 45/45; 5,471/5,471 full report; debug/round-trip clean; zero-fatal audit; all-32 O2/O3 inception exact | retained in this checkpoint |
| D.trivial-bool-diamond | bypass an exact O3 two-arm constant-Boolean phi diamond | none; rejected before contract movement | token move constructor 258 to 230 bytes; producer -11,956 text bytes; hot Ir -0.84%; O1 output exact | O1 CPU 1.00065x mean/1.00001x paired median; O3 CPU 1.00149x/1.00195x and wall 1.00373x mean | deterministic 219-object O1 manifest/final output and deterministic O3 outputs; prototype removed | rejected; local instruction saving does not survive representative O3 cost/layout |
| D.multi-return-query | admit repeat-stable queries with additional pure return paths | none; rejected before contract movement | four stable functions vs one; six generic calls removed; `Lexer::Run` -42 bytes; producer +4,204 text bytes | output-exact hot Ir -0.0034%; native screen flat | structural/stats/object probes deterministic; prototype removed before full gate | rejected; removed calls are cold |
| D.innermost-loop-helper | inline one highly repeated small internal loop-shaped helper at its unique call in a tiny innermost caller loop | none; rejected before contract movement | one 42-instruction helper expanded; hottest 3,623,141-call site removed; tokenizer +96 text bytes; producer +5,876 | three hot ABBA blocks about +2.7% wall/+2.9% user; Callgrind +0.0522% Ir | exact O1 object and valid O3 LowIR; prototype removed | rejected; duplicated cursor loop and exceptional control cost more than the call |
| D.selection-compare-cascade | thread an acyclic two-stage Boolean/scalar selection directly into its sole compare branch | none; rejected before contract movement | ten `u8` plus ten `i64` phis removed; `AppendUTF8` 495 to 405 MIR and 1,965 to 1,509 bytes; tokenizer -448 text; producer +7,108 | hot Ir -0.5699%; O1 CPU -0.80%, normalized -0.47% CPU/+1.38% wall; O3 CPU -0.17% and wall +0.73% | O1/O2 LowIR exact; deterministic 219-object O1 outputs and per-side O3 outputs; candidate reproduced its O3 compiler; prototype removed | rejected; material local saving is representative-O3 flat and normalized wall-negative |
| D.remainder-one | fold signed/unsigned remainder by one while retaining zero array address boundaries and borrowing stable zero-displacement deferred addresses | none; rejected before contract movement | nine remainders and six native divides removed; `Next` 947 to 881 MIR and 4,297 to 4,132 bytes; tokenizer -208 text; producer +1,832 | hot Ir +0.0064%; O1 normalized CPU -0.15%; O3 self flat and GCC -0.25%, normalized O3 +0.25% | exact hot output and deterministic 219-object manifests/finals; prototype removed | rejected; cold divide removal cannot repay contract/layout cost |
| D.query-local-store | preserve a repeat-stable query result across writes to proven nonescaping caller-local slots | none; rejected at static census | optimized tokenizer LowIR and object unchanged | no timing warranted | accepted tokenizer rebuilt exact at `81aa9bd37bcb3b53cf72b77856d810cbb816e4858f2b7f1ae83923adcff4cc0b`; prototype removed | rejected; remaining availability barriers are calls rather than caller-local stores |
| D.secondary-group | admit one additional same-parameter integer-constant group under the retained cleaned-body payoff proof | none; rejected at static census | no second clone passed payoff, including at a four-call secondary floor; tokenizer output unchanged | no timing warranted | focused O3 LowIR/object checks exact; prototype removed | rejected; no profitable compiler population |
| D.loop-wrapper-inline | inline one small acyclic wrapper around a loop helper at its unique innermost-loop call | none; rejected before contract movement | one 3,623,141-execution call removed; optimized LowIR +58 lines; `Run` +197 native bytes; tokenizer +208 text; producer +6,704 text | sixteen exact hot lanes about +3% wall/user | deterministic hot output; candidate removed before Callgrind/full gate | rejected; wrapper removal still duplicates and enlarges the hot loop region |
| D.terminal-phi-return | move a bounded terminal scalar phi/chain or return-adjacent phi branch onto two direct incoming edges | PA37 README plus positive forms, guard, level-isolation, bounded-stats, replay, and behavior property | tokenizer -84 text bytes; identifier predicate 185 to 109 bytes and frameless; fixed-point producer +9,700 text; hot Ir -1.3394%; O1/O2 isolated | O1 workload -1.59% wall/-1.12% CPU, normalized -2.63%/-0.98%; O3 workload -0.66% wall/-0.72% CPU, normalized -1.68%/-0.76% | PA37 188/188; PA38 45/45; 5,471/5,471; debug/round-trip and zero-fatal audit clean; stale incremental producer discarded; clean G2/G3 all-32 inception exact | retained in this checkpoint |
| D.leaf-whole-color | recolor complete callee-saved colors in leaf functions into completed-liveness-proven caller-saved registers | none; rejected before contract movement | hot token move 258 to 253 bytes and five to four saves; frame retained; clean O3 producer +2,096 text bytes | 16-run hot paired wall 1.00835x and user 1.01171x | exact hot output; clean candidate root; prototype removed before full gate | rejected; one whole color is insufficient and broad layout/code-size effects are negative |
| D.parameter-index-remat | rematerialize constant indexes from incoming pointer parameters instead of keeping their address homes | none; rejected at static screen | two pointer homes removed, but hot token move remains 258 bytes/five saves/framed; hot source object about +1.9 KiB | no timing warranted | deterministic MIR/object screen; prototype removed | rejected; address homes are not the ranges forcing the frame |
| D.copy-preserve-broad | preserve pointer carriers for every bounded fixed and unused dynamic copy | none; rejected before contract movement | initial form mis-modeled destructive address setup and crashed its G2 compiler; corrected broad form added 2.765M Ir to an unrelated string-copy body | broad total Ir -0.184%, but unrelated offsets obscured the local saving | failing G2 and narrowed safety proof retained as diagnostic evidence; broad prototype removed | rejected; proof and population were too broad |
| D.composite-copy-preserve | preserve incoming address carriers across a bounded direct-parameter copy and a later unused builtin copy in the same O3 composite move | PA38 README plus O0--O2 isolation, structural pressure/frame-address, negative-call, driver-replay, and behavior property | hot token move 258 to 234 bytes, five to four saves, frame 64 to 48; producer +5,904 `.text`; hot Ir -0.23337%, normalized -0.24056% | O3 workload -0.69% wall/-0.64% CPU, paired CPU -0.65%; final O1 block -3.41% wall/-1.06% CPU and output-exact | PA29 291/291; PA38 45/45; 5,471/5,471; debug/round-trip and zero-fatal audit clean; all-32 O2 G1/G2 and O3 G1/G2/G3 exact | retained in this checkpoint |
| D.adjacent-integer-normalizations | combine adjacent O2+ integer normalizations and select signed/unsigned narrow loads in final MIR | PA38 README plus O0/O1 isolation, safe-chain and negative-guard structure, debug, replay, encoding, and behavior property; PA37 unchanged | hot MIR -61, object `.text` -144; O3 producer -9,328 `.text`; hot Ir -0.8348%; O1 output exact | self paired median -1.79% wall/-0.65% CPU; GCC -0.62%/-0.17%; normalized -1.17%/-0.49%; dedicated stats counter rejected after layout regression | PA29 291/291; PA38 45/45; 5,471/5,471; debug/round-trip and zero-fatal audit clean; frozen exact; all-32 O2/O3 inception exact | retained in this checkpoint |
| D.transient-swap-staging | remove a complete write-only transient local and its first overwritten copy from an adjacent three-copy object swap | none; rejected before contract movement; a retained form would require positive/negative PA37 properties plus unrelated-call-argument survival | two real 80-byte swap populations simplified; macro body -115 native bytes; self producer +9,072 text bytes; hot Ir -0.6710% | self mean wall/CPU -0.20%/-0.05%; GCC +0.36%/-0.26%; normalized wall -0.56% but CPU +0.21%; paired normalized CPU effectively flat | corrected candidate exact on hot output and all 219 full-workload objects/final; initial self-move bug diagnosed without fixture changes; prototype removed | rejected; dynamic instruction saving did not clear the normalized close-result CPU gate |
| C | make O2 at least 5% faster than O1 | selected measured feature | pending | target `<0.95x` | pending | pending |
| D | make O3 at least 20% faster than O1 | selected measured feature | pending | target `<=0.80x` raw/normalized | pending | pending |
| Final | complete matrix and closure | no uncovered retained behavior | exact and deterministic | all goals reported | all gates clean | pending |

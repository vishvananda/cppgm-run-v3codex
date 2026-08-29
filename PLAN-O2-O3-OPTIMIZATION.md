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

All compared objects and final compilers were exact at a fixed workload level.
The O1 and O3 final hashes were respectively
`a836d867d7adaaa7679ff8ad5e8fd0546a526dc5e7c62ed122310ac6cfb7fba4`
and `f9aedfb6c438a2252f474632fd4000de4f135494f8c0f4906860b9a4eb8e60f2`.
The matrix scratch was removed and no stale benchmark process remained.

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

Fill one row for every retained or rejected dose.

| Phase/dose | Hypothesis | README/test movement | LowIR/MIR/object delta | Raw and normalized timing | Report/audit/inception | Decision/commit |
| --- | --- | --- | --- | --- | --- | --- |
| A0 | fill O2 row and refresh controls | none | O2 final hashes exact per fixed workload | O2 regression begins before O3; normalized loss larger | process/storage checks clean | diagnostic complete |
| A1 | verify every retained O2/O3 contract has property coverage | backfill only where absent | pending | output-neutral | pending | pending |
| A2 | split LowIR/native and named pass doses | experiment-only harness removed | O1/O2 endpoints byte-exact | native O2 is first harmful layer | focused builds exact | complete |
| B1 | locate first harmful LowIR increment | existing contracts plus needed negative guard | pending | pending | pending | pending |
| B2 | repair pass-order interference | property tests, not full-text definitions | pending | pending | pending | pending |
| B3.1 | prevent trace layout from displacing conditional fallthrough | PA38 README plus positive/negative structural control | O2 text -73,012 bytes; O3-workload result -73,316 text bytes | O1-workload CPU -0.92%; O3-workload CPU -1.59%; normalized floor still open | 5,471/5,471; debug 11/11; zero-fatal audit | retained in this checkpoint |
| C | make O2 at least 5% faster than O1 | selected measured feature | pending | target `<0.95x` | pending | pending |
| D | make O3 at least 20% faster than O1 | selected measured feature | pending | target `<=0.80x` raw/normalized | pending | pending |
| Final | complete matrix and closure | no uncovered retained behavior | exact and deterministic | all goals reported | all gates clean | pending |

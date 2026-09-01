# Plan: O2/O3 Generated-Compiler Optimization

Status: complete for O3; residual O2 normalized gap and 20% stretch recorded

Date: 2026-08-30

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

At the earlier post-D.loop-priority-inline checkpoint, the inversion remained
removed and O3 had a meaningful raw lead.  Three order-rotated all-32
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

### Pre-D1 retained checkpoint

The current checkpoint is commit `5e644a0e`, after the retained addressed-
scalar-slot promotion.  A fresh direct O1-producer versus O3-producer
comparison used the same requested-O3 219-object workload, three balanced
32-way blocks, and six samples per producer.  Fresh same-source GCC O1 and O3
controls used the same workload and ordering discipline:

| Producer | Mean wall | Mean aggregate CPU | O3/O1 |
| --- | ---: | ---: | ---: |
| `S_13` | 31.588 s | 906.662 s | control |
| `S_33` | 29.027 s | 819.812 s | 0.918905x / 0.904209x |
| `G_13` | 21.083 s | 591.527 s | control |
| `G_33` | 17.927 s | 500.553 s | 0.850277x / 0.846206x |

O3 is therefore about 8.1% faster by wall time and 9.6% faster by aggregate
CPU than the current O1-produced compiler.  The raw floor is decisively met,
but GCC gains about 15.0--15.4% over the same producer-level transition.  The
normalized O3/O1 ratios are consequently 1.080713x wall and 1.068545x CPU:
our O3 improvement still trails GCC's by about 6.9--8.1%.  Reaching the raw
20% target requires another 11.9 percentage points of wall improvement from
the present O3 result; the normalized target is stricter and must continue to
use fresh same-revision controls.

Both self producers emitted identical requested-O3 results: manifest
`4e1206ae289fd6cb76cb84fc159edbb7a1d1c4823297c6d50e504b9d57eea3ec`
and final compiler
`3d72f8607d98a33a3eba55adde272138ea983ba870a6fd1b29978a24a78bdefa`.
The exact current O1 fixed-point producer hashes
`5170138047eab3cb6f016cbef2297fb95f1e83f2e4544bf36bff861219cf7920`;
all 219 G1/G2 objects and the final compiler matched.  Benchmark reports are
`/tmp/v3-o23-current-s1-s3-o3-full-abba.txt` and
`/tmp/v3-o23-current-g1-g3-o3-full-abba.txt`.  These paths are evidence roots,
not repository dependencies.

The current requested-O3 hot compile retires 4,225,444,281 Callgrind
instructions in the self compiler versus 2,423,904,398 in the same-source
GCC-O3 compiler.  Inclusive caller counts and linked-symbol sizes identify
the following residuals:

| Region | Self evidence | GCC evidence | Interpretation |
| --- | ---: | ---: | --- |
| `Lexer::Peek(0)` specialized query | 383.23M flat instructions; 18.09M calls; 203-byte body | 110.32M; 7.31M surviving calls; 145-byte hot body plus 76-byte cold body | largest exposed call/fast-path gap |
| `Lexer::Run` | 573.91M; 9,491-byte body | 403.42M; 5,497-byte hot body plus 1,520-byte cold body | very large merged hot region |
| `TranslationCursor::Next` | 549.49M; 4,264-byte body | 445.21M; 2,145-byte hot body plus 3,832-byte cold body | GCC's total body is larger, but its hot part is half the size |
| `AppendUTF8` | 174.50M; 1,814-byte body | 123.44M; 1,242-byte hot body plus 76-byte cold body | residual scalar/control-flow work |
| `MacroProcessor::AddSourceToken` | 109.46M; 1,245-byte body | 62.71M; 645-byte hot body plus 262-byte cold body | another hot/cold and movement candidate |

Flat costs are not summed as independent opportunities because GCC has
inlined and partitioned different portions of the same call paths.  The key
new evidence is structural: GCC often has *more total code* for a function
while keeping far less of it in the hot fragment.  The next plan increment
therefore prioritizes hot/cold partitioning and fast-arm exposure over another
generic code-size pass or another whole-body inlining threshold sweep.

### Current D1 O2-promotion checkpoint

The low-growth O3 promotion screen is complete. Repeat-stable query reuse does
no useful O2 work on the current compiler: the macro translation unit visits
609 functions but discovers no eligible query or reuse, so that scan remains
O3-only. The zero-bounded signed-range fold also remains O3-only. At O2 it
changed only the tokenizer's local object by eight text bytes, grew the fixed-
point compiler by 32 text bytes, and measured 1.01304x wall / 1.01235x
aggregate CPU against the addressed-slot baseline.

Three existing bounded transformations do pay at O2 and are now retained
there: complete-use addressed-scalar-slot recovery, one acyclic constant-
Boolean diamond fold per function, and bounded terminal scalar-phi movement.
The addressed-slot promotion reduces the fixed-point O2 compiler from
7,961,170 to 7,903,218 text bytes. The Boolean fold reduces it to 7,893,122
bytes, and terminal-phi movement reduces the final compiler to 7,885,106
bytes. Thus the retained combination is 76,064 text bytes smaller than the
pre-D1 O2 compiler. Component screens used separate fixed-point producers;
the Boolean dose was about 1.8% wall-positive and 0.4% CPU-positive across its
two full blocks, while terminal movement was about 1.4% wall / 3.0% CPU
positive over the Boolean producer. A four-block hot compile was output-exact
and supported the Boolean wall direction without claiming more than a close
CPU result.

Fresh same-revision, position-balanced 32-way blocks give:

| Fixed workload | Producer ratio | Wall | Aggregate CPU |
| --- | --- | ---: | ---: |
| O1 | self O2/O1 | 0.955405x | 0.962863x |
| O1 | GCC O2/O1 | 0.885857x | 0.882203x |
| O1 | normalized | 1.078510x | 1.091429x |
| O2 | self O2/O1 | 0.945069x | 0.963116x |
| O2 | GCC O2/O1 | 0.884120x | 0.886624x |
| O2 | normalized | 1.068937x | 1.086273x |
| O3 | self O3/O1 | 0.887352x | 0.900661x |
| O3 | GCC O3/O1 | 0.850509x | 0.846506x |
| O3 | normalized | 1.043319x | 1.063975x |

The raw D1 O2 floor is now clear on both workloads; normalized O2 parity is
not yet reached. O3 is not eroded: against the prior O3 producer, the new O3
producer is 0.98255x wall / 0.99683x CPU and emits the identical requested-O3
result. The present O3 compiler is about 11.3% faster by wall and 9.9% faster
by aggregate CPU than the same-revision O1 compiler. The 20% stretch target
therefore still needs about 10 CPU percentage points, while the normalized
CPU gap is about 6.4%.

All fixed workloads contain 219 objects. Their manifest/final hashes are:

- O1: `936856f460923a63d428a84aa5c9e3d44668ed62cd8c558b96d82946727bd6d9`
  / `2be2df24e16fc24953de269dab22f77cbe70c05f6f1aedf26b354cad3d45326c`;
- O2: `1cdd15c62077ef4d699d5eab395b9072832fcfee8f26c5686874bc067e108d8e`
  / `a0fd80d621c39bcdeb5321c20ec0084a13010e60f56d67a35b52920513c842f6`;
- O3: `792e4fc16bc7dcf46814da424f9f49654c64764ef42c0582ff3e405f3394ce4b`
  / `9e8a1758a3be984657034698a2cf0e9885acd52aedcfed2880461ffffdce22c1`.

Each self compiler reproduces its fixed point exactly, and the corresponding
GCC O2/O3 producers emit the same 219 objects and final compiler. PA37 now
states O2/O3 behavior and uses role-based structural, bounded-stat,
serialization, native, and behavior properties rather than complete-program
matching. The stale exact O2 fixture that prescribed a retained addressed
slot was replaced by a property covering the same post-inlining store. PA37
passes 187/187, PA38 passes 45/45, and the through-PA38 report passes
5,470/5,470. PA37/PA38 debug and object-roundtrip lanes pass, the LowIR
contract audit is clean, and the PA39 file audit retains zero fatal findings
and the established 32 warnings.

## Starting pipeline census and attribution baseline

The initial attribution work preserved the separation between the LowIR
pipeline and the native pipeline.  This section records the starting
`fca27131` state; the execution ledger records every later retained addition.

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

### LowIR O3 addition at plan start

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

### Native O3 addition at plan start

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
only after the native hot-TU screen has shortlisted a candidate.  At the
initial baseline, self O1 and O3 executed 5,196,654,174 and 5,181,114,513
instructions respectively, only a 0.299% reduction.  Same-source GCC O1 and
O3 executed 2,952,710,945 and 2,424,753,931 instructions, a 17.88% reduction.
GCC's change is distributed across whitespace scanning, decoding, range and
identifier checks, literal scanning, vector growth, UTF-8 append, and macro
operator checks; it is broad interprocedural simplification, not one isolated
backend peephole.

For each timing window:

- use immutable compiler binaries and identical source, include, and link
  inputs;
- when a candidate changes O2/O3 code generation, distinguish G1 (the new
  implementation generated by the retained compiler) from G2 (the new
  implementation generated by a compiler that contains it); do not judge the
  candidate's producer-code benefit until G2 has self-applied the change, and
  require G2/G3 equality before retention;
- use `-j32`, `INCEPTION_BUILD_JOBS=32`, and
  `INCEPTION_OBJECT_BUILD_JOBS=32` for compiler and inception builds;
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

At plan start O3 had only bounded full unrolling, so removing the inversion
could not reach 20% by itself.  The initial candidate order was:

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

Inlining was first because prior GCC ablation evidence found it dominant on
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

### Current continuation order after the D1 O2-promotion screen

The initial O3 inversion and the 0.95 intermediate target are now cleared.
The D1 candidates have dispositions, but normalized O2 parity remains open.
The next work must address that remaining O2 floor and the 0.80 O3 target
without repeating the many threshold, whole-body-inline, alignment, and local
peephole experiments already closed by the ledger.

#### D0. Refresh the current O2 row and controls

Before another code change, rebuild `S_1`, `S_2`, `S_3`, `G_1`, `G_2`, and
`G_3` from commit `5e644a0e` and run fixed O1, O2, and O3 workloads.  Run
Clang O1/O2/O3 producers on fixed O1 and O3 workloads.  The current direct
S1/S3 and G1/G3 O3-workload result above is reusable, but the O2 row is older
than several retained O3 and common-native changes and must not be inferred
from it.  Record raw and normalized wall/CPU ratios, exact manifests, final
hashes, producer `.text`, optimizer time, and the requested-level cost.

This refresh decides whether O2 first needs code quality, pass-cost work, or
both.  It also supplies the same-revision denominator for every promotion
experiment below.

#### D1. Promote low-growth O3 winners to O2 where they pay

O2 should first reuse proven general transformations rather than acquire a
new speculative pass family.  Screen the retained O3 changes individually at
O2, ordered by demonstrated dynamic saving and low output growth:

1. repeat-stable query reuse;
2. complete-use addressed-scalar-slot promotion;
3. zero-bounded range and trivial Boolean-diamond simplification; and
4. terminal scalar-phi/return movement.

Do not initially promote grouped cloning, loop-priority inlining, or another
growth transform.  A promoted feature must keep its existing structural proof
and budgets, improve both raw and normalized O2, and not erode O3.  Its PA37
property test and README must be revised from O2 isolation to O2/O3 behavior;
the test must continue to identify roles and behavior rather than exact
program text.  If a feature's compile-time scan cost cancels its generated-
code saving at O2, leave it O3-only and record that disposition.

The D1 exit is `P_b(2:1) < 1.00` and `N_b(2:1) <= 1.00` on fixed O1 and O2
workloads.  Continue toward the earlier 0.95 Phase-C goal only with retained
features that improve O3 as well.

#### D2. Partition structurally cold native regions

This is the highest-upside new O3 candidate.  GCC's
`TranslationCursor::Next` has a 2,145-byte hot fragment and a 3,832-byte cold
fragment, whereas our 4,264 bytes remain in one text region.  The same pattern
appears in `Lexer::Run`, `AppendUTF8`, `Peek`, and `AddSourceToken`.  This is
not another function-alignment sweep: stronger 32/64-byte alignment was
already rejected, and padding cannot pack hot fragments together.

Proceed in four bounded steps:

1. census, for every O3 function, bytes and edges in blocks structurally cold
   because they raise, resume, or call a serialized `noreturn` boundary;
2. prototype cold-fragment emission using facts already present in serialized
   MIR, placing eligible fragments in a cold executable section while keeping
   explicit relocations between hot and cold fragments;
3. preserve symbol identity, local labels, debug locations, FDE/LSDA ranges,
   COMDAT/weak grouping, unwind behavior, and linker reachability; and
4. gate partitioning on a minimum cold-byte saving, a bounded number of
   cross-fragment edges, representable relative branches, and a per-object
   section/relocation budget.

Prefer deriving the decision from existing MIR `noreturn`, throw, resume, and
CFG facts.  Add no LowIR field merely to carry a native layout preference.  If
native replay cannot reconstruct a required fact, first prove that the
smallest explicit MIR addition is necessary and serializable.  Report hot and
cold bytes, fragments, cross edges, long-branch expansions, EH skips,
analysis bytes, and elapsed time.

The earliest owning contract is PA38: a student-facing high-level description
and structural/behavioral checks for hot/cold section placement, cross-section
control transfer, ordinary and throwing behavior, debug replay, and negative
EH/COMDAT/budget guards.  O0--O2 stay byte-exact during the first O3 screen.
Only after O3 retention should an O2 dose be measured under D1's rules.

This candidate advances only if hot-text footprint falls materially and a
complete 32-way block improves by at least 1% normalized CPU or corroborating
wall time.  A change that only renames or reorders sections without reducing
the executed footprint is rejected.

#### D3. Inline only proven fast arms and share the slow body

If D2 makes cold separation useful, evaluate a source-independent partial-
inlining transform for internal scalar queries with a small, side-effect-free
fast arm and one shared refill/error arm.  Inline the guard and fast return at
selected hot call sites, but keep exactly one out-of-line slow body.  This is
materially different from the rejected experiments:

- `D.group-header-inline` duplicated the complete loop-shaped clone; and
- `D.group-fast-sibling` left the fast wrapper out of line.

The new form is worth testing only if it reduces calls *and* exposes repeated
field loads to ordinary caller cleanup without duplicating refill, throw, or
EH regions.  Require an internal nonrecursive callee, a bounded acyclic fast
arm, exact argument/result types, no observable address identity, a single
shared slow entry, explicit per-site/per-caller/per-unit growth limits, and a
post-transform cleanup census.  Select sites from structural loop and call
facts, never from function names or source locations.

PA37 owns the partial-inline property if retained.  Its checks must cover the
fast positive shape, the surviving shared slow path, unlike calls, mutation
and volatile guards, serialized replay, bounded statistics, and behavior.
The primary outcome is fewer dynamic calls and hot instructions with no added
spill, save, frame, or hot-text regression.  Do not retain it for call-count
movement alone.

#### D4. Re-profile cursor, string, and token movement after the layout change

After D2/D3, take a new inclusive profile before selecting another rewrite.
Prioritize the remaining regions by whole-program avoidable instructions,
not their flat symbol totals.  For each of `TranslationCursor::Next`,
`AppendUTF8`, and `AddSourceToken`, compare self and GCC at instruction/basic-
block granularity and record:

- executed loads, stores, copies, calls, conditional branches, and backedges;
- hot-fragment bytes and fallthrough/jump structure;
- frames, spills, callee saves, and call-crossing live ranges; and
- the concrete LowIR-to-MIR sequence responsible for the largest delta.

Choose one general instruction family whose measured upper bound is at least
1% of the complete workload.  Re-evaluate an old rejected local improvement
only when D2/D3 has materially changed the condition that caused its
rejection.  For example, a formerly layout-negative copy fusion may be
retested after hot/cold partitioning; a cold divide removal with no dynamic
benefit may not.  This rule prevents cycling through the ledger.

#### D5. Allocation or SIMD only with a demonstrated residual population

If the remaining cost is movement across calls or joins, add live-range
splitting or region-local allocation only for the measured population and
cross it against framed/frameless layout.  If it is repeated adjacent typed
scalar work, build an SLP census first and add SIMD only when MIR can express
the operation, alignment and alias proofs are serialized, and the compiler
workload has enough dynamic instances to clear the 1% screen.  Neither family
is a default final phase merely because mature compilers implement it.

At every step, G2 is the code-quality candidate and G3 must be its exact fixed
point.  A G1-only win is diagnostic evidence, not a retention result.

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
  INCEPTION_BUILD_JOBS=32 \
  INCEPTION_OBJECT_BUILD_JOBS=32

/usr/bin/time -v make -C pa39 -j32 compare-cppgm++-inception \
  INCEPTION_OBJ_ROOT_BASE="$RUN_ROOT" \
  INCEPTION_BUILD_JOBS=32 \
  INCEPTION_OBJECT_BUILD_JOBS=32
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

### Initial G1 rejection of the O3 trivial Boolean-diamond dose

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
prototype was restored without student-contract or test movement.  This was
a valid decision for the G1 compiler that was measured, but it predated the
later requirement that a code-generation change be judged only after G2 has
self-applied it.  The fixed-point re-evaluation below supersedes this
performance disposition while preserving the original evidence.

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

### Rejected acyclic merge-phi caller-saved allocation

An O2+ native-allocation prototype admitted acyclic merge phis to the existing
exact transfer-to-last-use local-phi interval proof.  It could assign a
caller-saved register only when the complete linear interval was free of
claims and clobbers.  The first broad probe also ran at O1 and changed the
fixed workload, so the measured form was explicitly gated to O2/O3.  The
gated candidate reproduced the retained O1 hot object exactly at
`d9dbe51e6b5848711ff72e7b27fef7f5bd638a352db0ea19c20efbc2ae74a41e`.

The source-independent proof produced real local changes under O3.  In the
GCC-built tokenizer object it reduced `AppendUTF8` by 309 native bytes and
also shortened three smaller helpers, although complete compiler layout grew
128 `.text` bytes.  The clean self-built O3 producer grew 256 `.text` bytes,
from 7,972,210 to 7,972,466.  On the identical requested-O1 hot compile,
Callgrind increased from 4,341,389,715 to 4,341,442,410 instructions
(+52,695, +0.0012%).  Because the isolated deterministic producer comparison
was already slightly worse, no noisy full-workload timing was warranted.
The prototype was removed before README or PA38 property movement.

The later G1/G2 screening correction required a fixed-point audit before
treating that performance rejection as final.  G2 did self-apply the rule:
the tokenizer fell 416 text bytes and the complete producer fell from
7,972,210 to 7,897,986 bytes (-74,224).  That apparent saving was invalid.
Although O0 output remained exact and the two producers emitted identical O1
LowIR, G2 emitted different O1 MIR and a different object for unchanged input;
its repeated output was internally deterministic.  G2 then failed to build
G3 with invalid native operands, allocation failures, crashes, and corrupted
semantic state across unrelated translation units.  A merge phi's multiple
predecessor transfers cannot inherit the local loop-phi register proof merely
because their enclosing linear interval is claim- and clobber-free.  The
fixed-point failure upgrades the decision from a performance rejection to a
correctness rejection.

### Current producer-matrix refresh

After the adjacent-normalization checkpoint, new O1 and O2 self producers
were built from the same retained seed; the accepted O3 producer remained
source-current.  Their ELF `.text` sizes are 8,010,178, 7,951,154, and
7,972,210 bytes for O1, O2, and O3 respectively.  Three all-32 balanced
blocks on the fixed O1 workload put O2/O1 at 0.99408x mean wall and 0.99187x
aggregate CPU.  All six lanes per producer reproduced the same 219-object
manifest and final compiler.  O2 therefore clears the raw floor but has not
reached the 0.95 Phase-C target; its refreshed GCC-normalized cell remains to
be completed.

One balanced O3/O1 producer block gives 0.93245x wall and 0.91533x CPU on the
fixed O1 workload, and 0.90057x/0.91380x on the fixed O3 workload.  Matched
same-source GCC O3/O1 controls are 0.84644x/0.84463x and
0.85282x/0.84689x, respectively.  The corresponding preliminary normalized
ratios are therefore 1.1016x/1.0837x on O1 and 1.0560x/1.0790x on O3.
The raw inversion is gone and current O3 is roughly 7--10% faster than O1,
but O3 still captures materially less of the O1-to-O3 gain than GCC and
remains far from the 0.80 raw and normalized target.  Additional self blocks,
the O2 GCC cell, and Clang controls remain final-matrix work rather than being
silently inferred from this refresh.

### D.trivial-bool-diamond fixed-point re-evaluation retained

The corrected G1/G2 methodology materially changes the earlier disposition.
The source-independent O3 pass recognizes only an exact acyclic two-arm
diamond: the arms are otherwise empty jumps with one common predecessor, the
merge contains one `u8` phi immediately consumed by a branch, the phi has one
use and selects opposite constant Boolean values, and no exceptional edge is
being bypassed.  It retargets the original branch and repairs phi labels on
the two downstream edges.  O0, O1, and O2 do not run the pass.

The clean G2 compiler self-applies the transformation.  The hot token move
constructor falls from 234 to 206 native bytes and the complete compiler
falls from 7,972,210 to 7,966,354 ELF `.text` bytes (-5,856).  On the fixed
O1 hot compile, Callgrind falls from 4,341,389,715 to 4,303,862,921
instructions (-37,526,794, -0.8644%).  The same-source GCC control falls only
from 2,423,591,919 to 2,423,316,717 (-0.01135%), so the deterministic
normalized instruction ratio is 0.99147x, a 0.853% relative improvement.
Three balanced native hot-TU blocks also average about 0.991x wall and 0.984x
user time.

Six valid position-balanced blocks over the complete requested-O3 workload
all emitted deterministic per-side output.  Candidate/baseline mean wall and
aggregate-CPU ratios are 0.97883x and 0.99035x; block-median ratios are
0.97600x and 0.99642x.  The requested-O1 output remains exact and its warm
balanced measurements are approximately flat to slightly favorable.  Native
GCC full-build timing was contaminated by isolated scheduler tails, so no
precise native normalization is claimed from that window; the deterministic
GCC Callgrind control above establishes that the self improvement is not
created by adding proportionally more optimizer work to GCC.

PA37 now documents the high-level O3 rule and owns a property control rather
than an exact-program fixture comparison.  It checks that the private merge
is removed only at O3, that a shared Boolean remains, that arm work remains
conditional and observable, that reversed Boolean mapping behaves correctly,
and that noncanonical truth values preserve behavior.  Serialized O1 and O3
outputs are replayed through the driver.  PA38 receives no new contract
because neither MIR structure nor native encoding changed; its full,
through-target, debug, and round-trip gates still run.

PA37 passes 188/188, PA38 passes 45/45, and the report through PA38 passes
5,471/5,471.  PA37/PA38 debug and object-round-trip lanes pass, and the PA39
file audit remains at zero fatal findings and the established 32 warnings.
Two frozen O1 compiles of the established `preprocessor.cpp` input are
byte-identical at
`d9dbe51e6b5848711ff72e7b27fef7f5bd638a352db0ea19c20efbc2ae74a41e`.
Fresh 32-way fixed-point trees match every object and final compiler.  O2 has
219-object manifest
`9d3b0611867e8993c1c130599dba94b204beac8443ee924a9a29c23f6ae22217`
and final hash
`8696907c7cdc868b431509307c66d8fd85f12719aadb4a6d5e2e7ed70267c7f0`;
O3 has manifest
`e8c56c8601e75228b78ca84970d428a0d0de87d91b8cce0dc926c4f1468d5b28`
and G2/G3 final hash
`5fef334d3590bdb19af6f695287cd95d25b48690a8f463450c67793194a0d498`.
The complete O2 and O3 self/inception comparisons take 52.83 and 48.36
seconds respectively with 32 build and object workers.

### D.zero-bounded-signed-range retained

The next self-versus-GCC profile comparison isolated one remaining source
shape that GCC already canonicalized but the self compiler did not.  A signed
validation of one SSA value first rejected `x < 0`, then entered a private
block that rejected `x > C` for a nonnegative constant `C`.  The equivalent
single unsigned comparison `x ugt C` removes the intermediate decision.  In
the tokenizer this occurs in `AppendUTF8`: optimized LowIR loses the private
block and one comparison/branch pair, the object loses 16 text bytes, and the
function falls from 0x773 to 0x76b native bytes.

The source-independent O3 transform requires signed i8/i16/i32/i64 operands,
the same typed SSA value, constant zero and a nonnegative upper bound,
single-use comparison results, one ordinary predecessor for the otherwise
two-instruction upper block, a common rejection destination, and no rejection
phi that distinguishes the two original edges.  It repairs phis on the moved
accept edge.  A cheap terminal-pattern scan runs before graph/use construction,
and the transform removes the now-unreachable block itself.  An initial form
then reran simplify, DCE, and CFG cleanup after every fold; 22 requested-O3
hot lanes per side put that form at 1.00517x aggregate CPU.  Removing those
redundant cleanups left the tokenizer object byte-identical and changed the
same oracle to 0.99740x CPU and 0.99793x wall.  The final pass remains one
bounded attempt per function rather than a fixed point.

The clean G2 compiler has 7,970,114 bytes of ELF `.text`, 3,760 bytes above
the accepted 7,966,354-byte producer because of the implementation, but 48
bytes below G1 after self-application.  On the fixed O1 compile it executes
4,295,303,367 instructions versus 4,303,862,921 for the accepted producer
(-8,559,554, -0.19888%).  The same-source GCC control moves from
2,423,316,717 to 2,423,347,366 (+0.00126%), for a normalized instruction
ratio of 0.997999x (-0.20014%).  At requested O3, self moves from
4,312,137,267 to 4,303,639,623 (-8,497,644, -0.19706%) while GCC moves from
2,425,815,071 to 2,425,765,241 (-0.00205%); normalized O3 is 0.998050x
(-0.19501%).  Every profiled output is byte-identical at its requested level.

After several whole blocks were rejected intact for isolated 812--822 second
host events, a clean reversed 32-way O3 block measured candidate/baseline at
0.98539x wall and 0.99850x aggregate CPU.  A clean 32-way O1 block measured
0.96249x wall and 0.99292x CPU.  Both O1 producers emitted the same 219-object
manifest and final compiler; the O3 sides were internally deterministic and
differed only by the intended self-applied range fold.

PA37 documents the rule and owns a property/behavior reducer.  It proves
O0--O2 isolation, the positive unsigned replacement, accept-edge phi repair,
serialized O1/O3 replay, and guards for distinct rejection targets, distinct
SSA values, a shared upper block, a shared predicate, and a rejection phi.
PA38 receives no new contract because neither MIR semantics nor native
encoding changed.  G2 and G3 match all 219 objects and final compiler hash
`3bc6cd9f5f61f2ee6f67719a3867b8c565c9da054d90defa211583f1640b3ab0`.
PA37 passes 188/188, PA38 passes 45/45, and the through-PA38 report passes
5,471/5,471.  PA37/PA38 debug and object-round-trip lanes pass, and the PA39
file audit remains at zero fatal findings and the established 32 warnings.

### D.dynamic-small-copy rejected

The next native experiment tried to improve the remaining dynamic composite
move in the hot token constructor.  When the runtime size was at most 16
bytes, it selected first/last scalar chunks; larger sizes retained `rep
movsb`.  The complete PA38 suite passed and clean G2/G3 output was exact, but
the runtime selection enlarged the constructor from 206 to 302 bytes and
added work on every move.  Requested-O3 hot Callgrind rose from 4,303,639,623
to 4,330,485,600 instructions (+0.62380%).  The token move itself rose from
156,000,130 to 181,906,972 instructions over 1,413,664 calls.  Native timing
was flat to slightly slower.  The prototype was removed before contract or
test movement; fixed-size chunk selection and this runtime-size dispatch are
different optimization populations.

### D.addressed-scalar-slot retained

The follow-up returned to the LowIR source of the hot frame.  A one-byte
scalar reference temporary was stored directly, addressed, immediately
loaded through that address, and then copied to its destination.  The address
operation prevented ordinary scalar-slot promotion, leaving a frame solely
for that byte.  At O3, the small-object complete-use proof now also admits an
ordinary scalar slot when its address was materialized.  Dense address facts
follow only typed `addr`, pointer `copy`, and `index` chains.  Every use must be
a complete, type-compatible, nonvolatile load/store, an exact whole-slot copy
or zero initialization, or an address-carrier operation; a call argument,
unsupported use, nonzero or variable final offset, partial access, volatile
access, or conflicting type rejects the slot.  Rewritten memory operands
become direct scalar-slot operations and the existing promotion pass owns SSA
construction.  O0 through O2 retain the previous path and output.

In the hot token move constructor this removes the only slot and changes 206
native bytes to a frameless 185-byte body.  The macro object grows 5,036 ELF
`.text` bytes because the source-independent rule fires elsewhere too, while
the complete fixed-point compiler falls from 7,970,114 to 7,913,826 `.text`
bytes (-56,288).  On the exact requested-O3 hot compile, self Callgrind falls
from 4,303,639,623 to 4,225,444,281 instructions (-1.81696%).  Fresh GCC-built
baseline and candidate controls move from 2,425,846,483 to 2,423,904,398
(-0.08006%), giving a normalized instruction ratio of 0.982617x (-1.73829%).

Three position-balanced 32-way blocks per side over each complete workload
give:

| Fixed workload | Producer | Baseline wall / CPU | Candidate wall / CPU | Raw change | Normalized change |
| --- | --- | ---: | ---: | ---: | ---: |
| O1 | self O3 | 29.508 / 827.010 s | 28.572 / 816.570 s | -3.17% / -1.26% | -4.85% / -1.33% |
| O1 | GCC O3 | 17.915 / 499.757 s | 18.230 / 500.122 s | +1.76% / +0.07% | control |
| O3 | self O3 | 29.467 / 829.602 s | 29.188 / 820.948 s | -0.94% / -1.04% | -1.61% / -1.16% |
| O3 | GCC O3 | 18.027 / 501.138 s | 18.148 / 501.713 s | +0.67% / +0.11% | control |

The O1 workload is byte-identical between producers, with 219-object manifest
`aaa956b46d968221bff4ec0206a3b1e9465fbd0d7f2995fa1a0ae5cc46c32e25`
and final hash
`5170138047eab3cb6f016cbef2297fb95f1e83f2e4544bf36bff861219cf7920`.
The O3 baseline and candidate are each deterministic: their manifests are
`92fc1e30be87ff3104fa87ed2045586fcf5246d5a1f13debcb45b0ab003bf2fa`
and `4e1206ae289fd6cb76cb84fc159edbb7a1d1c4823297c6d50e504b9d57eea3ec`,
and their final hashes are
`14b54cc91bc20921e6ad94ad17159f1f69d25b8a7a9cc5415e123e7f9071bc7e`
and `3d72f8607d98a33a3eba55adde272138ea983ba870a6fd1b29978a24a78bdefa`.
Fresh G1/G2 trees match all 219 candidate objects and the candidate final
binary exactly.

PA37 documents the high-level O3 rule and owns a property/behavior control.
It checks positive direct, pointer-copy/zero-index, addressed-store,
whole-slot-copy, and zero-initialization cases; O0--O2 isolation; escape,
nonzero-index, variable-index, volatile, and partial-type guards; bounded
candidate/promotion/rewrite stats; serialized O3 replay; and O2/O3 behavior.
It does not compare an entire expected program.  PA38 is unchanged because no
new MIR or encoding contract is required.  PA37 passes 188/188, PA38 passes
45/45, and the through-PA38 report passes 5,471/5,471.  PA37/PA38 debug and
object-round-trip lanes pass, the LowIR contract audit remains clean, and the
PA39 file audit has zero fatal findings and the established 32 warnings.

### Rejected terminal return-address load folding

An O2+ MIR prototype folded the exact terminal sequence `lea address`,
`load result, [address]`, `ret result` into one indexed load when the address
did not depend on the overwritten setup register and displacement addition
was representable.  The source-independent three-instruction proof fired on
both generic and grouped lexer lookahead returns.  Each body lost one MIR
instruction and three native bytes; the fixed requested-O1 object remained
byte-exact.  PA38 stayed clean at 45/45 during the experiment.

This experiment also established the G1/G2 rule now recorded in the
measurement model.  G1 contained the new implementation but had been emitted
by the retained compiler, so the fold was not present in the compiler's own
hot functions: it grew 2,016 text bytes and regressed Callgrind by 112,352
instructions.  G2, emitted by G1, self-applied the fold.  It was 64 bytes
smaller than G1, 1,952 bytes larger than the retained producer, and reduced
the fixed hot compile from 4,341,389,715 to 4,322,191,395 instructions
(-19,198,320, -0.4422%).

The deterministic saving did not survive the representative decision gate.
Across two exact all-32 self blocks, candidate/baseline mean wall and CPU were
1.01030x and 0.99976x.  The same-source GCC candidate grew 1,536 text bytes
and measured 0.99918x/0.99882x in its balanced control block.  Normalized
wall/CPU were therefore about 1.0111x/1.0009x.  Every valid lane reproduced
manifest `09790d6a79e26780843e761893fdd53dc6d3641ffb05c4c07b525d171be110e1`
and final compiler
`8e9760ee54552b29f67f109a0ba6b7641d1c7aa59cbe9a9d2cdeead2c5e57ee8`.
The prototype was removed before README or property-test movement.

### Rejected repeated Boolean-diamond fixed point

The retained O3 Boolean-diamond pass deliberately folds one eligible diamond
per function.  A follow-up changed that single attempt into a bounded fixed
point: every successful iteration removes three blocks, so the loop must
terminate.  On the current tokenizer this removed the other nine generated
string-growth diamonds, reducing LowIR by 90 lines, MIR by 135 lines, and the
tokenizer object by 352 text bytes.  After self-application, the complete G2
producer was 14,704 text bytes smaller than the retained producer.

The extra static reduction was mostly cold.  An output-exact hot O1 Callgrind
compile moved from 4,303,862,921 to 4,303,248,595 instructions, only 614,326
instructions or 0.0143%, and native hot timing was effectively flat.  More
importantly, uncontaminated all-32 O3-workload lanes repeatedly put candidate
aggregate CPU at 784--787 seconds versus 777--779 seconds for the baseline,
about a 0.8--1.0% regression.  Reversing the producer order reproduced the
loss: the clean candidate lane used 784.30 seconds while the two intervening
baseline lanes used 776.64 and 778.91 seconds.  Separate 830--909 second host
events were discarded and did not determine the result.  Both sides emitted
219 deterministic objects; the candidate G3 hash was
`268f78fb8ee3aac25b2c33131520f11afcc05b67238befe328ba41e74c5e2564`.

The repeated scan was therefore removed.  No README or property test was
added: the retained one-diamond contract remains covered, while making it a
per-function fixed point is neither required nor profitable.

### D2 structurally cold native partition rejected

The D2 prototype used only final MIR control-flow facts.  It selected blocks
ending in `throw` or a serialized `noreturn` call, propagated coldness backward
only through all-cold successors, rejected cold-to-hot edges and host-EH
functions, grouped the cold blocks into one serialized suffix, materialized
old fallthroughs, and removed physical continuation after `noreturn`.  The
relocatable writer put sufficiently large ordinary strong suffixes in a shared
`.text.unlikely` section, externalized cross-section local branches, and gave
the cold fragment a steady-frame-state FDE.  Weak/COMDAT functions, small
fragments, and bounded object budgets remained guarded.

The population was real.  The optimized tokenizer object selected nine
fragments containing 68 blocks and 787 MIR instructions.  It moved 3,408
encoded bytes behind 77 cross-section fixups; the selected hot portions
totaled 21,749 bytes.  The macro object selected three fragments totaling 444
cold bytes and seven cross fixups.  An earlier form retained 4,585 tokenizer
cold bytes until unreachable post-`noreturn` tails were removed.  A provisional
PA38 property covered O2/O3 isolation, size and EH/weak guards, serialized MIR,
debug locations, ELF section and relocation structure, separate unwind
metadata, and hot and throwing behavior.  It passed the focused check and the
complete 45/45 PA38 suite.  An earlier clean all-32 prototype inception also
matched every object and its final compiler.

The performance gate did not support retention.  Three output-exact hot-TU
ABBA blocks measured candidate/baseline at 1.02286x wall and 1.01198x user
time.  One full 32-way requested-O1 ABBA block compiled the identical current
220-object workload and final binary.  Self candidate/baseline means were
0.99695x wall and 0.99374x aggregate CPU.  The same-source GCC-O3 producer
control was 0.99239x wall and 0.99875x CPU, making the normalized result
1.00459x wall and 0.99498x CPU.  Thus the only normalized improvement was
about 0.50% CPU, below D2's 1% threshold, while normalized wall and the hot
oracle moved in the wrong direction.  The linked candidate also added 30,192
`.text` bytes after including the implementation.  The implementation,
statistics, provisional PA38 README text, property fixture, and checker were
removed together.  Cold partitioning should not be retried without profile or
linker support that distinguishes dynamically cold edges more accurately than
structural `noreturn` reachability.

### D4 post-inline final-transfer residency retained

The D4 profile showed that the remaining `TranslationCursor::Next` gap was
not one isolated peephole.  O3 loop-priority inlining left fifteen impossible
merge inputs behind cloned `noreturn` arms.  Those inputs prevented an
otherwise final predecessor transfer from donating its frame home, and the
resulting edge-live carrier kept two additional callee-saved colors alive.
The retained family repairs those three connected layers:

1. immediately after the O3 loop-priority inline, truncate normal
   continuations after serialized `noreturn` calls and rerun value, dead-code,
   and CFG cleanup before post-inline promotion;
2. at O3, allow an acyclic merge source with earlier uses to donate its frame
   home when its exact final use is the predecessor's merge transfer, while
   retaining the loop-invariant, after-transfer-use, type, and cyclic guards;
   and
3. extend final-MIR callee-save recoloring from block-confined ranges to
   connected source-live regions, selecting one interference- and
   clobber-free destination for the complete region and retaining the existing
   all-or-nothing source-color rule.

The optimized hot function loses all fifteen impossible phi inputs, shares
the `%t0`, `%t80`, and `%t116` home, drops the `r12` and `r13` preserves,
shrinks its frame from 240 to 208 bytes, and shrinks from 4,264 to 4,157 native
bytes.  The complete fixed-point O3 compiler is 14,928 `.text` bytes smaller
than the accepted D1 producer.  Its G2 and G3 hashes are both
`49737c1838ad1068d5b40c571356a998ca468536fbd704590992da27c4c1492d`.
All requested-O1 full builds contain 219 objects, use canonical manifest hash
`be3c70b657dd4e3fc023d2367795617dd411081162dda9af42d10b1c6a390502`,
and produce final compiler hash
`f22ce5ec7d8e877f3df2ce80c9e7da6706eb45bafb87c824e50a8ea23f6f665b`.

The deterministic hot compile falls from 4,221,931,361 to 4,145,805,467
Callgrind instructions, a 1.8031% reduction.  The same-source GCC-O3 control
falls only 0.0059%, so the normalized instruction result is 0.982027x, or a
1.7973% gain.  Six position-balanced all-32 self pairs give median aggregate
CPU of 824.685 seconds before and 818.755 seconds after (0.992809x) and median
wall of 29.625 and 29.540 seconds (0.997131x).  Six GCC controls give median
CPU of 505.040 and 509.125 seconds (1.008088x), making normalized aggregate
CPU 0.984844x, a 1.5156% gain.  GCC wall time had two independently visible
high-CPU/high-wall candidate lanes and another scheduling-stretched lane, so
no normalized wall claim is made; the deterministic instruction oracle and
aggregate CPU independently clear the 1% gate.

PA37 now describes and checks the post-inline `noreturn` invariant by cloned
block role and removed phi predecessor.  PA38 describes and checks the O3
final-transfer proof, O2 isolation, earlier observable use, after-transfer and
loop-invariant guards, and connected-region recoloring across two successors
with the call-crossing negative retained.  The focused controls pass, PA37 is
187/187, PA38 is 45/45, and the through-PA38 and full reports are 5,470/5,470.
PA37/PA38 debug and object-roundtrip lanes pass, the LowIR audit remains clean,
and the PA39 file audit has zero fatal findings and the established 32
warnings.

### Rejected O3 fast-prefix partial inlining

The next D4 profile exposed a narrow partial-inlining opportunity in the
tokenizer: a small side-effect-free fast-return prefix guarded a cyclic slow
body, and sixteen natural-loop calls could share one outlined slow definition.
The prototype was deliberately bounded to one callee per translation unit, a
16-instruction prefix, a 96-instruction complete body, sixteen sites, one
bailout edge, no EH or object/slot results, and a single shared slow clone.
Provisional PA37 and PA38 properties covered level isolation, structural
selection and guards, bounded statistics, serialized replay, shared-slow
native lowering, the retained backedge, and behavior.  They passed without
matching complete LowIR or MIR program text.

The first implementation lived in `inline_o1.cpp`.  Although the pass fired
only in `pp_tokenizer.cpp`, that source placement changed unrelated O3 code
generation for the existing inliner: `Inliner::inline_call` shrank from
`0x40ee` to `0x3044` bytes.  A six-pair requested-O1 screen then regressed
normalized aggregate CPU by about 0.5% despite improving the hot compile by
1.12%.  Moving the pass to a final, separately linked translation unit,
replacing the generic clone machinery with a prefix-only remapper, processing
sites in reverse traversal order instead of instantiating `std::sort`, and
placing extension statistics after all established `Stats` fields restored
`inline_o1.o` byte-for-byte.  This was a useful destructive-interference
check: it removed the accidental same-TU O3 inlining/layout change without
changing the transformation or its dose.

The isolated fixed point was exact between G1 and G2 at compiler hash
`39330777891b1d1e43a34ae8cc10ba51218e76a4c78e6a161e9a47e8c35d7a64`.
Its text was 8,685,944 bytes versus D4's 8,637,928.  The hot requested-O1
compile remained output-exact and fell from 4,145,805,467 to 4,099,256,470
Callgrind instructions: a 1.1228% reduction.  A 24-site dose was slightly
worse than the retained 16-site experiment, so additional duplication was
not a plausible remedy.

The complete workload contradicted the hot oracle.  Three position-balanced
32-way pairs over the current 220-object requested-O1 workload measured self
mean wall/aggregate CPU at 28.360/812.250 seconds for D4 and
29.807/820.720 seconds for the isolated candidate, ratios of 1.051011x and
1.010428x.  Same-revision GCC-O3 producer controls moved from
18.207/503.087 to 18.000/502.193 seconds, ratios of 0.988649x and 0.998224x.
The normalized result was therefore 1.063078x wall and 1.012225x aggregate
CPU.  Every pair contained the same 220 objects, every object matched between
producers, and every lane had manifest
`5b6ea59b767b3b6964a79afc9168cd1953b3dea4a5dce362bf3756c1e50db658`
and final hash
`27a21ff02a13ccb0e2d5bfc5e6bfdd3c76e4d706a1929e12c0c2ed2b7ddb5dce`.

The remaining loss is not evidence that the retained framed/frameless layout
rules are semantically wrong: restoring the pre-existing inliner object did
not restore end-to-end throughput.  It is evidence that the copied-prefix
text and resulting producer-wide code placement/cache behavior outweigh the
local instruction saving across the source distribution.  Padding one object
or special-casing its link order would be a benchmark-sensitive workaround,
not a justified optimizer improvement.  The implementation, statistics,
source-set addition, provisional README text, fixtures, and checkers were all
removed.  Revisit this shape only with a materially lower-growth encoding or
profile/linker support that demonstrates a representative full-workload win.

### Rejected cyclic dead-parameter register reclamation

The residual O3 semantic-analysis profile exposed a genuine register-allocation
loss in `Analyzer::FindChild`: after the incoming node parameter's last
preheader use, its register remained unavailable throughout a loop.  A bounded
O2/O3 prototype allowed a dead incoming parameter to be reclaimed in a cyclic
block only when the current block could not reach any direct use of that
parameter.  The target function fell from 48 to 45 MIR instructions, from five
loop frame operands to two, and from 177 to 166 native bytes.

The first fixed-point attempt correctly found that direct parameter uses were
not a sufficient proof.  `index_symbol_labels` retained a deferred `%source +
112` address that was replayed in its loop; reclaiming `%source` therefore made
G2 crash in that function.  No fixture was changed.  The corrected proof kept
the allocator's sticky carrier mark and scanned for deferred addresses whose
use blocks remained reachable from the current cycle.  It rejected the ELF
case while retaining all six analyzer reclaims.  O0 and O1 representative
objects remained byte-exact.  A fresh 32-way bootstrap then matched all 219 G1
and G2 objects and the final compiler at
`874c5fba5e8584c838412f7964711ecee3f16759e2d253da3c40d5a236cbda82`;
the candidate was 6,348 `.text` bytes smaller than D4.

Representative throughput rejected even the corrected implementation.  In
one position-balanced requested-O1 block, candidate/baseline was 0.98499x wall
but 1.00252x aggregate CPU.  In the requested-O3 block it was 1.05614x wall
and 1.01239x aggregate CPU.  Each side was internally deterministic: the O1
lanes shared all 219 objects and final output, while the O3 baseline and
candidate each reproduced their respective manifest and final hashes.  The
reachable-address scan and the producer-wide layout change therefore cost more
than the small local spill saving.  The implementation was removed, and no
PA38 README or property test was added for rejected behavior.

### Rejected short dynamic-index base takeover

The next native screen targeted the remaining frame home in
`Program::FindEntry`.  Under register pressure, a dynamic `index` always wrote
through `rax` to a new frame home even when its register base was at its exact
final use.  Reusing that base for the indexed result removed the pointer home,
one callee save, and half of the frame in `FindEntry`; the body fell from 84 to
80 MIR instructions.  A broad O2/O3 form did not generalize cleanly: although
it reduced MIR in several semantic functions, it lengthened register pressure
in `InternTemplateArgumentList` and three index-table functions.

The narrowed proof required at least three uses, no call-crossing lifetime, at
most twelve LowIR positions from definition through final use, an exact final
use of the base, and no physical-register clobber.  O0 and O1 MIR remained
byte-exact.  Across the six representative O3 translation units it changed
only `Program::FindEntry` and `Program::EnsureEntry`; `program.cpp` lost eleven
final MIR instructions and seven scalar-movement instructions, while the
other five units were unchanged (the existing `pipeline.cpp` MIR serializer
limitation prevented a dump but both complete bootstrap generations compiled
it normally).  G2 and G3 matched all 219 objects and the final compiler at
`608dd02bf8b5f3dc560fd41b1980ec1bd9e6c249b4f92956ea868ad08f2e282d`.
The implementation plus its self-applied code grew the D4 producer by 756
`.text` bytes.

Representative normalized timing did not support retention.  On the fixed O1
workload, self candidate/baseline mean wall and aggregate-CPU ratios were
1.06403x and 1.00639x; GCC was 1.01158x and 0.99840x, giving normalized
1.05185x wall and 1.00801x CPU.  One self wall lane was visibly slow, so the
actual O3 workload was measured independently rather than rejecting on that
block alone.  There, self was 1.01325x wall and 1.00613x CPU while GCC was
1.01857x and 1.00503x.  Normalized wall was 0.99478x, but normalized CPU was
still 1.00110x and raw self throughput regressed in both measures.  Every O1
lane emitted the same 219 objects and final compiler; each O3 side was
internally deterministic at its expected G1 or G2 hash.  The prototype was
removed.  Because the behavior is rejected, no PA38 README or property test
was added; PA37 was unaffected throughout.

### Rejected complete scalar-home callee-save promotion

A new tokenizer-only oracle made the next native screen substantially faster.
It links the accepted self-O3 and same-revision GCC-O3 `pp_tokenizer` objects
against one common entry object, feeds both the same 634,463-byte preprocessed
compiler corpus, and requires output hash
`90db88a91d3942b657347250f3c18dd90ccb14e20ba4dd0f5edece1e06a58352`.
Native execution takes about 0.07 seconds and instruction-address Callgrind
takes about five seconds.  The accepted self and GCC controls retire
444,824,631 and 278,905,697 instructions respectively.  This is now the first
screen for tokenizer-path native experiments; retained candidates still need
the full fixed workload and same-source normalized controls.

The residual profile showed the translated scalar in
`TranslationCursor::Next` sharing one physical frame home across three phi
identities and being read repeatedly on the common ASCII path.  A bounded O3
prototype selected at most one compiler-created scalar home per function,
required at least eight complete scalar load/store/compare references, rejected
debug-visible, volatile, address-observed, non-scalar, and mixed-type homes,
rewrote every physical-home reference rather than one predecessor path, and
used an otherwise unmentioned callee-saved register.  Rewriting the complete
home avoided the unsound predecessor-transfer assumption from the rejected
acyclic merge-phi experiment and preserved values uniformly across calls.

The result was output-exact but decisively negative.  The tokenizer `.text`
grew from 28,949 to 29,095 bytes, and `Next` grew from 4,157 to 4,217 bytes.
Total oracle instructions rose from 444,824,631 to 448,205,534 (+0.7601%),
while `Next` alone rose from 38,878,635 to 41,343,637 (+6.3393%).  The
2,465,002-instruction increase is almost exactly four instructions per one of
the 616,072 calls: changing frame operands to register operands did not remove
the existing moves or comparisons, while the extra save, restore, and stack
alignment work ran on every call.  The prototype was removed without README
or property movement.  A successor must eliminate transfers or use a bounded
call-free caller-saved segment; complete-home callee-save promotion is not a
useful retry.

### Selected-parameter index-home correctness backfill

The first fixed-point build of the next native experiment exposed a general
pre-existing lowering bug rather than a failure of the experiment.  A
constant-index operation could switch from an intact incoming parameter
carrier to its preselected optional parameter home.  The encodable/deferred
path marked the setup transfer as required, but the materialized-index path
did not.  Under the self compiler this let `std::vector::insert(const T&)`
read an uninitialized `r8` after an EH marker and made G1 segfault while
building G2.

The fix marks the selected parameter home required as soon as lowering elects
to use it, independently of the later address-encoding path.  PA38 now states
that contract and its control uses EH, a large prefix copy, a post-copy
constant index, and a throwing noinline reader.  The structural assertion is
register-agnostic: it accepts either the intact ABI carrier or a defined
register/frame home and rejects only an uninitialized alternate home.  The
behavior and direct-driver replay run at O1, O2, and O3.  PA38 passes 45/45
and the through-PA38 report passes 5,470/5,470.  This correctness checkpoint
is commit `cd335c0a`; it was pushed before performance work resumed.

### Rejected guarded frame-store sinking

The residual tokenizer profile showed a compiler-created frame value stored
immediately before a scalar null guard, reloaded only below one arm, and then
read repeatedly there.  A bounded O3 prototype recognized only an exact
store/reload/test/conditional tail, required a non-debug temporary binding,
required every other reference to be a same-type nonvolatile reload, and
moved the store only to a direct single-predecessor successor proven to
dominate every reload.  It removed the pre-guard store and reload while
retaining the value in its defining register across the guard.

The local saving was real.  `pp_tokenizer.o` lost 16 text bytes and
`TranslationCursor::Next` lost 12 bytes.  The exact 634,463-byte tokenizer
oracle fell from 444,824,631 to 443,592,542 instructions (-0.2770%), with
`Next` itself falling from 38,863,701 to 37,631,612 (-3.1703%).  An
output-exact requested-O3 compile fell from 4,152,374,237 to 4,136,015,572
instructions (-0.3940%); the same-source GCC control rose only 0.0012%, for a
normalized deterministic ratio of about 0.99605x.  G1 and G2 matched at
compiler hash
`b15af072861d46ee0a0c77b1805e5531dbe1ded258ce627f5046ab9500effd88`.

The producer and representative timing did not justify retention.  The
implementation added 16,536 text bytes to `native/mir/optimize.o`; after 29
other objects changed, the linked compiler was still 17,952 text bytes larger
than the clean `cd335c0a` baseline.  A twelve-pair position-balanced all-32 O1
workload gave pair-median self ratios of 0.99533x wall and 1.00043x aggregate
CPU.  GCC ratios were 0.98963x and 0.99582x, so normalized medians regressed to
1.01030x wall and 1.00406x CPU.  Mean ratios told the same story: self
0.99595x/0.99951x, GCC 0.98881x/0.99529x, and normalized
1.00722x/1.00424x.  All lanes compiled 219 objects; invariant implementation
objects were exact within each producer/source class, while the harness's
lane-specific default object-root string intentionally varied the entry
object.

A destructive-interference control moved the implementation to a final
separate translation unit.  It preserved the transformed objects and
deterministic gain, reduced linked growth to 15,680 text bytes, and reached an
exact 220-object G1/G2 fixed point.  Once the extra source compilation was
counted, however, its three self CPU pairs were -0.09%, +0.13%, and +0.35%; the
median raw CPU ratio regressed about 0.13%.  One later GCC lane was visibly
host-contaminated and the declared GCC window was discarded, but the already
negative raw self result was sufficient to reject the isolated dose.

Both forms were removed.  No PA38 README or property fixture was added for
rejected behavior; PA37 was unaffected.  The fast tokenizer and no-cache
Callgrind oracles remain useful screening tools, but a small dynamic store
saving must not be retained when representative normalized throughput moves
the other way.

### Rejected correlated constant-group specialization

The next profile comparison isolated 94.7 million self instructions in the
generic `IsInRanges` binary-search helper, while GCC had folded the helper
into its identifier predicates.  The existing O3 grouped specialization saw
four E1-table call sites but grouped only one literal integer argument, did
not resolve local `addr @global` producers, required eight sites, and required
an instruction-count reduction.  A bounded prototype lowered the screen to
four sites, resolved direct global-address producers, collected every
correlated uniform argument within the selected group, and allowed a clone
when it removed at least two parameters even if cleanup left the same body
instruction count.

The intended population was the only useful tokenizer change: four calls
sharing the 45-entry E1 table and count became one-argument calls to a 74-byte
clone.  The two four-entry E2 calls retained the general helper.  Output was
exact, but `pp_tokenizer.o` grew 26 text bytes.  More importantly, the fast
oracle rose from 444,824,631 to 445,061,023 instructions (+0.0531%).  The
retained general helper's 12,520,207 flat instructions became 2,355,781 in the
remaining general calls plus 10,637,190 in the clone: specialization added
472,764 helper instructions, only partly offset by a 105,638-instruction
reduction in `IsIdentifierBody`.

The prototype was removed before fixed-point or full-workload escalation.
Removing two constant call arguments does not simplify this loop enough to
repay the clone and its changed calling sequence.  A successor would need a
generic bounded decision-tree or post-specialization loop transformation,
not a wider grouping threshold.  No PA37 contract or property test was added
for rejected behavior; PA38 was unaffected.

### Rejected exact-80 direct-copy encoding

The remaining self profile attributed 89.5 million instructions to the
out-of-line `std::deque` data-swap helper used by the macro processor.  This
was not a retry of transient-swap staging: a narrow O2+ native prototype kept
all three 80-byte copies and selected five unaligned vector load/store pairs
for each one instead of `rep movsb`.  The existing 33--64-byte direct-copy
contract and every other size remained unchanged.

The intended helper was the only relevant hot change.  Its three compact
copies became fifteen vector pairs, its body grew from 241 to 372 bytes, and
the macro-processor object grew 176 text bytes.  A fresh 32-way candidate
self compiler took 17.97 seconds to build and grew 3,892 text bytes.  The
tokenizer-only oracle was byte-exact and unchanged because that translation
unit contains no 80-byte copy, confirming that it cannot screen this
macro-processor hypothesis.

The faster native oracle compiled `preprocessor.cpp` with the generated
baseline and candidate compilers.  All outputs were byte-exact at
`fa3fd18990e4cb205e55d49f904a2f2324db3796a644cf441e6be29e54832b77`.

Twenty position-balanced software `task-clock` observations per side averaged
866.454 ms for baseline and 865.714 ms for candidate.  The ten pair ratios
had mean 0.99916x and median 0.99989x; excluding the first cold pair changed
the pair-mean direction to 1.00051x.  This is flat well inside noise and far
below the 1% retention gate, while code size increases.  Hardware cycles and
instructions were unavailable, but software `task-clock` provided the needed
sub-millisecond CPU oracle without emulation or Cachegrind.

The prototype was removed before a full workload or inception escalation.
No PA38 README or property test was added for rejected behavior; PA37 was
unaffected.  The profile's large repeated-instruction attribution reflects
the accounting of compact string operations, not a demonstrated native-time
opportunity.

### Rejected shared-loop wrapper ownership

The residual self/GCC comparison showed opposite identifier-predicate
ownership.  The self compiler had flattened the small initial-character
wrapper into `Lexer::Run` while retaining the shared binary-search loop as a
call; GCC instead retained a 201-byte initial-character helper and localized
both searches inside it.  This was distinct from the rejected broad shared-
loop and loop-wrapper inlining doses: an O3-only prototype selected an
internal acyclic wrapper of at most 64 instructions with at least two direct
uses, exactly two calls to the same shared non-hinted loop of at most 64
instructions, and no other calls.  It localized the two loops into the
wrapper and then treated the now-cyclic wrapper as an ordinary shared loop so
it would not be duplicated into its larger callers.

The prototype produced exactly the intended general shape on the tokenizer.
`IsIdentifierInitial` reappeared as a 192-byte helper, close to GCC's
201-byte body; `Lexer::Run` fell by 172 bytes, while the complete tokenizer
object grew by 108 text bytes.  Three large caller copies became calls to the
single owned wrapper, and `IsIdentifierBody` continued to share the general
range helper.  A fresh 32-way self build completed in 17.94 seconds.  Across
the full producer population the candidate grew 2,816 text bytes and 16 data
bytes.

Both generated producers emitted the exact same fixed-O1 hot object at
`d9dbe51e6b5848711ff72e7b27fef7f5bd638a352db0ea19c20efbc2ae74a41e`.
Twenty position-balanced software `task-clock` observations per side averaged
866.160 ms for baseline and 869.011 ms for candidate, an aggregate ratio of
1.00329x.  The ten balanced-block ratios averaged 1.00337x, and the unpaired
medians were 864.125 and 867.515 ms.  Thus matching GCC's ownership choice in
isolation is slightly slower on our backend and increases the producer
footprint; it does not clear the 1% retention gate.

The prototype was removed before full-workload or inception escalation.  No
PA37 README or property test was added for rejected behavior; PA38 was
unaffected.  A successor needs a code-generation improvement inside the
range search or its call sites rather than another ownership-only retry.

### Rejected query-family fast-postcondition helper

The retained repeat-stable-query pass removes 31 exact-signature `Peek(0)`
calls, but the optimized tokenizer still executes the specialized query about
18.09 million times.  A deliberately unsafe upper-bound prototype treated a
normally returning higher-index call in the same specialization family as if
it preserved the earlier zero-index scalar result.  It removed another 12
static full-query calls and 6.60 million dynamic calls, reduced the hot
Callgrind total from 4,152,374,237 to 4,051,687,396 instructions (-2.425%),
and improved a 20-observation native screen by 1.74%.  That result was useful
only as a ceiling: `Peek(n)` can extend and mutate the queue, so reusing the
old scalar is not a valid transformation.

A safer prototype retained only the control fact implied by a normally
returning higher-index query, then redirected a later `Peek(0)` to a generated
fast helper which reloads the current head value.  The helper avoided the
full query guard without assuming that memory or the old result survived.
During the first self build this experiment also exposed a dataflow-meet bug:
when predecessor facts differed, the prototype cleared the exact value but
incorrectly kept the first predecessor's fast fact.  The resulting compiler
could loop in tokenization.  Intersecting the fast fact independently at
every join fixed the failure; the corrected compiler completed the tiny and
hot O1 probes promptly and emitted byte-exact objects.

The corrected shape replaced 19 full specialized-query calls with calls to a
30-byte reload helper.  It grew the tokenizer by 51 text bytes and a fresh
32-way self compiler by 12,096 text bytes and 16 data bytes.  Both producers
emitted the exact hot object at
`d9dbe51e6b5848711ff72e7b27fef7f5bd638a352db0ea19c20efbc2ae74a41e`.
Twenty position-balanced software `task-clock` observations per side averaged
859.673 ms for baseline and 867.732 ms for candidate, an aggregate ratio of
1.00937x.  Every one of the ten balanced blocks favored the baseline.  Thus
the safe part of the idea is almost 1% slower and increases code size; the
large upper-bound gain depended on the invalid cached-value reuse.

Both prototypes were removed before full-workload or inception escalation.
No PA37 README or property test was added for rejected behavior; PA38 was
unaffected.  A future retry would need a sound memory-value proof strong
enough to recover the scalar reuse itself, not merely a postcondition helper.

### Rejected force-inlined query-family fast arm

A stricter successor to the out-of-line helper proved when a general query's
normal return establishes that the exact occupancy storage is nonzero.  It
required an unsigned `occupancy <= index` refill loop, a nonvolatile load,
and every normal return to be dominated either by the loop's false edge or by
a nonzero test of a reload from the identical address.  The intervening path
could contain only pure scalar work and nonvolatile loads.  A grouped
zero-index clone then supplied a bounded helper which reloaded the current
head, and existing force-inline replay removed the helper call and refill
guard.  Facts were intersected at joins and cleared by ordinary memory
barriers.  This avoided the unsound cached-value assumption in the first
prototype.

The prototype exposed and corrected an experiment-local meet bug: clearing
an exact result at a join while retaining one predecessor's fast fact could
make the generated tokenizer loop.  Intersecting the two facts independently
restored exact behavior.  A property control covered the positive structural
proof, mutating early return, changed receiver, store, one-predecessor join,
O0--O2 isolation, bounded statistics, force-inline replay, and native
behavior.  The implementation was split into bounded helpers until the file
audit again had zero fatal findings.  Because the optimization failed its
performance gate, neither that implementation nor its PA37 contract was
retained.

The final audit-clean compiler grew 25,996 text bytes.  Twenty exact-output
software `task-clock` observations per side improved the hot O1 compile from
864.178 to 853.560 ms (`0.98771x`), and Callgrind fell from 4,152,374,237 to
4,111,150,753 instructions (`0.99007x`).  Same-source GCC Callgrind was flat
at `1.000034x`, so the normalized hot instruction ratio was about `0.99004x`.
The complete workloads did not preserve that magnitude.  Four clean,
order-balanced O1 blocks measured self at `1.00000x` wall / `0.99727x` CPU
and GCC at `1.00275x` / `0.99792x`, only `0.99726x` / `0.99935x` normalized.
Two order-balanced O3 blocks measured self at `1.00435x` wall / `0.99765x`
CPU and GCC at `1.00109x` / `1.00006x`, approximately `1.00325x` /
`0.99759x` normalized.  Thus O3 CPU improved only 0.24% after normalization,
wall regressed 0.33%, and the paired wall direction was unfavorable.  G1,
G2, and G3 were exact at the 219-object manifest
`acf5f80f9a56ec61072d5d50c42533a6b1347f047b943fbfddb7c59d4e9b4b2c`
and final hash
`314a6e0f848bdec2a0ed116f5c95bd514d4acee2a8b7f7a004a3bef732faa1a5`.

The dose was removed.  A future query-family retry needs a broader fast-arm
population or materially smaller proof implementation; another layout-only
variation of this single family is below the complete-workload threshold.

### Rejected bounded integer equality-set lowering

The next source-diverse structural screen found repeated acyclic
short-circuit `x == C` chains which ultimately selected one of two control
successors.  An O3-only LowIR prototype recognized at least four distinct
integer constants on one typed SSA selector, required private pure compare
and connector blocks with no exceptional control, repaired moved phi edges,
and replaced the Boolean materialization with an existing LowIR `switch`.
The first switch-only form regressed the tokenizer from 444,824,631 to
445,751,354 Callgrind instructions because generic native switch lowering
materialized every constant in a register.

A paired PA38 prototype lowered bounded same-target switches spanning at most
64 values to one range check, a 64-bit mask, `bt`, and two branches.  Combining
both layers removed 75 optimized LowIR lines from the tokenizer, reduced
`Lexer::Run` by 160 native bytes, and reduced tokenizer Callgrind to
442,183,990 instructions (`0.994064x`).  On the complete hot translation unit,
self Callgrind fell from 4,152,374,237 to 4,124,775,988 (`0.993354x`) while the
same-source GCC control was effectively flat at `1.000068x`, for a normalized
instruction ratio of `0.993286x`.  Output was exact throughout.  These results
confirmed the local code-generation opportunity but did not meet the plan's
1% complete-workload gate.

The initial general matcher added 25,032 text bytes to the self producer.  A
narrower direct-control implementation preserved the exact optimized output
and reduced that growth to 20,288 bytes, but its self-applied matcher alone
was still 16,281 bytes.  Four stable 32-way requested-O1 samples per side for
the initial form measured `1.00632x` wall / `1.00033x` aggregate CPU.  A fresh
ABBA block for the compact form measured `1.00211x` wall / `0.99808x` CPU,
with an exact common 219-object manifest and final compiler.  Thus a 0.67%
hot instruction saving became only a 0.19% full CPU improvement while wall
remained unfavorable.  The proof machinery and native opcode surface were
removed.  No PA37/PA38 README or property was retained for rejected behavior;
a future attempt needs a substantially smaller generic formulation or a
broader population that clears the complete-workload threshold.

### Rejected bounded dense jump-table lowering

The refreshed self/GCC profile attributed 77.3 million flat instructions to
the macro processor's generic `IsOperator` query.  GCC emitted four
constant-propagated caller variants and used a jump table in the remaining
seven-way character switch, while self retained one large generic function
with a linear compare chain.  Readonly-address caller specialization had
already been rejected above, so a distinct O3 native experiment tested only
the general switch-lowering hypothesis.

The prototype retained a switch as one serialized MIR operation when it had
at least six nonnegative immediate cases, a range of at most 64 values, and
at least 25% density.  It emitted an unsigned range guard followed by a
relative-offset table and indirect transfer, used only encoder-reserved
scratch registers, and left O0--O2, host-EH functions, sparse switches, and
all other control flow unchanged.  The intended 7-way macro query and a
dense 10-way switch were selected.  The macro query grew from 846 to 898
bytes because its 28-entry table outweighed the removed comparisons.  The
complete generated producer nevertheless shrank 824 text bytes through
secondary self-application effects.

The encoding and control-flow model were sound in the focused screen:
`make test-pa38` remained 45/45, the optimized MIR exposed the bounded table
without matching complete program text, and the generated compiler completed
the hot translation.  The standalone tokenizer was an intentional negative
control: its hot function was unchanged and its no-cache Callgrind count moved
only from 444,824,631 to 444,858,987 (+0.0077%).

The full hot requested-O3 compile also rejected the hypothesis.  Output
changed only by the intended native switch encoding, while Callgrind fell
from 4,152,374,237 to 4,149,972,172 instructions (`0.999422x`, or -0.0578%).
The flat `IsOperator` cost fell only from 77,325,557 to 76,966,148
instructions (-0.465%).  Common early cases make the existing short chain
competitive with the fixed table setup; GCC's much larger result depends on
propagating each literal spelling at its caller, not on jump-table selection
alone.  This deterministic saving is far below the 1% escalation threshold,
so the MIR opcode and encoder surface were removed before PA38 contract or
property movement and before a full workload or inception build.

### Rejected final exception-chain inlining and constant-phi closure

The remaining tokenizer profile attributed 84.7 million instructions to the
istream-buffer iterator construction used by `main`.  An initial O3-only
prototype let the final definition-removing inline wave expand arbitrary EH
bodies.  Its G1 compiler built, but G1 could not build G2: it inlined a
212-instruction hashtable assignment body into an ordinary caller and native
lowering reported a protected-region state mismatch.  This was a real failed
safety proof, not a fixture discrepancy, and the broad form was discarded.

The corrected experiment admitted only a two-link chain: one discardable
single-use EH helper could move into a weak, inline-preferred, acyclic, void,
fixed-arity wrapper of at most 32 instructions, and that wrapper could then
move to its sole direct use.  Existing caller, callee, and translation-unit
budgets still bounded the combined expansion.  A final constant-phi closure
then recognized phi dependency components whose only external inputs were one
identical scalar literal or global address, allowing ordinary cleanup and
slot promotion to consume the newly exposed state.  Large standalone EH
bodies remained blocked.  The narrow form compiled exception-heavy
`elf_writer.cpp`, and its first G2/G3 pair matched all 219 objects and the
linked compiler at hash
`0a9face0f29c9fa7b6f4eb0ba36837e9db6b974019ea094112bca24abfa29ef2`.

The local result was substantial and exact.  Optimized tokenizer LowIR fell
from 2,431 to 2,336 lines, tokenizer text fell from 45,100 to 44,843 bytes,
and the no-cache tokenizer oracle fell from 444,824,631 to 434,802,113
instructions (`0.977469x`, or -2.2531%).  Nevertheless, the representative
O1 producer-quality screen rejected it.  Three all-32 ABBA blocks matched the
same 219-object manifest and final compiler in every lane, but mean self wall
and aggregate CPU ratios were `1.004680x` and `1.002509x`.  The same-source GCC
control ratios were `0.999543x` and `0.999264x`, producing normalized losses of
0.514% wall and 0.325% CPU.

An isolation revision allocated and populated the two EH eligibility tables
only in the final O3 wave.  It improved the exact hot native O1 compile by
0.902% paired user time, proving that disabled O1 bookkeeping had leaked from
the first form.  The complete workload still regressed: mean wall/CPU were
`1.009041x`/`1.003272x`, paired medians were
`1.012677x`/`1.003314x`, and five of six CPU pairs favored the baseline while
the sixth was flat.  All lanes matched manifest
`368c4bcf0ad17a110d2dc7b3370380bc2e9034420422bb9929a785b4d377dd65`
and final hash
`9f24fe2a572348ed127b9f143f5e6e0bbc01f65f9a7837f7d9219672d73c6871`.

Both narrow forms were removed.  No PA37/PA38 README or property was added for
rejected behavior.  The result is useful evidence that this specific library
wrapper exposes a real missed optimization, but an EH-specific inliner plus a
general phi-component solver adds too much producer footprint and unfavorable
whole-program layout.  A successor needs a materially smaller existing-pass
extension or a broader population; the tokenizer result alone is not a reason
to retain the contract.

### Rejected pre-definition planned-phi spill protection

The refreshed self/GCC residual attributed 26.2 million flat instructions to
`std::vector<unsigned>::_M_fill_append`, versus 6.36 million in GCC.  Comparing
the current O1 and O3 MIR exposed direct destructive interference between two
otherwise useful optimizations.  O3's load reuse keeps the vector end pointer
live across the fast/slow split.  That transient preserved-register pressure
causes the reactive allocator to evict the already-reserved fast-loop cursor
before its first incoming edge.  The eviction stores the old, not-yet-defined
register contents and redirects all later phi transfers through a frame home.
O1's shorter load lifetime leaves the cursor resident.  This explains one
surprising O3 regression beyond the previously recorded pass-cost and layout
effects: reducing loads in LowIR can lengthen a native live range enough to
undo a retained loop-residency win.

A narrow O2/O3 prototype made a planned phi home ineligible for reactive
spilling only before the phi's linear definition.  The pressure-producing
value then used the existing direct-to-frame fallback; initialized phi homes,
ordinary planned values, O0/O1, EH rules, and spill safety were unchanged.
The intended fast fill loop recovered register residency, removed its
per-iteration pointer load/store/update, and shrank `_M_fill_append` from 396
to 379 bytes.  PA38 remained 45/45.  Fresh outer/inner/object-32-way candidate
builds reached an exact 219-object fixed point: G2 and G3 shared manifest
`beac56a1442c711d7507b765ba4ff51291428afb355fd150545398bf3b5744fd`
and final compiler hash
`60afc1b2699cc973e9a4cc981e2409373486fae35c42e35a79404264541ecba9`.

The deterministic complete hot-TU screen rejected retention.  Baseline and
candidate emitted the exact object hash
`fa3fd18990e4cb205e55d49f904a2f2324db3796a644cf441e6be29e54832b77`,
but total Callgrind instructions moved only from 4,152,374,237 to
4,152,103,265 (`0.9999347x`, -0.0065%).  The target helper improved from
26,215,525 to 25,511,530 flat instructions (-2.685%), showing that the restored
loop shape is real but dynamically too small.  Other code-shape displacement
offset part of that saving: `program.o` grew 48 text bytes and the fixed-point
compiler grew 3,392 text bytes.  The prototype was removed before README or
property-test movement and before representative timing.  A useful successor
must prevent this interference for a broader proven population, or combine it
with a loop transformation whose complete-workload upper bound exceeds 1%.

### Rejected nonescaping-slot alias refinement

The next self/GCC residual screen found 44.8 million flat instructions in
`SpellingTable::FindPosition`, a helper which GCC absorbs into its caller.  In
the self-generated O3 LowIR, each collision iteration reloaded the begin and
end pointers of an unchanged `vector<unsigned>`, recomputed its size and mask,
and repeated a string-length load.  The late memory-GVN analysis already knew
that the intervening bookkeeping stores targeted nonescaping local slots, but
its unknown-memory version treated every store as a barrier whenever any
unknown-pointer load existed in the function.

An O2/O3-only prototype stopped such proven nonescaping-slot stores from
advancing the unknown-pointer version.  This is a sound generic alias
refinement: an external unknown pointer cannot name a local slot whose address
never escapes.  It removed the probe-loop begin load, all five repeated
size/mask instructions, and repeated object-length loads; the optimized macro
LowIR shrank 1,805 bytes.  PA38 remained 45/45, a 32-way G1/G2 compiler build
completed with all 219 objects, the complete hot output stayed byte-exact at
`fa3fd18990e4cb205e55d49f904a2f2324db3796a644cf441e6be29e54832b77`,
and G2 compiler text shrank 2,088 bytes.

The deterministic complete-TU result was too small to retain.  Callgrind fell
from 4,152,374,237 to 4,149,717,594 instructions (`0.999360211x`, or
-0.063979%).  First-generation native timing was neutral, as expected before
the candidate optimized its own hot paths; the G2 sub-second wall samples were
noisy and did not establish a throughput win.  The alias rule and its new pass
parameter were removed before README/property movement and before full timing
or G3 escalation.  A future memory-SSA refinement needs a broader population
or must combine with an existing transformation to clear the 1% deterministic
gate.

### Rejected complete leaf save elimination

The 94.7-million-instruction `IsInRanges` residual exposed a more specific
version of the earlier rejected whole-color leaf recoloring.  That earlier
dose removed only one of five saves in a framed constructor and paid all
remaining frame overhead.  The new O3-only prototype instead required every
callee-saved color in a leaf to have a conflict-free caller-saved replacement.
It also admitted the exact defining register move when boundary liveness
proved that recoloring would coalesce it, then removed the resulting identity.
Partial leaf changes and all O0--O2 output remained excluded.

The intended binary-search helper became completely save-free: its frame fell
from 16 to zero bytes, `r15` returned to its incoming `rdx` carrier, its setup
move disappeared, and the body shrank from 72 to 63 bytes.  PA38 remained
45/45.  The standalone tokenizer oracle retained output hash
`90db88a91d3942b657347250f3c18dd90ccb14e20ba4dd0f5edece1e06a58352`
and fell from 444,824,631 to 443,950,319 instructions (`0.998034479x`, or
-0.196552%).  Fresh 32-way G1/G2 builds completed with all 219 objects.  The
G2 producer grew 1,056 text bytes and 16 data bytes, while both producers
emitted the exact complete hot object hash
`fa3fd18990e4cb205e55d49f904a2f2324db3796a644cf441e6be29e54832b77`.

The source-diverse complete-TU count was still only a 0.187879% win:
4,152,374,237 to 4,144,572,806 instructions (`0.998121212x`).  `Lexer::Run`
itself fell 3,641,411 flat instructions (`0.993655072x`), so the intended hot
effect was real, but native timing was indistinguishable at the sub-second
screen and the deterministic total missed the 1% gate by a wide margin.  The
leaf policy, defining-move coalescing exception, and identity cleanup were
removed before README/property movement, full timing, or G3 escalation.  This
closes complete leaf save elimination as an independent explanation of the
large O3 gap; a future allocator change needs to eliminate work inside the hot
loops rather than only their per-call prologue and epilogue.

### Rejected conditional-call carrier preservation

Instruction-address attribution showed that every common `AppendUTF8` entry
paid five callee-save pushes and pops even though its ten string-growth calls
sit in bypassable conditional arms.  An O3-only final-MIR prototype recognized
only functions with at least four exact branch/call/join diamonds, used
completed physical-register liveness, and rewrote a complete callee-saved
color uniformly across both sides of every join.  A unique defining move from
an incoming ABI carrier could be coalesced.  When that caller-saved carrier
crossed a call arm, the prototype stored it at the beginning of that arm and
reloaded it immediately after the call; the bypass path retained the same
register without a transfer.  Dynamic-stack, host-EH, and debug functions were
excluded.  This avoided the predecessor-local phi assumption that made the
earlier merge-phi allocation experiment unsound.

The proof eliminated the two complete parameter-carrier colors: `rbx` folded
back into incoming `rdi`, `r15` folded back into incoming `rsi`, and
`AppendUTF8` fell from five to three saved registers.  The path-local homes and
repeated cold-arm stores/reloads enlarged its native body from 1,814 to 2,011
bytes and tokenizer `.text` by 204 bytes.  The standalone tokenizer remained
byte-exact at
`90db88a91d3942b657347250f3c18dd90ccb14e20ba4dd0f5edece1e06a58352`.
Its Callgrind count fell from 444,824,631 to 443,561,255 instructions
(`0.997159834x`, or -0.284017%).

A second completed-liveness probe split disjoint source-live regions and
allowed the same guarded preservation inside eligible call arms.  It could
not prove a caller-saved placement for `r12`, `r13`, or `r14`; conflicts in
ordinary blocks, rather than the call arms alone, keep those colors live.
Thus the safe measured form is also the useful ceiling for this bounded
approach.  Because the five-second tokenizer oracle isolates the target path
and the saving is far below 1%, the prototype and its temporary MIR identity
were removed before README/property movement, full timing, or inception.
Further work should target loop/body instructions or a general live-range
split with a substantially larger static ceiling, not add more call-arm
special cases.

### Retained private structured-table lower-bound prefilter

The next source-diverse residual was a repeated binary search over one private
structured integer table.  Four calls shared the table address and count but
varied the search key.  The earlier correlated-constant prototype established
that merely removing those two parameters made this path slower.  The retained
O3 dose therefore lowers the ordinary eight-call specialization threshold to
four only when specializing the correlated arguments also enables a proven
dynamic shortcut: a key below the minimum table item must return zero without
entering the search.

The proof is deliberately independent of compiler source names and of the
observed fixture.  It accepts only an initialized internal table of positive
same-typed signed scalar literals, proves that the table address and its copy
aliases are used only at one target parameter, rejects object/export/TLS and
relocation aliases, and rejects stores, conversions, indirect uses, other
calls, volatility, and observable operations in the clone.  A monotone
multiple analysis admits only loads at offsets aligned to the table element
width.  Abstract CFG traversal then forces signed comparisons between the key
and those table loads under `key < minimum`, explores both sides of unknown
branches, and requires every reachable return to be literal zero.  The entry
edge may bypass to an existing standalone zero-return block only after all of
those conditions hold.  A failed privacy, alignment, CFG, or return proof
restores the ordinary eight-call threshold and profitability rule.

PA37 now describes that student-facing contract.  Its new role-based control
derives the selected clone from the redirected call population, derives the
minimum from the structured global, and checks the reduced signature,
table-read role, entry predicate, standalone false return, unlike-table call,
escaped-table rejection, and unaligned-load rejection.  It also checks
O0--O2 isolation, bounded stats (one clone and four calls), serialized replay,
and native behavior.  It does not match the complete generated program or a
generated clone name.

The final deterministic complete-TU oracle retained the exact object hash
`fa3fd18990e4cb205e55d49f904a2f2324db3796a644cf441e6be29e54832b77`.
Self-O3 Callgrind instructions fell from 4,152,374,237 to 4,083,375,532
(`0.983383313x`, -1.661669%).  Rebuilding the same source with the GCC-O3
producer moved from 2,425,032,747 to 2,425,304,853 (`1.000112207x`,
+0.011221%), so the normalized result is `0.983272982x` (-1.672702%).
The implementation adds 30,108 producer text bytes and 624 data bytes.

Four endpoint-balanced 32-way native blocks, eight observations per side,
kept the requested-O3 raw result in the close-result band: aggregate self wall
and CPU were `1.001894x` and `1.000413x`.  One GCC block was visibly host
contaminated, so its aggregate normalized result is not evidence.  The robust
block medians were self `0.996832x` wall / `1.000581x` CPU, GCC `1.005232x` /
`1.000726x`, and normalized `0.991643x` / `0.999856x`.  Thus no native O3 CPU
win is claimed; retention rests on the output-exact deterministic normalized
gain with no reproducible raw-throughput regression.  The O1 workload,
although the feature is inactive in generated code, remained exact and its
aggregate normalized wall/CPU ratios were `0.984375x`/`0.997927x`.

Fresh explicit-32-way G1, G2, and G3 builds each completed all 220 objects and
converged to final compiler hash
`140951ada1e274935e588ad1d5392051022f77f627dbccb0d5e6e5ece3220a38`.
PA37 passed 187/187, PA38 passed 45/45, and the through-PA38 report passed
5,470/5,470.  The PA37/PA38 debug and round-trip lanes were clean; the
frontend-source-set, LowIR-contract, and compiler-layout audits also passed.
The aggregate debug target still exposes unrelated stale PA13 fixture diffs,
which this optimization does not change.

### Current Clang residual and rejected bounded same-root alias refinement

A fresh current-source Clang 21.1.8 O3 control completed all 220 objects with
explicit 32-way construction.  On the same complete source compile, all three
producers emitted object hash
`fa3fd18990e4cb205e55d49f904a2f2324db3796a644cf441e6be29e54832b77`.
The retained self compiler retires 4,083,375,532 Callgrind instructions,
Clang retires 3,021,056,729, and GCC retires 2,425,304,853.  Current self is
therefore `1.351638x` Clang and `1.683655x` GCC; Clang itself is `1.245640x`
GCC.  This rules out treating a single GCC-only lowering choice as the whole
remaining explanation.

The buffered zero-index query remains the clearest common structural gap.
Self has 38 static calls to its 203-byte specialized helper; that helper runs
18,094,496 times, with another 1,007,402 calls to the 319-byte generic body.
Clang keeps one 248-byte body and records 21,099,249 surviving calls, while
GCC's dominant 145-byte hot / 76-byte cold clone records 7,314,471 calls.
Inlining changes attribution of the slow cursor calls, so these call counts
are not independent savings, but they show that both better body formation
and better fast-path exposure remain relevant.  They also explain why the
previous copied-prefix prototype was directionally correct locally even
though its code growth lost on the complete workload.

One concrete body discrepancy was a reload of a queue count after stores to
bounded, disjoint elements derived from the same base.  A general prototype
carried relative address ranges through nonnegative literal, `and`, add,
multiply, and shift indexes, invalidated an available load for different
unknown roots or overlapping ranges, and admitted the refinement only in O3
functions of at most 96 LowIR instructions.  The bound avoided the broad
form's destructive live-range interference: the tokenizer object shrank 50
text bytes, the specialized query shrank from 203 to 193 bytes, and no hot
tokenizer function grew.  Standalone tokenizer output remained exact at
`90db88a91d3942b657347250f3c18dd90ccb14e20ba4dd0f5edece1e06a58352`,
while its Callgrind count fell from 444,824,631 to 434,874,083
(`0.977630x`, -2.236960%).

That promising isolated result was still too narrow in the complete compile.
A fresh 32-way G1 completed all 220 objects, but the complete source compile
moved only from 4,083,375,532 to 4,075,533,961 instructions (`0.998080x`,
-0.192036%).  The candidate producer also grew 4,812 text bytes for the range
and alias machinery.  It missed the 1% deterministic floor by a wide margin,
so the address-range analysis, API extension, and bounded policy were removed
before README/property movement, G2/G3, full timing, or inception.  A future
memory refinement must affect a much larger source-diverse population; this
queue-count reload is not a sufficient next win by itself.

### Rejected shared acyclic-child ownership

The next Clang/GCC comparison exposed a different punctuator ownership shape.
The retained self compiler absorbed the large, single-use `ScanPunctuator`
wrapper into `Lexer::Run` but left its smaller shared emitter out of line.
GCC instead kept the wrapper and localized the emitter inside it.  An O3-only
prototype selected one source-independent call-graph shape: an internal,
hinted, acyclic 256--512-instruction wrapper with one direct use and exactly
two calls to an internal, hinted 49--72-instruction child that also had calls
outside the wrapper.  It localized only that pair and preserved the wrapper
through later inline waves without serializing changed source metadata.

The intended shape was produced.  `Run` shrank from 9,362 to 7,502 native
bytes, while a standalone 3,865-byte `ScanPunctuator` owned both emitter
copies and the separate emitter definition disappeared.  The duplication
raised tokenizer text from 29,027 to 30,627 bytes.  Exact tokenizer behavior
was retained at
`90db88a91d3942b657347250f3c18dd90ccb14e20ba4dd0f5edece1e06a58352`,
and its isolated instruction count improved from 444,824,631 to 435,508,902
(`0.979058x`, -2.094248%).

The source-diverse gate reversed that result.  A fresh explicit-32-way G1
completed all 220 objects; its producer grew 8,320 text and 32 data bytes.
The complete source compile emitted the exact retained object hash
`fa3fd18990e4cb205e55d49f904a2f2324db3796a644cf441e6be29e54832b77`,
but retired 4,084,815,400 instructions versus 4,083,375,532 for the retained
compiler (`1.000353x`, +0.035262%).  The prototype was therefore removed
before README/property movement, G2/G3, native timing, or inception.  The
result reinforces that GCC-like ownership in one tokenizer path is not itself
a source-diverse O3 win on this backend.

### D.common-path-memory bounded O3 MIR cleanup retained

The next compiler comparison found two small but complementary memory shapes
in the residual `TranslationCursor::Next` path.  One was a pair of adjacent
64-bit field transfers which GCC and Clang combine into one vector transfer.
The other was the private frame stage immediately before a null guard which
the earlier isolated guarded-store experiment removed.  The guarded-store
dose alone had been rejected: its local and deterministic savings were real,
but too small to survive the representative normalized timing gate.  Retesting
it together with the adjacent transfers changes that decision without
weakening the earlier evidence; the combined source-diverse saving is large
enough to retain and every measured native CPU block is favorable.

The retained O3-only MIR pass recognizes two exact same-block copy forms.  The
first is four adjacent nonvolatile scalar load/store instructions.  The second
is the allocation-produced eight-instruction form in which each scalar passes
through its own private frame stage.  Both require one common address base,
adjacent source and destination offsets, and disjoint complete 16-byte source
and destination ranges.  They become one pointer-preserving, direct-chunk
16-byte copy using the encoder's reserved vector scratch.  The staged proof
counts every use of the logical frame binding, accepts either compiler
temporaries or uniquely identified explicit source slots, rejects an ambiguous
coalesced offset, escape, extra use, volatility, and debug-variable range, and
preserves the merged instruction's source location.  Direct non-indexed memory
ranges are therefore now an explicitly supported fixed-copy MIR operand; this
does not add anything to serialized LowIR.

The companion guard cleanup remains deliberately narrower than ordinary store
sinking.  It accepts only the exact load/store/reload/test/conditional block
tail and a one- or two-block direct fallthrough chain.  The stage must have one
unambiguous private binding, all remaining references must be nonvolatile
loads in the one consuming block, every path into that block must be the
proved single predecessor, and the defining carrier must remain intact.  The
test then reads the carrier directly, the store moves to the beginning of the
consuming arm, and the bypass does neither frame operation.  Observable,
escaped, ambiguous, parameter, and debug-variable homes are unchanged.

PA38 now describes these relationships as student-facing behavior.  Its new
control checks direct and explicitly staged positive forms, overlap,
different-base, volatile, multiply-used-stage, volatile-guard, and
escaping-guard negatives, a later scalar-carrier reuse guard, and both taken
and bypass behavior at O0 through O3.  The later-use control was added during
review and reproduced a real bad-code failure before the completed liveness
proof was required.  The property checker derives bases, offsets, frame homes,
control-flow arms, and encodings from the emitted MIR; it neither matches a
complete MIR program nor prescribes physical registers.  It also replays the
fixture through the public driver, verifies the two-load/two-store vector
encoding, and requires fused and moved instructions to retain source
locations.  O0, O1, and O2 continue to expose the scalar/eager forms.

Against accepted commit `938a172e`, the tokenizer object falls from 30,066 to
29,938 total text bytes and `TranslationCursor::Next` falls from 4,157 to
4,085 bytes.  Across the complete 220-object O3 producer, aggregate object
text falls from 9,492,237 to 9,461,354 bytes (-30,883), and the linked compiler
text falls from 8,668,036 to 8,637,892 (-30,144).  The complete source compile
retains object hash
`fa3fd18990e4cb205e55d49f904a2f2324db3796a644cf441e6be29e54832b77`.
Self Callgrind falls from 4,083,375,532 to 4,035,350,012 instructions
(`0.988238770x`, -1.176123%).  The same-source GCC control moves from
2,425,304,853 to 2,425,746,438 (`1.000182074x`), giving a normalized
`0.988058870x` (-1.194113%) result.

Balanced explicit-32-way native blocks corroborate the deterministic CPU
direction.  On the fixed O1 workload, the three-block aggregate
candidate/baseline wall and CPU are `0.987640x` and `0.993938x`; the individual
CPU ratios are `0.999037x`, `0.993295x`, and `0.989471x`, and all object and
final hashes are exact within each pair.  The requested-O3 window was extended
from three to five blocks after one candidate wall observation rose to 32.37
seconds without a corresponding CPU rise.  All five CPU ratios remain
favorable at `0.991806x`, `0.992869x`, `0.998982x`, `0.991536x`, and
`0.996748x`; aggregate CPU is `0.994389x` and block-median CPU is `0.992869x`.
The contaminated aggregate wall ratio is `1.017896x`, block-median wall is
`1.005164x`, and removing only the disclosed 32.37-second observation leaves
wall effectively flat at `1.002309x`.  No native wall win is claimed.
Retention rests on the 1.19% output-exact normalized instruction reduction,
with consistent native aggregate CPU support.

A compiler produced by the accepted checkpoint intentionally differs from
the first candidate-produced generation because explicit source slots make
the optimization newly self-applicable.  Starting from the current candidate,
all 220 G2/G3 objects and the final compilers are byte-exact at hash
`9c133b78b881cfbebc8763db847dc56612a55ce69c514a5ca375a0767d93d53b`.
PA38 passes 45/45 and the through-PA38 report passes 5,470/5,470.  PA37/PA38
debug and object-roundtrip lanes pass, including the new location assertions;
the LowIR-contract, compiler-layout, rename-manifest, frontend-source-set, and
semantic/lowering/native owner audits are all clean.

### D.forwarded-boolean-control bounded O3 decision forwarding retained

The post-memory-cleanup residual moved the largest shared gap back into
`Lexer::Run`: the retained self compiler spent about 573 million flat
instructions there, versus 403 million for GCC and 488 million for Clang.
Instruction-address inspection found a repeated source-independent lowering
shape in the named-operator path.  Comparison results and literal Boolean
values were first widened through an `i64` phi, truncated to `u8`, forwarded
through a one-instruction block, and only then branched on.  The generated
machine code materialized and spilled the intermediate Boolean even though
each incoming edge already carried the complete decision.

The retained O3-only cleanup moves that final decision onto each incoming
edge.  It accepts only the exact private `i64 phi -> trunc u8 -> jump ->
branch` chain, requires every phi input to be a comparison result or literal
zero/one, requires the phi and truncation to have one use, and requires each
incoming block to end in the merge jump.  It preserves preceding calls and
other work on the incoming edge.  Shared intermediate values, non-Boolean
inputs, successor phis, a cyclic forwarding region, and functions containing
EH instructions retain the original form.  Every successful iteration removes
the merge and forwarding blocks, bounding repeated analysis by half the
original block population.  The pass uses existing serialized LowIR facts and
adds no contract field or native-only side channel.

PA37 now describes the property and its safety boundary.  The role-based
control proves O0--O2 isolation, the O3 positive rewrite, the surviving
incoming effect and comparison decision, and native behavior at every level.
It also has relationship-based negative cases for sharing, a value such as
256 whose `u8` truncation changes truth, successor phis, a backedge through
the forwarding merge, and an active EH region.  The checker recognizes the
local instruction/block relationship rather than matching the complete
program or generated names.

The five-second tokenizer oracle retains output hash
`90db88a91d3942b657347250f3c18dd90ccb14e20ba4dd0f5edece1e06a58352`.
Its text falls 272 bytes, `Lexer::Run` falls 170 bytes, and Callgrind falls
from 432,481,075 to 426,592,567 instructions (`0.986384357x`, -1.361564%).
The source-diverse complete-TU oracle retains object hash
`fa3fd18990e4cb205e55d49f904a2f2324db3796a644cf441e6be29e54832b77`.
Self instructions fall from 4,035,367,233 to 3,987,039,512
(`0.988023960x`, -1.197604%).  The same-source GCC producer moves from
2,425,637,731 to 2,425,787,225 (`1.000061631x`, +0.006163%), giving a
normalized `0.987963071x` result (-1.203693%).

The implementation adds 13,836 object-text bytes in `boolean_cfg.o` and
`pipeline.o`.  Across the other 46 changed compiler objects, self-application
removes 8,425 text bytes, so the complete producer grows only 5,411 object
text bytes and 5,424 linked text bytes.  Three balanced explicit-32-way
native blocks corroborate the deterministic result.  On the fixed O1
workload, aggregate candidate/baseline wall, user CPU, and total CPU are
`0.987361x`, `0.989658x`, and `0.989975x`.  On the requested-O3 workload they
are `0.979446x`, `0.988956x`, and `0.989050x`.  Every O1 lane emits the same
220-object manifest `61dc227d...8eac` and final compiler `7e8e4683...68b1`.
The O3 baseline and candidate outputs are each internally deterministic; the
candidate reproduces manifest `cc72de07...3954` and fixed-point compiler
`0f0d476b...bd3b0`.

The PA37 suite passes 187/187 and the through-PA38 report passes
5,470/5,470.  PA37/PA38 debug and object-roundtrip lanes, the LowIR-contract,
compiler-layout, rename-manifest, frontend-source-set, and owner audits pass.
The PA39 file audit passes with its established warnings after the pre-existing
248-line specialization function was restored to the 240-line limit in the
separate whitespace-only commit `7afc2156`.  Fresh 32-way self/inception
comparison matches all 220 objects and the final candidate compiler exactly.

### Rejected late compared-reference selection scalarization

The next residual investigation began in `MacroProcessor::AddSourceToken`,
whose self flat cost was 109.46 million instructions versus 62.71 million for
GCC.  Its final O3 LowIR implemented each inlined `std::max` by loading two
scalars for the comparison, selecting one of their addresses through a
two-input pointer phi, and then loading the selected scalar again.  One
selected address named an ordinary scalar reference temporary, so the pointer
phi also prevented addressed-slot recovery and left a constant division result
in a frame slot.  This was a general LowIR relationship, not a source-name or
program-content opportunity.

An O3-only prototype recognized an exact compare/branch, two private empty
arms, pointer phi/copy/load merge, and reused the already loaded values in a
new scalar phi.  It required nonvolatile equal-typed scalar loads, one-use
pointer carriers, and the exact two-predecessor diamond; it moved no memory
operation.  Late addressed-slot and ordinary slot promotion then removed the
newly unblocked reference temporaries.  In `AddSourceToken`, MIR fell from 225
to 219 instructions, scalar loads/stores fell from 46/40 to 44/40, the body
fell from 1,245 to 1,219 bytes, and the first division by 104 became the
existing multiplication-based constant-divisor sequence rather than `idiv`.
The macro object lost 4,112 text bytes.  Across the complete compiler, the
prototype plus its new implementation produced 221 objects and reduced linked
text from 8,643,316 to 8,490,672 bytes (-152,644).

The source-diverse dynamic result was much smaller.  On the same requested-O3
hot compile, self Callgrind moved from 3,987,039,512 to 3,978,503,118
instructions (`0.997858964x`, -0.214104%).  The same-source GCC control moved
from 2,425,787,225 to 2,427,463,992 (`1.000691226x`, +0.069123%), for a
normalized `0.997169695x` (-0.283031%) result.  Candidate self and GCC emitted
the same object at hash
`bd986cf1180cb4973bed91fd66e18b31d59f52dc5234b9ba1196c75187d7abc4`.
The named hot functions, including `AddSourceToken`, retained their exact flat
instruction totals: the longer constant-divisor sequence offset the removed
loads and stores under the instruction-count oracle.

Three balanced native ABBA blocks, six observations per side, then rejected
the dose decisively: candidate/baseline was `1.00585x` wall and `1.01250x`
user CPU, with every block nonpositive for user time.  A fresh G1 build used
explicit 32-way object compilation; G2 and contract movement were unwarranted
after both the normalized deterministic threshold and native direction
failed.  The prototype and its scratch build were removed.  Large cold text
reduction and exposing an existing strength reduction are not sufficient when
representative generated-compiler throughput regresses.

### Rejected retained-result constant-division expansion

The next shared GCC/Clang residual exposed a metric-sensitive general gap.
The accepted self compiler contains 4,823 static scalar `div`/`idiv` sites,
versus 556 in GCC and 1,172 in Clang.  In the hot parenthesis-annotation loop,
`vector<Token>::size()` performs `idiv 104` on every iteration while both host
compilers use a multiplication-based constant-divisor sequence.  The native
encoder already implements signed and unsigned magic division, but recognizes
only a division followed by an explicit result move or return.  Machine copy
propagation can forward the architectural `rax`/`rdx` result into its consumer
and delete that move, leaving the encoder unable to tell quotient from
remainder.

An O2/O3-only prototype retained the selected quotient or remainder register
as a second MIR division operand, protected that fixed metadata from local
alias rewriting, and let the existing encoder consume it when a late constant
setup was visible.  A first narrower form marked only results whose original
home was already the architectural result register; a fresh G1 proved that
later forwarding was the common case, so the corrected form marked every
scalar division result.  No LowIR operation or source-program special case was
added.  PA38 remained 45/45.

The corrected form reduced static hardware division sites from 4,823 to 407
and replaced the intended hot `idiv 104`.  `AnnotateParentheses` nevertheless
grew from 549 to 569 bytes, and the complete linked producer grew from
8,643,316 to 8,736,504 text bytes (+93,188), because thousands of compact
hardware divisions became longer multiply/shift sequences.  Candidate self
and GCC producers emitted the same hot object at hash
`f120712339f98ce1e372d2e4431c57394b5ee6161e42c893958bb4bac42d94fb`.

The deterministic and hardware screens both rejected retention.  Self
Callgrind rose from 3,987,039,512 to 4,014,046,895 instructions
(`1.006773794x`, +0.677379%), while the same-source GCC control moved from
2,425,787,225 to 2,425,563,383 (`0.999907724x`, -0.009228%), for a normalized
`1.006866703x` regression.  The target loop itself rose from 62,870,618 to
67,328,718 flat instructions.  Because one hardware division can cost more
cycles than several retired instructions, a six-block ABBA native screen was
also required rather than relying on Callgrind alone.  Across twelve
observations per side, candidate/baseline was `1.005780x` wall, `1.007150x`
user CPU, and `1.008729x` aggregate CPU.  Thus the longer sequences are slower
on the actual workload as well as under the instruction oracle.  The MIR
extension and scratch compiler were removed before contract or test movement;
do not retry broad constant-division expansion without a narrower population
selected by measured hardware benefit.

### Rejected lower O3 late hinted-nonleaf cap

The next residual check asked whether the shipped late hinted-nonleaf limit
was still too permissive after the retained layout and cleanup changes.  The
hot `vector<Token>::push_back` was 769 bytes in the accepted self producer
versus a 603-byte GCC implementation, and its inclusive self/GCC difference
was about 37.1 million instructions, or 0.93% of the complete self workload.
A public `hint-late-cap` sweep showed that 40, rather than the shipped 48,
changed this instantiation into a 56-byte wrapper with an out-of-line
reallocation path.  The prototype therefore changed only the default O3 cap
to 40; O1 and O2 retained 48, and an explicit user override still won.

Two generations were built with explicit 32-way object compilation.  The G2
linked producer shrank from 8,643,316 to 8,339,024 text bytes (-3.52%), and
the fixed-O1 hot output remained byte-exact.  The requested-O3 hot object
changed as expected, but candidate self and GCC producers emitted exactly the
same object at
`f9ec8d5080a4a1e4702ed594d1eb6e0a4c139cc917a30b1a40ad3b658396cfb7`.
This demonstrates that the dose changed generated code rather than exploiting
a producer mismatch.

The complete source-diverse counter result was nevertheless nearly flat.
Self Callgrind moved from 3,987,039,512 to 3,983,125,692 instructions
(`0.999018364x`, -0.098164%), while the same-source GCC control moved from
2,425,787,225 to 2,424,007,938 (`0.999266512x`, -0.073349%).  The normalized
result was only `0.999751671x`, a 0.024833% improvement.  In six native ABBA
blocks, the self paired medians improved 1.459% wall and 0.311% user CPU, but
the GCC control also improved 0.939% wall and was flat in user CPU.  The
corresponding normalized gains were only 0.525% wall and 0.311% user CPU.

This misses the plan's 1% complete-workload gate by a wide margin despite the
large static shrink, consistent with the measured sub-1% dynamic upper bound
of the motivating call family.  G3, contract, and test movement were
unwarranted.  The O3 policy prototype and its scratch compilers were removed;
do not revisit a global late-inline cap without a new source-diverse dynamic
population or a selective proof that clears the gate.

### Retained conditional-prvalue transfer elision

The next residual attributed 46.75 million flat self instructions to
`MacroProcessor::AddSourceToken` beyond the same-source GCC implementation.
Its conditional `Token` prvalue was first constructed in a 104-byte staging
slot, then moved into the named local and destroyed.  Both host compilers
construct the two conditional arms directly in the named local.  A general
frontend/lowering ceiling therefore extended the existing temporary-storage
elision machinery to a same-type conditional prvalue selected for copy or
move construction.  It redirected both arms into the final destination and
removed the outer transfer and temporary destruction; it did not inspect a
source name, class name, or program spelling.

The ceiling was broad enough to self-apply but remained bounded in the current
compiler.  G1 changed only the two implementation objects.  G2 and G3 were
byte-identical at compiler hash
`6bed2c2acfed126f0cea6dad35fdfc467db238fe2548a40b645ae693a5d0e49a`;
29 of 220 final objects differed from the accepted compiler, including the
two implementation objects, so 27 ordinary workload objects contained at
least one eligible transfer.  Linked text fell from 8,643,316 to 8,628,224
bytes (-15,092).  In `AddSourceToken`, optimized LowIR fell from 248 to 205
lines and the native body fell from 1,245 to about 899 bytes.

The deterministic ceiling clears the ordinary performance threshold.  On the
same requested-O3 complete-TU compile, self Callgrind fell from
3,987,039,512 to 3,940,638,858 instructions (`0.988362128x`, -1.163787%).
The same-source GCC control moved from 2,425,787,225 to 2,425,821,160
(`1.000013989x`, +0.001399%), giving a normalized `0.988348302x`
(-1.165170%) result.  Baseline and candidate self/GCC producers all emitted
the same object hash
`fa3fd18990e4cb205e55d49f904a2f2324db3796a644cf441e6be29e54832b77`.

The initial prototype was rejected because it changed frontend O0 execution
and left no serialized fact that could justify changing observable calls and
lifetime operations during LowIR replay.  That rejection identified the
missing boundary rather than a bad optimization.  The retained design adds a
narrow PA13 call-site permission, `[elision=copy]`.  PA17 emits it only on the
outer copy/move construction from a private same-type conditional prvalue,
while O0 retains and executes the distinct transfer and cleanup.  O1 preserves
the marker.  O2/O3 consume it only after a typed LowIR proof establishes two
or more predecessor producers into the same private source, complete source
use closure, dominance of the destination and source addresses, and either an
ordinary matched cleanup or the exact protected transfer/cleanup region.

This makes direct source compilation and serialized O0 replay carry the same
permission and keeps unmarked arbitrary LowIR calls fully observable.  PA13
positive/negative controls validate the metadata surface, PA17 checks the
source-to-LowIR relationship and O0 behavior, and PA37 checks level isolation,
positive ordinary/EH consumption, an unmarked control, an escaping-source
and pre-transfer destination-observation guards, bounded stats, replay, and
behavior without exact complete-program matching.  Direct source objects and
objects produced from serialized O0 LowIR match at O0 through O3.  The PA13,
PA17, and PA37 focused properties pass; the full through-PA38 report is clean
at 5,471/5,471, PA37/PA38 debug and round-trip lanes pass, the LowIR/ownership/
layout audits pass, and the file audit remains at zero fatal findings and the
established 32 warnings.  On the current accepted producer, the contract-
backed candidate reduces `AddSourceToken` from 373 to 297 optimized LowIR
lines and from 1,245 to 899 native text bytes; the complete macro object falls
454 text bytes.

The contract-backed all-32 O3 self/inception build is an exact 221-object fixed
point.  Both manifests hash to
`428812e573ea2c40954daa5bb766feca9d4a20de49d986a9e1debbc52641b8ed`
and both linked compilers hash to
`0c2517b8ec6dde228eef87e8c68bf32cbaf54ee1f5db128e3d224cca0ce3a8aa`.
The additional pass implementation makes the linked fixed-point compiler
2,652 text bytes larger than the prior 8,643,316-byte compiler even though
ordinary affected objects shrink; this implementation cost is included in
the performance result rather than hidden by the local census.

The deterministic remeasurement reproduces the ceiling.  The contract-backed
self compiler retires 3,941,156,570 instructions, `0.988491977x` of the prior
3,987,039,512.  Same-revision GCC retires 2,425,766,269, `0.999991361x` of
the prior 2,425,787,225.  The normalized result is `0.988500517x`, a 1.14995%
gain, and the absolute self/GCC gap falls from `1.643606443x` to
`1.624705818x`.  The same current input and output hash under Clang retires
3,021,593,393 instructions, placing self at `1.304331x` of Clang.  Self, GCC,
and Clang all emit the exact hot object at
`fa3fd18990e4cb205e55d49f904a2f2324db3796a644cf441e6be29e54832b77`.

Three order-balanced complete 32-way O3 workload pairs corroborate the
deterministic result.  Self user-CPU ratios are `0.988709x`, `0.989953x`, and
`0.991827x`, with paired median `0.989953x` and aggregate `0.990162x`.
Same-source old/current GCC ratios are `1.005709x`, `1.000626x`, and
`0.995007x`, with paired median `1.000626x` and aggregate `1.000439x`.
Therefore normalized user CPU is `0.989334x` by paired median and `0.989727x`
in aggregate.  Normalized wall is `0.967865x` by paired median and
`0.977086x` in aggregate, but the CPU result is the primary native claim.

The residual is broad enough that the next experiment must not mistake flat
symbol ownership for savings.  Inclusive phase boundaries attribute about
934 million of the remaining 1.515 billion self/GCC instruction gap to
preprocessing, 436 million to parsing, 44 million to semantic/lowering work,
79 million to optimized-LowIR/native preparation, and the remainder to driver
overhead.  The largest visible family is the specialized `Peek(0)` query:
self makes about 18.22 million out-of-line calls and attributes 391 million
exclusive instructions to the generated group clones, while GCC leaves about
7.44 million out-of-line query calls.  This is evidence for a broader
interprocedural value/postcondition opportunity, not a license to restore the
rejected helper-only or force-inline variants.  Those variants already showed
that guard removal without a sound reusable scalar value, or a narrow
25-KiB proof implementation, does not survive the complete workload.  A
successor must prove the head value survives bounded append-only mutations or
find a broader, cheaper population before it is timed.

### Retained multi-group readonly-string specialization

The next preprocessing residual revisited readonly-address specialization
under a materially different population and implementation cost from the
earlier rejected `D.address-group` dose.  That prototype cloned only the
largest repeated address group through a separate path: twelve rewritten
calls reduced the hot deterministic count by just 0.143%, while the complete
32-way CPU result was flat.  The retained form reuses the existing bounded
mixed-group specializer and considers independent readonly-string values for
the same target.  It therefore amortizes one analysis and one call rewrite
path across the complete source-diverse population instead of paying new
machinery for one address.

The final PA37 rule accepts the direct address of an internal, structured,
NUL-terminated `storage=readonly` byte global.  It folds only exact
nonvolatile `i8`/`u8` loads reached through copies and constant indexes,
preserves signed-byte interpretation, and repairs every surviving phi edge
after a literal branch or switch is decided.  EH-bearing targets are skipped.
Externally replaceable, writable, unterminated, volatile, dynamically indexed,
and dynamically supplied cases retain the original path.  A clone must fold
at least one serialized byte load and satisfy the existing complete-clone
payoff inequality; one call is sufficient only when that inequality pays.
At most twelve string groups are selected for one target, within the existing
24-clone/1,536-instruction translation-unit budgets.  O0 through O2 are
unchanged.  This consumes existing PA13 structured-global facts and adds no
new LowIR field or native-only preparation fact.

A three-group dose was deliberately stopped before contract movement.  Its
deterministic normalized improvement was only about 0.917%, and its complete
32-way aggregate result was just -0.36% user and -0.25% total CPU.  Expanding
to all profitable bounded groups found eight `MacroProcessor::IsOperator`
spellings, rewrote 56 static calls, and created eight 11-instruction clones
(88 cloned instructions total).  The optimized macro LowIR grew by only 1,227
serialized bytes while its native text fell by 688 bytes.  The generic body
remains whenever unlike calls survive; in the measured macro population all
groups pay, so later reachability removes it.  The final fixed-point compiler
text is 8,662,216 bytes, 16,248 bytes above the prior accepted compiler
because the implementation cost is retained in the measurement;
self-application still shrinks the bootstrap compiler by 632 text bytes.

On the same requested-O3 deterministic translation-unit compile, the final
self compiler moves from 3,941,156,570 to 3,882,420,398 retired instructions
(`0.985096717x`, -1.490328%).  Fresh same-source GCC moves from 2,425,766,269
to 2,425,799,473 (`1.000013688x`, +0.001369%), so the normalized result is
`0.985083233x` (-1.491677%) and the absolute self/GCC gap falls from
`1.624705818x` to `1.600470460x`.  Fresh Clang is effectively exact at
3,021,591,668 versus 3,021,593,393 instructions; the self/Clang gap falls from
`1.304330549x` to `1.284892475x`.  All four hot outputs hash to
`fa3fd18990e4cb205e55d49f904a2f2324db3796a644cf441e6be29e54832b77`.

Two order-reversed complete 32-way pairs confirm the broader result.  Candidate
versus baseline user-CPU ratios are `0.983761x` and `0.993604x`; total-CPU
ratios are `0.984788x` and `0.994414x`.  Their aggregate ratios are
`0.976414x` wall, `0.988666x` user, and `0.989585x` total CPU.  Both candidate
lanes reproduce the same 221-object manifest and final compiler, as do both
baseline lanes.  The candidate is an exact 221-object G2/G3 fixed point at
manifest
`e1cd90956c5b4fc939e4b42f5ba641e152c7cd1e28c9f9130786ee481032b2b3`
and compiler
`858b9ea20e151c3b8dd33f8818638eba567643633f3da3ffd68c466a4bd3db6c`.

PA37 documents the general rule and adds level-isolation, multi-group,
serialized-byte, signed-byte, phi-repair, profitability, external/mutable/
unterminated/dynamic/volatile/indexed negative, replay, and behavior
properties without matching a complete emitted program.  PA37 is 188/188,
PA38 is 45/45, and the cumulative through-PA38 report is 5,471/5,471.
PA37/PA38 debug and object-roundtrip lanes pass, and the file audit has zero
fatal findings with 31 advisory findings.  The aggregate debug command still
reproduces pre-existing PA13 reference drift (the accepted prior compiler
emits the same current output); it is not caused by this O3-only increment.
The retained implementation, contract, and test commit is `85bac3e1`.

### Retained stable-prefix query boundary

The next query-family dose resolves the contract problem exposed by the two
earlier rejected fast-query prototypes.  A private helper, generated clone
name, or compiler-source pattern is not evidence that a later query may reuse
an earlier result.  PA13 now provides the narrow serialized permission
`[query=stable_prefix]` for a direct, fixed-arity function whose final
parameter is an integer index and whose result is a supported scalar.  After a
normally returned query at index `m`, a query at `n <= m` with the same
preceding arguments may reuse the previously established prefix; calls,
stores, unknown or negative indexes, different preceding arguments, and
indirect signatures remain conservative barriers.  O0 through O2 preserve
the fact but do not consume it.

PA17 exposes the source spelling `cppgm_stable_prefix` and rejects invalid
result, index, variadic, and indirect forms.  The tokenizer marks its ordinary
indexed `Peek` query through the feature-test spelling, so the optimization is
not tied to a program-content match.  Semantic analysis, lowering, typed
LowIR text, and compiler-object serialization all carry the fact; the compiler
object version is consequently 5.  O3 repeat-stable analysis consumes it, and
constant-group clones retain a transient transformation-derived family/index
relationship until the final index is removed.  Dead-parameter cleanup clears
metadata that its transformed signature can no longer satisfy.

The assignment coverage follows the boundary.  PA13 has structural and
behavioral positive coverage plus float-result, indirect, missing-index,
variadic, and void-result negatives.  PA17 checks valid source production and
four invalid source shapes.  PA37 discovers clone composition from call
relationships and checks O0--O2 isolation, ascending/equal reuse, descending,
different-receiver, unknown/negative-index and effect barriers, bounded stats,
serialized replay, and behavior.  Its object lane proves direct and text-
replayed compiler objects agree at O0 through O3.  PA38 accepts surviving
metadata and exercises driver replay.  No property matches a complete emitted
program or a generated symbol name.

The final deterministic requested-O3 oracle emits the exact object hash
`fa3fd18990e4cb205e55d49f904a2f2324db3796a644cf441e6be29e54832b77`.
Self instructions fall from 3,882,420,398 to 3,787,890,268
(`0.975651753x`, -2.434825%).  Same-revision GCC moves from 2,425,799,473 to
2,426,082,982 (`1.000116872x`, +0.011687%), giving normalized
`0.975537740x` (-2.446226%) and reducing the absolute self/GCC gap from
`1.600470460x` to `1.561319335x`.  Clang moves from 3,021,591,668 to
3,021,438,710 (`0.999949378x`), so self/Clang falls from `1.284892475x` to
`1.253671059x` and the normalized gain is 2.4299%.  The linked fixed-point
compiler text is 8,675,772 bytes, 13,556 bytes above the preceding retained
producer; that implementation cost is included in every result.

One order-reversed complete 32-way block corroborates the deterministic
result.  Self mean wall/aggregate CPU moves from 28.815/804.670 to
27.925/792.065 seconds, raw `0.969113x`/`0.984335x`.  GCC moves from
18.280/511.405 to 18.860/513.825, `1.031729x`/`1.004732x`; the GCC wall
window is too layout/scheduling-sensitive to make a large normalized wall
claim, while normalized CPU is `0.979699x`.  Clang moves from
20.685/568.940 to 20.350/568.425, `0.983805x`/`0.999095x`, giving normalized
self wall/CPU of `0.985067x`/`0.985227x`.  Each producer side repeats its own
221-object manifest and final compiler exactly.

The through-PA38 report is clean at 5,472/5,472; PA37/PA38 debug and object-
round-trip lanes, the LowIR contract/ownership/layout audits, and the file
audit all pass.  Explicit 32-way G1/G2/G3 builds converge across all 221
objects at manifest
`5a6203a8c3c4b4acd5ea98a9421908296e0c46a94a35ffd12f529fd1c0062257`
and final compiler
`bdd8dcafbfa0f6ab9f50122e750fda3afda2b115f12ccb1fc49c8d5a634a9160`.
G1, G2, and G3 take 18.26, 29.85, and 28.73 seconds wall with the required 32
workers.  The retained implementation, contract, and tests are in `d54e7e03`.

### Rejected native frame-pressure follow-ups

Three native follow-ups were screened against the stable-prefix checkpoint
without retaining a new backend contract.  First, a conditional-call bridge
recolored complete parameter regions into caller-saved registers and inserted
preservation only around an actual call.  It eliminated three saved regions
in the tokenizer, but not in `AppendUTF8`, added 68 MIR instructions and
14,592 producer text bytes, and regressed ten balanced frozen-TU task-clock
pairs from 811.636 to 813.513 ms by means (`1.002352x`; paired median
`1.002911x`).

Second, final-MIR frame-slot coalescing used exact binding liveness to merge
scalar compiler-temporary slots.  It reduced `AppendUTF8`'s stack reservation
from 208 to 80 bytes and its body from 1,814 to 1,655 bytes, but the broad
form added 19,344 producer text bytes and measured `1.005144x` by paired mean.
A bounded form restricted to functions with at least eight bindings and a
128-byte frame added 19,536 text bytes and measured `1.007573x` by paired
mean.  Both forms were removed.

The cheaper third form sent only O3 acyclic, non-loop-carried fallback `phi`
homes through the existing reusable temporary-slot pool.  Its first prototype
exposed a real lifetime-model violation: a pooled pointer-phi home was later
donated to another phi, but the pool still recorded only the first logical
value's last use.  A Boolean then overwrote the home while a copied pointer
still used it, and G1 crashed in `MacroProcessor::EvaluateBuiltinProbe`.
No source or fixture was changed to conceal the failure.  The corrected
prototype made pooled homes private; a temporary PA38 relationship/behavior
reducer proved that sequential Boolean phis reused one slot only while a
still-live copied pointer retained a different home.

The corrected form reduced `AppendUTF8` from a 208-byte to an 80-byte frame
and from 1,814 to 1,655 body bytes.  Its fixed-point producer shrank 17,024
text bytes, and all G1/G2 objects and the final compiler matched at hash
`49b77e47e8b356d2598d0f687aba60ebe14ebcb83a895ff03d9c1fbbf665e0af`.
Ten balanced frozen-TU task-clock pairs improved from 817.245 to 814.577 ms
by aggregate means (`0.996735x`); paired mean and median ratios were
`0.996853x` and `0.997630x`.  One clean all-32 requested-O1 ABBA block was
output-exact and improved wall from 27.970 to 27.605 seconds (`0.986950x`),
but aggregate CPU moved from 783.225 to 783.545 seconds (`1.000409x`).  A
position-reversed extension had an obvious slow final candidate lane and was
discarded as a contaminated window; its preceding CPU observations were also
flat to unfavorable.  The safe implementation therefore remained far below
the 1% source-diverse CPU gate and was removed together with its provisional
README and property movement.

### Rejected current-baseline fast-wrapper retry

The fast-return split was re-evaluated after stable-prefix reuse changed the
dominant query population.  The prototype selected a structurally
repeat-stable internal query with at least eight direct calls, retained its
bounded pure return region as an 18-instruction wrapper, moved the complete
body into one internal slow function, and reran inlining only for that wrapper.
No source name or complete program shape participated in selection.

Inlining the wrapper at all 26 surviving sites grew tokenizer LowIR by 19,739
bytes and tokenizer text by 1,068 bytes.  The fixed-point compiler grew 12,544
text bytes.  A two-block native ABBA screen appeared favorable at `0.99098x`
wall and `0.98071x` user, but the deterministic oracle correctly rejected the
dose: self instructions rose from 3,787,890,268 to 3,790,914,713
(`1.000798451x`).  This is another concrete case where the native-only screen
is useful for triage but cannot replace the instruction and normalized gates.

A lower-duplication form copied the wrapper only into the caller with the
largest static population: 16 calls, leaving the other 7/2/1 caller
populations intact.  It grew tokenizer LowIR by 12,448 bytes and the
fixed-point compiler by 14,456 text bytes.  Self instructions rose to
3,791,093,445 (`1.000845636x`); same-source GCC moved only from 2,426,082,982
to 2,426,118,959 (`1.000014829x`).  The normalized ratio is therefore
`1.000830795x`, and the absolute gap worsens from `1.561319335x` to
`1.562616471x`.  Every baseline/candidate self/GCC hot object is byte-identical
at `fa3fd1899...`.  Both prototypes were removed before contract or test
movement: the current baseline confirms the earlier fast-prefix rejection
under the corrected self/GCC metric.

### Rejected forwarded-Boolean inversion extension

The next residual isolated a direct CFG inefficiency rather than call
ownership.  `SpellingTable::FindPosition` widened a comparison result through
an `i64` phi, truncated it to `u8`, compared the result with zero, and then
branched.  A bounded extension of the retained forwarded-Boolean fold accepted
an optional one-use `eq`/`ne` comparison with literal zero or one after the
truncation and moved the appropriately inverted branch to each acyclic phi
predecessor.  It retained the existing cycle, EH, sharing, Boolean-value, and
successor-phi guards and did not use function or source identity.

The transformation removed nine optimized LowIR lines from the hot helper,
reduced it from 377 to 324 native bytes, and lowered its flat instruction count
from 44,813,784 to 35,821,900.  The macro object lost 48 text bytes and the
fixed-point compiler lost 1,576 text bytes.  The complete deterministic hot
compile improved from 3,787,890,268 to 3,778,912,306 instructions
(`0.997629825x`).  Same-source GCC moved from 2,426,082,982 to 2,426,094,893
(`1.000004910x`), giving normalized `0.997624927x` and moving the gap to
`1.557611088x`.  All self/GCC outputs remained exact at `fa3fd1899...`.

The saving is genuine but wholly accounted for by that helper and is only
0.2375% of the complete workload, far below the 1% retention floor.  The
prototype was removed before README/test movement or a full native workload;
future work may include this relationship only as part of a broader Boolean
or string-lookup simplification whose combined population clears the gate.

### Retained terminal staged-object-swap lowering

The next residual revisited the hot deque exchange at the LowIR layer instead
of teaching the native backend an exact 80-byte encoding.  PA13 already gives
`copyobj` the necessary correctness contract: the source and destination do
not overlap, except that identical addresses are permitted.  Consequently no
new opcode, metadata, producer fact, native-only preparation path, or object
version was justified.  A late O3 pass instead recognizes a terminal complete
swap which saves the first object in one nonescaping object slot, copies the
second object over the first, copies the saved bytes over the second, and
returns.  The proof accepts either one aggregate capture or nonvolatile scalar
captures covering every byte.  It rejects partial coverage, volatile access,
calls or escape, observable intervening work, multiple blocks, nonterminal
traffic, and mismatched sizes or alignments.  Selection is entirely structural
and does not use function, symbol, source, or exact program identity.

The replacement is ordinary serialized LowIR: paired scalar loads followed by
paired stores in chunks justified by the declared alignment.  Loading both
values before either store also preserves PA13's identical-address no-op case.
The PA37 handout describes the relationship at student level, and its focused
control checks O0--O2 isolation, two positive construction forms, balanced
scalar transfer structure, incomplete/volatile/escaped/nonterminal negatives,
serialized replay, and O0/O3 behavior without fixing temporary names, register
choices, or complete LowIR text.

On the stable-prefix fixed workload, the self-built compiler falls from
3,787,890,268 to 3,710,968,687 Callgrind instructions (`0.979692764x`).  The
same source compiled by GCC moves from 2,426,082,982 to 2,426,042,427
(`0.999983284x`), so the normalized ratio is `0.979709141x`, a 2.029086%
source-diverse improvement.  The absolute self/GCC gap moves from
`1.561319335x` to `1.529638825x`.  The 80-byte deque helper loses its 160-byte
frame, dead 80-byte initialization, and three `rep movsb` sequences, shrinking
from 241 to 157 bytes; the fixed-point compiler grows 20,164 linked text bytes.
Both baseline and candidate hot objects remain byte-identical at
`fa3fd1899...`, so no source-output drift explains the result.

The focused check, PA37 189/189, PA38 45/45, and the complete 5,472/5,472
through-PA38 report pass.  The PA38 file audit has zero fatal findings.  The
required explicit-32-way G1/G2 build reproduces all 221 objects and the final
compiler byte-for-byte at
`898abea36dd40c8e6d3bcd8173f06458f9035133c2c2db7b5ed9d0769260b2f4`.
The increment is retained, but `1.529638825x` remains above the 1.5 target, so
profiling and measured O3 work continue from this checkpoint.

### Rejected loop scalar-reference specialization

The post-swap profile exposed two natural loops whose induction variables
remained in scalar slots solely because a vector reallocation slow path took
their address.  A bounded O3 prototype derived a proof from existing LowIR:
it accepted a by-address parameter only when every use in the complete callee
body was a same-type, complete, nonvolatile scalar load.  When at least two
natural-loop calls passed exact scalar-slot addresses, it made one internal
by-value clone, inserted ordinary loads at those call sites, and reused the
existing addressed-scalar and SSA promotion passes.  No parameter-access
metadata or PA13 contract change was needed; the callee body supplied the
whole no-write/no-capture proof.

The macro translation unit redirected both qualified calls to one clone.  Its
optimized induction slots disappeared, `AnnotateParentheses` kept the index
in a register, shared three index multiplications as one, and shrank from 525
to 490 bytes.  The cold clone was 690 bytes and the macro object grew 683 text
bytes.  A fresh explicit-32-way fixed point was exact between G2 and G3 at
compiler hash
`3c6b4e9d3de308bb29abc7dd11d5b821704dcc034d85dff4bc6fd4f5c1668776`.

The complete deterministic screen rejected the dose.  Self instructions fell
from 3,710,968,687 to 3,705,297,621 (`0.998471810x`), while same-source GCC
rose from 2,426,042,427 to 2,426,594,342 (`1.000227496x`).  The normalized
ratio was `0.998244713x`, only a 0.175529% improvement, and the absolute gap
would have moved only from `1.529638825x` to `1.526953870x`.  Both compilers
emitted the exact same hot object at `fa3fd1899...`.  The implementation and
source-set entry were removed before README or test movement; the existing
addressed-scalar contract remains unchanged.

### Rejected nested-loop phi residency

The retained parser profile exposed a large outer suffix loop whose `u32`
result is already represented as a six-input loop-carried phi.  The native
planner did not consider that phi because uses in nested loops also mark it
loop-invariant relative to those inner loops, and the generic nested-invariant
exclusion took precedence over the dedicated phi path.  A one-line O3
prototype let loop-carried phis take that path even when they also carried the
nested-loop invariant flag.  This used only existing LowIR structure and
needed no PA13 contract or producer change.

The isolated plan appeared promising: the result moved from a frame slot to
`r15` and the nominal frame fell from 800 to 784 bytes.  The complete lowering
census showed the destructive interference, however.  Pinning `r15` across
the 241-block, 68-call, 38-EH outer loop displaced shorter-lived values:
`ParsePostfixSuffixes` grew from 1,959 to 1,972 MIR instructions, scalar
loads/stores rose from 421/344 to 427/347, frame bindings rose from 391 to 401,
and the outer loop's frame operands rose from 893 to 899.  Its two profiled
copies grew by 33,469 and 18,221 instructions.  Other newly admitted phis also
recolored functions in both directions, confirming that this was a broad
register-plan change rather than a parser-only effect.

An explicit-32-way G1/G2 build was exact at compiler hash
`a4b04f4a1ac8eafc11ce3ecd8ee52965501fb0b7449f5df1b48276b9d8d445a1`,
and all self/GCC hot outputs matched the retained hash `fa3fd1899...`.
Nevertheless, self Callgrind instructions regressed from 3,710,968,687 to
3,711,676,614 (`1.000190766x`), while same-source GCC was effectively flat at
2,426,040,715 (`0.999999294x`).  The normalized ratio was `1.000191472x` and
the absolute gap would rise from `1.529638825x` to `1.529931708x`.  The
prototype was therefore removed before contract movement; the existing
nested-invariant exclusion is intentional until a bounded plan can prove that
the long phi claim will not displace more valuable interval residents.

### Rejected grouped early-exit table unroll

The next tokenizer residual was `IsNamedOperator`: self already inlined the
helper into `Lexer::Run`, but retained a thirteen-entry table loop while GCC
grouped the table by string length.  A bounded late-O3 prototype fully
unrolled only single-latch loops with constant trip counts, acyclic bodies,
early exits, no EH, no escaping SSA values, and a private structured readonly
table indexed by the sole induction phi.  It stable-sorted entries by a
repeated integer discriminator, skipped the remainder of a group after the
first failed discriminator guard, and folded exact typed table fields from
the existing serialized initializer.  The proof used only ordinary PA13
LowIR; it introduced no metadata, opcode, source identity, or hidden native
preparation path.

A fresh compiler built by the retained fixed point reproduced the intended
candidate.  `Lexer::Run` grew from 9,110 to 10,290 bytes, while its exact-output
tokenizer oracle fell from 416,963,964 to 404,332,731 Callgrind instructions
(`0.969695x`).  A complementary exact-width 1--8-byte scalar lowering of the
remaining equality-only `memcmp` calls was immediately rejected: despite
using non-overreading chunks and preserving output, it raised the tokenizer
count to 462,161,356 instructions.

The grouped unroll itself did not survive the source-diverse gate.  On the
complete hot translation unit, self rose from 3,710,968,687 to 3,711,919,128
instructions (`1.000256117x`), while same-source GCC rose from 2,426,042,427
to 2,426,391,957 (`1.000144074x`).  The normalized ratio therefore regressed
to `1.000112026x`, and the absolute gap rose from `1.529638825x` to
`1.529810185x`.  All generated objects remained exact at `fa3fd1899...`.
Both implementations were removed before README or test movement; this is
another measured case where a large isolated loop win destructively
interferes with the complete producer.

### Rejected argument-memory LowIR boundary restoration

The next prototype tested the contract extension needed to distinguish a
call that may write arbitrary memory from one whose writes are confined to
its pointer arguments. It restored the previously removed parameter facts
`capture=nocapture` and `access=read|write`, added the function effect
`effects=argmemonly`, carried all three through typed LowIR text and compiler
objects, and raised the compiler-object version. Source lowering emitted the
facts for the supported memory builtins rather than matching compiler source
or symbol spellings in the optimizer.

The O2/O3 memory-GVN consumer used the facts conservatively. A local slot
passed to a `nocapture` boundary remained a local memory class; an
`argmemonly` call invalidated every class rooted at a writable slot argument,
preserved classes rooted at distinct slots, and retained the ordinary unknown
memory barrier for any nonlocal or unresolved writable argument. A focused
handwritten probe demonstrated the intended relationship: a load from the
read-only source slot was reused across a copy call, while the destination
load remained after the call. Text emission and O0 LowIR replay also
round-tripped the facts.

The compiler population was zero. The tokenizer object was byte-identical to
the retained checkpoint. More decisively, a fresh explicit-32-way G1/G2
compiler build produced identical `.text` for all 221 objects and an
identical linked compiler at
`01eaef45d89020d0202b8a8fb6208b306694e4423076840829b44a0f714c2c15`.
The new consumer therefore changed no compiler code at all. The prototype,
object-version bump, and transport fields were removed, so PA13 continues to
exclude the metadata as required by the LowIR minimization audit.

A future retry needs a demonstrably populated producer, such as a sound
interprocedural body summary or a source-level semantic boundary used by real
code, before changing PA13. Parser/serializer support and a synthetic
handwritten optimization are not sufficient justification by themselves.

### Retained complete parameter-object memory boundary

The successful successor supplies the population and semantic justification
missing from `D.argmem-boundary` without restoring that rejected surface.
PA13 now admits the single positive pointer fact `object_bytes=N`: the pointer
denotes the beginning of an N-byte complete semantic object region, and a
pointer reached from it and retained or returned through that boundary remains
inside the region.  It is not a claim that the callee is `nocapture`, readonly,
write-only, or argument-memory-only.  Thus ordinary effect and capture facts
continue to come from actual LowIR bodies, while the extent only bounds an
otherwise conservative escape.  Omission remains the representation for an
ordinary pointer or an incomplete object.

This is an O0 source-language/object-model fact rather than hidden optimizer
input.  PA17 lowering emits it for complete implicit object parameters,
class-reference and by-address arguments, and indirect result storage.  The
same fact is carried on indirect call signatures, including pointer-to-member
calls, and survives textual LowIR and compiler-object replay.  Completeness is
checked before querying layout, so incomplete classes retain the prior O0
path.  Compiler-object version 6 records the field.  The existing shared ABI
predicate owns the complete-boundary decision; no parallel native-only
preparation fact or compiler-source identity was added.

At O3, a whole-program analysis derives exact per-parameter write effects and
bounded capture interval sets from function bodies.  It may infer an exclusive
parameter only when every call of an internal, non-address-taken function
supplies a private object or an already exclusive parameter.  Capture sets
preserve holes rather than collapsing to one bounding interval.  Direct body
effects take precedence at a known call; the declared extent is only the
fallback for an unresolved effect.  The memory-GVN consumer therefore reuses
a load only across disjoint retained/write intervals.  It also understands a
finite nonnegative dynamic offset, including a safe integer mask, when the
scaled access cannot wrap or leave the complete object.

The useful memory rewrite exposes three general O3 cleanups.  A dominated
integer use may consume a single-predecessor equality fact, except at calls
where the operand may denote by-address storage.  Literal loop-carried stores
may feed the existing scalar forwarding relation, after which an exactly known
loop-phi edge may be threaded when successor phis require no repair.  Finally,
up to 64 positive constant-offset parameter addresses may be rematerialized in
their call-free use segments instead of remaining live across a call.  EH,
volatile and atomic operations, overlap, unknown offsets, external/indirect
effects without an extent, address observability, object copies, zeroing,
varargs changes, and exhausted budgets remain barriers.

The first complete native screen found a measurement-path regression rather
than a generated-code regression.  The exclusivity fixed point rescanned every
instruction for each possible callee, making `program_lowerer.cpp` take 20.57
seconds versus 7.37 seconds at the retained checkpoint and stretching the
32-way critical path to about 38 seconds.  The final implementation indexes
all calls once and keeps a reverse callee index.  It reuses that index for
capture and effect propagation, making every round proportional to the call
graph rather than the product of functions and instructions.  On the largest
translation unit, the result is byte-identical to the slow prototype and takes
8.16 seconds with stats; 9,336 call sites converge in 4 nocapture, 5 capture,
4 exclusivity, and 9 effect rounds.  The complete program-memory analysis is
charged to `memory_gvn_ns` (305.3 ms there), and the population and round counts
are reported so this failure mode cannot hide outside ordinary telemetry.

Coverage follows the serialized contract and does not match a complete program
or compiler symbol spelling.  PA13 has valid transport plus zero and
nonpointer rejection controls.  PA17 structurally discovers complete ordinary,
implicit-object, by-address/reference, indirect-result, indirect-call, and
incomplete boundaries and checks O0 behavior.  PA37 discovers the relevant
load/call/interval relationships and checks O0--O2 isolation, exact-body and
bounded fallback effects, disjoint and overlapping captures, gaps, masked and
unbounded indexes, direct/atomic/zeroing barriers, equality storage identity,
loop closure, stats, replay, and behavior.  PA38 discovers the post-call field
address relationship, then checks O3 rematerialization, pressure/stack
nonregression, replay, and behavior.  These are student-implementable
structural and behavioral properties, not fixture-content recognition.

The final common-input deterministic oracle compares the prior retained
producer with the version-6 indexed producer and refreshes both host controls.
Self falls from 3,710,732,416 to 3,621,818,198 instructions
(`0.976038634x`, -2.396137%).  GCC rises from 2,425,759,535 to 2,427,489,853
(`1.000713310x`), yielding `0.975342912x` normalized (-2.465709%) and moving
the self/GCC gap from `1.529719810x` to `1.492001375x`.  Clang rises from
3,021,419,141 to 3,022,985,108 (`1.000518289x`), yielding `0.975533026x`
normalized (-2.446697%) and moving self/Clang from `1.228142222x` to
`1.198093298x`.  The three current producers emit the exact object hash
`09d9fdc0...`; the three checkpoint producers emit `fa3fd189...`.

Three alternating explicit-32-way pairs on the exact final executable also
improve both workloads.  O1 aggregate wall and CPU are `0.993193145x` and
`0.988085190x`; O3 aggregate wall and CPU are `0.993226899x` and
`0.988883011x`.  Every side repeats its own linked compiler exactly in all
three lanes.  The all-32 G1/G2 fixed point contains 221 objects per generation
and linked compiler hash
`30132bf283fb14479319c3103f17e839414bef9c3a0fcb64fac3687283f0a810`.
The linked implementation grows from 8,695,716 to 8,751,672 text bytes and
from 339,400 to 340,152 data bytes; that full cost is included above.
PA37 passes 190/190, PA38 passes 45/45, and the through-PA38 report passes
5,473/5,473.  PA37/PA38 debug and object-roundtrip lanes pass; all LowIR,
layout, rename, source-set, and ownership audits pass; and the PA39 file audit
has zero fatal findings with its 31 established advisories.

### Rejected external-write reuse across stable calls

The first follow-up reused the complete-object memory analysis without adding
another serialized LowIR fact. It computed a whole-program fixed point for
whether each internal function can write memory outside its own frame, then
allowed repeat-stable query values to survive calls whose only writes are to
callee-local slots. External and indirect calls still required an existing
`readnone` or `readonly` boundary, and volatile, atomic, parameter, global,
unknown, and unresolved writes remained barriers. Selection used only call
and memory structure; it did not inspect source, symbol names, or complete
program identity.

The proof was populated but too small. Static query reuses rose from 31 to 53
and removed 882,230 dynamic group-query calls. The exact fixed-point producer
grew 4,424 text bytes, while the common-input self Callgrind count fell from
3,621,818,198 to 3,609,969,619 (`0.996728555x`, -0.327144%). That is sound
progress but remains well below the 1% source-diverse retention floor.
`lowiropt` and PA37's existing 190 properties passed; no README or test
contract was added for behavior that was not retained. The prototype was
removed rather than accumulating another whole-program summary for a
sub-threshold result.

The same opportunity was re-evaluated after fast/slow function versioning,
this time together with a bounded join-`phi` extension for path-specific
stable-query results. The extension did not expose another surviving reuse:
after ordinary cleanup, the no-write call proof again removed exactly 882,230
dynamic group-query calls. Against the newer accepted producer, Callgrind
fell from 3,553,692,388 to 3,541,698,583 instructions
(`0.996624974x`, -0.337503%). Ten per-side pinned task-clock observations
were flat at baseline/candidate means of 991.785/991.605 ms
(`0.999819x`), and the linked candidate grew from 8,786,048 to 8,792,536 text
bytes. The join machinery therefore adds no realized population, and later
retained work has not materially changed the original rejection.

### Rejected compared-quotient constant-division encoding

The residual macro profile exposed one narrower successor to the rejected
broad retained-division prototype. MIR copy forwarding can leave an
immediately following comparison reading the architectural quotient in RAX,
but the existing constant-division encoder recognized only a result move or
return. A structural native prototype also accepted an adjacent two-operand
comparison that directly reads RAX and does not read RDX. It did not change
LowIR, MIR serialization, source lowering, or optimization-level policy.

The intended `AnnotateParentheses` loop replaced its `idiv 104` with the
existing magic-division sequence and grew 20 object text bytes. Across the
fixed-point compiler, the dose grew text by 30,316 bytes. G1 and G2 matched
all 221 objects and the linked compiler exactly at
`cc7c277fbe68f233b01a272c6cfa69ebaba603c5c59aa8c8127aaa11aad5a006`.
Twenty position-balanced software task-clock observations per side improved
self from 794.301 to 787.439 ms (`0.991360952x`), while the same-source
GCC-built control moved from 535.797 to 536.522 ms (`1.001352191x`), for a
promising but borderline `0.990022253x` normalized result.

The deterministic oracle contradicted that native window. Relative to the
external-write-only prototype, self Callgrind rose from 3,609,969,619 to
3,621,663,067 instructions (`1.003239210x`). The two experiments together
were effectively flat against the retained checkpoint at `0.999957168x`,
despite carrying both implementation and footprint costs. PA38's existing 45
properties remained clean, but the dose did not justify a new PA38 contract.
Both prototypes were removed. A future quotient optimization must shorten the
sequence or establish a stronger source-diverse native win; merely recovering
every hidden constant divide repeats the previously measured
retired-instruction regression.

### Current complete-workload matrix checkpoint

The first complete native matrix after `D.complete-object-memory` used fresh
self O1/O2/O3 and same-source GCC O1/O2/O3 producers. Each comparison is one
position-balanced ABBA block, every lane uses an explicit 32-way build, and
each cell contains two observations per producer. The fixed O1, O2, and O3
workloads each contain 221 objects. Within each workload all self and GCC
producers emit the same manifest and linked compiler:

| Workload | Manifest | Linked compiler |
| --- | --- | --- |
| O1 | `891cb5d15e24cc064ee6bbe6aff9bb2e632580be41133d4bd13f806a02ef8a1f` | `182e670fccec5ae60efdc07cd1e6bfbbaacedfbe3835f1a40d05d6e63f2b3821` |
| O2 | `94785f0fff7623f791cbb5854afc8b0273243356a3dad12dede85e2cd1a757fe` | `fb027c911e09da251d9d7e500519973ffcc6ee6dcc403b8b1e21a92815b3354c` |
| O3 | `9819456f89c0149f1575fdccdf6d04d6996a699b6b7f36ee63c16f304cb75bff` | `30132bf283fb14479319c3103f17e839414bef9c3a0fcb64fac3687283f0a810` |

The pair-local ratios are:

| Workload | Ratio | Wall | Aggregate CPU | Same GCC ratio | Normalized wall / CPU |
| --- | --- | ---: | ---: | ---: | ---: |
| O1 | self O2/O1 | `0.981739x` | `0.977720x` | `0.889954x` / `0.890990x` | `1.103135x` / `1.097342x` |
| O2 | self O2/O1 | `0.981031x` | `0.985111x` | `0.929568x` / `0.928782x` | `1.055363x` / `1.060648x` |
| O3 | self O2/O1 | `0.993572x` | `0.988435x` | `0.894231x` / `0.892353x` | `1.111091x` / `1.107673x` |
| O1 | self O3/O1 | `0.870853x` | `0.864519x` | `0.837747x` / `0.840538x` | `1.039518x` / `1.028530x` |
| O2 | self O3/O1 | `0.872822x` | `0.855679x` | `0.869012x` / `0.866371x` | `1.004383x` / `0.987659x` |
| O3 | self O3/O1 | `0.876790x` | `0.866759x` | `0.880547x` / `0.873533x` | `0.995734x` / `0.992245x` |

O2 now meets the hard raw no-regression floor on every workload, but its gain
is only 1.2--2.2% rather than 5%, and it remains 6.1--10.8% behind the GCC
producer-level transition after normalization. O3 improves complete native
CPU by 13.3--14.4%. It has reached normalized parity on the O2 and O3
workloads and is within 2.9% on O1, but another 5.6--6.7 raw percentage points
are needed for the 20% stretch target. The deterministic hot result of
`0.698859x` therefore remains a useful upper bound, not a substitute for the
complete native criterion.

The next O2 screen should promote an already retained, source-independent O3
transformation rather than inventing new LowIR surface. Fixed O1 workloads
separate generated-code quality from added O2 pass cost; only a successful
producer is then measured on requested O2 work. The strongest candidates are
terminal staged-object swaps, private-table prefiltering, and complete-object
memory analysis. Promote them individually in that order of implementation
cost, retaining only a structural dose that improves the complete normalized
matrix without eroding O3.

### Rejected O2 terminal staged-object-swap promotion

The first promotion screen changed only the existing terminal staged-object
swap gate from O3 to O2. It added no LowIR surface or implementation and left
requested-O1 output byte-exact. The resulting explicit-32-way O2-produced
compiler shrank by 104 linked text bytes. Twenty position-balanced software
task-clock observations per side on the frozen `preprocessor.cpp` compile
were consistently favorable: candidate/baseline was `0.994193750x` by the
mean of ten block ratios and `0.993608856x` by their median, with nine of ten
blocks favoring the candidate. All output objects matched exactly.

The complete requested-O1 workload did not preserve enough of that local
gain. One all-32 ABBA block reproduced the same 221-object manifest and final
compiler in every lane. Mean aggregate CPU moved from 886.030 to 883.185
seconds (`0.996789048x`), only a 0.3211% improvement and below the plan's 0.5%
close-result threshold. Wall time was scheduler-sensitive and unfavorable at
30.735 versus 31.370 seconds (`1.020660485x`). The original O3 behavior and
its PA37 coverage remain unchanged; the O2 gate prototype was removed without
moving the student contract. Continue with the private-table-prefilter dose.

### Rejected O2 private-table-prefilter promotion

The second promotion factored the retained grouped specializer so O2 could
consider only private structured tables; ordinary integer groups, readonly
strings, stable-prefix recording, and the O3 loop-priority wave remained
inactive. The PA37 private-table control produced the same one clone and four
redirected calls at O2 and O3. An explicit-32-way O2-produced compiler
completed 221 objects, shrank by 376 linked text bytes, and retained exact
requested-O1 output.

The focused native screen was strong. Twenty position-balanced software
task-clock observations per side moved from 951.385 to 921.774 ms
(`0.968875376x`); every output object was exact. The same-source GCC-O2
control was essentially flat at `0.998099976x`, giving a normalized hot ratio
of `0.970719767x`.

That concentration did not generalize enough to the complete compiler
workload. One all-32 self ABBA block reproduced the same 221-object manifest
and final compiler in every lane and measured `0.996111471x` wall and
`0.994711547x` aggregate CPU. Three forward-order GCC blocks were discarded
whole after a repeatable sustained-build late-lane slowdown contaminated one
lane. A clean position-balanced reverse block put GCC candidate/baseline at
`0.990854455x` wall and `0.999802750x` CPU. The resulting normalized ratios are
`1.005305537x` wall and `0.994907789x` CPU: only a 0.5092% normalized CPU
gain, with unfavorable normalized wall. The prototype was removed before
requested-O2 pass-cost testing or contract movement. The retained O3
private-table behavior and its student-facing structural/behavioral coverage
remain unchanged. Continue with complete-object-memory promotion.

### Rejected O2 complete-object-memory promotion

The third promotion moved only the existing whole-program parameter-memory
analysis from O3 to O2. O2 consumed the PA13 `object_bytes` fact for disjoint
load reuse, while equality propagation, literal loop-edge threading, and
parameter-address rematerialization remained O3-only. The focused PA37
control exercised the expected O2 memory-analysis population before its old
level-isolation assertion. No new LowIR fact or implementation was proposed.

An explicit-32-way O2-produced compiler completed 221 objects and shrank by
2,584 linked text bytes. Twenty position-balanced software task-clock
observations per side on the exact-output hot compile measured
`0.994102950x` by aggregate means and `0.995584988x` by block median, again in
the close-result band. The complete workload rejected the dose: one all-32
ABBA block reproduced a common 221-object manifest and final compiler, but
candidate/baseline was `1.005060398x` wall and `1.000807686x` aggregate CPU.
Because raw representative throughput already regressed, the prototype was
removed before GCC normalization, requested-O2 pass-cost testing, or PA37
contract movement. The bounded-memory analysis and its companion cleanups
remain O3-only.

### Rejected edge-live final-MIR color components

The first post-matrix D5 probe refined the retained final-MIR callee-saved
recoloring proof. The existing component builder joined two adjacent blocks
whenever both mentioned the same physical source color, even when completed
liveness proved that color dead across their edge. The prototype joined
blocks only across an edge on which the source was live out of the predecessor
and live into the successor. This let independent lifetimes choose independent
destinations without changing LowIR, serialized MIR, ABI rules, or any O0--O2
policy.

The refinement changed real O3 register choices in the large tokenizer caller,
but not the higher-ceiling `AppendUTF8` residue: that function retained its
208-byte stack allocation, five callee-saved registers, and 1,814-byte body.
The tokenizer object lost 42 text bytes and `Lexer::Run` lost 12 bytes. An
explicit-32-way G1 and G2 each completed all 221 compiler objects; the G2
compiler contained 8,001,138 text bytes, 816 fewer than the retained
8,001,954-byte producer after including the new implementation.

The deterministic complete-TU gate rejected the dose. Baseline and candidate
emitted the identical object
`09d9fdc0bdd901d35c4f46075a4109b1a0c29ddb51fd5a17428335a2379dabba`,
while Callgrind moved from 3,621,818,198 to 3,620,514,655 instructions
(`0.999640086x`, only -0.035991%). That is far below the 1% D5 floor and
shows that separating dead-edge color components is not a material source-
diverse allocator win. The prototype was removed before G3, native timing,
or PA38 contract movement. Further allocation work must split values that
are genuinely live through hot call/branch regions, not merely refine the
connectivity of already disjoint physical-color lifetimes.

### Retained bounded fast/slow versioning and sibling transfer

The next D5 dose separates a common call-free return corridor from one large
internal O3 function.  This does not require another PA13 LowIR fact.  Internal
linkage, direct uses, parameter representations, instructions, memory effects,
exception operations, slot-address uses, and calls are already serialized.
Recursion and address observability are derived from those uses and the TU call
graph.  Encoding any of those conclusions again would duplicate information
that PA37 can and must prove from the ordinary LowIR contract.  PA37 therefore
owns the versioning transform; the only new downstream representation is the
PA38 machine-level sibling transfer.

The bounded transform first performs a cheap per-function shape screen, so a
TU without a plausible candidate never pays to construct the call graph.  It
then considers internal, nonrecursive, non-address-observable, fixed-arity void
functions with at most six direct scalar parameters, 128--768 instructions,
at least four call instructions, and no `inline=free` permission.  The retained
entry-to-return corridor is call-free and at most 128 instructions, may expand
pure scalar diamonds, and must expose at least three bailout edges.  Every
bailout precedes an externally visible store; a private-slot store is admitted
only when its address is never taken.  At most one function per TU is selected,
preferring the largest slow-only body.  A complete non-inline internal clone
keeps the original body.  The original identity keeps the corridor, and all
bailouts converge on one block that calls the clone with the exact original
parameter tuple and returns.  A prevalidated phi/comparison cleanup removes a
private continuation only when both incoming edges can be threaded without
successor-phi repair.

PA38 recognizes the resulting final direct void call and return structurally.
When the enclosing function has no slots, dynamic stack, exception or variadic
state, its sole call passes the original zero-to-six direct scalar parameters
unchanged and in order, and both signatures are compatible, MIR records a
terminal sibling call.  ELF emission restores the complete frame and saved
register state and jumps directly to the target.  Changed arguments, local
storage, indirect or additional calls, stack arguments, and exceptional or
variadic boundaries retain the ordinary call/return path.

The PA37 property checks O0--O2 isolation, bounded selection, identity and
parameter relationships, one shared fallback, at least three bailout edges,
the retained guard and clone arms, phi cleanup, replay, and behavior without
matching a source name or whole-program text.  The PA38 property checks the
terminal transfer and argument-register relationships, guarded ordinary-call
forms, every optimization level, replay, and both fast and slow behavior
without fixing registers or a complete MIR dump.  The student READMEs describe
the same implementable structural contract.

The first native full-build measurements exposed destructive code-layout
interference in the compiler that implements the pass: inserting roughly 450
lines of new helpers before the established repeat-stable specialization code
displaced that existing hot implementation and produced repeated candidate
outliers near 820 aggregate CPU seconds.  A mechanically inert move of the new
helper family after the existing hot code restored stable throughput.  This is
why the source order is deliberate even though it has no LowIR or object-output
semantic effect.

Against the retained complete-object-memory checkpoint, deterministic frozen
compile instructions fell from 3,621,818,198 to 3,553,692,388
(`0.981190163x`).  A freshly rebuilt same-source GCC control was essentially
flat, 2,427,240,778 to 2,427,216,076 (`0.999989823x`), for a normalized
`0.981200149x` and a gap reduction from `1.492154479x` to `1.464102196x`.
Every lane emitted
`09d9fdc0bdd901d35c4f46075a4109b1a0c29ddb51fd5a17428335a2379dabba`.
The linked self producer grows from 8,751,672 to 8,786,048 text bytes and 160
data bytes.  In the selected hot function, the original identity becomes a
98-byte wrapper while the complete slow clone retains the 1,814-byte body.

Two explicit-32-way self full-build ABBA blocks reproduced the common
221-object manifest and exact final compiler.  Candidate/baseline was
`0.999102656x` wall and `0.992814333x` CPU in the first block, then
`0.995681900x` wall and `0.992333429x` CPU in the reversed block.  A same-source
GCC ABBA control was `0.998927901x` wall and `1.000506098x` CPU.  The normalized
self ratios are therefore `1.000174942x` wall and `0.992312127x` CPU for block
one, and `0.996750515x` wall and `0.991831466x` CPU for block two.  The
deterministic normalized 1.88% saving clears D5, and the stable 0.77--0.82%
normalized full-build CPU saving corroborates it despite one flat wall block.

Focused PA37 and PA38 controls, 5,473/5,473 tests through PA38, both serial
debug/round-trip lanes, `git diff --check`, the zero-fatal file audit (with its
32 baseline warnings), and the layout, rename, source-set, semantic, lowering,
and native-ownership audits all pass.  Explicit-32 G1/G2 is exact.  Retain this
dose; refresh the complete O1/O2/O3 matrix after the next checkpoint rather
than substituting this incremental result for the existing raw O3/O1 row.

### Rejected scalar fast/slow extension

The immediate follow-up re-evaluated the old grouped scalar fast-wrapper idea
because its marginal conditions had materially changed.  The general call
graph analysis, complete-frame sibling transfer, and layout-stable helper
implementation are now retained, so the retry needed only scalar-return
fallback support and a second bounded candidate slot.  The experimental small
class required an internal non-hinted 24--127-instruction scalar function, at
least one call, a call-free return corridor, and at least one pre-effect
bailout.  Two functions and 1,024 cloned instructions per TU bounded the
combined large/small dose.  No LowIR contract addition was involved.

The compiler population included the intended 32-instruction specialized
query called 11,498,094 times.  It became a 47-byte fast wrapper around its
unchanged 116-byte body, and the frozen output remained exact at
`09d9fdc0...`.  The explicit-32 G1 producer grew from 8,786,048 to 8,809,908 text
bytes and by 16 data bytes.  Deterministic instructions nevertheless regressed
from 3,553,692,388 to 3,569,164,614 (`1.004353845x`).

Call-edge counts explain the reversal: only 4,342,853 calls used the proposed
fast arm, while 7,155,241 calls took the slow edge.  The fast arm removes three
native instructions, but the majority path adds the wrapper-to-clone transfer.
Inclusive work in the query family consequently rose from 626,202,337 to
641,122,175 instructions.  This is a generated-code loss, not another unknown
compiler layout effect.  The prototype was removed before test or README
movement and the worktree returned exactly to the retained checkpoint.

### Rejected serialized prefix-consumer boundary

The next prototype tested the smallest explicit LowIR addition that could
make the majority-slow scalar query safe to optimize without duplicating its
slow arm. A fixed scalar function could carry `[prefix=consume]`, produced
from a source attribute and preserved through textual LowIR and compiler
objects. The promise identified a state-consuming operation compatible with
an available index-zero `[query=stable_prefix]` observation on the same typed
argument tuple. O3 deferred ordinary inlining of marked consumers, derived a
unique acyclic call-free consuming arm against a call-containing slow arm,
cloned only that arm, redirected a proved query/consume pair to it, and reused
the already observed scalar result. Selection was bounded to eight clones,
256 cloned instructions per translation unit, and 32 instructions per fast
arm. No filename, source spelling, or function-symbol match participated.

The boundary and transformation were self-host stable: an explicit-32-way G1
and G2 matched every compiler object and the final binary at
`8dbb59a78dc81c32890a2343d31254db2b11050eda4ba98b7e49cdd920ef7bfc`.
On the tokenizer reducer, the final O3 LowIR fell from 4,608 to 4,204
instructions and from 345,678 to 316,192 bytes; the temporary fast clone was
fully inlined and pruned. The implementing compiler nevertheless grew by
14,864 linked text bytes, from 8,786,048 to 8,800,912.

The deterministic complete hot compile rejected the new contract. Baseline
and candidate emitted the identical object
`09d9fdc0bdd901d35c4f46075a4109b1a0c29ddb51fd5a17428335a2379dabba`,
while Callgrind moved only from 3,553,692,388 to 3,553,043,913 instructions
(`0.999817522x`, a 0.018248% saving). That is far below the 1% D5 boundary
for a new serialized promise and cannot be described as the large
optimization needed to justify expanding PA13. The source attribute,
semantic and LowIR transport, compiler-object version, optimizer machinery,
and provisional fixtures were removed together. No PA13, PA17, PA37, or PA38
contract movement remains. The result also closes this specific
query/consume design: further work must target a source-diverse dynamic cost,
not add more metadata to expose this scalar edge.

### Rejected global one-shot inline-cap reduction

The refreshed self/GCC profile showed a superficially attractive layout
difference: GCC keeps `ScanPunctuator` separate and its surviving scalar query
calls are overwhelmingly fast, while the accepted self compiler folds
`ScanPunctuator` into `Lexer::Run` and leaves 7,155,234 of 11,498,094 grouped
query calls taking the refill arm. The existing public `once-cap` control
tested whether that discrepancy was simply excess one-shot inlining.

Caps 64, 96, and 256 all retained a separate 2,934-byte
`ScanPunctuator`; they reduced `Lexer::Run` from 9,313 bytes to 1,166, 2,266,
and 7,075 bytes respectively. Each output remained exact, but isolated
tokenizer probe compilers regressed the pinned native hot compile: cap 64 was
+3.185% wall/+2.027% user, cap 96 was +2.548%/+3.356%, and cap 256 was
+2.532%/+2.027%. The smaller outline loses useful locality or introduces
calls whose cost exceeds the footprint reduction. Global cap changes remain
rejected; follow-up work must identify individual dynamic sites rather than
infer profitability from GCC's final partition.

### Rejected post-state refill-site expansion

The next prototype tested the inverse of the rejected scalar fast wrapper
without adding LowIR metadata. A bounded O3 matcher recognized an internal
index-zero stable-prefix query whose three-block body guards one refill call
with a constant parameter-relative load. In natural loops it selected only
calls for which every nearby predecessor path either stored the guarded field
or invoked another operation on the identical parameter-derived object. The
runtime guard retained correctness; the structural relationship was used only
for profitability. Selection was source-name independent and capped at 12
sites, 48 query instructions, and 128 predecessor instructions.

Two generated-code forms were screened through isolated probe links against
the accepted producer. Fully expanding six selected queries removed those
calls, grew `Lexer::Run` by 558 bytes and the tokenizer object by 560 bytes,
and improved 12-per-side pinned task-clock only to `0.996629370x`
(-0.337063%). A refined inverse split expanded the guard/refill arm but sent
the fast edge to one shared 30-byte fast-return helper. At six sites it grew
`Lexer::Run` by 684 bytes and the object by 740 bytes for `0.994878446x`
(-0.512155%); widening to 12 sites grew the function by 1,199 bytes and the
object by 1,780 bytes while falling back to `0.997201623x` (-0.279838%). Every
candidate emitted the accepted hot object hash
`09d9fdc0bdd901d35c4f46075a4109b1a0c29ddb51fd5a17428335a2379dabba`.
All variants miss the 1% gate before implementation footprint, so the
matcher, selected inliner, and transient clones were removed without PA37 or
PA38 contract movement.

### Rejected grouped loop-leaf inlining

The next screen revisited the late grouped specializations themselves.  Eight
surviving scalar clones were only 11 LowIR instructions each, call-free, and
shared by loop and non-loop users.  Three of them were called from the central
loop in `AnnotateParentheses`; those calls accounted for about 19.34 million
flat instructions, while GCC had folded the equivalent predicates into the
loop.  A producer-selected late-inline wave therefore admitted only newly
created, non-EH scalar grouped clones of at most 12 instructions, and only at
call blocks proven to belong to natural loops.  Ordinary late-inline caller
and translation-unit budgets still applied.  No source name or program-text
match participated.

Inlining every use grew the macro object by 2,491 text bytes and improved an
isolated producer only `0.998381287x`.  Restricting the dose to natural-loop
sites removed the three calls from `AnnotateParentheses`, but grew that body
from 525 to 776 bytes and the macro object by 1,815 text bytes.  Its isolated
12-per-side pinned task-clock result was `0.987787789x`, so it advanced to a
fresh explicit-32-way G1.  The complete implementing compiler grew 8,664
linked text bytes.  Ten five-compile pinned ABBA batches measured
`0.991382860x` by aggregate means, `0.991153185x` by trimmed means, and
`0.991903553x` by medians: a real but sub-1% native saving.

The deterministic escalation rejected the dose.  Candidate and baseline
emitted the identical object
`09d9fdc0bdd901d35c4f46075a4109b1a0c29ddb51fd5a17428335a2379dabba`,
but total Callgrind instructions increased from 3,553,692,388 to
3,557,188,669 (`1.000983853x`).  The transformed
`AnnotateParentheses` inclusive cost did fall from about 87.92 million to
84.40 million instructions, but duplicated predicate code, pass work, and
producer footprint offset it.  The selected-inliner API and clone tracking
were removed without PA37/PA38 movement.  The follow-up target is the more
general native inefficiency exposed by this experiment: each surviving
45-byte predicate contains redundant narrow-value extensions and a
constant-register compare.

### Rejected grouped-predicate native cleanup

The grouped-leaf result prompted a source-independent native follow-up.  At
O2 and O3, a dying register-backed integer input was allowed to carry its
conversion result, the existing adjacent-normalization pass composed a folded
narrow-load proof through the immediately following wider normalization, and
an encodable 32- or 64-bit comparison literal remained an immediate instead of
being materialized in `rdx`.  These are machine-selection properties only;
the prototype did not change LowIR or add hidden source/program recognition.

On the macro translation unit, the conversion rule removed 94 MIR
instructions, immediate comparison selection removed another 142, and
normalization composition brought the total reduction to 281.  Each hot
grouped predicate's true arm lost the load-to-register copy, both redundant
extensions, and the constant-register move.  A fresh explicit-32-way G1
contained all 221 objects, shrank the implementing compiler by 22,256 linked
text bytes, successfully compiled the frozen TU, and emitted its exact
accepted object hash
`09d9fdc0bdd901d35c4f46075a4109b1a0c29ddb51fd5a17428335a2379dabba`.

The source-diverse saving was nevertheless below the retention floor.  Ten
per-side pinned software `task-clock` observations measured baseline versus
candidate means of 786.957/787.852 ms (`1.001137292x`), medians of
788.535/787.535 ms (`0.998731825x`), and trimmed means of
787.374/787.911 ms (`1.000682649x`).  Hardware instruction events were
unavailable.  Because the three-instruction dominant-arm saving still has a
deterministic ceiling of about 0.97%, two complete explicit-32-way O3 ABBA
blocks were used instead of a long software-profiler run.  A periodic
position-two host outlier struck the candidate in the first block and the
baseline in the reversed block.  Excluding those two label-independent
outliers, three candidate lanes averaged 721.987 seconds user and 27.123
seconds wall against 726.027 and 27.377 for baseline: `0.994436x` user and
`0.990743x` wall.  Every repeated producer yielded the same 221-object
manifest and final hash.  The source-diverse CPU saving is real but only
0.56%; the prototype was removed without PA38 contract movement.

A more aggressive terminal fold was also rejected for correctness.  It wrote
the comparison byte directly to `al` and removed the three-instruction
widen/transfer/narrow return tail, saving another 42 macro MIR instructions.
The resulting explicit-32-way G1 built, but failed the first frozen-TU compile
with `invalid internal paste sequence`.  Internal narrow scalar returns
currently provide a normalized full-register value to their callers; the
machine ABI's low-byte observation alone is therefore not a sufficient proof
for this rewrite.  Removing only that fold restored the exact frozen output.

### Rejected terminal store/load return forwarding

The majority-slow grouped query was then examined at the LowIR memory layer.
Its refill edge obtains a scalar, stores it into a bounded complete-object
region, and enters a terminal merge which recomputes the same indexed address,
reloads the scalar, and returns it.  A general O3 prototype returned the
already stored SSA value directly on such an incoming edge.  It recursively
proved the two address calculations equivalent, proved every repeated address
input load stable across the direct edge, and rejected volatile/debug accesses,
EH, calls, atomics, overlapping intervening stores, nonterminal loads, and
joins with observable prefix work.  All of those facts came from ordinary
serialized LowIR and the retained PA13 `object_bytes` boundary; no additional
LowIR field, source annotation, or native-only preparation fact was needed.

The intended specialized query changed exactly as expected.  Its refill block
now returned the value from `TranslationCursor::Next` after updating the
queue, while the populated edge retained the ordinary indexed load.  Native
code removed four instructions from each of the 7,155,241 refill calls.  The
extra direct epilogue grew the helper from 116 to 121 bytes, however, and the
deterministic ceiling was only about 28.62 million instructions, or 0.81% of
the 3.55-billion-instruction workload.  The general proof added 11,662 text
bytes to `memory_gvn.o`; a fresh explicit-32-way 221-object G1 grew by 10,592
linked text bytes (9,616 in its ELF `.text` section).

The hot translation unit remained deterministic at the accepted
`09d9fdc0...` object hash, and the existing PA37 and PA38 suites passed
190/190 and 45/45.  Ten-per-side pinned software `task-clock` observations
measured baseline/candidate means of 788.247/790.912 ms (`1.003380920x`),
medians of 789.350/790.560 ms (`1.001532907x`), and trimmed means of
788.534/790.070 ms (`1.001948236x`).  The prototype was removed before README
or property-test movement.  This result answers the LowIR-contract question:
the missing transformation was derivable from the current contract, but its
complete dynamic opportunity did not justify retaining the machinery.

The terminal-memory prototype was also screened as a bundle with the
independent rejected grouped-predicate native cleanup.  Relinking only their
disjoint implementation objects from the two exact G1 builds produced a valid
compiler just 576 ELF `.text` bytes larger than baseline and the same hot
object hash.  Ten balanced observations per side were still flat:
candidate/baseline mean, median, and trimmed `task-clock` ratios were
`1.001780636x`, `0.999218504x`, and `1.002306278x`.  Later lanes saw rising
host load, but it struck both labels and did not conceal a stable one-percent
direction.  Reconstructing or promoting the combined source dose was therefore
unwarranted.

### Rejected terminal call-result residency bundle

One stronger retry combined the terminal store/load proof with a final-MIR
fact the earlier LowIR-only screen could not exploit.  After replacing the
refill edge's jump with a direct return, the completed terminal MIR can prove
that a narrow call result is observed only by same-width stores and the final
return.  A bounded prototype retained that value in the ABI result carrier,
relocated one explicitly defined conflicting temporary into an unused
caller-saved register, and removed the adjacent result move and normalization.
It rejected debug-variable functions, implicit register relationships,
intermediate control barriers, wider observations, redefinitions, and tails
without a safe scratch color.  The LowIR half conservatively treated distinct
pointer parameters as potentially aliasing and required stable recursively
equivalent address inputs plus disjoint bounded intervening writes.  Both
halves used existing serialized LowIR and MIR facts; no PA13 addition or
native-only preparation metadata was required.

The combined hot refill edge removed six dynamic instructions instead of four,
and the selected helper shrank from 116 to 114 bytes instead of growing to 121.
Its slow edge executes about 7,155,234 times, giving a static upper bound near
42.93 million instructions, or 1.21% of the accepted 3,553,692,388-instruction
workload.  The implementation cost remained substantial: `memory_gvn.o` grew
from 145,193 to 161,577 text bytes, `native/mir/optimize.o` from 115,248 to
121,056, and the explicit-32-way G2 producer from 8,786,048 to 8,806,928 linked
text bytes.  The candidate's hot object was deterministic at
`4ace790303069b5dc80b4330f3285697864b454578a194d376d8d1da4d31a120`.

The first implementation ran full function analysis before its shape check;
moving the cheap terminal-load prefilter ahead of CFG and address construction
removed that avoidable compiler work.  After an explicit-32-way G1/G2 rebuild,
ten balanced pinned observations per side measured baseline/candidate means of
988.369/980.081 ms, medians of 987.100/979.415 ms, and trimmed means of
987.954/979.735 ms.  The resulting ratios are `0.991614x`, `0.992215x`, and
`0.991681x`: a stable 0.78--0.84% native improvement, but still below the 1%
source-diverse retention floor.  PA37 remained 190/190, the earlier combined
screen kept PA38 45/45, and no stale Valgrind process was present.  The complete
prototype was removed without README or test movement; its stronger result
supersedes, but does not overturn, the earlier terminal-memory rejection.

### Rejected read-only scalar shadow plus compared quotient

The next experiment tested whether the two previously marginal loop
improvements became worthwhile as one coherent dose.  An O3-only whole-body
proof found internal or weak by-address scalar parameters whose complete
`object_bytes` region was observed only by exact-width nonvolatile loads and
zero-offset pointer carriers.  At a qualifying caller, the argument value was
copied into a private call-only shadow immediately before the call.  The
caller's original scalar slot then became address-free and ordinary slot
promotion kept its loop induction value in SSA.  This used the existing PA13
complete-object contract and callee body; it required no new LowIR field.  The
dose also restored the narrow native encoder experiment which consumes an
architectural quotient read directly by the adjacent comparison.

The intended macro loop changed structurally: its induction slot became a
loop phi, three repeated index multiplications were shared, the slow vector
growth edge alone stored the value to an addressed shadow, and the hot
`idiv 104` became the existing magic-division sequence.  The selected
`AnnotateParentheses` body shrank from 525 to 515 bytes, but additional sound
scalar populations grew the macro object by 1,195 text bytes and the
explicit-32-way G1 producer by 51,004 linked text bytes.  The PA37 suite
remained clean at 190/190.

Ten balanced software `task-clock` observations per side initially looked
just strong enough: self means were 784.801/780.350 ms (`0.994328x`), while
same-source GCC means were 532.355/534.866 ms (`1.004720x`), for a normalized
`0.989660x`.  The deterministic check rejected that conclusion.  Complete
hot-workload Callgrind rose from 3,553,692,388 to 3,561,135,639 instructions
(`1.002094x`), and `AnnotateParentheses` itself rose from 81,243,910 to
82,339,040 inclusive instructions (`1.013480x`).  Scalar residency therefore
does not cancel the previously measured retired-instruction cost of replacing
one hardware `idiv` with a longer magic sequence.  Both prototypes were
removed without PA37/PA38 README or property movement.  A successor should
target the stable-query fast/slow body, whose larger slow-edge and unconditional
save costs can clear the deterministic floor together, rather than retrying
this division encoding.

### Retained terminal-query slow-suffix extraction and scalar sibling transfer

The closing O3 increment targets that stable-query body without adding a new
PA13 field.  Existing serialized LowIR already provides the required direct
call graph, scalar types, complete parameter-object extents, instructions,
stores, loads, and control flow.  The new bounded PA37 transform recognizes an
internal nonrecursive three-block scalar query with at least eight direct call
sites, a guard selecting a pure terminal load/return arm or one straight-line
slow arm, and a slow call result stored into the recursively equivalent
terminal load address.  Interval reasoning over literals, copies, masks, and
nonnegative products proves the access stays in the PA13 `object_bytes`
boundary.  Every dependency load must remain stable through later stores;
unknown, overlapping, volatile, exceptional, address-observable, recursive,
or unbounded forms are rejected.  At most one function per translation unit
is selected.  Its slow suffix becomes one internal non-inline scalar helper,
and the wrapper's slow arm becomes an exact call/return.

PA38 extends the existing complete-frame sibling rule to exact integer and
pointer scalar returns.  A scalar wrapper must have multiple return arms and
its terminal slow call must pass the original direct scalar parameters in
order.  Final MIR then performs two independently proved cleanups: a one-block
helper may keep a call result in the ABI return carrier through same-width
stores and the final return, and a strict three-block sibling wrapper may keep
its incoming ABI parameter live instead of creating an eager home.  Explicit
carrier conflicts are recolored only to a globally unused scratch register.
The sibling opcode is now a CFG control barrier, preventing a false physical-
layout fallthrough edge.  Slots, stack arguments, debug variables, EH,
varargs, changed results or arguments, wider observations, unsafe conflicts,
and nonterminal uses retain the ordinary path.

The PA37 README and role-discovered property control cover the positive split,
bounded dynamic address, shared slow helper, O0--O2 isolation, replay, stats,
behavior, and unbounded/overwritten negatives.  The PA38 README and structural
control discover the helper, producer, wrapper, changed-result guard, incoming
parameter, and ABI result carrier from their relationships rather than names,
fixed registers, or complete MIR text.  They prove ordinary calls below O3,
the O3 sibling edge, absence of false fallthrough, both MIR retention
properties, replay, counters, and slow/fast behavior.  The fixture reports one
split, eight call sites, twelve extracted LowIR instructions, one retained
terminal result, and one retained sibling parameter at O3; all counters are
zero below O3.

The final deterministic common-input result falls from 3,553,692,388 to
3,505,367,910 self instructions (`0.986401615x`).  The freshly rebuilt
same-source GCC control moves from 2,427,216,076 to 2,427,456,273
(`1.000098960x`), so the normalized ratio is `0.986304010x`, a 1.369599%
improvement.  The self/GCC frozen-TU gap falls from `1.464102196x` to
`1.444049868x`; all three producers emit the exact object hash
`09d9fdc0bdd901d35c4f46075a4109b1a0c29ddb51fd5a17428335a2379dabba`.
The linked self compiler grows from 8,786,048 to 8,839,600 text bytes and from
340,312 to 341,480 data bytes.

The first full O1-workload screen caught implementation-layout interference:
placing the new object in the middle of the source list displaced every later
compiler object and made a reversed block about 5.2% slower in aggregate CPU,
even though the O1 output was exact and the O3 generated-code oracle improved.
The final mechanically inert source-set order preserves every established
object position and appends the new implementation object.  With that order,
a reversed explicit-32-way block emits the same 222-object result and linked
hash on every lane.  Candidate means are 26.545 seconds wall / 762.220 seconds
aggregate CPU versus 27.830 / 790.995 for baseline; changing host load makes
the full mean too favorable for a precise claim, but the clean adjacent pair
alone is `0.990363232x` wall and `0.983188069x` CPU.  Thus the deterministic
gain is corroborated and the O1 regression is removed rather than hidden.

The complete report passes 5,473/5,473.  PA37/PA38 debug and debug object-
roundtrip lanes pass; the LowIR contract, source-set, diff, and PA39 file
audits pass with zero fatal findings and the established 32 warnings.  Fresh
all-32 G1/G2 builds match every object and the linked compiler at
`5846b8b045a32b3969a5fb3261b01e96881f806eb8ddf3235ea09bc4f62ab575`.

This closes the O3 work.  The original complete matrix put O3 13.3--14.4%
ahead of the same-revision O1 producer and at normalized parity on the fixed
O2 and O3 workloads.  The subsequently retained fast/slow versioning and this
increment improve that position further.  The full-workload 20% stretch was
not separately demonstrated, and O2's normalized deficit remains recorded.
No untried lead has evidence comparable to the two retained structural
partitioning wins: global inline caps, broad or partial query inlining,
isolated memory forwarding, register pinning, local peepholes, and strength
reduction have measured dispositions.  Further work should begin with a fresh
profile and require another source-diverse one-percent population rather than
continue this plan speculatively.

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
| D.readonly-string-groups-3 | reuse the retained grouped specializer for the three most frequent readonly-string values | none; stopped before contract movement | three independent clones; deterministic output exact | normalized hot Ir -0.917%; full aggregate user -0.36%, total CPU -0.25% | exact hot and repeated full-build outputs; dose superseded | rejected at this breadth; complete-workload gain missed the 0.5% close-result threshold |
| D.readonly-string-groups | specialize every profitable bounded internal readonly-string group and fold exact serialized bytes | PA37 README plus O0-O2 isolation, multi-group structure, signed-byte/phi/replay/behavior positives, and replaceable/writable/unterminated/dynamic/volatile/indexed negatives | 56 calls, eight clones/88 instructions; macro LowIR +1,227 bytes, macro text -688; fixed-point producer +16,248 text vs prior accepted | self Ir `0.985097x`; GCC `1.000014x`; normalized `0.985083x`; gap `1.624706x` to `1.600470x`; full aggregate wall/user/CPU `0.976414x`/`0.988666x`/`0.989585x` | PA37 188/188; PA38 45/45; 5,471/5,471; PA37/38 debug/round-trip clean; zero-fatal audit; 221-object G2/G3 exact | retained in `85bac3e1`; broader reuse clears deterministic and complete-workload gates |
| D.stable-prefix-query | serialize a general stable-prefix query promise and reuse established lower prefixes at O3 | PA13/PA17/PA37/PA38 READMEs plus shape, source-production, level, relationship, barrier, replay, object-roundtrip, stats, and behavior properties | object version 5; tokenizer query fact; 6.60M dynamic full calls removed; fixed-point producer +13,556 text bytes | self Ir `0.975652x`; GCC `1.000117x`; normalized `0.975538x`; gap `1.600470x` to `1.561319x`; full raw wall/CPU `0.969113x`/`0.984335x`, GCC-normalized CPU `0.979699x`, Clang-normalized CPU `0.985227x` | PA37 188/188; PA38 45/45; 5,472/5,472; debug/round-trip and all audits clean; exact 221-object G1/G2/G3 | retained in `d54e7e03`; contract-backed general reuse clears deterministic and complete-workload gates |
| D.stable-prefix-fast-wrapper-retry | split one repeat-stable query into a bounded fast wrapper and shared slow body, then inline all callers or only the largest caller population | none; rejected before contract movement | all: 26 calls and producer +12,544 text; narrow: 16 calls, 7/2/1 left, producer +14,456 text; hot objects exact | all self Ir `1.000798x`; narrow self `1.000846x`, GCC `1.000015x`, normalized `1.000831x`; gap `1.561319x` to `1.562616x` | explicit-32-way G1 for both forms; deterministic self/GCC hot objects; prototypes removed | rejected; current stable-prefix baseline reproduces the earlier fast-prefix loss under the corrected normalized metric |
| D.forwarded-boolean-inversion | thread a one-use zero/one equality or inequality after a forwarded Boolean truncation onto its acyclic phi predecessors | none; rejected before contract movement | hot lookup 377 to 324 bytes; macro -48 text; producer -1,576 text; hot objects exact | self Ir `0.997630x`; GCC `1.000005x`; normalized `0.997625x`; gap `1.561319x` to `1.557611x` | explicit-32-way G1 and deterministic self/GCC hot controls; prototype removed | rejected alone; sound 0.2375% source-diverse saving remains below the 1% floor |
| D.terminal-staged-object-swap | replace a structurally complete terminal private-slot object swap with aligned scalar exchanges | PA37 README plus O0--O2 isolation, aggregate/field-wise positives, incomplete/volatile/escape/nonterminal negatives, identical-address and ordinary behavior | deque helper 241 to 157 bytes and loses 160-byte frame/dead initialization/three `rep movsb`; producer +20,164 linked text; hot objects exact | self Ir `0.979693x`; GCC `0.999983x`; normalized `0.979709x`; gap `1.561319x` to `1.529639x` | PA37 189/189; PA38 45/45; 5,472/5,472; zero-fatal file audit; exact explicit-32-way 221-object G1/G2 and final | retained in this checkpoint; existing PA13 `copyobj` semantics supply the whole correctness proof |
| D.loop-scalar-reference | clone a bounded read-only by-address scalar parameter by value so repeated loop callers can promote their induction slots | none; rejected before contract movement | two macro calls redirected; induction slots removed; `AnnotateParentheses` 525 to 490 bytes; macro +683 text | self Ir `0.998472x`; GCC `1.000227x`; normalized `0.998245x`; gap `1.529639x` to `1.526954x` | exact hot object; explicit-32-way G2/G3 fixed point exact; prototype removed | rejected; sound source-independent slot recovery is only a 0.176% normalized gain |
| D.nested-loop-phi-residency | admit loop-carried phis that are also invariant relative to nested loops to the preserved-register planner | none; rejected before contract movement | suffix parser 1,959 to 1,972 MIR, scalar loads/stores 421/344 to 427/347, frame bindings 391 to 401; producer -701 text bytes | self Ir `1.000191x`; GCC `0.999999x`; normalized `1.000191x`; gap `1.529639x` to `1.529932x` | exact hot outputs; explicit-32-way G1/G2 exact; prototype removed | rejected; whole-loop register pinning destructively displaces more valuable short intervals |
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
| D.trivial-bool-diamond | bypass an exact O3 two-arm constant-Boolean phi diamond after measuring its self-applied G2 form | PA37 README plus O0--O2 isolation, structural guards, serialized replay, and behavior property; PA38 unchanged | token move constructor 234 to 206 bytes; producer -5,856 text bytes; hot Ir -0.8644%; O1 output exact | O3 self mean -2.12% wall/-0.97% CPU and block median -2.40%/-0.36%; GCC Ir -0.011%; normalized Ir -0.853% | PA37 188/188; PA38 45/45; 5,471/5,471; debug/round-trip and zero-fatal audit clean; frozen exact; all-32 O2 and O3 G2/G3 exact | retained after G1/G2 re-evaluation; initial G1-era rejection superseded |
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
| D.conditional-copy-elision | serialize conditional-prvalue copy/move elision permission and consume it after a private-slot/CFG/lifetime proof | PA13/PA17/PA37 README plus syntax rejection, source emission, O0/O1 isolation, ordinary/EH positive, unmarked/source-escape/destination-observation negative, stats, replay, fixed point, and behavior properties | `AddSourceToken` 373 to 297 optimized LowIR lines and 1,245 to 899 native bytes; macro object -454 text bytes; exact 221-object fixed point; linked implementation cost +2,652 text bytes | self/GCC normalized Ir `0.988501x`; gap `1.643606x` to `1.624706x`; current self/Clang `1.304331x`; normalized full O3 CPU paired/aggregate `0.989334x`/`0.989727x` | 5,471/5,471; PA37/38 debug/round-trip and all audits clean; zero-fatal file audit; self/inception manifest and final exact; three balanced self/GCC pairs | retained; serialized contract reproduces the deterministic win and clears 1% normalized native CPU |
| D.transient-swap-staging | remove a complete write-only transient local and its first overwritten copy from an adjacent three-copy object swap | none; rejected before contract movement; a retained form would require positive/negative PA37 properties plus unrelated-call-argument survival | two real 80-byte swap populations simplified; macro body -115 native bytes; self producer +9,072 text bytes; hot Ir -0.6710% | self mean wall/CPU -0.20%/-0.05%; GCC +0.36%/-0.26%; normalized wall -0.56% but CPU +0.21%; paired normalized CPU effectively flat | corrected candidate exact on hot output and all 219 full-workload objects/final; initial self-move bug diagnosed without fixture changes; prototype removed | rejected; dynamic instruction saving did not clear the normalized close-result CPU gate |
| D.merge-phi-register | reuse the exact local-phi interval proof for acyclic O2/O3 merge phis | none; rejected before contract movement | G1 +256 text and O1 output exact; G2 self-applied to -74,224 text but changed O1 MIR/object despite exact LowIR | G1 hot Ir +0.0012%; G2 performance invalidated by miscompile | O0 exact; G2 repeat deterministic; G2-to-G3 failed across unrelated TUs; prototype removed | rejected as unsound; predecessor transfers need a stronger proof |
| D.terminal-address-load | fold a terminal `lea`/load/return into one representable indexed load | none; rejected before contract movement | two hot returns each -1 MIR/-3 bytes; G2 producer +1,952 text; hot Ir -0.4422%; O1 output exact | self wall/CPU 1.01030x/0.99976x; GCC 0.99918x/0.99882x; normalized 1.0111x/1.0009x | PA38 45/45; exact 219-object manifest/final; G1/G2 distinction verified; prototype removed | rejected; deterministic saving is normalized-flat and wall-negative |
| D.trivial-bool-repeat | repeat the retained Boolean-diamond fold to a bounded per-function fixed point | none; rejected before contract movement | nine more tokenizer diamonds removed; tokenizer -352 text; G2 producer -14,704 text; hot Ir -0.0143% | hot native flat; clean all-32 O3 CPU about +0.8--1.0%, reproduced with reversed order | deterministic 219-object outputs; candidate G3 hash recorded; prototype removed | rejected; cold size saving does not repay repeated O3 work/layout loss |
| D.zero-bounded-range | combine a private O3 `x < 0` / `x > C` signed rejection chain into one unsigned bound | PA37 README plus O0--O2 isolation, structural guards, phi repair, replay, and behavior property; PA38 unchanged | `AppendUTF8` 0x773 to 0x76b; tokenizer -16 text; G2 producer +3,760 text implementation cost | O1 Ir -0.1989%, normalized -0.2001%; O3 Ir -0.1971%, normalized -0.1950%; clean full O1 wall/CPU -3.75%/-0.71%, O3 -1.46%/-0.15% | PA37 188/188; PA38 45/45; 5,471/5,471; debug/round-trip clean; zero-fatal audit; all 219 G2/G3 objects and final hash exact | retained; prefilter and no redundant cleanup preserve raw O3 direction |
| D.dynamic-small-copy | use direct chunks for runtime copy sizes up to 16 bytes, retaining `rep movsb` otherwise | none; rejected before contract movement | hot token move 206 to 302 bytes; move self cost 156.00M to 181.91M Ir | hot requested-O3 Ir +0.6238%; native flat/slower | PA38 45/45 during experiment; G2/G3 exact; prototype removed | rejected; dispatch and footprint outweigh small-copy saving |
| D.addressed-scalar-slot | recover complete nonescaping scalar slots whose derived addresses have only safe uses | PA37 README plus positive/negative, level-isolation, bounded-stats, replay, and behavior property; PA38 unchanged | hot token move 206 to 185 bytes and frameless; final producer -56,288 `.text`; O0--O2 output exact | O1 self -3.17% wall/-1.26% CPU, normalized -4.85%/-1.33%; O3 self -0.94%/-1.04%, normalized -1.61%/-1.16%; hot normalized Ir -1.738% | PA37 188/188; PA38 45/45; 5,471/5,471; debug/round-trip and audits clean; all 219 G1/G2 objects and final exact | retained in this checkpoint |
| D1.repeat-stable-O2 | promote repeat-stable query reuse to O2 | none; rejected before contract movement | macro visits 609 functions but finds zero stable queries, signatures, or reuses | static rejection; added scan has no output benefit | direct O2 stats deterministic; threshold restored | rejected; remains O3-only |
| D1.zero-bounded-O2 | promote the zero-bounded signed-range fold to O2 | none; existing O3 contract remains | tokenizer -8 text bytes; fixed-point O2 producer +32 text bytes | 1.01304x wall / 1.01235x CPU versus addressed-slot O2 | exact fixed point and 219-object outputs; threshold restored | rejected; scan/layout cost exceeds the tiny O2 population |
| D1.addressed-boolean-terminal-O2 | reuse three bounded O3 reductions at O2 | PA37 README and role/behavior properties now require O2/O3; brittle exact fixture replaced by post-inlining property; PA38 contract unchanged | fixed-point O2 `.text` 7,961,170 to 7,885,106 (-76,064); final 219-object O1/O2/O3 outputs deterministic | fixed O1 self 0.955405x/0.962863x wall/CPU, normalized 1.078510x/1.091429x; fixed O2 self 0.945069x/0.963116x, normalized 1.068937x/1.086273x; O3 no-erosion 0.98255x/0.99683x | PA37 187/187; PA38 45/45; 5,470/5,470; debug/round-trip and LowIR/file audits clean; self and GCC O2/O3 exact | retained; raw O2 floor cleared, normalized parity still open |
| D2.cold-partition | group structurally raising O3 MIR behind a serialized suffix and emit eligible strong fragments in `.text.unlikely` | provisional PA38 README and structural/ELF/unwind/debug/behavior property passed, then removed with the rejected feature; PA37 unchanged | tokenizer 9 fragments/3,408 cold bytes/77 cross fixups; macro 3/444/7; linked producer +30,192 `.text` | hot wall/user +2.29%/+1.20%; full self wall/CPU -0.30%/-0.63%; GCC -0.76%/-0.12%; normalized +0.46% wall/-0.50% CPU | focused property and PA38 45/45 clean; earlier all-32 prototype inception exact; all timed O1 outputs exact | rejected; misses the 1% normalized gate and regresses both normalized wall and the hot oracle |
| D4.final-transfer-region | clean cloned `noreturn` continuations, reuse an acyclic source home at its exact final phi transfer, and recolor complete connected source-live regions | PA37/PA38 READMEs plus role-, liveness-, level-, guard-, replay-, and behavior-based properties | `Next` frame 240 to 208 bytes, preserves -2, body 4,264 to 4,157 bytes; producer -14,928 `.text`; hot Ir -1.8031% | full self wall/CPU 0.997131x/0.992809x; GCC CPU 1.008088x; normalized CPU 0.984844x and normalized Ir 0.982027x; normalized wall intentionally unclaimed | PA37 187/187; PA38 45/45; 5,470/5,470 full report; debug/round-trip clean; zero-fatal audit; 219-object G2/G3 exact | retained; general three-layer movement family clears the deterministic and normalized CPU gates |
| D.fast-prefix-partial | copy one bounded pure fast-return prefix at hot loop calls and share one cyclic slow clone | provisional PA37/PA38 structural, level, guard, stats, replay, native, and behavior properties passed, then were removed with the rejected feature | isolated `inline_o1.o` exact; hot Ir -1.1228%; producer +48,016 text bytes; 16 sites beat the 24-site dose | self wall/CPU 1.051011x/1.010428x; GCC 0.988649x/0.998224x; normalized 1.063078x/1.012225x | exact G1/G2; all three 220-object self/GCC pairs and final compiler exact; implementation and contract movement removed | rejected; representative normalized CPU regresses despite the local deterministic saving |
| D.cyclic-dead-parameter | reclaim a dead incoming parameter register in a cycle only when neither the parameter nor a register-backed deferred address can be replayed from that cycle | none; rejected behavior was not moved into PA38 | `FindChild` 48 to 45 MIR and 177 to 166 bytes; corrected fixed-point producer -6,348 `.text`; O0/O1 representative outputs exact | O1 wall/CPU 0.98499x/1.00252x; O3 wall/CPU 1.05614x/1.01239x | initial G2 crash diagnosed as a replayed deferred address; corrected all-32 G1/G2 exact at 219 objects and final hash `874c5fba...` | rejected and removed; representative O3 regresses despite the local allocation win |
| D.dynamic-index-takeover | reuse a final-use dynamic-index base only for a short, repeated-use, call-free range after allocation fails | none; rejected behavior was not moved into PA38; PA37 unchanged | `FindEntry` 84 to 80 MIR, frame 32 to 16, one save removed; narrowed six-TU screen changed only two `program.cpp` functions; fixed-point producer +756 `.text` | O1 normalized wall/CPU 1.05185x/1.00801x; O3 raw self 1.01325x/1.00613x and normalized 0.99478x/1.00110x | O0/O1 MIR exact; 219 G2/G3 objects and final hash `608dd02b...` exact | rejected and removed; local frame win does not improve normalized CPU or raw throughput |
| D.complete-scalar-home | replace every load/store/compare of one heavily reused compiler scalar frame home with an unused callee-saved register | none; rejected behavior was not moved into PA38; PA37 unchanged | tokenizer `.text` 28,949 to 29,095; `Next` 4,157 to 4,217 bytes; output exact | tokenizer oracle 444,824,631 to 448,205,534 Ir (+0.7601%); `Next` 38,878,635 to 41,343,637 (+6.3393%) | five-second instruction-address oracle exact; full workload unwarranted; prototype removed | rejected; four instructions/call of save/restore/alignment tax outweigh unchanged move count |
| D.selected-parameter-index-home | preserve the setup transfer whenever constant-index lowering selects an optional parameter home | PA38 README plus O1/O2/O3 structural, behavior, EH, and direct-driver replay property; PA37 unchanged | fixes an uninitialized alternate parameter carrier after a large copy and EH marker | correctness-only; no performance claim | PA38 45/45; 5,470/5,470 through report; original G1 crash reproduced before the fix | retained correctness backfill in pushed commit `cd335c0a` |
| D.guarded-frame-store | sink one exact temporary frame definition below the only scalar-guard arm that dominates all later reloads | none; rejected behavior was not moved into PA38; PA37 unchanged | tokenizer -16 text bytes; `Next` -12; producer +17,952; isolated producer +15,680 and one workload object | hot Ir -0.3940%, normalized -0.395%; 12-pair normalized medians +1.030% wall/+0.406% CPU; isolated raw CPU median +0.13% | inline G1/G2 exact at 219 objects; isolated G1/G2 exact at 220; prototypes removed | rejected; local instruction saving loses to representative normalized cache/layout behavior |
| D.correlated-constant-group | specialize all correlated constant/global parameters within one four-site O3 call group | none; rejected behavior was not moved into PA37; PA38 unchanged | four E1-table calls lose two arguments; 74-byte clone; tokenizer +26 text bytes | exact fast oracle +236,392 Ir (+0.0531%); specialized helper path itself +472,764 Ir | immediate deterministic rejection; prototype removed before fixed point | rejected; parameter removal does not simplify the binary-search loop |
| D.copy80 | select direct vector chunks for exact 80-byte O2+ copies | none; rejected behavior was not moved into PA38; PA37 unchanged | deque swap 241 to 372 bytes; macro object +176 text; producer +3,892 text | 20 observations/side: aggregate task-clock 0.99915x; pair mean 0.99916x, median 0.99989x; warm pair mean 1.00051x | exact generated object; fresh 32-way candidate self compiler; prototype removed before full/inception gate | rejected; native CPU is flat while code grows |
| D.shared-loop-owner | localize two calls to one shared loop inside a multiply-used acyclic wrapper, then preserve that cyclic wrapper | none; rejected behavior was not moved into PA37; PA38 unchanged | retained 192-byte identifier wrapper; `Run` -172 bytes; tokenizer +108 text; producer +2,816 text/+16 data | 20 observations/side: aggregate task-clock 1.00329x; balanced-block mean 1.00337x; medians 864.125/867.515 ms | exact fixed-O1 hot object; fresh 32-way self compiler; prototype removed before full/inception gate | rejected; GCC-like ownership alone is slightly slower on this backend |
| D.query-family-fast | carry a general query's fast-return postcondition to later zero-index calls and reload through a generated helper | none; rejected behavior was not moved into PA37; PA38 unchanged | unsafe ceiling removed 6.60M dynamic full calls and -2.425% hot Ir; corrected helper redirected 19 static calls, tokenizer +51 text, producer +12,096 text/+16 data | unsafe ceiling -1.74% task-clock but invalid; corrected 20-observation aggregate 1.00937x and all ten blocks slower | corrected tiny/hot outputs exact; join bug reproduced and fixed in prototype; all code removed before full/inception gate | rejected; safe guard elimination is slower and the apparent win required unsound cached-value reuse |
| D.query-family-inline-fast | prove a general query's nonzero postcondition, reload through a bounded helper, and force-inline only its fast arm | none; drafted PA37 property and README were removed with rejected behavior; PA38 unchanged | 12 full calls redirected; hot Ir -0.993%; producer +25,996 text; O1 output exact | hot task-clock 0.98771x; O1 normalized wall/CPU 0.99726x/0.99935x; O3 normalized 1.00325x/0.99759x | focused property, PA37 187/187, PA38 45/45, 5,470/5,470, debug/round-trip, zero-fatal audit, and exact G1/G2/G3 before removal | rejected; full O3 wall direction failed and 0.24% normalized CPU is too small for the proof/code-size cost |
| D.equality-set-bitmask | replace a private four-plus integer equality OR with direct control and lower a bounded same-target switch through a 64-bit membership test | none; rejected behavior was not moved into PA37/PA38 | tokenizer -75 LowIR lines, `Run` -160 native bytes; compact producer +20,288 text; hot Ir -0.6646% normalized | initial full wall/CPU 1.00632x/1.00033x; compact ABBA 1.00211x/0.99808x | exact tokenizer/hot output, exact G1/G2 and common 219-object manifest/final; prototype removed | rejected; 0.19% full CPU gain and unfavorable wall do not repay a 16-KiB matcher/new native surface |
| D.dense-jump-table | lower bounded dense six-plus-case O3 switches through a relative native table | none; rejected behavior was not moved into PA38; PA37 unchanged | intended 7-way and 10-way switches selected; `IsOperator` +52 bytes; producer -824 text bytes | hot Ir 0.999422x (-0.0578%); `IsOperator` flat -0.465%; tokenizer negative control +0.0077% | PA38 45/45; valid serialized MIR and generated compiler; stopped before full workload/inception | rejected; GCC's larger win comes from caller constant propagation, while the fixed table scarcely improves the generic switch |
| D.final-eh-chain | move one discardable EH helper through a small unique weak wrapper, then close identical-constant phi dependencies | none; rejected behavior was not moved into PA37/PA38 | tokenizer 2,431 to 2,336 LowIR lines and -257 text bytes; initial narrow G2/G3 exact; isolated implementation still enlarged the producer | hot Ir -2.2531%; first normalized wall/CPU +0.514%/+0.325%; isolated full wall/CPU +0.904%/+0.327% | broad form failed G2 EH validation; narrow G2/G3 exact; both three-block 219-object screens exact; prototypes removed | rejected; strong local saving does not survive representative producer throughput |
| D.predefinition-phi-spill | protect a reserved planned phi home from eviction before its first definition at O2/O3 | none; rejected behavior was not moved into PA38; PA37 unchanged | fast fill cursor regains residency; helper 396 to 379 bytes; `program.o` +48 text; producer +3,392 text; 219-object G2/G3 exact | helper flat Ir -2.685%; complete hot Ir 0.9999347x (-0.0065%) | PA38 45/45; exact hot object and all-32 fixed point; prototype removed before full timing | rejected; genuine O3 load-lifetime interference, but complete-workload dose is far below 1% |
| D.nonescaping-slot-alias | do not let a proven nonescaping local-slot store invalidate unknown-pointer load availability at O2/O3 | none; rejected behavior was not moved into PA37; PA38 unchanged | hot macro LowIR -1,805 bytes; spelling probe loses loop-invariant begin/size/mask reloads; producer -2,088 text; 219-object G1/G2 complete | complete hot Ir 0.999360211x (-0.063979%); native G1 neutral and G2 samples inconclusive | PA38 45/45; exact hot object; all-32 G1/G2 completed; prototype removed before full/G3 gate | rejected; sound generic improvement is dynamically far below the 1% retention floor |
| D.complete-leaf-save | recolor a leaf only when all callee-saved colors and their defining moves can disappear | none; rejected behavior was not moved into PA38; PA37 unchanged | range helper frame 16 to zero, 72 to 63 bytes; G2 producer +1,056 text/+16 data; 219-object G1/G2 complete | tokenizer Ir 0.998034479x; `Run` flat 0.993655072x; complete hot Ir 0.998121212x (-0.187879%) | PA38 45/45; exact tokenizer and hot outputs; all-32 G1/G2 completed; prototype removed before full/G3 gate | rejected; eliminating all leaf call overhead remains far below the 1% retention floor |
| D.conditional-call-carriers | coalesce complete parameter colors into caller-saved carriers and preserve them only inside repeated bypassable call arms | none; rejected behavior was not moved into PA38; PA37 unchanged | `AppendUTF8` saves 5 to 3, body 1,814 to 2,011 bytes; tokenizer +204 text; region extension cannot prove the other three colors | tokenizer Ir 444,824,631 to 443,561,255 (`0.997159834x`, -0.284017%) | exact tokenizer output; five-second oracle; prototype and temporary MIR identity removed before full/inception gate | rejected; safe path-local carrier preservation is far below the 1% floor |
| D.private-table-prefilter | correlate four private structured-table calls only when specialization also proves a below-minimum false-return shortcut | PA37 README plus role-based positive, unlike-table, escape, alignment, level, bounded-stats, replay, and behavior properties; PA38 unchanged | four calls lose table/count arguments and gain one entry bound; producer +30,108 text/+624 data; O0--O2 workload output exact | complete hot self Ir `0.983383x`, GCC `1.000112x`, normalized `0.983273x`; O3 native self CPU `1.000413x`, normalized block-median CPU `0.999856x` | PA37 187/187; PA38 45/45; 5,470/5,470; PA37/38 debug/round-trip and three zero-fatal audits clean; all-32 220-object G1/G2/G3 exact | retained; deterministic normalized gain is 1.67% with representative raw O3 in the close-result band |
| D.bounded-relative-alias | preserve exact field loads across bounded disjoint stores from the same unknown base, only in small O3 functions | none; rejected behavior was not moved into PA37; PA38 unchanged | tokenizer -50 text, specialized query 203 to 193 bytes, no hot-function growth; producer +4,812 text | tokenizer Ir `0.977630x`; complete hot Ir `0.998080x` (-0.192036%) | exact tokenizer behavior; one all-32 220-object G1; prototype removed before G2/full/inception | rejected; bounded form avoids destructive interference but its complete-workload population is far below the 1% floor |
| D.shared-acyclic-child-owner | localize exactly two calls to a shared small hinted child inside a single-use large acyclic hinted wrapper, then preserve that wrapper | none; rejected behavior was not moved into PA37/PA38 | `Run` -1,860 bytes, new wrapper 3,865 bytes, tokenizer +1,600 text; producer +8,320 text/+32 data | tokenizer Ir `0.979058x`; complete hot Ir `1.000353x` (+0.035262%) | exact tokenizer/hot outputs; one explicit-32-way 220-object G1; prototype removed before G2/full/inception | rejected; isolated ownership win becomes a source-diverse regression |
| D.common-path-memory | combine same-object adjacent i64 transfers and allocation-stage variants into one direct 16-byte copy, and retest exact guarded private-stage sinking as a complementary dose | PA38 README plus O0--O2 isolation, relationship/behavior, later-use and other negative proofs, debug-location, replay, and encoding properties; no complete-program or fixed-register matching | `Next` 4,157 to 4,085 bytes; tokenizer -128 total text; 220-object producer -30,883 text; linked compiler -30,144 text | complete self Ir `0.988239x`, GCC `1.000182x`, normalized `0.988059x`; O1 native wall/CPU `0.987640x`/`0.993938x`; O3 CPU aggregate/median `0.994389x`/`0.992869x`, wall inconclusive | PA38 45/45; 5,470/5,470; PA37/38 debug and round-trip clean; all audits clean; 220-object G2/G3 and final hash exact | retained; combined source-diverse gain supersedes the earlier isolated guarded-store rejection |
| D.forwarded-boolean-control | move an exact private Boolean phi/trunc/forwarding decision onto its incoming edges | PA37 README plus O0--O2 isolation, local relationship, incoming-effect, sharing, non-Boolean, successor-phi, cycle, EH, replay, and behavior properties | tokenizer -272 text and `Run` -170; 46 nonimplementation objects -8,425 text; complete producer net +5,411 object/+5,424 linked text | tokenizer Ir `0.986384x`; complete self `0.988024x`, GCC `1.000062x`, normalized `0.987963x`; O1 native wall/CPU `0.987361x`/`0.989975x`; O3 `0.979446x`/`0.989050x` | PA37 187/187; 5,470/5,470 through report; debug/round-trip and all audits clean; all-32 220-object self/inception exact | retained; source-diverse normalized saving clears 1% and both native workloads corroborate it |
| D.compared-reference-selection | replace a private compared pointer selection and selected reload with a scalar phi of the values already loaded | none; rejected before contract movement | `AddSourceToken` -6 MIR/-26 bytes; macro object -4,112 text; G1 linked compiler -152,644 text including implementation | self Ir `0.997859x`, GCC `1.000691x`, normalized `0.997170x`; native hot wall/user `1.00585x`/`1.01250x` | deterministic self/GCC hot output; explicit-32-way 221-object G1; prototype removed before G2 | rejected; mostly cold size reduction and strength reduction remain below the 1% gate and regress native CPU |
| D.retained-division-result | preserve the architectural quotient/remainder identity after machine copy forwarding so existing constant-division encoding still applies | none; rejected before contract movement | static `div`/`idiv` 4,823 to 407; hot loop uses magic division; `AnnotateParentheses` +20 bytes; linked producer +93,188 text | self Ir `1.006774x`, GCC `0.999908x`, normalized `1.006867x`; native hot wall/user/CPU `1.005780x`/`1.007150x`/`1.008729x` | PA38 45/45; deterministic candidate self/GCC hot object; explicit-32-way 220-object G1; prototype removed before G2 | rejected; broad strength reduction increases retired work, footprint, and measured hardware CPU |
| D.hint40-O3 | lower only the default O3 late hinted-nonleaf cap from 48 to 40 while preserving explicit overrides | none; rejected before contract movement | hot token push becomes a 56-byte wrapper; G2 linked producer -304,292 text bytes; O1 hot output exact | self Ir `0.999018x`, GCC `0.999267x`, normalized `0.999752x`; native normalized paired medians `0.994755x` wall/`0.996894x` user | exact self/GCC O3 hot object; explicit-32-way G1/G2; stopped before G3 and removed | rejected; large static shrink yields only 0.025% normalized Ir and sub-1% native gains |
| D.conditional-prvalue-transfer-prototype | construct both arms of a same-type conditional class prvalue directly in the final copy/move destination | initial prototype had no serialized permission; its rejection established the PA13 boundary now owned by `D.conditional-copy-elision` | G2/G3 exact; 29/220 objects changed, 27 nonimplementation; producer -15,092 text; `AddSourceToken` 1,245 to about 899 bytes | self Ir `0.988362x`, GCC `1.000014x`, normalized `0.988348x`; gap 1.643606x to 1.624456x | identical self/GCC hot object; explicit-32-way G1/G2/G3; PA17 O0 incompatibility reproduced; prototype removed | superseded by retained contract-backed `D.conditional-copy-elision`; this row records why the hidden-metadata form was rejected |
| D.conditional-call-bridge | recolor complete parameter regions into caller-saved carriers and preserve only around one actual call | none; rejected behavior was not moved into PA38 | three regions outside `AppendUTF8`; +68 MIR instructions; producer +14,592 text | ten-pair task-clock mean `1.002352x`, paired median `1.002911x` | exact frozen output; prototype removed | rejected; preservation traffic and implementation footprint exceed the local saving |
| D.final-frame-slot-coalescing | merge nonoverlapping scalar compiler-temporary frame bindings after final MIR liveness | none; rejected behavior was not moved into PA38 | `AppendUTF8` frame 208 to 80 and body 1,814 to 1,655 bytes; broad/bounded producer +19,344/+19,536 text | broad paired mean `1.005144x`; bounded `1.007573x` | PA38 45/45 during screening; prototypes removed | rejected; whole-function binding machinery regresses the fast oracle despite local compaction |
| D.phi-fallback-slot-pool | allocate O3 acyclic fallback phi homes from the existing reusable temporary pool | provisional PA38 README and relationship/behavior reducer removed after rejection | safe `AppendUTF8` frame 208 to 80/body 1,814 to 1,655; fixed-point producer -17,024 text; unsafe precursor overwrote a still-live copied pointer | frozen task-clock aggregate `0.996735x`, paired median `0.997630x`; clean full O1 wall `0.986950x`, CPU `1.000409x` | unsafe G1 crash diagnosed; corrected property passed and 221-object G1/G2 exact at `49b77e47...`; prototype removed | rejected; safe source-diverse CPU is flat and below the 1% gate |
| D.grouped-early-exit-unroll | fully unroll one bounded acyclic private-table loop and group iterations by a folded discriminator | none; rejected before contract movement | `Lexer::Run` 9,110 to 10,290 bytes; tokenizer Ir `0.969695x`; optional scalar `memcmp` lowering regressed to 462.16M Ir | self `1.000256x`, GCC `1.000144x`, normalized `1.000112x`; gap 1.529639x to 1.529810x | exact tokenizer and complete hot objects; fresh explicit-32-way G1; prototype removed | rejected; isolated 3.03% saving is canceled by complete-producer interference |
| D.argmem-boundary | restore parameter capture/access and an argument-memory-only call effect, then consume them in memory GVN | none; rejected before PA13/PA37 contract movement | focused source-slot reuse works; tokenizer exact; all 221 G1/G2 `.text` sections and linked compiler exact | no timing warranted because generated compiler code is unchanged | explicit-32-way G1/G2; compiler `01eaef45...` exact; prototype removed | rejected; no real compiler population, so the removed LowIR surface remains unjustified |
| D.complete-object-memory | serialize a positive complete parameter-object extent, derive body effects/capture intervals, and consume disjoint regions at O3 | PA13/PA17/PA37/PA38 README plus pointer-only syntax/transport, source-production, level, structural positive/negative, replay, native-pressure, and behavior properties; no complete-program matching | compiler object v6; 1,387 populated extents on the largest TU; indexed 9,336-site analysis; linked producer +55,956 text/+752 data; final G1/G2 exact | self Ir `0.976039x`; GCC `1.000713x`; normalized `0.975343x`; gap `1.529720x` to `1.492001x`; Clang-normalized `0.975533x`; final O1/O3 native CPU `0.988085x`/`0.988883x` | 221-object all-32 fixed point at `30132bf2...`; focused suites, full report, debug/round-trip, and all zero-fatal audits clean | retained; populated O0 semantic fact justifies the narrow LowIR addition, while indexed fixed points remove the initial critical-path regression |
| D.external-write-stable-reuse | preserve an established repeat-stable query result across calls proven not to write outside their own frame; later recheck adds path-specific join phis | none; rejected behavior was not moved into PA37 | both screens remove the same 882,230 dynamic query calls; recheck producer +6,488 text | original/recheck self Ir `0.996729x`/`0.996625x`; recheck task-clock `0.999819x` | `lowiropt` and PA37 190/190; original exact fixed point `c18cd945...`; explicit-32 recheck G1/G2 and Callgrind; prototypes removed | rejected; join phis expose no additional surviving reuse and later retained work does not move the sound dose near 1% |
| D.compared-quotient-encoding | let existing constant-division encoding consume a quotient read directly by the adjacent comparison | none; rejected behavior was not moved into PA38; LowIR/MIR contracts unchanged | hot `idiv 104` removed; object +20 text; producer +30,316 text; 221-object G1/G2 exact | self task-clock `0.991361x`, GCC `1.001352x`, normalized `0.990022x`; self Ir `1.003239x` over its direct control and combined Ir `0.999957x` vs retained | PA38 45/45; exact fixed point `cc7c277f...`; prototypes removed | rejected; borderline native win contradicts deterministic instruction cost and adds footprint |
| D.readonly-scalar-shadow-plus-quotient | copy a scalar into a call-only shadow after proving the internal by-address callee only reads it, then pair recovered loop SSA residency with adjacent quotient encoding | none; rejected before contract movement; existing PA13 `object_bytes` and callee bodies were sufficient | induction slot becomes phi; `AnnotateParentheses` 525 to 515 bytes; macro +1,195 text; G1 producer +51,004 text | self task-clock `0.994328x`; GCC `1.004720x`; normalized `0.989660x`; self Ir `1.002094x`; selected body inclusive Ir `1.013480x` | PA37 190/190; exact explicit-32-way G1 and deterministic hot output; no stale Valgrind; prototypes removed | rejected; normalized native threshold is contradicted by deterministic regression, so neither PA37 nor PA38 behavior is retained |
| C.terminal-swap-o2 | promote the retained terminal staged-object swap from O3 to O2 | none; existing PA37 O3 contract retained after rejection | O2 producer -104 text bytes; requested-O1 workload exact | hot task-clock mean/median `0.994194x`/`0.993609x`; full CPU `0.996789x`, wall `1.020660x` | 20 exact hot observations per side; one exact all-32 221-object ABBA block | rejected; complete CPU gain is only 0.321% and wall is unfavorable |
| C.private-table-o2 | run only the retained private structured-table prefilter at O2 | none; existing PA37 O3 contract retained after rejection | fixture O2/O3 shapes exact; O2 producer -376 text bytes; requested-O1 workload exact | hot normalized `0.970720x`; full self CPU `0.994712x`; full normalized wall/CPU `1.005306x`/`0.994908x` | focused O2 stats; 20 exact hot observations per side; exact all-32 self and position-balanced GCC blocks | rejected; source-diverse full CPU gain is only 0.509% and normalized wall regresses |
| C.complete-memory-o2 | run the retained whole-program parameter-memory analysis at O2 while keeping its companion cleanups at O3 | none; existing PA37 O3 contract retained after rejection | O2 producer -2,584 text bytes; requested-O1 workload exact | hot mean/median `0.994103x`/`0.995585x`; full wall/CPU `1.005060x`/`1.000808x` | focused O2 population; 20 exact hot observations per side; one exact all-32 221-object ABBA block | rejected; complete raw throughput regresses before pass-cost or normalized escalation |
| D.edge-live-color-components | split final-MIR physical-color components across adjacent blocks only when the source is live on their edge | none; rejected behavior was not moved into PA38 | tokenizer -42 text, `Lexer::Run` -12 bytes, producer -816 text; `AppendUTF8` unchanged | complete hot Ir `0.999640086x` (-0.035991%) | exact hot object; explicit-32-way 221-object G1/G2; prototype removed before G3/full timing | rejected; real but immaterial register-choice refinement, far below the 1% D5 floor |
| D.fast-slow-function-versioning | keep one bounded call-free return corridor under the original identity, restart every bailout in a complete slow clone, and lower the exact final call as a sibling transfer | PA37/PA38 READMEs plus level-isolated structural, guard, replay, native-relationship, and behavior properties; no source-name or whole-program matching | no LowIR contract addition; producer +34,376 text/+160 data; selected original 98 bytes plus unchanged 1,814-byte slow clone | self Ir `0.981190163x`, GCC `0.999989823x`, normalized `0.981200149x`; two full-build normalized CPU blocks `0.992312127x`/`0.991831466x` | focused controls; 5,473/5,473; serial debug/round-trip and all zero-fatal audits clean; explicit-32 G1/G2 and every measured output exact | retained; clears deterministic D5 and stable full CPU corroboration after removing measured source-layout interference |
| D.scalar-fast-slow-extension | reuse the retained versioning and complete-frame sibling machinery for one additional small non-hinted scalar-return function | none; rejected behavior was not moved into PA37/PA38 | intended 32-instruction query becomes 47-byte wrapper plus unchanged 116-byte clone; producer +23,860 text/+16 data; output exact | self Ir `1.004353845x`; query inclusive Ir `1.023825906x` because 7.16M/11.50M calls take the slow edge | exact frozen object; explicit-32 G1 producer; prototype removed before fixed point/full timing | rejected; majority-slow population makes the extra transfer a deterministic generated-code loss |
| D.prefix-consumer-boundary | serialize a source-produced prefix consumer so O3 can inline only its unique call-free consuming arm after an available index-zero stable-prefix query | none; provisional PA13/PA17/PA37/PA38 movement removed after rejection | tokenizer final O3 LowIR 4,608 to 4,204 instructions and 345,678 to 316,192 bytes; temporary clone fully pruned; producer +14,864 text | self Ir `0.999817522x` (-0.018248%); same-source normalization not escalated after missing the deterministic 1% gate | exact frozen object; explicit-32 G1/G2 and final hash exact; prototype fully removed | rejected; the measured benefit is too small to justify a new PA13 LowIR promise and compiler-object version |
| D.once-cap | lower the existing global one-shot inline cap so GCC-like helper partitioning reduces `Lexer::Run` pressure | none; public diagnostic override only | caps 64/96/256 retain 2,934-byte `ScanPunctuator`; `Run` becomes 1,166/2,266/7,075 bytes vs 9,313 | cap 64 +3.185%/+2.027% wall/user; cap 96 +2.548%/+3.356%; cap 256 +2.532%/+2.027% | deterministic isolated probe outputs exact | rejected; smaller global layout loses useful locality and call removal |
| D.post-state-query-inline | expand only index-zero guarded query calls structurally reached after same-object state changes, with full-body and shared-fast-return variants | none; rejected before PA37/PA38 movement | six-site full `Run` +558/object +560 bytes; six-site inverse split `Run` +684/object +740; 12-site split `Run` +1,199/object +1,780 | pinned task-clock `0.996629370x`, `0.994878446x`, and `0.997201623x` respectively | exact hot output for every dose; isolated probe links; prototypes removed before full self-host | rejected; best 0.51% generated-code result misses 1% before pass footprint |
| D.grouped-loop-leaf-inline | inline only tiny call-free grouped clones at natural-loop sites while retaining their shared non-loop bodies | none; rejected before PA37/PA38 movement | three hot calls removed; `AnnotateParentheses` 525 to 776 bytes; macro +1,815 text; producer +8,664 text | isolated task-clock `0.987787789x`; full G1 batched mean/trimmed/median `0.991382860x`/`0.991153185x`/`0.991903553x`; self Ir `1.000983853x` | exact hot output; explicit-32-way G1; candidate Callgrind; prototype removed | rejected; sub-1% native result contradicts deterministic total and duplicated code; optimize the shared predicate encoding instead |
| D.grouped-predicate-native | reuse a dying conversion input, compose its adjacent load-normalization proof, and retain encodable 32/64-bit comparison immediates at O2/O3 | none; rejected before PA38 movement | macro -281 MIR instructions; grouped true arm -3 MIR; G1 producer -22,256 linked text; hot object exact | hot task-clock mean/median/trimmed `1.001137292x`/`0.998731825x`/`1.000682649x`; clean full O3 CPU/wall `0.994436x`/`0.990743x`; static ceiling about -0.97% | explicit-32-way 221-object G1 and two full ABBA blocks; exact outputs; position-two host outlier repeated under opposite labels; prototype removed | rejected; source-diverse CPU gain is 0.56% and deterministic ceiling misses 1% |
| D.narrow-compare-return | write a terminal narrow comparison directly to the ABI byte return register and remove its widen/transfer/narrow tail | none; rejected before PA38 movement | macro -42 further MIR; G1 producer -800 text vs the safe native pair | explicit-32-way G1 failed its first frozen compile with `invalid internal paste sequence` | build completed, execution gate failed; removing only this fold restored exact output | rejected as unsound; callers rely on the current full-register narrow-return invariant |
| D.terminal-store-load-return | return an edge's stored scalar directly instead of entering a pure terminal merge that recomputes its equivalent address and reloads it | none; rejected before PA37 movement; existing PA13 `object_bytes` is sufficient | hot refill edge -4 native instructions; helper 116 to 121 bytes; `memory_gvn.o` +11,662 text; G1 +10,592 linked text; output exact | hot task-clock mean/median/trimmed `1.003380920x`/`1.001532907x`/`1.001948236x`; bundled-native screen `1.001780636x`/`0.999218504x`/`1.002306278x`; deterministic ceiling about -0.81% | PA37 190/190; PA38 45/45; explicit-32-way 221-object G1 plus exact hybrid relink; prototypes removed | rejected; relationship needs no new LowIR fact and neither isolated nor bundled dose clears the 1% gate |
| D.terminal-call-result-residency | combine terminal store/load return forwarding with final-MIR retention of a narrow call result in its ABI carrier through same-width stores | none; rejected before PA37/PA38 movement; existing serialized facts are sufficient | refill edge -6 dynamic instructions; helper 116 to 114 bytes; G2 producer +20,880 linked text | pinned mean/median/trimmed `0.991614x`/`0.992215x`/`0.991681x`; static ceiling about -1.21% | PA37 190/190; PA38 45/45 in the combined screen; explicit-32-way G1/G2; deterministic hot output; no stale Valgrind | rejected; stable 0.78--0.84% saving remains below the 1% source-diverse gate after pass overhead was moved behind a cheap prefilter |
| D.terminal-query-slow-suffix | extract one proved slow scalar query suffix, use an exact scalar sibling transfer, and retain the call result/incoming parameter in their ABI carriers | PA37/PA38 READMEs plus role-discovered positive, level, guard, replay, stats, native-relationship, CFG-terminal, and behavior properties | no LowIR contract addition; one split/eight sites/12 instructions; one result and parameter retention; producer +53,552 text/+1,168 data | self Ir `0.986401615x`, GCC `1.000098960x`, normalized `0.986304010x`; gap `1.464102196x` to `1.444049868x`; conservative full O1 pair `0.990363232x` wall/`0.983188069x` CPU | 5,473/5,473; debug/round-trip and zero-fatal audits clean; exact 222-object G1/G2 and final `5846b8b0...` | retained; final source order removes measured implementation-layout interference while preserving the generated-code win |
| C | make O2 at least 5% faster than O1 | current retained contracts remain covered | current fixed workloads exact | raw CPU `0.977720x` / `0.985111x` / `0.988435x`; normalized `1.097342x` / `1.060648x` / `1.107673x` | one all-32 ABBA block per self/GCC cell | raw hard floor met; residual normalized and 5% targets recorded on closure |
| D | make O3 at least 20% faster than O1 | all retained additions covered | current fixed workloads exact; two later structural wins retained | matrix raw CPU `0.864519x` / `0.855679x` / `0.866759x`; normalized `1.028530x` / `0.987659x` / `0.992245x`; later increments improve O3 further | complete matrix plus deterministic/full corroboration; final G1/G2 exact | practical O3 goal and normalized O2/O3-workload parity achieved; 20% stretch not independently demonstrated |
| Final | complete matrix and closure | no uncovered retained behavior | three 221-object matrix workloads exact; final increment exact across 222 objects | original/current gaps and every retained/rejected dose recorded | 5,473/5,473; debug/round-trip and all audits clean; final 32-way inception exact | complete; no untried source-diverse lead justifies extending the plan |

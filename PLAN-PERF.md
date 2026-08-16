# Performance Plan: Sub-15-Second Frozen Compile

Status: implementation and local gates complete

Date: 2026-08-16

Baseline commit: `f3ac28a4dec1535bc5a698c056905a9ae365a238`

## 1. Objective

Reduce the default compilation time of the PA39 frozen
`semantic_overload.cpp` benchmark to less than 15 seconds without PGO, while
preserving the staged compiler's correctness and the performance requirements
in `spec.md`.

The engineering target is a median wall time of at most 14.5 seconds on a calm
host. The margin matters: a result that only occasionally rounds below 15
seconds is not complete.

This plan treats the Fable `PLAN-PERF.md` and `PLAN-PERF2.md` documents as an
idea bank, not as a list of patches to transplant. Their durable lessons are:

- establish an A/A noise floor before trusting A/B timing;
- combine timings with exact work counters and output checks;
- use interleaved baseline/candidate runs on a variably loaded host;
- make representation and allocation costs visible before redesigning them;
- use verify-on-hit before trusting a risky cache;
- keep changes small, independently testable, and independently revertible;
- record rejected experiments so they are not rediscovered; and
- defer whole-compiler inception runs until a meaningful batch is ready.

The actual priorities below come from this compiler's profile, not Fable's.

## 2. Non-negotiable constraints

The following constraints apply to every optimization:

1. No PGO during this project. PGO can be evaluated separately after the
   intrinsic costs have been fixed.
2. No filename, benchmark, source-text, or expected-output special cases.
3. No shelling out to a host compiler, reference compiler, or prior solution.
4. No persistent or process-global semantic cache. Each invocation must be
   self-contained and deterministic.
5. Preserve the compact-ID, direct-index, explicit-worklist, bounded-backend,
   and near-linear-allocation requirements in `spec.md`.
6. A failure first observed in PA36, PA37, PA38, or inception must be reduced
   to the earliest assignment that owns the broken behavior. It must not be
   left only as a late-stage or PA39 regression.
7. Every accepted optimization is its own cohesive, tested commit. Do not
   accumulate several unrelated performance edits in one commit.
8. The checked-out extended-source repository is read-only for this work. All
   scripts, tests, and implementation changes belong in this repository.

## 3. Current baseline

### 3.1 Correctness state

At the baseline commit, the cumulative checked-in test report is clean. A full
inception run passed before the final file-audit refactor, but inception was
intentionally not rerun after that refactor. Therefore, the baseline commit is
not recorded as a freshly verified inception anchor.

### 3.2 Frozen compile

Source:

`~/cppgm-extended-pa39-source-layout/benchmarks/self_compile/stable/semantic_overload.cpp`

One provisional default-optimization run on a loaded host produced:

| Metric | Baseline |
| --- | ---: |
| Wall time | 29.10 s |
| User time | 27.32 s |
| System time | 1.77 s |
| Peak RSS | 938,608 KiB |
| Object size | 93,544,200 bytes |
| Object SHA-256 | `c379baae6ae84b4d66f304337dbf789704c1c73a5a6c361f8a30280304c02a2a` |

This is a provisional profile point, not the final statistical baseline. The
host load was approximately 6.6 on 32 logical CPUs and had nonzero CPU
pressure. Phase counters and flat-profile rankings are still useful; absolute
wall time must be established with the protocol in section 4.

### 3.3 Phase telemetry

The current counters reported:

| Phase | Time | Selected work |
| --- | ---: | --- |
| Preprocess | 2.714 s | 491,395 tokens |
| Parse | 0.547 s | 282,540 semantic nodes |
| Semantic analysis | 9.294 s | 993,003 lookups; 1,834,009 scope visits |
| Typed-to-LowIR lowering | 1.467 s | 282,097 LowIR instructions |
| PA37 LowIR optimization | 5.018 s | 2,684,871 instruction visits |
| Native lowering | 7.577 s | 331,925 input MIR instructions |
| Machine optimization | 0.215 s | 965,863 instruction visits |
| Encoding | 0.974 s | 70,790 fixups |
| Payload serialization | 0.362 s | 80,529,393 payload bytes |

The existing `adapt_ns` field overlaps other stages and must not be added to
these times. Phase 0 will replace overlapping totals with clearly documented,
non-overlapping measurements.

Other useful observations:

- semantic analysis created 575,344 declarations and considered 417,638
  overload candidates;
- template work had 97,266 requests and 69,519 reported cache hits;
- semantic peak accounting reported about 1.16 GB;
- PA37 visited each input instruction about 9.5 times;
- PA37 ran slot-related passes 43,811 times over 9,390 functions;
- native lowering emitted 283,341 optimized MIR instructions; and
- payload reservation grew once, with no full-buffer copies.

### 3.4 Flat CPU profile

A separate `perf` sample placed the following symbols near the top:

| Approx. CPU share | Symbol or cost family |
| ---: | --- |
| 9.47% | native `FunctionLowerer::spill_one` |
| 7.30% | `InternedStringTable::InternRange` |
| 6.40% | allocator `_int_malloc` |
| 6.11% | native `has_live_location_alias` |
| 3.41% | `memcmp` |
| 2.91% | `std::_Hash_bytes` |
| 2.11% | allocator free/merge path |
| 2.04% | native `reclaim_dead_parameter_register` |
| 1.72% | semantic `LookupUnqualifiedCandidate` |
| 1.58% | lexer `Peek` |
| 1.40% | string-keyed unordered-table lookup |

The three named native register/value routines alone account for about 17.6%
of sampled CPU. General allocation and string hashing/comparison costs form a
second large family. These are the first targets.

### 3.5 Final measured candidate

The final compiler candidate is commit `d74971d5`. Its immutable executable
has SHA-256
`3b1b69f6bfbe53cddc68b3b0592e0bae161db27018652f392d7dc4ab086ee60d`.
No PGO was used.

A predeclared five-run window started at load average 2.40 on 32 logical CPUs,
with CPU `some avg10=0.00`, no competing self-compile or Valgrind child, and
no swap activity. No observation was discarded:

| Run | Wall | User | System | Peak RSS |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 14.45 s | 13.09 s | 1.34 s | 963,672 KiB |
| 2 | 14.45 s | 13.15 s | 1.29 s | 963,656 KiB |
| 3 | 14.39 s | 13.12 s | 1.27 s | 963,728 KiB |
| 4 | 14.23 s | 13.01 s | 1.21 s | 963,772 KiB |
| 5 | 13.97 s | 12.69 s | 1.27 s | 963,528 KiB |
| **Median** | **14.39 s** | **13.09 s** | **1.27 s** | **963,672 KiB** |

This is 50.5% below the provisional 29.10-second wall baseline. Peak RSS is
2.67% above that provisional baseline and remains within the 3% guardrail.
Every run emitted exactly 93,544,200 bytes with SHA-256
`c379baae6ae84b4d66f304337dbf789704c1c73a5a6c361f8a30280304c02a2a`.

Two direct ABBA blocks against the immutable `f3ac28a` executable corroborate
the cumulative result under the same host conditions. The original compiler's
four-run medians were 27.885 s wall, 26.160 s user, and 940,392 KiB RSS; the
final compiler's were 14.430 s wall, 13.080 s user, and 962,786 KiB RSS. The
paired medians are -48.55% wall, -50.22% user, and +2.38% RSS. All eight
objects were exact and deterministic.

The retained command shape was:

```sh
/usr/bin/time -f '%e %U %S %M' /tmp/v3codex-final2-cppgm++ \
  -I dev/src -c -o /tmp/v3codex-final2-calm-N.o \
  benchmarks/self_compile/stable/semantic_overload.cpp
```

The final explicit telemetry run reported:

| Phase or counter | Final result |
| --- | ---: |
| Preprocess | 1.405 s |
| Parse | 0.275 s |
| Semantic analysis | 4.671 s |
| Typed-to-LowIR lowering | 1.196 s |
| PA37 LowIR optimization | 3.275 s |
| Native lowering | 1.532 s |
| Machine optimization | 0.207 s |
| Encoding | 1.019 s |
| Payload serialization | 0.347 s |
| Interner rehashes / rehash hash bytes | 0 / 0 |
| Interner occupied probes | 2,846,493 |
| Live-location scans / visits | 0 / 0 |
| Spill value visits | 41,639 |

The phase timings come from an instrumented run and are structural evidence,
not a replacement for the non-instrumented five-run wall-time gate.

## 4. Measurement protocol for an intermittently loaded host

### 4.1 Immutable executables

Each timing comparison uses two immutable compiler executables copied from
clean commits:

- A: the relevant accepted baseline;
- B: the candidate commit.

Both are built with the same toolchain and flags. Profiling, statistics, and
sanitizer builds are never used for headline timing.

### 4.2 A/A calibration

At the beginning of a measurement session, run the same executable in both A
and B positions for at least two ABBA blocks. This establishes the session's
paired timing noise and detects systematic order effects.

Record for every run:

- wall, user, and system time;
- peak RSS;
- output size and SHA-256;
- load average and CPU pressure before the block; and
- compiler commit, host compiler version, and command line.

Do not discard a slow result after seeing it. A block may be deferred only by
a predeclared pre-run screen, such as severe load or CPU pressure. Once an
ABBA block begins, retain all four observations.

### 4.3 A/B acceptance

Use interleaved ABBA order to prevent host drift from consistently favoring a
candidate. Compare paired ratios and report medians rather than a best run.

- Screening a large expected change: two ABBA blocks.
- Accepting a clear change above roughly 8%: at least three ABBA blocks.
- Accepting a marginal change: at least five ABBA blocks.

A candidate is retained only when:

1. a relevant structural counter improves or the implementation removes a
   demonstrably unnecessary operation;
2. median user-time improvement exceeds both 3% and the measured A/A noise;
3. wall time agrees in direction or is statistically neutral under host load;
4. most paired observations favor the candidate; and
5. correctness, determinism, and memory gates pass.

User time is the more stable relative signal. The final under-15-second claim
is based on wall time: five candidate-only runs in a predeclared calm-host
window, with median at most 14.5 seconds and no run silently discarded.

### 4.4 Output gates

For representation-only, allocation-only, lookup-only, and scheduling changes
that are intended to be observationally neutral, the benchmark object bytes
must match the accepted baseline exactly.

A legitimate optimizer or backend decision change may alter object bytes. In
that case it must:

- produce identical bytes across repeated candidate runs;
- have an explained and reviewed reason for the change;
- pass the owning assignment's object/behavior tests; and
- receive an earliest-owning-PA regression when it exposes a prior coverage
  gap.

Object hashes are diagnostic gates, not a replacement for assignment tests.

### 4.5 Benchmark set

The frozen `semantic_overload.cpp` compile is the primary measurement. Smaller
proxies are used to shorten iteration, but cannot declare the goal complete:

- `semantic_model.cpp` for frontend and semantic experiments;
- a version-matched O0 LowIR replay generated by the same candidate frontend
  for isolated PA37/native measurements;
- small lexer/parser tests for interning and syntax-tag changes; and
- a backend-heavy translation unit selected by phase counters, if needed.

Never use a stale LowIR fixture as performance evidence for a changed
frontend. It may remain a correctness fixture only if its contract requires
fixed textual input.

### 4.6 Profiling discipline

Timing and profiling are separate activities:

- counters identify repeated work and prove its removal;
- flat `perf` identifies CPU owners;
- call-graph or callgrind runs are used only when flat ownership is ambiguous;
- allocation census data identifies counts, bytes, growth, and peak live
  ownership; and
- clean non-instrumented ABBA runs confirm the actual result.

After a large hotspot is fixed, reprofile. Do not keep optimizing from an old
ranking.

## 5. Correctness, reducer, and commit loop

### 5.1 During a change

Use the narrowest owner test while editing, for example:

```sh
make -C paN check TEST=path/to/test
```

Then use selected cumulative reports as the fast cross-stage signal:

```sh
make test-report ACTIVE_TEST_REPORT_PAS='paN pa36 pa37 pa38'
```

The exact list depends on the affected stages. PA names are passed explicitly
so a frontend edit does not pay for unrelated reports at every iteration.

### 5.2 Before each accepted commit

For the earliest owning PA `N`:

1. run `make test-paN`;
2. run `make test-report-through-paN`;
3. run selected later reports for every downstream stage directly affected;
4. run `perl scripts/cppgm_file_audit.pl --stage pa39 --paths dev/src` and
   require zero fatal findings; and
5. run the relevant performance proxy and ABBA gate.

Shared frontend/IR representation changes also require a full through-PA38
report before acceptance. Otherwise, run the full through-PA38 report at least
every two or three performance commits so failures stay close to their cause.

### 5.3 Reducer rule

When a failure appears:

1. preserve the failing input and command;
2. identify whether the failure is frontend semantics, LowIR optimization,
   native lowering, encoding, or driver behavior;
3. reduce source or LowIR while keeping the same failure and exit/behavior;
4. identify the earliest assignment whose public contract includes the bug;
5. add the reduced regression there before or with the fix; and
6. run that PA's cumulative report plus the originally failing late stage.

A PA37 or inception discovery is not automatically a PA37 test. For example,
a name-lookup bug found while self-hosting belongs to its earliest semantic PA;
a LowIR rewrite bug belongs to PA37; and an x86 lowering bug belongs to its
earliest backend PA.

### 5.4 Commit policy

Commit each validated changeset immediately. A commit should contain one
performance hypothesis, its implementation, its counters or reducer tests,
and any directly necessary documentation. Keep measurement infrastructure and
compiler behavior changes in separate commits.

If an experiment loses or is neutral, revert only that experiment and record
the result in the experiment ledger. Do not leave speculative code behind and
do not hide a loss inside a later win.

### 5.5 Inception cadence

Inception is not part of the per-commit loop. Plan for no more than three
scheduled runs unless a failure requires a focused investigation:

1. after the first high-risk native/IR batch, or when the benchmark reaches
   roughly 20--22 seconds;
2. after a frontend representation/semantic batch that changes the code used
   to compile the compiler itself; and
3. on the final clean sub-15-second commit.

Run inception only from a clean committed tree. If it fails, reduce the issue
and place the regression at the earliest owning PA before continuing.

## 6. Phase budget

The current 29-second compile cannot reach the goal through one micro-tweak.
The following budgets set priorities and make the required cumulative savings
explicit:

| Phase | Provisional baseline | Target budget |
| --- | ---: | ---: |
| Preprocess | 2.71 s | 1.25 s |
| Parse | 0.55 s | 0.45 s |
| Semantic analysis | 9.29 s | 5.75 s |
| Typed-to-LowIR | 1.47 s | 1.00 s |
| PA37 LowIR optimization | 5.02 s | 1.80 s |
| Native lowering | 7.58 s | 2.50 s |
| Machine optimization | 0.22 s | 0.18 s |
| Encoding | 0.97 s | 0.70 s |
| Payload serialization | 0.36 s | 0.25 s |
| Other/preparation/write margin | about 0.9 s | 0.6 s |
| **Total** | **about 29.1 s** | **about 14.5 s** |

These are directional budgets, not mandates to distort the compiler. After
each batch, replace them with new measurements. If one phase beats its budget,
the saved margin can cover a harder phase.

## 7. Staged changesets

### Phase 0: Reproducible measurement and observability

#### 0A. Repository-owned GNU-time A/B harness

Add a script in this repository that:

- runs immutable A and B executables in ABBA order;
- invokes the frozen source without modifying the extended checkout;
- records wall/user/system/RSS, hashes, sizes, load, and CPU pressure;
- rejects mismatched exit status immediately;
- distinguishes exact-byte and deterministic-byte modes; and
- writes machine-readable results plus a concise summary.

Do not copy Fable's BSD `time -lp` assumption; this host uses GNU time.

Acceptance: validate the script with an A/A session and a deliberately
different executable or harmless wrapper fixture.

#### 0B. Non-overlapping phase timers

Document and enforce non-overlapping timer boundaries for frontend,
typed-to-LowIR, PA37, native lowering, machine optimization, encoding, and
writing. Remove or rename overlapping totals such as the current ambiguous
`adapt_ns` display.

Acceptance: the sum is within a documented small margin of compiler user time,
and disabled counters add no meaningful timing noise.

#### 0C. Exact work counters

Add low-overhead, disabled-by-default counters needed by the first tracks:

- interner calls, hits, misses, input bytes, probe lengths, comparisons,
  rehashes, and table capacity;
- native value-state entries, full-map scans, scanned entries, alias queries,
  live-location updates, spill candidates, and spill decisions;
- PA37 pass eligibility, dirty blocks/functions, visits by pass, and skip
  reasons; and
- semantic lookup-cache hits/misses, invalidations, dependency edges, template
  retries or candidate work, and allocation ownership already available in
  `SemanticAnalysisStats`.

Acceptance: counters explain the existing profile and remain consistent across
repeat runs.

### Phase 1: Native value/register bookkeeping

Likely earliest owners: PA29/PA30, depending on the exact invariant changed.

This is first because measured native register/value maintenance is the
largest concentrated CPU owner and its current operations scan all live values
inside other scans.

#### 1A. Incremental location occupancy

Replace `live_location_counts()` rescans with exact per-location live counts
maintained when a value is defined, moved, spilled, consumed, or retired.
Centralize transitions so every mutation updates the index once.

`has_live_location_alias` should become an O(1) lookup while preserving its
current semantics exactly.

Gates:

- exact object bytes for the frozen benchmark;
- counters show full live-value scans removed from alias checks;
- regressions for multiple names sharing a location, parameter retirement,
  edge-live values, calls/clobbers, and cyclic moves; and
- PA29/PA30 plus selected PA36/PA37/PA38 reports.

#### 1B. Reverse occupant and spill-candidate indexes

Maintain the values occupying each GPR/XMM location and a compact set of
eligible spill candidates. Make `spill_one` examine viable candidates rather
than the entire string-keyed value table.

Preserve the existing farthest-use choice and deterministic tie order. Any
change in chosen register or spill is treated as a backend behavior change,
not dismissed as harmless.

Gates: reduce scanned spill candidates sharply, retain exact bytes unless a
reviewed decision correction is intentional, and add owner-level reducers for
every changed corner case.

#### 1C. Dense per-function value IDs

Intern LowIR value names once per function, assign dense IDs, and use vectors
for value facts, use counts, last uses, spill state, and location ownership.
Keep original spellings only where diagnostics or serialized output require
them.

Do this after 1A/1B so the invariant changes are separately reviewable.

Phase target: native lowering at or below 2.5 seconds, with the named native
hotspots no longer prominent and with bounded work per MIR instruction.

### Phase 2: PA37 pass eligibility and incremental scheduling

Earliest owner: PA37 for optimizer transformations. A source-semantic bug found
by these tests still belongs to its earlier semantic PA.

#### 2A. Per-function eligibility summaries

Compute cheap function facts once and skip passes that cannot apply. The first
target is slot optimization: the current run invokes slot-family passes 43,811
times over 9,390 functions, many of which have no relevant slot behavior.

Also summarize presence of branches, calls, phis, memory operations, and
rewriteable value forms where those facts provably rule out a pass.

Gates: exact LowIR and object bytes; counters must attribute every skip to a
sound eligibility fact.

#### 2B. Compact value, slot, block, and def/use facts

Assign dense function-local identities and retain reusable block, def/use, and
slot facts. Avoid rebuilding string-keyed sets and maps in each pass.

Gates: unchanged transform decisions and output order, lower allocation and
hash counts, and exact optimized LowIR bytes.

#### 2C. Dirty worklists instead of whole-function reruns

When a rewrite changes a block or value, revisit only the dependent blocks and
facts. Fuse adjacent analysis walks only when their current ordering and
observable decisions remain identical. Preserve a bounded convergence policy;
do not introduce unbounded fixpoint retry.

This is higher risk. Add reduced PA37 tests for every dependency edge that can
reenable simplify, DCE, CFG, or slot work.

#### 2D. Inliner follow-up

Only after reprofiling, cache immutable callee summaries and remove measured
inliner scans/copies. The current inliner is about 0.73 seconds, so it is not a
reason to delay 2A--2C.

Phase target: PA37 at or below 1.8 seconds and instruction visits below roughly
four times input, unless counters demonstrate necessary work not represented
by that ratio.

### Phase 3: Frontend interning and syntax identities

The shared string table first becomes contract-visible at PA10's syntax-tree
boundary. PA4 owns preprocessing behavior but does not use `frontend_intern`;
therefore PA10 is the earliest owner for the interner and syntax-tag changes,
followed by semantic downstream tests.

#### 3A. Split interner traffic by caller and purpose

Use the Phase 0 counters to distinguish:

- first-time token spellings;
- repeated token lookups;
- grammar/syntax tag strings;
- identifier/type/member strings; and
- substring/range cases that require hashing bytes.

Do not redesign the table until this split identifies the dominant source of
the measured 7.3% `InternRange` cost.

#### 3B. Preinterned token and grammar-tag IDs

Give fixed token kinds and grammar tags stable IDs within an invocation. Syntax
`Make`, `IsTag`, `HasDirectChildTag`, and `HasDescendantTag` must compare IDs,
not reintern a string literal in a hot traversal.

Preserve first-use ordering for dynamically interned source spellings when that
ordering is observable.

Gates: exact token/syntax behavior, PA4 and PA9/PA10 regressions, exact frozen
object bytes, and a measured fall in interner calls and input bytes.

#### 3C. Range interner mechanics

After 3A/3B, improve only mechanisms still shown hot: reuse a hash already
computed by tokenization, strengthen hash finalization if probe histograms show
clustering, tune capacity/reservation, or avoid redundant byte comparison on a
proven miss path.

Every hash-table change must report maximum and percentile probe lengths and
must preserve canonical equality. A faster average with pathological probes is
not acceptable.

#### 3D. Frontend allocation census and reservation

Measure token, syntax node, edge, string-byte, and temporary-buffer growth.
Reserve exact or high-confidence capacities and eliminate repeated literal
string construction only where counts show meaningful ownership.

Phase target: preprocessing at or below 1.25 seconds, parse at or below 0.45
seconds, and `InternRange` below roughly 2% of samples.

### Phase 4: Semantic lookup, cache maintenance, and allocation

Likely owners span PA11 and later semantic assignments. Each change must name
its earliest contract owner before editing tests.

This compiler already has compact `NameId` values, a scope-aware lookup cache,
dependency tracking, and extensive semantic counters. Fable's recommendation
to add a scope index is therefore not directly applicable.

#### 4A. Publish existing semantic counters

Expose the full available lookup, overload, template, dependency, invalidation,
and allocation statistics through the production driver's opt-in stats mode.
Measure `semantic_model.cpp` and the primary frozen source.

#### 4B. Audit cache value versus maintenance cost

The profile contains lookup-cache dependency and invalidation routines, but a
hot cache is not necessarily a useful cache. Build measurement-only modes that
disable a cache or verify every hit by recomputing the uncached answer.

Use verify-on-hit before any risky key reduction or dependency simplification.
Keep a cache only when avoided lookup work exceeds keying, dependency, and
invalidation work on the actual benchmark set.

#### 4C. Make invalidation and dependencies precise

If counters show broad invalidation, index dependencies by compact scope/name
identity and update only affected entries. If they instead show low reuse,
remove or narrow the unprofitable cache rather than optimizing its machinery.

#### 4D. Semantic allocation ownership

The provisional semantic peak exceeds 1 GB. Attribute bytes and live peaks to
declarations, type objects, overload candidates, lookup entries, template
state, and scratch collections. Prefer per-analysis arenas, slabs, dense
side-table vectors, reusable scratch buffers, and proven `reserve()` changes.

Do not retain whole semantic objects merely to improve cache hit rate, and do
not add a process-global cache.

Phase target: semantic analysis at or below 5.75 seconds with lower or
well-justified peak RSS.

### Phase 5: Lowering, encoding, and remaining allocation

Reprofile before choosing work here. Candidate investigations include:

- dense IDs in typed-to-LowIR maps that still hash stable string names;
- per-function and per-block MIR capacity reservation;
- earlier release/reuse of function-local transient state;
- avoiding measured `Instruction` vector reallocations and moves;
- compact fixup bookkeeping; and
- eliminating repeated encoding-size work only if counters prove it.

Do not optimize payload reservation first: the current serializer grows once
and reports no full-buffer copies. Its measured 0.36 seconds cannot explain
the goal gap.

Targets: typed lowering at or below 1.0 seconds, encoding at or below 0.7
seconds, and payload serialization at or below 0.25 seconds.

### Phase 6: Reprofile and close the measured gap

At roughly 20--22 seconds and again near 16--18 seconds:

1. freeze a clean accepted commit;
2. rerun exact phase counters and a flat CPU profile;
3. run callgrind or allocation census only for ambiguous new leaders;
4. replace the priority list and phase budgets with the new evidence; and
5. choose only work large enough to move the remaining gap.

Possible final work is deliberately not prescribed now. The dominant profile
after Phases 1--4 is more reliable than the current profile for selecting it.

## 8. Explicitly deferred or rejected strategies

These are outside the current goal unless new evidence and a separate decision
change the scope:

- PGO;
- benchmark-specific dispatch or precomputed output;
- persistent cross-run caches;
- host compiler or reference-compiler assistance;
- stale LowIR performance fixtures;
- `-march=native` as a substitute for algorithmic improvement;
- LTO as the primary optimization plan;
- parallel compilation of one translation unit;
- switching the global allocator before an ownership census;
- broad memoization without completeness and invalidation proofs; and
- rewrites justified only by fewer lines of code or a microbenchmark.

PGO may be evaluated after the intrinsic sub-15-second goal is met, but its
result is reported separately and never used to qualify this plan.

## 9. Experiment ledger

Update this table after every accepted or rejected experiment. Times are
medians from the protocol above, not isolated best runs.

| Commit/experiment | Hypothesis and change | Structural result | User / wall / RSS | Output gate | Tests | Decision |
| --- | --- | --- | --- | --- | --- | --- |
| `f3ac28a` provisional | Unified default O2 path and prior correctness fixes | Baseline counters in section 3 | 27.32 s / 29.10 s / 938,608 KiB, single loaded-host run | SHA in section 3 | Cumulative report clean; inception predates final refactor | Establish statistical baseline in Phase 0 |
| `82d1a8a7`, `409870e7` | GNU-time ABBA harness with output and interrupted-session gates | Real A/A smoke showed a false 5.2% paired wall-time difference under host drift | A/A runs ranged from 28.06 s to 112.83 s while other host compiles started | Exact bytes | Five harness unit tests; file audit has zero fatal findings | Accepted; use structural counters and predeclared load screens |
| Phase 0C native counters | Measure live-location, spill, and dead-parameter scans before changing them | 63,977 occupancy scans visited 25,281,680 values; 4,249 spill attempts visited 10,322,866 values for 1,576 candidates and 877 spills | 26.15 s / 27.87 s / 940,536 KiB, single stats run | 93,544,200 bytes | PA29 195/195; through PA29 4,084/4,084; selected PA30/36/37/38 290/290 | Accepted; proceed with incremental occupancy |
| Phase 1A incremental occupancy | Maintain live GPR/XMM counts on value definition, movement, and last consumption | Occupancy scans and their 25,281,680 visits fell to zero; 179,458 constant-time updates; native lowering 7.49 s to 2.63 s | Three ABBA blocks: paired median user -16.95%, wall -16.26%, RSS -0.07%; individual runs retained despite 23.04--69.45 s host contention | Exact 93,544,200-byte object and baseline SHA | PA29 195/195; through PA29 4,084/4,084; selected PA30/36/37/38 290/290 | Accepted; native spill scans are now the next measured owner |
| Phase 1B indexed spill occupants | Search only live register occupants; fall back to the original map order for a tied farthest use | Spill visits 10,322,866 to 41,639; alias queries 1,489,578 to 72,344; 116 tie fallbacks; native lowering 2.63 s to 1.96 s | Five ABBA blocks against Phase 1A: paired median user -11.30%, wall -9.11%, RSS -0.01%; four of five blocks favored the candidate | Exact 93,544,200-byte object and baseline SHA | PA29 195/195; through PA29 4,084/4,084; selected PA30/36/37/38 290/290 | Accepted; reprofile before further native work |
| Phase 3A interner counters | Attribute table work to token, syntax, parser, and semantic callers without changing normal output | 48,345,105 calls: 48,205,477 hits, 139,628 misses, 805,094,824 hash bytes, 62,736,301 occupied probes; syntax tag queries alone account for 43,903,384 calls | Loaded-host structural run only: 80.02 s / 85.45 s / 940,876 KiB; excluded from timing evidence | Exact 93,544,200-byte object and baseline SHA | PA10 164/164; through PA10 583/583; selected PA11/12/15/29/30/36/37/38 848/848 | Accept instrumentation; eliminate repeat tag-query interning while retaining first-use identity ordering |
| Phase 3B syntax tag identity cache | Cache compiler-owned static tag pointers only after the ordinary interner establishes their first-use identity | 43,592,963 tag-cache hits; total calls 48,345,105 to 4,752,142; hash bytes 805,094,824 to 104,260,499; occupied probes 62,736,301 to 8,112,257 | Three ABBA blocks versus Phase 1B: paired median user -12.31%, wall -11.81%, RSS +0.04%; normal retained sample 17.48 s / 19.18 s / 941,392 KiB | Exact 93,544,200-byte object and baseline SHA | PA10 164/164; through PA10 583/583; selected downstream 848/848; through PA38 5,153/5,153 | Accepted; source-location and token-spelling repetition are the next measured interner owners |
| Phase 3C source provenance cache | Make the tag cache non-evicting, cache the current file identity after first interning, and notify the token sink only when an output token is emitted | Tag-cache misses stabilize at 169; 490,575 source-file hits versus 287 misses; location notifications 981,860 to 490,862; total interner calls 4,752,142 to 2,710,196 | Five ABBA blocks versus Phase 3B: paired median user -0.89%, wall +1.87%, RSS -0.12%; two blocks had large mid-block host spikes, so timing is inconclusive | Exact 93,544,200-byte object and baseline SHA | PA2 26/26; through PA2 79/79; PA10 164/164; through PA10 583/583; selected downstream 848/848 | Accepted for stable tag-cache coverage and eliminated work; do not credit it toward the time goal without later corroboration |
| Phase 5A in-place LowIR simplification | Compact each block's instruction vector in place instead of allocating and filling a replacement vector on every simplifier run | Pass/rewrite counts unchanged; simplify time 1.572 s to 1.122 s and total LowIR optimization 5.238 s to 4.704 s in retained stats runs | Five ABBA blocks versus Phase 3C: paired median user -2.56%, wall -3.12%, RSS +0.26% | Exact 93,544,200-byte object and baseline SHA | PA37 86/86; through PA37 5,127/5,127 | Accepted; continue eliminating per-pass allocation while preserving the PA37 optimized text/object contract |
| Phase 5B in-place LowIR rewrite passes | Compact DCE, local/single-store forwarding, dead-slot removal, and promotion results in place | Pass/rewrite counts unchanged; DCE 0.434 s to 0.336 s, slot passes 1.369 s to 0.994 s, total LowIR optimization 4.704 s to 4.196 s | Five ABBA blocks versus Phase 5A: paired median user -1.48%, wall -2.19%, RSS +2.06% | Exact 93,544,200-byte object and baseline SHA | PA37 86/86; through PA37 5,127/5,127 | Accepted; retained capacity explains the measured RSS increase and remains below the 3% guardrail |
| Phase 4A contiguous lookup dependencies | Replace per-entry and per-scope dependency vectors with contiguous append-only arenas and intrusive indices; remove a cache-to-cache dependency path that had no producer | 376,227 entries and 619,654 scope/name owners formerly held about 310,000 spilled tiny vectors; 1,514,641 registrations now append to two arenas; cache storage accounting fell 149,547,808 bytes and semantic analysis fell from 6.34 s to 5.94 s in comparable stats runs | Three ABBA blocks versus Phase 5B: paired median user -10.82%, wall -11.24%, RSS -0.06%; candidate medians 15.485 s user and 17.090 s wall | Exact 93,544,200-byte object and baseline SHA | PA11 68/68; through PA11 653/653; selected PA12/15/36/37 459/459 | Accepted; reprofile because allocation and frontend work have shifted substantially |
| Phase 2A translation fast path | Send only `?` and backslash through phase-1 trigraph/UCN/splice lookahead, and keep lexer value/location lookahead in one queue | Ordinary source characters bypass three nested queues; lexer consumption performs one packed queue operation instead of three parallel operations | Three ABBA blocks versus Phase 4A: paired median user -4.50%, wall -4.11%, RSS -0.01%; candidate medians 14.825 s user and 16.445 s wall | Exact 93,544,200-byte object and baseline SHA | PA1 30/30 plus course 23/23; through PA1 53/53; selected PA2/10/12/30/37 554/554 | Accepted; retain the complete slow path for every character that can begin a phase-1 transformation |
| Phase 4B remove lookup result cache | Recompute unqualified lookups instead of maintaining a cache whose invalidation and dependency bookkeeping costs exceed its hits on declaration-heavy input | Scope visits rise from 1,834,009 to 3,139,714, but 1,514,641 dependency registrations and all cache invalidation work disappear; semantic storage accounting falls by 157,283,872 bytes and 452 lines of cache machinery are removed | Three ABBA blocks versus Phase 2A: paired median user -4.36%, wall -4.78%, RSS +0.01%; candidate medians 13.920 s user and 15.365 s wall, including one retained 16.92 s host-load spike | Exact 93,544,200-byte object and baseline SHA | PA11 68/68; through PA11 653/653; selected PA12/15/36/37 459/459 | Accepted; reprofile the simpler lookup path and continue toward the final calm-host wall-time gate |
| `a5c6a2a6` semantic allocation and pair hashing | Reserve the declaration vector from the completed syntax tree and use a one-multiply mixer for 32-bit scope/name pairs | Binding capacity falls from 1,048,576 to 583,346; accounted semantic peak falls by about 100.5 MB; type and template hashes retain the stronger general mixer | A combined session with Phase 5C was invalidated when another checkout began sustained compilation; normal direct screening moved from 15.47 s / 13.96 s to 14.70 s / 13.46 s | Exact 93,544,200-byte object and baseline SHA | PA11 68/68; PA12 166/166 plus course 14/14; PA37 86/86; through PA38 5,153/5,153 | Accepted for the measured allocation reduction; do not treat the corrupted paired session as timing evidence |
| `8ac2226d` compact LowIR graph storage | Keep the common first two CFG edges inline and avoid moving every instruction when a single-block dead-store pass makes no change | Common CFG nodes no longer allocate an inner vector; the dead-store pass uses one byte mask and compacts only after one of its 131 changes | Shared loaded-host screening with the prior row reached 14.70 s / 13.46 s in a normal direct run; its ABBA session is excluded because unrelated self-host/Valgrind work raised runs to 31--39 seconds | Exact 93,544,200-byte object and baseline SHA | PA37 86/86; through PA38 5,153/5,153 | Accepted for eliminated allocations and unchanged work/output; final cumulative timing must corroborate it |
| `7d9484ab` small-scope name index | Answer lookups in scopes with at most four names from an intrusive local list before touching the large global scope/name hash table | Empty and tiny scopes avoid the global table; every entry remains in that table so crossing the threshold requires no rebuild and preserves lookup order | Two ABBA blocks versus the prior compiler: paired user -2.67%, wall -2.26%, RSS +0.12%; a reversed-order screen also favored the candidate by 1.53% user | Exact 93,544,200-byte object and baseline SHA | PA11 68/68 plus course 2/2; PA12 166/166 plus course 14/14; through PA38 5,153/5,153 | Accepted; retain the global index as the exact fallback for larger scopes |
| `00baa1a7` semantic model reservation | Use syntax-node count as a general scale hint for binding, name-entry, visible-name, scope, and child-edge vectors; use a two-thirds hint for scopes to avoid excess capacity | Frozen final sizes fit without growth (575,345 bindings, 513,199 name entries, 526,444 visible names, and 334,770 scopes); ordinary growth remains available for expansion-heavy inputs | Initial all-vector hint over two ABBA blocks: paired user -1.39%, wall -0.84%, RSS -0.04%; final tighter scope hint direct screen 13.55 s user / 14.94 s wall | Exact 93,544,200-byte object and baseline SHA | PA11 68/68 plus course 2/2; PA12 166/166 plus course 14/14 | Accepted for removing known vector reallocations with bounded memory; include in the next cumulative gate |
| `56035dff` local type hashing | Keep the unchanged canonical-type hash combine in the TypeTable implementation so hot callers inline it without exposing implementation through the header | Removes repeated out-of-line `MixHash` calls while retaining the exact hash and insertion order | One ABBA block: paired user and wall about -1.2%, RSS +0.03% | Exact 93,544,200-byte object and baseline SHA | PA11 and PA12 owner suites clean | Accepted as a local hot-path reduction with exact hash behavior |
| `90c1e7de` semantic index sizing | Pre-size entry, visible-name, and canonical-type open-address indexes from syntax-node count | Avoids every intermediate index rehash and type-vector growth; ordinary growth remains available | Entry/name stage over two ABBA blocks: paired user -1.04%, wall -0.82%, RSS -0.12%; added type reserve produced 14.45 s wall / 13.25 s user candidate medians and paired wall -1.16% | Exact 93,544,200-byte object and baseline SHA | PA11 and PA12 owner suites clean | Accepted for eliminating measured growth and preserving open-address behavior |
| `23343174`, `e692961b` explicit tool control and audit separation | Replace hidden statistics/include-path environment behavior with `--stats`, `-nostdinc`, and `-isystem`; have the PA34 harness translate its legacy reference sidecar into explicit options | File audit falls to zero fatal findings; PA37/PA30/PA31 telemetry remains available only when requested; the existing PA34 include-next owner test exercises explicit include roots | No intended compiler performance change; post-refactor direct screen 15.05 s wall / 13.73 s user under host variation | Exact frozen object and baseline SHA | Affected PA3--9, PA14, PA28/29/37 suites clean; PA34 370/370; through PA38 5,153/5,153 | Accepted as the required file-audit and reproducibility cleanup |
| `f597c4ef` transient PA37 arenas | Give short-lived simplify, DCE, slot, and forwarding hash nodes a stack-backed monotonic arena while retaining their existing key, hash, and traversal types | PA37 elapsed falls from about 3.75 s to 3.27 s; simplify falls from 0.96 s to 0.75 s and DCE from 0.315 s to 0.173 s with identical pass counts and 198,087 rewrites | Two ABBA blocks on the core arena candidate: paired wall -1.55%, user -2.59%, RSS +0.17%; candidate medians 14.645 s wall / 13.195 s user | Exact 93,544,200-byte object and baseline SHA | PA37 86/86; through PA38 5,153/5,153 | Accepted; transient allocations are released at each pass boundary and never become semantic state |
| `d74971d5` child tag reuse and interner sizing | Intern a direct-child query tag once per scan and size the translation-unit spelling index from source scale before preprocessing | Removes 540,488 redundant tag-cache probes; interner rehashes fall 13 to 0, rehash hash bytes 7,087,253 to 0, and occupied probes 3,596,705 to 2,846,493 | Final five-run gate: median 13.09 s user / 14.39 s wall / 963,672 KiB RSS; direct two-block comparison against `f3ac28a`: paired wall -48.55%, user -50.22%, RSS +2.38% | All candidate and comparison objects exactly 93,544,200 bytes with baseline SHA | PA10 164/164; through PA10 583/583; selected PA11/12/37/38 362/362; through PA38 5,153/5,153 | Accepted; final non-PGO target and 14.5-second safety margin are met |
| Rejected: inline `MixHash` | Put the unchanged semantic-table hash body in the header so callers can inline it | Removed the 1.16% standalone profile symbol, but expanded the body across semantic callers | Three ABBA blocks versus Phase 2A: paired median user -1.07%, wall -1.12%, RSS effectively unchanged; one candidate outlier dominated its raw median | Exact 93,544,200-byte object and baseline SHA | Exact frozen compile only; reverted before assignment reports | Rejected as below the 3% noise/acceptance threshold |
| Rejected: syntax access microcache | Add a last-tag entry, cheaper pointer indexing, and inline trivial syntax-arena accessors | Targeted the 2.48% `InternTag`/`IsTag` pair plus small accessor symbols, but did not reduce aggregate CPU work | Three ABBA blocks versus Phase 2A: paired median user -0.10%, wall +1.10%, RSS effectively unchanged | Exact 93,544,200-byte object and baseline SHA | Exact frozen compile only; reverted before assignment reports | Rejected; call-site patterns and code-size effects erased the local savings |
| Rejected: dense LowIR value index | Build one name index per function and use vector facts for simplification and DCE; skip a CFG visit made redundant by the following DCE/CFG pair | LowIR stats elapsed fell from 3.75 s to 3.52 s and DCE from 0.29 s to 0.19 s; a custom expression table subexperiment regressed and was removed before the paired run | Three ABBA blocks versus Phase 2A: paired median user -0.31%, wall -0.03%, RSS +0.18% | Exact 93,544,200-byte object and baseline SHA | PA37 86/86 after an existing CFG test caught and localized a C++11 insertion-order bug | Rejected; building and probing the shared index offset the allocator savings end to end |
| Rejected: one-entry semantic lookup cache | Retain only the most recent direct name result per scope with complete negative-result invalidation | 1,622,997 hits and 2,254,866 misses, but semantic time stayed near 4.95 seconds and accounted storage grew 4,194,304 bytes | Normal direct screen regressed to 15.22 s / 13.92 s | Exact 93,544,200-byte object and baseline SHA | Exact frozen compile only; reverted before assignment reports | Rejected; even minimal result-cache maintenance costs more than recomputation |
| Rejected: compact syntax tag cache | Shrink the pointer-keyed tag cache from 4,096 to 512 entries so its working set fits L1 | Storage shrank, but the 169 live tag pointers incurred more open-address probes | Two ABBA blocks: paired user +4.21%, wall +4.07%, RSS -0.12% | Exact 93,544,200-byte object and baseline SHA | Exact frozen compile only; reverted | Rejected; collision probes dominate the footprint reduction |
| Rejected: syntax tag fingerprints and literal slots | Filter tag mismatches with a node fingerprint, then try a compile-time literal hash into a separate direct cache | The fingerprint removed about 36.3 million pointer-cache probes; the literal cache reduced ordinary tag-cache hits from 44.7 million to 9.4 million, but expanded hot call-site code and added up to 512 KiB | Fingerprint two-block result: paired user -0.86%, wall -0.59%; literal-cache result: paired user +1.37%, wall +1.01% | Exact 93,544,200-byte object and baseline SHA | Exact frozen compile only; both variants reverted | Rejected; large structural counter movement did not become end-to-end speedup |
| Rejected: merged LowIR type/definition facts | Fold the simplifier's string-keyed type map into its definition map | Simplifier telemetry fell from about 0.887 s to 0.857 s, but constructing large definition records during the type census offset the removed table | Direct whole-compile screening showed no improvement | Exact 93,544,200-byte object and baseline SHA | Exact frozen compile only; reverted | Rejected; retain compact type records and separate availability order |
| Rejected: cheaper tag-cache pointer hash | Replace the mixed pointer hash with shifts only | Preserved exact output, but did not improve the direct whole-compile screen and reduced collision protection | Single direct screen 13.39 s user / 14.82 s wall; no structural benefit supported retaining it | Exact 93,544,200-byte object and baseline SHA | Exact frozen compile only; reverted | Rejected before assignment reports; retain the measured robust mixer |

For a rejected experiment, record the temporary commit or patch identifier,
the counter movement, and the reason for rejection even after reverting it.

## 10. Completion criteria

The project is complete only when one clean commit satisfies all of the
following:

1. five frozen benchmark runs in a predeclared calm-host window have median
   wall time at most 14.5 seconds;
2. interleaved comparison against the original baseline demonstrates that the
   gain is not host-load noise;
3. output is deterministic, and every intentional byte change is explained
   and covered at its owning PA;
4. peak RSS does not regress by more than 3% without a documented and measured
   tradeoff;
5. `make test-report-through-pa38` is clean;
6. the file audit has no fatal findings;
7. root `make inception` passes from that exact clean commit;
8. all reducers live at the earliest owning assignments; and
9. the plan ledger, final counters, commands, and commit hashes are current.

The final commit is then pushed only after the tests, audit, benchmark, and
inception evidence all refer to that same tree.

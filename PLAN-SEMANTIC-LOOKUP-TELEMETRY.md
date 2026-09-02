# Plan: Semantic Lookup and Telemetry Hot Paths

Status: complete; lookup cache rejected, telemetry-off retained, and table
consolidation closed audit-only

Date: 2026-09-02

## Objective

Reduce avoidable semantic-analysis work without restoring mechanisms whose
maintenance cost has already exceeded their benefit.  This plan will:

- remove the five core lookup-cache statistics that cannot change because the
  corresponding cache no longer exists;
- measure repeated core lookups without changing normal compiler behavior,
  then retain a narrow cache only if an immutable, graph-expensive population
  proves profitable;
- provide and measure a compile-time telemetry-off production configuration
  that removes purely observational hot-path updates while preserving the
  normal statistics-capable build; and
- continue compact-table consolidation only when an exact shared mechanism has
  an obvious performance or simplification benefit.

The work must preserve all student-facing source, semantic, LowIR, MIR, native,
object, executable, and diagnostic-exit contracts.  No test may inspect
production source, private class/function names, or exact implementation
content.  Any new semantic behavior must be described at the earliest owning
assignment and tested structurally or behaviorally there.

## Governing rules

1. Do not restore the former general `(scope, name, kind)` result cache.  It is
   a rejected baseline, not an untried design.
2. Diagnostic lookup measurement is enabled only when statistics are requested
   and must not execute in normal compilation or performance lanes.
3. A retained lookup cache must target a population that is materially
   different from both rejected designs: stable complete-class lookup after
   actual base/using graph traversal is the initial candidate.  Cheap direct,
   lexical, and ordinary namespace lookup remains uncached.
4. Cache correctness must follow semantic lifetime.  A class cannot be cached
   while its members, direct bases, specializations, or replay state can still
   change.  If that lifetime cannot be proved locally, reject the cache.
5. Telemetry-off may remove only observation.  IDs, generations, epochs,
   recursion/depth state, capacity decisions, cache state, and any counter read
   by production policy remain active regardless of configuration.
6. The normal build remains statistics-capable.  A telemetry-off executable
   must reject `--stats` and `--stats-functions` clearly rather than emit
   plausible zero values.
7. A common table primitive must preserve zero sentinels, exact growth points,
   probing order, stable IDs, hash/equality call behavior, counters, allocation
   timing, storage accounting, and resource checks.  Similar-looking loops are
   insufficient evidence.
8. Do not replace the semantic stable-ID indexes with `std::unordered_map` or
   the existing tombstone-based `flat_hash_map`.
9. Do not merge the PA7 namespace-declaration engine, PA8 namespace-
   initialization engine, and core semantic model.  Their staged contracts and
   mutation lifecycles remain separate; only proven low-level mechanics may be
   shared.
10. Performance retention uses same-source controls.  Use exact output hashes,
    task-clock or aggregate CPU as the primary work metric, wall time as a
    secondary scheduling metric, and GCC/Clang normalization when producer
    code changes.
11. A result below 1% on repeated source-diverse CPU measurements receives no
    performance credit unless the change also provides an obvious, independently
    valuable simplification with no repeated regression.
12. Use 32-way outer, inner, and object parallelism for every self/inception
    run.  Before timing, verify that no stale Cachegrind, Valgrind, `perf`,
    benchmark, or inception process is running.
13. Run focused fast checks for every increment.  Commit each bounded retained
    increment, and push after no more than three retained commits and
    immediately after a high-risk semantic or build-configuration milestone.

## Starting evidence

The planning checkpoint is `07ea39fc` on clean `v3opt`, synchronized with
`origin/v3opt`.

The core `semantic::Program` publishes five cache counters, but the current
tree has no core result cache and no increment of those counters.  Core lookup
already indexes using relations by the names they can publish, so graph lookup
does not scan every using edge.

Targeted current-source statistics gave this initial workload shape:

| Translation unit | Queries | Scope visits/query | Edge visits/query |
| --- | ---: | ---: | ---: |
| `semantic/model/program.cpp` | 213,300 | 3.26 | 0.16 |
| `semantic/templates/classes.cpp` | 385,127 | 4.00 | 0.29 |
| `semantic/templates/function_instantiation.cpp` | 380,087 | 3.96 | 0.28 |
| `preprocess/preprocessor.cpp` | 39,074 | 2.52 | 0.09 |

The apparently missing broad cache is historical rejected work:

- `92c06acd` added precise dependency-aware core lookup caching;
- `27d4e94e` removed 452 lines of it;
- the removal increased scope visits from 1,834,009 to 3,139,714 but eliminated
  1,514,641 dependency registrations, reduced semantic storage by 157,283,872
  bytes, and improved paired median user/wall time by 4.36%/4.78%; and
- a later one-entry cache achieved 1,622,997 hits and 2,254,866 misses but
  regressed the direct workload and added 4,194,304 accounted bytes.

The compact-table audit in `PLAN-COMPILER-CORE-DEDUPLICATION.md` also already
established that only template-argument partitions and function-template result
identities had byte-equivalent cached-hash rebuilds.  They now share
`RebuildCachedHashSlots`; the remaining tables have distinct hashes, duplicate
or overwrite policies, sequences, or request-state transitions.

## Validation ladder

### Fast checks

- rebuild the directly changed objects and `dev/cppgm++`;
- run a representative PA11 lookup set covering using directives, using
  declarations, qualified lookup, inheritance, ambiguity, overloads, and
  templates;
- run any focused script/unit test added for the telemetry build mode;
- run `make test-pa11` and the current architecture/file audits after retained
  semantic increments.

### Cumulative checks

- `make test-report-through-pa11` after core lookup/counter changes;
- selected PA12, PA20, PA23, PA37, and PA38 suites when their semantic/table or
  generated-code surfaces are touched;
- `make test-report-through-pa38` after every retained performance mechanism;
- debug/round-trip checks relevant to changed later surfaces and all
  architecture/file audits at closure.  The optional strict-witness lane is
  required only when witness logging changes: the production driver has never
  emitted that reference-maintainer artifact;
- final root `make inception` with explicit 32-way PA39 settings.

Output comparisons are byte-exact where the repository supplies serialized or
object references.  Failing semantic fixtures compare failure status, not
diagnostic text.

## Phase L0: Freeze the baseline

1. Record the source/compiler hashes, compiler section sizes, architecture
   audits, file audit, and current fast test counts.
2. Verify no stale profiling or inception process is consuming the host.
3. Preserve one fixed source-diverse compiler workload and the four targeted
   translation units above for diagnostic comparisons.
4. Record normal O1/O3 self and same-source GCC/Clang controls from the most
   recent valid plan ledger; refresh only the lanes required to judge a
   retained candidate.

Commit boundary: the plan and baseline ledger only.  Push it before production
changes.

## Phase L1: Remove impossible lookup-cache telemetry

1. Remove the five dead fields from core `semantic::Program`, semantic `Stats`,
   lowering aggregation, and all integrated `cppgm++` reports.
2. Leave the real PA7 and PA8 standalone cache counters untouched.
3. Keep `lookup_queries`, `lookup_scope_visits`, and `lookup_edge_visits`; they
   describe the current implementation and feed the next experiment.
4. Prove by repository-wide search that no core report still advertises an
   impossible counter.

No new student-facing fixture is needed because this removes unsupported
diagnostic fields and changes no language behavior.  Existing PA7/PA8 cache
invalidation fixtures continue to cover the standalone caches.  PA11 lookup
behavior and through-PA11 are the fast semantic gates.

Commit and push this bounded cleanup after its gates pass.

## Phase L2: Measure stable repeated expensive lookup

Add temporary stats-only shadow measurement around graph lookup:

- identify the logical key by start scope, name, lookup kind, and candidate
  ambiguity mode;
- store the prior result only in a diagnostic structure enabled by a non-null
  stats request;
- always recompute the authoritative result and compare structural result
  identity afterward;
- count repeat keys, stable versus changed repeats, complete-class repeats,
  result families, and the scope/edge visits that a hit could have avoided;
- distinguish direct-return lookups from calls that actually traversed a base
  or using edge; and
- account all diagnostic storage separately.

Run the four targeted translation units and a 32-way all-source stats workload.
The shadow structure is an experiment: remove it after the decision unless its
diagnostic value clearly justifies permanent stats-only complexity.

### L2 decision gate

Proceed to L3 only if complete-class, graph-traversing repeats are stable and
numerous enough to plausibly clear the 1% end-to-end CPU gate.  Changed results,
mostly direct hits, or a small avoided-work population rejects L3 without a
production prototype.

Record and commit the measurement result in this plan.  Do not commit temporary
instrumentation that has no lasting diagnostic value.

## Phase L3: Conditional complete-class graph cache

This phase exists only if L2 passes.

1. Cache only a lookup beginning in a provably complete class and only after
   the first computation traversed a base/using graph.
2. Exclude lexical unqualified lookup, mutable namespace lookup, and cheap
   direct results.
3. Prefer an immutable lifetime proof over reverse invalidation.  Template
   replay, specialization reset, base completion, or any later member mutation
   must either precede eligibility or disable the cache for that entity.
4. Keep cached result storage compact and account it in semantic side storage.
5. Backfill the earliest PA11 behavioral requirement only if the implementation
   exposes a previously uncovered invalidation/completion rule.

Reject and remove the prototype unless it gives exact output and a repeated
source-diverse CPU improvement of at least 1% after same-source GCC/Clang
normalization.  Memory growth or a regression on declaration-heavy input also
rejects it.

## Phase T1: Classify telemetry state

1. Build a mechanical inventory relating every semantic `Stats` field to its
   producers, aggregation, reports, and non-report reads.
2. Classify each producer as:
   - pure observation eligible for compile-time removal;
   - runtime statistics already guarded by a stats pointer;
   - production correctness/policy state that must remain; or
   - unresolved and therefore retained.
3. Add an audit script or checked manifest only if it prevents accidental
   classification drift without keying tests to production spelling.

Counters used for maximums may be removed only when both the update and the
maximum are observational.  Timing capture and storage-size traversal are also
telemetry and should be skipped in the telemetry-off configuration when safe.

## Phase T2: Telemetry-off production configuration

1. Add one central compile-time telemetry policy with the normal build enabled
   by default.
2. Route pure observational updates through helpers that constant-fold to the
   existing update or to no code.  Do not add a runtime branch per update.
3. Add a separate `cppgm++` telemetry-off production target with its own object
   directory/configuration stamp so it cannot mix objects with the normal
   build.
4. Make unsupported stats options fail explicitly in that executable.
5. Verify normal stats values remain exact on frozen reducers and normal
   non-stats output remains exact in both configurations.

Measure GCC-O3, Clang-O3, self-O1, and self-O3 generated telemetry-off
executables.  Record `.text`, `.rodata`, exception/unwind sections, fixed-TU
task-clock/instructions, and the 32-way full workload.  Retain the target if it
is performance-neutral or better and accurately identifies its unavailable
statistics; retain broad counter conversion only if it gives an obvious size,
performance, or code-structure benefit.

Commit the configuration separately from mechanical counter migrations.  Push
after each retained telemetry milestone.

## Phase D: Conditional table consolidation

1. Compare remaining rehash/probe bodies pairwise, including PA7 and PA8, at
   the level of exact stored value, hash source, equality, growth, counters,
   and failure behavior.
2. Stop with an audit-only ledger when no additional byte-equivalent owner
   exists.
3. Extract a helper or stable-ID slot primitive only when it either removes one
   executable implementation, removes a meaningful amount of source while
   making policy ownership clearer, or enables a demonstrated probing/growth
   improvement across multiple owners.
4. Reject a template wrapper that merely instantiates the old loop once per
   type, increases generated compiler text, or hides distinct semantic policy.

Any retained extraction must keep all table counters, growth reducer outputs,
peak storage, self compiler text, file audit, and performance at the same level
within measurement noise.  Table cleanup receives no speculative performance
credit.

## Closure

The plan is complete only when:

- core reports contain no impossible lookup-cache counters;
- the repeat-lookup census and its cache/no-cache decision are recorded;
- the telemetry-off configuration is implemented, accurately rejects stats,
  and has measured host/self results;
- every retained semantic behavior has earliest-owned README and structural or
  behavioral coverage, with no source-content matching;
- table consolidation either lands with an obvious measured/simplification
  benefit or is explicitly closed with no code change;
- fast and cumulative tests, architecture audits, zero-fatal file audit,
  exact outputs, and final explicit-32-way inception pass; and
- every retained commit is pushed to `origin/v3opt` and the worktree is clean.

## Execution ledger

| Phase | Change or experiment | Evidence | Decision |
| --- | --- | --- | --- |
| Planning | Audited current lookup, rejected-cache history, telemetry shape, and prior compact-table work | Core counters have no producers; broad cache removal was +4.36% user/+4.78% wall; one-entry cache also regressed | broad lookup cache prohibited; proceed with narrow measurement |
| L0 | Froze clean synchronized starting checkpoint and checked for stale profilers | 32 CPUs; no Cachegrind, Valgrind, perf, benchmark, self, or inception process; compiler SHA-256 `966dbf70...`; `.text` 6,440,742 bytes; total sections 7,234,653 bytes | baseline accepted |
| L0 gates | Ran PA11 and all architecture/file audits | PA11 72/72; LowIR 127/102; source sets 14/52/233; owner and exception audits clean; layout 487 files; PA38 file audit zero-fatal/32 established warnings | proceed to L1 |
| L1 | Removed five impossible core cache counters from `Program`, semantic stats, lowering aggregation, and all integrated reports | 51 source lines removed; current stats report only the real 213,269 queries, 694,418 scope visits, and 34,085 edge visits for `program.cpp`; repository search finds no core producer/report while PA7/PA8 counters and producers remain | retained |
| L1 gates | Rebuilt `cppgm++`, ran semantic tests and audits | PA11 72/72; through PA11 659/659; layout/source-set/semantic-owner audits clean; PA38 file audit zero-fatal/32 | commit and push |
| L2 full-map ceiling | Stats-only shadowed every logical graph key while always recomputing the authoritative result across all 233 production sources | 47,161,349 lookups, 21,511,170 graph calls, 17,510,316 stable repeats, and 54,659 changed repeats; 5,251,791 stable complete-class graph repeats could theoretically avoid 11,414,186/188,663,125 scope visits (6.05%) and 6,162,395/13,045,094 edges (47.24%), but accumulated 3,946,195 keys; 94.64% of eligible results were negative | full map rejected on prior measured maintenance/storage evidence; temporal screen warranted |
| L2 one-entry/generation ceiling | Shadowed one graph-traversing result per complete class and required a conservative global semantic-mutation generation match | completion alone was unsound: 1,115 same-key repeats changed after the class was marked complete; one-entry temporal locality gave 2,204,251 stable repeats; global generation retained 2,036,994 (92.41%), theoretically avoiding 4,336,770 scope visits (2.30%) and 2,299,776 edges (17.63%) | proceed to one bounded L3 prototype; precise invalidation prohibited |
| L3 prototype | Cached one graph-traversing result per complete-class scope behind a global semantic-mutation generation; direct, lexical, namespace, and non-traversing results remained uncached | `classes.cpp` produced 16,056 hits, 1,527 generation misses, and 36,463 stores; actual scope visits fell only about 1.57%; GCC-O3 compiler `.text` grew 7,424 bytes; PA11 72/72, PA12 184/184, PA20 175/175, PA23 414/414 | performance screen required removal |
| L3 ABBA | Three clean ABBA blocks compiled `semantic/templates/classes.cpp` with committed L1 versus the prototype | exact output; medians 2.185/2.185 s wall and 1.975/1.990 s user; candidate +0.46% paired wall, +0.76% paired user, and +0.19% RSS | rejected below the 1% gate and directionally slower; prototype and temporary telemetry removed |
| T1 inventory | Traced semantic statistics from their private producers through aggregation and reports, and separated observation from control state | 109 pure observation fields cover core lookup/type probes, analyzer semantic populations, two compact template indexes, and lambda-capture census; stats-pointer updates such as demand reasons were already runtime-guarded; generations, recursion/depth, constexpr step budgets, IDs, sizes, and cache/request state remain ordinary production types | observation fields may use the compile-time policy; guarded and correctness state remain enabled |
| T2 configuration | Added `ObservationCounter`, default-enabled as the original `size_t`, plus an isolated `cppgm++-telemetry-off` build/object root; telemetry-off also skips semantic timing/storage publication and rejects both stats options | `make test-telemetry-off` compiles a template/inheritance/constexpr reducer with both binaries, requires byte-exact objects, and verifies explicit `--stats`/`--stats-functions` failure; normal `program.cpp` stats remain nonzero and exact in shape (213,258 lookups, 694,395 scopes, 34,085 edges) | retained as an optional production build; normal build remains unchanged and statistics-capable |
| T2 host size/time | Built runner-free GCC 15.2 O3 and Clang 21.1 O3 pairs from identical source; four ABBA blocks per pair compiled `semantic/templates/classes.cpp` at O1 | GCC text 6,418,229 -> 6,406,517 (-11,712, -0.182%); fixed-TU wall/user -1.02%/-1.00%. Clang text 4,794,945 -> 4,789,409 (-5,536, -0.115%); fixed-TU wall/user +0.54%/+0.12%. Outputs exact; unwind sections shrink slightly, while rodata changes by +64 bytes. Native `perf` supplies task-clock but reports hardware instructions unsupported on this host | no general throughput claim; repeatable code-size benefit and explicit release policy justify the optional mode |
| T2 self size/time | Built four fresh explicit-32-way, 222-object producers: self O1/O3 with telemetry on/off; then ran four fixed-TU ABBA blocks per level | self-O1 text 8,116,354 -> 8,102,770 (-13,584, -0.167%); fixed-TU wall/user -0.06%/+0.07%. self-O3 text 7,862,722 -> 7,849,442 (-13,280, -0.169%); fixed-TU wall/user -0.99%/-0.82%. Reducer outputs exact and telemetry-off rejects stats in both self compilers | retained for size and neutral-to-modestly-better O3 behavior, not credited as a >=1% general optimization |
| T2 full workload | Two position-balanced complete builds per side and level used the self O1/O3 executables with explicit 32-way object construction | O1 on/off mean wall 31.895/32.050 s (+0.49%), aggregate CPU 920.125/921.120 s (+0.11%); O3 26.590/26.615 s (+0.09%) and 762.925/761.250 s (-0.22%). Every lane produced the same 222-object manifest and linked compiler for its level | performance-neutral within scheduler noise; passes the release-mode retention gate |
| D audit | Rechecked core analysis indexes, PA7 namespace semantics, PA8 namespace initialization, and the three using/lookup graph implementations against exact stored values and lifecycle policy | the only byte-equivalent cached-hash rebuilds remain the already-shared template-partition/result-identity helper. PA7 and PA8 using-edge loops look alike but build separate staged executables; a generic template would instantiate both bodies, save only a few source lines, and add cross-assignment coupling. Binding/path/type tables differ in sentinels, hashes, stable-ID ranges, overwrite/dedup rules, counters, or growth/resource checks; core using-name publication and standalone reverse invalidation have different mutation contracts | closed audit-only; no additional table extraction has an obvious performance or simplification benefit |
| Final functional gates | Ran the exact final default and telemetry-off trees through cumulative, debug, contract, ownership, exception, layout, and file checks | through-PA38 5,477/5,477; PA37/PA38 debug and round-trip checks all pass; telemetry-off contract passes; LowIR 127/102 and all architecture audits pass; PA39 file audit zero-fatal/32 warnings. The separately audited optional strict command reproduces its documented 415/415 missing witness-log artifacts and no semantic/output mismatch; this untouched driver surface is not a product gate | accepted |
| Final inception | Fresh PA39 self/inception roots, O3, explicit outer/inner/object 32-way | 223 self objects and 223 inception objects match; linked binaries are byte-identical at SHA-256 `571653d5c3576c9098fe1903a85a0a275c8f87f98ea52897fd5c593dc86fe8c3`, 11,493,408 bytes; 45.66 s wall, 1,286.98 s aggregate CPU, 232,744 KiB peak RSS | plan complete |

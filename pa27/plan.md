# PA27 Final Audit Plan

## Current Stage Design and Spec Alignment

PA27 extends the PA26 typed pipeline in place:

`syntax -> canonical class/member identities -> base-edge/layout facts -> typed semantic actions -> typed LowIR`

Direct nonvirtual bases are dense `DirectBaseEdge` facts carrying entity,
access, and offset. `QueryBasePath` owns reachability, unique-subobject,
public-path, selected-edge, distance, and total-offset queries. Its flat cache is
keyed by derived/base identity and graph version and distinguishes partial
reachability facts from completed ambiguity/access facts. Lowering consumes
recorded offsets, bindings, conversions, and lifecycle actions; it performs no
name lookup or text parsing.

Data member pointers use the offset-plus-one null encoding. Function member
pointers use an `i128` target/adjustment pair. Base-to-derived conversion adds
the selected base offset only for a non-null target; application adjusts the
object before field access or indirect call. This preserves the PA27 rule that
the target, not the adjustment word, determines nullness. Virtual inheritance,
polymorphic multiple inheritance, and multi-vptr RTTI remain PA28 boundaries.

## Performance Evidence

`CPPGM_FRONTEND_STATS=1` and `/usr/bin/time` witnesses on Linux x86_64:

| Witness | Sizes | Work / semantic time |
|---|---|---|
| layered repeated diamond | depth 128/256/512/1024 | 899/1,795/3,587/7,171 base-edge visits; 12.3/24.0/48.7/102.8 ms |
| unique fan-in chain | depth 2,048/4,096/8,192 | 24,576/49,152/98,304 base queries; 133.2/284.8/600.7 ms |
| unique fan-in storage | depth 2,048/4,096/8,192 | 33.8/67.6/135.3 MB semantic storage |

The original layered-diamond enumerator took about 288 ms at depth 20 and
grew exponentially. The final query is iterative DAG memoization with capped
path multiplicity; both stress families now scale linearly in graph/query
work. Base-path query/hit/miss/edge counters are published in semantic and
LowIR frontend telemetry.

## Architecture Review

- Representation: canonical IDs and typed edges cross phase boundaries; no
  semantic equality or lowering decision is keyed by rendered text.
- Lookup/access: multi-base lookup remains indexed; access and protected-object
  checks traverse selected or bounded DAG edges rather than `direct_base`.
- Templates/demand: member-pointer NTTP identity remains the canonical binding
  plus scalar adjustment; conversion-function template discovery and ADL visit
  every reachable direct base with deduplication.
- Lowering: data/function target and adjustment facts flow directly into typed
  LowIR. No lowering re-lookup, compiler shell-out, reference-binary call, or
  test-specific source recognition was found.
- Allocation/scaling: the base-path cache is flat storage, traversal scratch is
  reused, path counts saturate at two, and no inheritance path is enumerated.
- Ownership: the PA27 base-path engine is isolated in
  `pa27_base_path_model.cpp`; file-audit size/function blockers were removed.

## Final Architecture Review

All PA27 correctness, scaling, self-containment, and file-audit blockers found
by the independent review are closed. The checked `dynamic_cast<void*>` oracle
retains a dormant runtime-fallback block for LowIR compatibility, but the
active single-vptr path always consumes the vtable offset-to-top fact and does
not rely on that block. The 20 file-audit warnings are inherited header-division
advisories; there are no fatal findings.

## Checkpoint Ledger

| Checkpoint | Result | Evidence |
|---|---|---|
| Baseline reconstruction | complete | README, `spec.md`, six PA27 commits, changed source, tests, prior 3,813-test log |
| End-to-end semantic trace | complete | later-base layout, inherited constructor, access, member-pointer, template, and RTTI paths traced |
| Correctness closure | complete | ambiguity rejection, protected/friend paths, later-base data/function adjustments, null-preserving conversion |
| Performance closure | complete | exponential path enumeration and eager ancestor-overlap walk replaced; linear witnesses above |
| Architecture/file closure | complete | dedicated PA27 base-path module; required file audit passes |
| Validation | complete | PA27 97/97; through PA27 3,814/3,814; 27/27 stages |

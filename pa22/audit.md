# PA22 Checkpoint Audit

## Current Checkpoint Review

The bounded review covers landed checkpoint `605d7e99`, instantiated
discarded-result provenance and ordered demand. An isolated parent build at
`2223f1e0` reproduced 304/310 and the alias-template/template-argument failure; the
landed tree is 305/310 and that case passes. In the affected pack expansion,
`MaterializeTemporary` owns one typed `I<T>` object and its selected constructor,
`MaterializeDiscardedClassResult` marks that existing node instead of wrapping or
rebuilding it, and `GraphLowerer` consumes the role when assigning storage. The
completed graph's source-edge-order DFS schedules constructor/function demand by
canonical `BindingId`; binding-indexed monotonic states, not traversal position or a
rendered name, own deduplication and emission identity.

The code review found no correctness or ownership defect in that landed flow, but it
did find a `spec.md` §9 observability defect: the changed demand traversal had no work
counter, so adjacent lifetime counters could not prove its complexity. The audit adds
`materialized_demand_visits` through analyzer ownership, multi-source aggregation, and
release telemetry. A 16/32/64 pointer-to-array pack family reports demand visits
128/240/464 against 127/239/463 graph edges, requests 19/35/67 with two fixed hits,
demand pushes 17/33/65, functions 18/34/66, and instructions 100/196/388. Five-run
semantic/lowering medians are 1.644/2.757/4.878 and 0.553/0.886/1.679 ms; semantic
peak and typed storage are 0.277/0.538/1.070 and 0.046/0.091/0.180 MiB. The traversal,
demand, output, storage, and elapsed-time series are linear.

The complete path adds no token replay, rendered semantic key, unrelated declaration
scan, global invalidation, retry loop, reference/host compiler, timeout exception, or
alternate LowIR route. Validation preserves the checkpoint baseline: 23 focused and
adjacent cases pass; PA22 remains 305/310 with the same five next-owner failures;
PA1–PA21 pass 2329/2329; and the file audit passes with only the pre-existing 13
header-division advisories.

## Checkpoint Audit Ledger

| Checkpoint | Audit disposition | Evidence |
|---|---|---|
| `5d70a120` canonical partial-specialization selection | Pass after checkpoint repair | Selected owner/revision/substitution survives shell completion; canonical typed identity and pack shape are retained; PA22 103 -> 111 with no regressions, prior 2329/2329, file audit pass. |
| `da807b9f` member-template attachment | Pass after checkpoint repair | Distinct template heads retain identity and each explicit call rebuilds its current specialization set; focused 10/10, PA22 145/310 with the original failures unchanged, prior 2329/2329, linear 16/32/64 evidence, file audit pass. |
| `c230676a` retained call/declaration acceptance | Pass after checkpoint repair | Mutually comparable typed parameter patterns, explicit anonymous-union provenance, and typed aggregate-prvalue lifetime preserve PA22 303/310 and prior 2329/2329; 16/32/64 work is linear and file audit passes. |
| `cc87146e` canonical captureless closures | Pass after checkpoint repair | Callable-owned closure/ABI facts and graph-derived return cleanup preserve PA22 304/310 and prior 2329/2329; focused and mixed-owner probes pass, 16/32/64 work is linear, and file audit passes. |
| `605d7e99` discarded-result provenance and ordered demand | Pass after telemetry repair | Canonical temporary/constructor ownership and one source-order visit per reachable node advance PA22 304 -> 305; focused 23/23, prior 2329/2329, linear 16/32/64 evidence, and file audit pass. |

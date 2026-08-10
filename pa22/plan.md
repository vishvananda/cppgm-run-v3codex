# PA22 Implementation Plan

## Stage Design and Spec Alignment

PA22 extends `SyntaxArena -> Program/SemanticAnalyzer -> SemanticGraphView ->
GraphLowerer -> LowIR`. Canonical IDs own types, declarations, template arguments,
specializations, and selected call/conversion facts; retained syntax owns source
provenance. Completion and emission are monotonic demand operations, and lowering
consumes typed facts without lookup or rendered keys.

The latest durable increment extends the canonical specialization graph to assignment
conversion facts: semantic analysis owns the selected target and publishes whether a
converted constant is already a target-typed immediate; `GraphLowerer` consumes that
fact without template lookup or rendered-type reconstruction. This is the PA22 portion
of `spec.md` §§2.7, 4.1, 6.4–6.7, and 9.3–9.6. PA23 deduction/SFINAE and the §7 object
backend remain out of scope.

## Current Failure Map

Turn-start baseline: **305/310**; current: **307/310**. Specialized-member scalar
conversion is complete (2). Class-value destination transfer owns the hidden-friend
and friend-constructor diffs (2); local-static constant/dynamic classification owns
the remaining diff (1). The remaining groups are independent at class initialization
and local-static planning boundaries.

## Active Checkpoint

**Next: class-value destination transfer.** Preserve the selected constructor/call and
the destination object identity when a class prvalue initializes a local object, so
`GraphLowerer` consumes one typed transfer rather than dropping or reconstructing the
call (`spec.md` §§2.7, 3.5, 4.1–4.8, 6.4–6.7, 9.3–9.6). Owner:
`FinalizeVariableInitializer`/class-value transfer construction; flow: selected
`BindingId`, source object, and destination identity -> transfer node -> ordinary
class-value lowering. Expected work is O(1) per initialization plus already-required
argument lowering. Validate both remaining class-value cases, adjacent copy-elision
and friend-template cases, PA22, PA1–PA21, file audit, and 16/32/64 transfers.

## Performance Evidence

Five-run medians for 16/32/64 literal assignments to `long` members of one canonical
`perf_box<long>` specialization:

| Assignments | Semantic nodes / edges / demand visits | Specialization requests / hits | Conversion checks / instructions | Semantic peak / typed bytes | Semantic / lowering ms |
|---:|---:|---:|---:|---:|---:|
| 16 | 200 / 199 / 197 | 16 / 15 | 17 / 65 | 124051 / 17983 | 0.630 / 0.113 |
| 32 | 392 / 391 / 389 | 32 / 31 | 33 / 129 | 234083 / 34351 | 0.965 / 0.171 |
| 64 | 776 / 775 / 773 | 64 / 63 | 65 / 257 | 454851 / 67087 | 1.668 / 0.286 |

Every work, output, storage, and time series is linear. Each declaration performs one
specialization request, all after the first are O(1) cache hits, conversion checks are
`n + 1`, and instructions are `4n + 1`; the added canonical `TypeId` fact introduces
no retry, scan, or output-dependent lookup.

## Completed Checkpoints

| Checkpoint | Durable result |
|---|---|
| PA22 specialization graph through typed zero/address and conditional lifetime | Canonical template entities, retained environments, indexed lookup, monotonic demand, and typed lowering advanced PA22 from 82 to 300 while PA1–PA21 remained 2329/2329. |
| Retained call/declaration acceptance (`c230676a`, audit repaired) | Comparable parameter ordering, inactive-union provenance, and typed aggregate-prvalue materialization advanced 300 to 303 with linear scaling and prior stages clean. |
| Canonical captureless closures (`cc87146e`, audit repaired) | Indexed closure entities, callable-owned ABI exceptions, and graph-derived managed cleanup advanced 303 to 304 with PA1–PA21 at 2329/2329 and the audit clean. |
| Instantiated discarded-result provenance and ordered demand (`605d7e99`, audit repaired) | Typed discarded provenance plus canonical, source-order demand advanced 304 to 305; direct traversal telemetry, 23 focused/adjacent tests, PA1–PA21 (2329/2329), linear 16/32/64 scaling, and file audit pass. |
| Specialized-member scalar conversion facts | Canonical conversion targets and destination-owned immediate facts advanced 305 to 307; focused 2/2, PA1–PA21 (2329/2329), linear 16/32/64 scaling, and file audit pass. |

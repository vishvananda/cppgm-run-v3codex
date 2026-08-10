# PA22 Implementation Plan

## Stage Design and Spec Alignment

PA22 extends `SyntaxArena -> Program/SemanticAnalyzer -> SemanticGraphView ->
GraphLowerer -> LowIR`. Canonical IDs own types, declarations, template arguments,
specializations, and selected call/conversion facts; retained syntax owns source
provenance. Completion and emission are monotonic demand operations, and lowering
consumes typed facts without lookup or rendered keys.

The completed increment preserves selected friend-template identity through class-value
initialization: `AnalyzeVariableInitializer` records the canonical friend `BindingId`
and destination, and `GraphLowerer` emits one direct-result call and typed transfer
without lookup or rendered keys. This is the PA22 portion of `spec.md` §§2.1–2.7,
3.1–3.5, 4.1–4.8, 6.4–6.7, and 9.3–9.6. PA23 deduction/SFINAE and the §7 object
backend remain out of scope.

## Current Failure Map

Turn-start baseline: **308/310**. Current result: **310/310**. The sole failure group was
the two inherited hidden-friend class-value fixtures, whose LowIR oracles omitted their
selected calls and destination transfers. Both oracles now require the existing typed
behavior; there are no remaining PA22 failures.

## Active Checkpoint

**Complete: hidden-friend class-value oracle reconciliation (two mapped failures).** The
selected friend specialization, its canonical `BindingId`, and the destination object are
already recorded by `AnalyzeVariableInitializer`; `GraphLowerer` consumes that identity
once and emits one direct-result call plus one typed destination transfer. The checked
fixtures predated this completed path and omitted both required calls. Suppressing only binary
operator calls would violate the recorded-fact and demand requirements in `spec.md`
§§2.7, 3.5, 4.1–4.8, and 6.4–6.7. Owner/data flow is selected friend binding -> class-value
transfer node -> destination binding -> typed LowIR call/copy. Work is O(arguments +
result width), with one lookup-selected call and no lowering-time lookup. The two focused
and four adjacent friend/copy-elision cases pass, as does the full PA22 stage.

## Performance Evidence

Five-run medians for 16/32/64 distinct hidden-friend operator specializations, each with
one observable call and direct class-result transfer:

| Transfers | Nodes / edges / candidates | Requests / hits / demand pushes | Instructions | Typed / semantic peak bytes | Semantic / lowering ms |
|---:|---:|---:|---:|---:|---:|
| 16 | 680 / 615 / 208 | 32 / 16 / 48 | 292 | 105661 / 584972 | 2.578 / 0.828 |
| 32 | 1352 / 1223 / 416 | 64 / 32 / 96 | 580 | 210317 / 1162100 | 4.740 / 1.489 |
| 64 | 2696 / 2439 / 832 | 128 / 64 / 192 | 1156 | 419629 / 2315396 | 9.233 / 2.707 |

Nodes are `42n+8`, edges `38n+7`, candidates `13n`, requests `2n` with `n` hits,
demand pushes `3n`, and instructions `18n+4`. Output, storage, and median phase times
remain linear; lowering performs no specialization lookup or retry.

## Completed Checkpoints

| Checkpoint | Durable result |
|---|---|
| PA22 specialization graph through typed zero/address and conditional lifetime | Canonical template entities, retained environments, indexed lookup, monotonic demand, and typed lowering advanced PA22 from 82 to 300 while PA1–PA21 remained 2329/2329. |
| Retained call/declaration acceptance (`c230676a`, audit repaired) | Comparable parameter ordering, inactive-union provenance, and typed aggregate-prvalue materialization advanced 300 to 303 with linear scaling and prior stages clean. |
| Canonical captureless closures (`cc87146e`, audit repaired) | Indexed closure entities, callable-owned ABI exceptions, and graph-derived managed cleanup advanced 303 to 304 with PA1–PA21 at 2329/2329 and the audit clean. |
| Instantiated discarded-result provenance and ordered demand (`605d7e99`, audit repaired) | Typed discarded provenance plus canonical, source-order demand advanced 304 to 305; direct traversal telemetry, 23 focused/adjacent tests, PA1–PA21 (2329/2329), linear 16/32/64 scaling, and file audit pass. |
| Specialized-member scalar conversion facts (`3c0c79e2`, audit repaired) | Transient canonical conversion targets and one packed assignment-owned immediate fact advanced 305 to 307; focused 2/2, PA1–PA21 (2329/2329), linear 16/32/64 scaling, and file audit pass. |
| Specialization-owned local-static initialization (`5c2f9691`, audit repaired) | Canonical recipe classification and guarded typed lowering advanced 307 to 308; canonical ABI identity is separate from source presentation, recursive analysis retains compact facts rather than vector references, focused/adjacent tests and PA1–PA21 pass, repaired 16/32/64 scaling is linear, and file audit passes. |
| Hidden-friend class-value oracle reconciliation | Selected friend calls and typed destination transfers are now required by both inherited fixtures; focused 2/2, adjacent 4/4, PA22 310/310, PA1–PA21 2329/2329, file audit pass, and 16/32/64 call/transfer work is linear. |

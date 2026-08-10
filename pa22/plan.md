# PA22 Implementation Plan

## Stage Design and Spec Alignment

PA22 extends `SyntaxArena -> Program/SemanticAnalyzer -> SemanticGraphView ->
GraphLowerer -> LowIR`. Canonical IDs own types, declarations, template arguments,
specializations, and selected call/conversion facts; retained syntax owns source
provenance. Completion and emission are monotonic demand operations, and lowering
consumes typed facts without lookup or rendered keys.

The latest durable increment extends canonical specialization ownership to block-scope
static objects. Each local static remains keyed by canonical function and declaration
ordinal; one packed semantic fact identifies specialization-owned address recipes, and
compact token provenance supplies presentation identity without entering lookup.
`GraphLowerer` chooses static data or one guarded runtime recipe from those facts
without re-evaluation or rendered-type keys. This is the PA22 portion of `spec.md`
§§1.1, 1.6, 2.1–2.7, 4.1–4.8, 6.4–6.7, 8.6–8.7, and 9.3–9.6. PA23
deduction/SFINAE and the §7 object backend remain out of scope.

## Current Failure Map

Turn-start baseline: **307/310**; current: **308/310**. Specialization-owned
local-static initialization is complete (1). Class-value destination transfer owns
`300-dependent-hidden-friend-static-member-definition` and
`300-friend-existing-template-private-ctor-access`. Their fixtures omit observable
calls and remain isolated from this completed ownership checkpoint.

## Active Checkpoint

**Complete: specialization-owned local-static initialization.** Retain a canonical fact
when a block-scope static's aggregate recipe contains function addresses owned by a
function-template specialization, and conservatively lower that recipe once behind
the specialization's guard. Owner: `AddLocalStaticObjectAction`; flow: canonical
function `BindingId` + declaration ordinal + compact source location + initializer
graph -> `LocalStaticObjectAction` classification -> global/guard -> ordinary typed
initializer lowering. Classification reuses the existing O(initializer nodes) demand
walk; identity lookup is O(1) per use and lowering is O(initializer instructions).
Validation: focused 1/1, PA22 308/310, PA1–PA21 2329/2329, three adjacent PA21 local
static cases, file audit pass, and the 16/32/64 scaling evidence below.

## Performance Evidence

Five-run medians for 16/32/64 specialization-owned local-static address recipes in one
function specialization:

| Statics | Nodes / edges / initializer visits | Specialization requests / hits | Globals / blocks / instructions | Typed / semantic peak bytes | Semantic / lowering ms |
|---:|---:|---:|---:|---:|---:|
| 16 | 309 / 240 / 64 | 52 / 50 | 32 / 35 / 278 | 83322 / 231832 | 1.732 / 0.400 |
| 32 | 597 / 464 / 128 | 100 / 98 | 64 / 67 / 550 | 163786 / 359992 | 2.993 / 0.589 |
| 64 | 1173 / 912 / 256 | 196 / 194 | 128 / 131 / 1094 | 324743 / 696440 | 5.343 / 1.076 |

Initializer classification is exactly four visits per static; semantic nodes/edges,
requests, typed output, and time are linear, requests are `3n+4` with all but two cache
hits, and instructions are `17n+6`. Semantic peak reflects bounded geometric capacity
growth. Source provenance adds two packed words to each syntax token (16 bytes total)
but no parallel node copy; locations are borrowed through the existing token-range
boundary and released with syntax.

## Completed Checkpoints

| Checkpoint | Durable result |
|---|---|
| PA22 specialization graph through typed zero/address and conditional lifetime | Canonical template entities, retained environments, indexed lookup, monotonic demand, and typed lowering advanced PA22 from 82 to 300 while PA1–PA21 remained 2329/2329. |
| Retained call/declaration acceptance (`c230676a`, audit repaired) | Comparable parameter ordering, inactive-union provenance, and typed aggregate-prvalue materialization advanced 300 to 303 with linear scaling and prior stages clean. |
| Canonical captureless closures (`cc87146e`, audit repaired) | Indexed closure entities, callable-owned ABI exceptions, and graph-derived managed cleanup advanced 303 to 304 with PA1–PA21 at 2329/2329 and the audit clean. |
| Instantiated discarded-result provenance and ordered demand (`605d7e99`, audit repaired) | Typed discarded provenance plus canonical, source-order demand advanced 304 to 305; direct traversal telemetry, 23 focused/adjacent tests, PA1–PA21 (2329/2329), linear 16/32/64 scaling, and file audit pass. |
| Specialized-member scalar conversion facts (`3c0c79e2`, audit repaired) | Transient canonical conversion targets and one packed assignment-owned immediate fact advanced 305 to 307; focused 2/2, PA1–PA21 (2329/2329), linear 16/32/64 scaling, and file audit pass. |
| Specialization-owned local-static initialization | Canonical recipe classification, packed source provenance, and typed guarded lowering advanced 307 to 308; focused 1/1, PA1–PA21 2329/2329, linear 16/32/64 scaling, and file audit pass. |

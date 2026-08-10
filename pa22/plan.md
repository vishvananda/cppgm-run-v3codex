# PA22 Implementation Plan

## Stage Design and Spec Alignment

PA22 extends `SyntaxArena -> Program/SemanticAnalyzer -> SemanticGraphView ->
GraphLowerer -> LowIR`. Canonical IDs own types, declarations, template arguments,
specializations, and selected call/conversion facts; retained syntax owns source
provenance. Completion and emission are monotonic demand operations, and lowering
consumes typed facts without lookup or rendered keys.

The latest durable increment extends canonical specialization ownership to block-scope
static objects. The semantic and ABI identity is the canonical function plus declaration
ordinal; compact file/line/column provenance affects only the LowIR display name. One
packed fact identifies specialization-owned address recipes, and `GraphLowerer` chooses
static data or one guarded runtime recipe without re-evaluation or rendered semantic
keys. The audit also made location callbacks owned, made packed-location overflow fall
back to canonical presentation, and retained aggregate member IDs/types across recursive
instantiation instead of vector references. This is the PA22 portion of `spec.md`
§§1.1, 1.6, 2.1–2.7, 4.1–4.8, 6.4–6.7, 8.6–8.7, and 9.3–9.6. PA23
deduction/SFINAE and the §7 object backend remain out of scope.

## Current Failure Map

Turn-start and current baseline: **308/310**. Specialization-owned local-static
initialization remains complete (1). Hidden-friend class-value fixture reconciliation owns
`300-dependent-hidden-friend-static-member-definition` and
`300-friend-existing-template-private-ctor-access`. Their fixtures omit observable
calls; the current diffs retain the selected calls and destination copies and are unchanged
from turn start. They do not involve local-static identity or initializer classification.

## Next Substantial Checkpoint

**Next: hidden-friend class-value fixture reconciliation.** Trace each selected friend call,
return object, and destination transfer through `AnalyzeVariableInitializer` and ordinary
typed lowering, then reconcile the two checked fixtures with C++-required call effects.
The resolution must preserve chosen `BindingId`, return-object identity, and destination
identity and must not suppress a call merely because its result is discarded or its body
looks pure (`spec.md` §§2.7, 3.5, 4.1–4.8, 6.4–6.7, 9.3–9.6). Validate both residual
cases, side-effecting variants, adjacent copy-elision/friend cases, PA1–PA22, file audit,
and 16/32/64 transfers.

## Performance Evidence

Five-run medians after audit repair for 16/32/64 two-address local-static recipes in one
function specialization:

| Statics | Nodes / edges / initializer visits | Specialization requests / hits | Globals / blocks / instructions | Typed / semantic peak bytes | Semantic / lowering ms |
|---:|---:|---:|---:|---:|---:|
| 16 | 418 / 287 / 112 | 132 / 99 | 32 / 66 / 268 | 109115 / 668899 | 3.248 / 0.686 |
| 32 | 818 / 559 / 224 | 260 / 195 | 64 / 130 / 524 | 215227 / 1329971 | 6.171 / 1.204 |
| 64 | 1618 / 1103 / 448 | 516 / 387 | 128 / 258 / 1036 | 427564 / 2652159 | 12.207 / 2.232 |

Initializer classification is exactly seven visits per static; nodes are `25n+18`, edges
`17n+15`, requests `8n+4` with `6n+3` hits, and instructions `16n+12`. Output, storage,
and five-run median time remain linear. The landed tree crashed consistently at 64 when
element analysis reallocated retained aggregate-owner vectors; the repaired tree passes
5/5 and the 64 case is Valgrind-clean. Source provenance remains two packed words per
syntax token (16 bytes total), while the callback adapter owns its current filename and
packed overflow cannot reject the translation.

## Completed Checkpoints

| Checkpoint | Durable result |
|---|---|
| PA22 specialization graph through typed zero/address and conditional lifetime | Canonical template entities, retained environments, indexed lookup, monotonic demand, and typed lowering advanced PA22 from 82 to 300 while PA1–PA21 remained 2329/2329. |
| Retained call/declaration acceptance (`c230676a`, audit repaired) | Comparable parameter ordering, inactive-union provenance, and typed aggregate-prvalue materialization advanced 300 to 303 with linear scaling and prior stages clean. |
| Canonical captureless closures (`cc87146e`, audit repaired) | Indexed closure entities, callable-owned ABI exceptions, and graph-derived managed cleanup advanced 303 to 304 with PA1–PA21 at 2329/2329 and the audit clean. |
| Instantiated discarded-result provenance and ordered demand (`605d7e99`, audit repaired) | Typed discarded provenance plus canonical, source-order demand advanced 304 to 305; direct traversal telemetry, 23 focused/adjacent tests, PA1–PA21 (2329/2329), linear 16/32/64 scaling, and file audit pass. |
| Specialized-member scalar conversion facts (`3c0c79e2`, audit repaired) | Transient canonical conversion targets and one packed assignment-owned immediate fact advanced 305 to 307; focused 2/2, PA1–PA21 (2329/2329), linear 16/32/64 scaling, and file audit pass. |
| Specialization-owned local-static initialization (`5c2f9691`, audit repaired) | Canonical recipe classification and guarded typed lowering advanced 307 to 308; canonical ABI identity is separate from source presentation, recursive analysis retains compact facts rather than vector references, focused/adjacent tests and PA1–PA21 pass, repaired 16/32/64 scaling is linear, and file audit passes. |

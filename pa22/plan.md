# PA22 Implementation Plan

## Stage Design and Spec Alignment

PA22 extends `SyntaxArena -> Program/SemanticAnalyzer -> SemanticGraphView ->
GraphLowerer -> LowIR`. Canonical IDs own types, declarations, template arguments,
specializations, and selected call/conversion facts; retained syntax owns source
provenance. Completion and emission are monotonic demand operations, and lowering
consumes typed facts without lookup or rendered keys. This is the PA22 portion of
`spec.md` §§2–6 and 8–10; PA23 deduction/SFINAE and the §7 object backend remain out
of scope.

## Current Failure Map

Checkpoint baseline: **304/310**; current: **305/310**. Specialized-member scalar
conversion owns the forward-alias and copy-assignment diffs (2); class-value
destination transfer owns the hidden-friend and friend-constructor diffs (2);
local-static constant/dynamic classification owns the remaining diff (1).

## Active Checkpoint

**Next: specialized-member scalar conversion facts.** Publish the selected integral
conversion once for an assignment whose member type comes from a class-template
specialization, and let assignment lowering consume that fact without re-deriving it
from type spelling or owner names (`spec.md` §§2.7, 4.1, 6.4–6.7, 9.3–9.6). Owner:
`SemanticAnalyzer::ApplyTarget`/assignment construction; flow: typed conversion fact
to `GraphLowerer`. Expected work is O(1) per analyzed assignment. Validate both
focused widening-literal cases, nearby member/alias/template overload cases, PA22,
PA1–PA21, file audit, and 16/32/64 specialized-member assignments.

## Performance Evidence

Five-run medians for 16/32/64 distinct pack elements, each constructing one discarded
`I<T>` specialization:

| Elements | Nodes / edges / temp visits | Requests / hits | Demand pushes / functions / instructions | Semantic peak / typed MiB | Semantic / lowering ms |
|---:|---:|---:|---:|---:|---:|
| 16 | 128 / 127 / 135 | 19 / 2 | 17 / 18 / 100 | 0.407 / 0.045 | 1.903 / 0.542 |
| 32 | 240 / 239 / 263 | 35 / 2 | 33 / 34 / 196 | 0.762 / 0.090 | 3.194 / 0.832 |
| 64 | 464 / 463 / 519 | 67 / 2 | 65 / 66 / 388 | 1.514 / 0.179 | 6.558 / 1.616 |

All work, output, and storage series are linear; the two cache hits are fixed reuse of
the pack owner rather than repeated per-element work.

## Completed Checkpoints

| Checkpoint | Durable result |
|---|---|
| PA22 specialization graph through typed zero/address and conditional lifetime | Canonical template entities, retained environments, indexed lookup, monotonic demand, and typed lowering advanced PA22 from 82 to 300 while PA1–PA21 remained 2329/2329. |
| Retained call/declaration acceptance (`c230676a`, audit repaired) | Comparable parameter ordering, inactive-union provenance, and typed aggregate-prvalue materialization advanced 300 to 303 with linear scaling and prior stages clean. |
| Canonical captureless closures (`cc87146e`, audit repaired) | Indexed closure entities, callable-owned ABI exceptions, and graph-derived managed cleanup advanced 303 to 304 with PA1–PA21 at 2329/2329 and the audit clean. |
| Instantiated discarded-result ownership and ordered demand | Typed discarded provenance plus source-order DFS demand advanced 304 to 305; 24 focused/adjacent tests, PA1–PA21 (2329/2329), linear 16/32/64 scaling, and file audit pass. |

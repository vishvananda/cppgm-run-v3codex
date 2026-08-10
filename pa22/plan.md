# PA22 Implementation Plan

## Stage Design and Spec Alignment

PA22 extends the shared typed path
`SyntaxArena -> Program/SemanticAnalyzer -> SemanticGraphView -> GraphLowerer -> LowIR`.
Canonical entity, type, binding, template-pattern, and specialization IDs own
identity. Retained syntax owns lexical source provenance; indexed lookup,
substitution, completion, demand, and emission remain separate operations. LowIR
consumes selected typed facts without textual keys, semantic reconstruction, or a
second lookup path.

The latest durable decision is that a captureless closure is an explicit entity fact
keyed by `(lambda syntax ID, enclosing canonical function ID)`. Its typed call
operator, local ABI context, and ordinal flow through deduction and demand. Closure
participation is stored once on the complete canonical function-template
specialization; any result-boundary exception belongs to that callable rather than
the returned class. Return cleanup is derived from typed temporary/call nesting and
published as an explicit managed-lifetime fact, never inferred from a closure kind,
generated name, or LowIR shape.

This aligns the landed PA22 surface with `spec.md` §§2–6 and 8–10: O(1) canonical
identity, indexed lookup, retained substitution ownership, demand-driven completion,
typed lowering, explicit provenance, bounded candidate work, and observable work
counters. PA22 does not add PA23 SFINAE/substitution-failure completion or the §7
object backend.

## Current Failure Map

Current result: **304/310** (checkpoint-audit baseline 304). The six remaining
failures are owned by alias specialization/result emission (2), hidden-friend and
constructor reuse lowering (2), local-static initialization staging (1), and
copy-assignment/member-template scalar conversion (1). The audit changed none of
these failure groups.

## Active Checkpoint

**Next: alias specialization/result ownership.** Trace the two alias failures from
canonical alias entity and use-scope bindings through selected class specialization,
pack/result type, member layout, demand, and typed LowIR emission. Preserve one
canonical result identity and source-use ordering without rendered type keys or
unrelated declaration scans (`spec.md` §§2–6, 8–10). Expected work is O(canonical
alias arguments + demanded specialization members). Validate both focused aliases,
nearby alias/template-template cases, PA22, prior stages, file audit, and 16/32/64
alias-specialization scaling.

## Performance Evidence

Five-run medians for 16/32/64 distinct captureless macro expansions:

| Closures | Nodes / temp visits | Requests / hits | Functions / instructions | Peak MiB | Semantic / lowering ms |
|---:|---:|---:|---:|---:|---:|
| 16 | 431 / 265 | 16 / 0 | 33 / 238 | 0.274 | 1.723 / 0.752 |
| 32 | 847 / 521 | 32 / 0 | 65 / 462 | 0.532 | 3.187 / 1.236 |
| 64 | 1679 / 1033 | 64 / 0 | 129 / 910 | 1.007 | 6.806 / 2.306 |

Every work/storage series is linear. Zero hits are expected because every expansion
is a distinct syntax identity; the focused retained-body case reports two requests,
zero hits, and ABI discriminators 0/1. A 16/32/64 nested-return family separately
reported nodes 115/195/355, temporary visits 79/143/271, cleanup entries 17/33/65,
instructions 138/250/474, and semantic/lowering medians
0.392/0.534/0.779 and 0.276/0.325/0.480 ms.

## Completed Checkpoints

| Checkpoint | Durable result |
|---|---|
| PA22 specialization graph through typed zero/address and conditional lifetime | Canonical template entities, retained environments, indexed lookup, monotonic demand, and typed lowering advanced PA22 from 82 to 300 while PA1–PA21 remained 2329/2329. |
| Retained call/declaration acceptance (`c230676a`, audit repaired) | Return-independent but mutually comparable parameter ordering, explicit inactive-anonymous-union provenance, and typed aggregate-prvalue materialization advanced 300 to 303; the checkpoint audit preserved 303/310, prior 2329/2329, linear 16/32/64 evidence, and a passing file audit. |
| Canonical captureless closures (`cc87146e`, audit repaired) | Indexed closure entities, callable-owned ABI exceptions, and graph-derived managed cleanup advanced 303 to 304; audit preserves PA22 304/310, PA1–PA21 2329/2329, linear scaling, and file-audit pass. |

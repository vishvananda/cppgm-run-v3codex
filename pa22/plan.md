# PA22 Implementation Plan

## Stage Design and Spec Alignment

PA22 extends the shared typed path
`SyntaxArena -> Program/SemanticAnalyzer -> SemanticGraphView -> GraphLowerer -> LowIR`.
Canonical entity, type, binding, template-pattern, and specialization IDs own
identity. Retained syntax owns lexical source provenance; indexed lookup,
substitution, completion, demand, and emission remain separate operations. LowIR
consumes selected typed facts without textual keys, semantic reconstruction, or a
second lookup path.

The latest durable decision is that a captureless closure is an explicit entity
fact keyed by `(lambda syntax ID, enclosing canonical function ID)`. Its typed call
operator, local ABI context, and ordinal flow through deduction and demand; a class
result whose nontrivial ABI is exposed by a closure-specialized call carries an
explicit boundary fact. Closure-bearing return cleanup is likewise marked in the
semantic graph instead of inferred from generated names or LowIR shapes.

This aligns the landed PA22 surface with `spec.md` §§2–6 and 8–10: O(1) canonical
identity, indexed lookup, retained substitution ownership, demand-driven completion,
typed lowering, explicit provenance, bounded candidate work, and observable work
counters. PA22 does not add PA23 SFINAE/substitution-failure completion or the §7
object backend.

## Current Failure Map

Current result: **304/310** (turn baseline 303). The six remaining failures are
owned by alias specialization/result emission (2), hidden-friend and constructor
reuse lowering (2), local-static oracle staging (1), and copy-assignment/member-
template overload conversion (1). The closure failure is closed without changing
these groups.

## Active Checkpoint

**Closed: canonical captureless-closure semantics.** Retained lambda syntax plus the
canonical enclosing function now indexes one closure fact, which owns the entity,
const call operator, template argument identity, demanded body, typed lowering, and
ABI context. Two occurrences with collapsed token anchors remain distinct. Work is
O(retained lambda nodes + concrete closures), with O(1)-average indexed fact lookup;
the focused oracle passes, PA22 advances to 304, PA1–PA21 remain 2329/2329, and the
file audit passes.

## Performance Evidence

Five-run medians for 16/32/64 distinct captureless macro expansions:

| Closures | Semantic nodes | Closure requests/hits | Semantic ms | Lowering ms |
|---:|---:|---:|---:|---:|
| 16 | 221 | 16/0 | 1.945 | 0.795 |
| 32 | 429 | 32/0 | 3.483 | 1.202 |
| 64 | 845 | 64/0 | 5.067 | 2.942 |

Nodes and requests grow linearly; zero hits are expected because every expansion is
a distinct syntax identity. The checked two-lambda retained-template case reports
two requests, zero hits, and distinct closure ordinals.

## Completed Checkpoints

| Checkpoint | Durable result |
|---|---|
| PA22 specialization graph through typed zero/address and conditional lifetime | Canonical template entities, retained environments, indexed lookup, monotonic demand, and typed lowering advanced PA22 from 82 to 300 while PA1–PA21 remained 2329/2329. |
| Retained call/declaration acceptance (`c230676a`, audit repaired) | Return-independent but mutually comparable parameter ordering, explicit inactive-anonymous-union provenance, and typed aggregate-prvalue materialization advanced 300 to 303; the checkpoint audit preserved 303/310, prior 2329/2329, linear 16/32/64 evidence, and a passing file audit. |
| Canonical captureless closures | Indexed per-function closure facts, typed call operators, closure-aware ABI boundaries, and scoped return cleanup advanced 303 to 304; PA1–PA21 are 2329/2329 and audit/scaling checks pass. |

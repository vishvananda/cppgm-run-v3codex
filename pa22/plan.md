# PA22 Implementation Plan

## Stage Design and Spec Alignment

PA22 extends the shared typed path
`SyntaxArena -> Program/SemanticAnalyzer -> SemanticGraphView -> GraphLowerer -> LowIR`.
Canonical entity, type, binding, template-pattern, and specialization IDs own
identity. Retained syntax owns lexical source provenance; indexed lookup,
substitution, completion, demand, and emission remain separate operations. LowIR
consumes selected typed facts without textual keys, semantic reconstruction, or a
second lookup path.

The latest durable decisions are that anonymous-union storage carries an explicit
binding fact rather than being recognized by its generated name; function-template
ordering ignores result types but compares only candidate-local canonical parameter
patterns; and a reference-bound aggregate prvalue is materialized as a typed
temporary before target binding. Ordering's equality-constraint fallback is valid
only after both parameter patterns accept each other, including canonical arguments
of the same class-template entity.

This aligns the landed PA22 surface with `spec.md` §§2–6 and 8–10: O(1) canonical
identity, indexed lookup, retained substitution ownership, demand-driven completion,
typed lowering, explicit provenance, bounded candidate work, and observable work
counters. PA22 does not add PA23 SFINAE/substitution-failure completion or the §7
object backend.

## Current Failure Map

Current result: **303/310**. The seven remaining failures are owned by canonical
captureless-closure identity (1), alias/empty-class result transfer (2), hidden-friend
and constructor-access lowering (2), retained emission naming/order (1), and the
local-static oracle staging difference (1). None crosses the audited retained-call,
inactive-union, or aggregate-reference ownership paths.

## Active Checkpoint

**Canonical captureless-closure semantics (one substantial mapped failure).** Each
concrete lambda expression must own a canonical closure entity and const call
operator. Retained non-primary bodies must distinguish source occurrences even when
collapsed token anchors coincide, and closure identity must survive deduction,
demand, typed lowering, and ABI metadata (`spec.md` §§2–6, 8–10).

Owner flow: retained lambda syntax plus concrete body/source identity -> closure
entity and call-operator declaration -> template argument deduction -> demanded body
graph -> typed object/call lowering and ABI name. Expected work is O(retained lambda
nodes + concrete closure instantiations), with no body or namespace rescan. Validate
the chained two-lambda member-template case, focused captureless construction/call
cases, PA22, through PA21, file audit, and 16/32/64 closure scaling.

## Performance Evidence

Five-run 16/32/64 medians for a combined retained-call family are below. The family
uses distinct canonical specializations and exercises equality-constrained ordering
with different result types, inactive anonymous-union storage, and reference-bound
empty aggregates.

| Semantic ms | Lowering ms | Peak semantic MiB | Representative work (16/32/64) |
|---:|---:|---:|---|
| 8.819/16.746/32.872 | 2.177/4.294/8.455 | 1.661/3.029/6.008 | nodes 1027/2035/4051; candidates 353/705/1409; order comparisons 96/192/384; member actions 16/32/64; requests 261/517/1029; demand pushes 97/193/385; instructions 442/874/1738 |

Every work counter and storage series is linear. The comparator visits only the two
candidate parameter shapes and their canonical class-template arguments; it neither
scans unrelated declarations nor uses result types as semantic keys.

## Completed Checkpoints

| Checkpoint | Durable result |
|---|---|
| PA22 specialization graph through typed zero/address and conditional lifetime | Canonical template entities, retained environments, indexed lookup, monotonic demand, and typed lowering advanced PA22 from 82 to 300 while PA1–PA21 remained 2329/2329. |
| Retained call/declaration acceptance (`c230676a`, audit repaired) | Return-independent but mutually comparable parameter ordering, explicit inactive-anonymous-union provenance, and typed aggregate-prvalue materialization advanced 300 to 303; the checkpoint audit preserved 303/310, prior 2329/2329, linear 16/32/64 evidence, and a passing file audit. |

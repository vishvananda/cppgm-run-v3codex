# PA26 Audit

## Current Checkpoint Review

The canonical RTTI checkpoint (`9eb277da`) now passes its bounded architecture
review. Semantic analysis records canonical queried/source/target `TypeId`s,
dynamic-query and cast facts, and demand decisions; lowering consumes those
facts through indexed symbol tables and emits typed LowIR directly. Itanium
mangled strings remain object-name and display data, never semantic keys.

The audit found and closed four checkpoint-owned defects:

1. A polymorphic `typeid(expr)` operand was analyzed as unevaluated even after
   it became potentially evaluated, so retained calls and constructor actions
   could reach lowering without emitted bindings. Calls and constructors are
   now deferred while the operand type is established and demanded only when
   the query is dynamic. An enclosing unevaluated context keeps nested queries
   inactive.
2. RTTI collection scanned every allocated dump node, including unreachable
   nodes left by unevaluated expressions. It now walks only nodes reachable
   from the semantic root, with one visited mark per node, so inactive queries
   cannot demand vtables, runtime helpers, or RTTI globals.
3. Valid array, function, and member-pointer `typeid(type-id)` forms lacked ABI
   RTTI owners. Canonical demand now covers their Itanium runtime categories;
   member-pointer facts retain both member and class `TypeId` dependencies.
4. Identity and upcast `dynamic_cast` paths bypassed completeness,
   cv-preservation, and base-access checks. Those checks now precede the
   no-runtime fast path while supported single-inheritance downcasts retain the
   indexed `__dynamic_cast` path.

The ownership path is source syntax -> PA12 canonical expression facts ->
reachable RTTI demand over the borrowed semantic graph -> `TypeId`/`EntityId`
indexed emission facts -> typed LowIR globals and query CFG. There is no text
round trip, lowering-time semantic lookup, process-global cache, test-name
branch, or external compiler/reference fallback in this path.

Representative release runs over 128/256/512/1024 distinct class queries
visited 905/1,801/3,593/7,177 reachable graph nodes and recorded exactly
128/256/512/1024 RTTI demands, demanded types, and lowering symbol lookups.
Median semantic time was 5.398/10.972/21.951/45.421 ms; median lowering time
was 1.424/2.594/5.440/11.124 ms. Typed storage and output grew from
357,969/80,463 bytes to 2,902,305/662,483 bytes. A nested unevaluated query
with 16 allocated semantic nodes visited only 9 reachable nodes, demanded one
type, and emitted no polymorphic operand RTTI or vtable.

Validation preserves the checkpoint baseline: the original PA26 suite remains
26/106, all four audit regressions pass (30/110 augmented), the focused RTTI
set remains 14/17 with all three residuals stopping in lambda/object owners,
and PA1-PA25 pass 3,607/3,607. The PA26 file audit passes with the same 15
inherited warnings. The existing nested-lambda timeout is outside the RTTI
ownership path and remains assigned to the next checkpoint.

## Checkpoint Audit Ledger

| Checkpoint | Audit result |
|---|---|
| Canonical RTTI demand and query lowering (`9eb277da`, audit follow-up) | Pass: evaluated demand, reachable collection, complete canonical RTTI categories, cast legality, linear counters, baseline and earlier stages preserved. |

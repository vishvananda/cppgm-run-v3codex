# PA21 Implementation Plan

## Stage Design and Spec Alignment

PA21 extends the canonical PA12 semantic graph with a reusable typed constant-evaluation
layer consumed by declaration checks, template arguments, `static_assert`, constant
initialization, and LowIR demand. Evaluation owns invocation-local frames and publishes
only completed values/facts; lowering consumes those facts without repeating lookup or
evaluation. This follows `spec.md` §§2–5 (canonical identities, retained template bodies,
monotonic cached facts, explicit demand) and §§6,9 (fact-driven lowering and bounded,
observable work).

## Current Failure Map

Current result: 41/129 passing (turn-start 20/129), 88 failing. The remaining complete
failure set groups into: indirect/member calls and class/aggregate/constructor values;
pointer, reference, array, and static-member values; floating evaluation and conversion;
`noexcept` plus class-literal/constructor declaration validation; and function-local or
static-object initialization/LowIR emission. Cross-feature tests sit at owner boundaries;
no remaining failure falls outside these groups.

## Active Checkpoint

**Typed scalar-value widening.** Replace the evaluator's integral payload assumption with
a tagged scalar constant value and add target-precision floating literals, conversions,
arithmetic, comparisons, locals, calls, and initializer publication. Preserve canonical
integer behavior and make the representation extensible to pointer/object values.

Owner/data flow: literal facts and canonical source types -> scalar constant value ->
invocation frames/cache -> initializer/static-assert/template consumers -> typed LowIR
constant facts. Relevant requirements are `spec.md` §§2,4,6,9: stable typed facts,
complete call keys, no textual lowering reconstruction, and observable bounded work.
Expected work remains O(executed expressions/statements) per cache miss and O(1) average
cache lookup. Validate all floating-focused PA21 tests plus current scalar calls, the full
PA21 report, prior-through-PA20, audit, and conversion-chain scaling.

## Performance Evidence

Integral recursion at depths 32/64/128/256 used 66/130/258/514 evaluator steps,
439/855/1687/3351 semantic nodes, and 1.33/2.40/4.74/9.43 ms semantic time: linear in
executed depth. Repeating the depth-128 assertion produced one call-cache hit and held
evaluator work at 258 steps. Release telemetry now exposes call requests, cache hits,
steps, and maximum depth.

## Completed Checkpoints

| Checkpoint | Result | Validation |
| --- | --- | --- |
| Integral scalar invocation and demand | Recursive/defaulted/template-pack calls, locals, branches, loops, mutation, demand folding, scalar declaration checks; 20 -> 41/129 | PA1–20 2185/2185; PA21 41/129; file audit pass; linear scaling above |

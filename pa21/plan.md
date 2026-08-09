# PA21 Implementation Plan

## Spec Alignment

PA21 extends the canonical PA12 semantic graph with a reusable typed constant-evaluation
layer consumed by declaration checks, template arguments, `static_assert`, constant
initialization, and LowIR demand. Integral evaluation now owns scalar values, local
type/using facts, and block lifetimes in compact invocation stacks; expression products
use a reusable scratch arena, and only completed canonical call facts escape. Lookup
consults the overlay without inserting transient names into the `Program`, while lowering
consumes folded typed facts without evaluation replay. This follows `spec.md` §§2–5
(canonical identity, indexed lookup, complete monotonic call keys), §§6,8 (fact-driven
lowering and explicit temporary ownership), and §§9–10 (bounded observable work and
self-contained evaluation).

## Current Failure Map

Current result: all original 41/129 handout passes remain, plus one course audit
regression (42/130 overall), with 88 handout failures. The remaining failure set groups
into: floating scalar evaluation/conversion; indirect, member, and callable-object calls;
class/aggregate/constructor values; pointer/reference/array/static-member values;
`noexcept` and literal-class/constructor declaration validation; and function-local
static initialization/LowIR emission. These are later value/call/storage owners rather
than regressions in the audited integral invocation path.

## Active Checkpoint

**Typed scalar-value widening.** Replace the invocation stack and call cache's integral
payload with a tagged scalar constant value. Add target-precision floating literals,
conversions, arithmetic, comparisons, locals, calls, and initializer publication while
preserving the audited overlay/scratch ownership and canonical integer behavior. Keep the
representation extensible to pointer and object identities.

Owner/data flow: literal facts and canonical source types -> scalar constant value ->
invocation frames/cache -> initializer/static-assert/template consumers -> typed LowIR
constant facts. Relevant requirements are `spec.md` §§2,4,6,9: stable typed facts,
complete call keys, no textual lowering reconstruction, and observable bounded work.
Expected work remains O(executed expressions/statements) per cache miss and O(1)-average
cache lookup. Validate floating-focused PA21 tests, existing integral calls/overlays, the
full PA21 report, prior-through-PA20, file audit, and scalar conversion-chain scaling.

## Performance Evidence

Integral recursion at depths 32/64/128/256 used 66/130/258/514 evaluator steps,
33/65/129/257 peak locals, and 332/652/1,292/2,572 scratch nodes. Canonical
scopes/declarations/semantic nodes stayed fixed at 5/7/11, while peak stage bytes were
105,030/180,934/333,924/643,556. Repeating depth 128 produced one call-cache hit and held
work at 258 steps. Release telemetry exposes requests, hits, steps, depth, peak locals,
scratch nodes, and canonical graph counts.

## Completed Checkpoints

| Checkpoint | Result | Validation |
| --- | --- | --- |
| Integral scalar invocation and demand | Recursive/defaulted/template-pack calls, locals, scoped type/using overlays, branches, loops, local mutation, demand folding, scalar declaration checks; ownership audit repaired canonical-graph leakage | PA1–20 2,185/2,185; all 41/129 handout passes plus audit regression; file audit pass; fixed canonical graph and linear scaling above |

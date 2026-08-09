# PA21 Implementation Plan

## Stage Design and Spec Alignment

PA21 extends the canonical PA12 semantic graph with a reusable typed constant-evaluation
layer consumed by declaration checks, template arguments, `static_assert`, constant
initialization, and LowIR demand. Evaluation owns tagged scalar values, local type/using
facts, and block lifetimes in compact invocation stacks; floating binding facts use a
compact binding index plus dense payload table instead of widening every hot binding.
Expression products use a reusable scratch arena, and only completed canonical call facts
escape. Lookup consults the overlay without inserting transient names into the `Program`,
while lowering consumes folded typed facts without evaluation replay. This follows `spec.md` §§2–5
(canonical identity, indexed lookup, complete monotonic call keys), §§6,8 (fact-driven
lowering and explicit temporary ownership), and §§9–10 (bounded observable work and
self-contained evaluation).

## Current Failure Map

Current result: 49/130 pass, with all previous passes retained and seven scalar tests added;
81 failures remain. The complete remainder partitions by owner into class/aggregate/array
values and constructors; indirect, member, operator, and callable-object calls;
pointer/reference/subobject/static-member address values; literal-class and constexpr
constructor declaration validation; `noexcept` deduction; and function-local static
classification, guards, and LowIR emission. The remaining floating-suffix test depends on
array values and is assigned to the object-value owner, not scalar arithmetic.

## Active Checkpoint

**Invocation object store and constructor/member execution.** Add invocation-local object
and array storage keyed by compact object/subobject identity, execute selected constexpr
constructors and member functions against recorded layout/member facts, and intern only
completed immutable object values for binding publication and call reuse. This bundles
aggregate initialization with constructor/member access at one stable object-value
boundary while leaving pointer arithmetic and runtime local-static guards separate.

Owner/data flow: selected constructor/member declarations plus canonical layouts ->
invocation object slots and subobject IDs -> immutable completed object value -> member,
array, call, initializer, and LowIR consumers. Relevant requirements are `spec.md` §§2,4,
6,8–9: stable identity, demand-separated completion, fact-driven lowering, phase-local
ownership, and bounded work. Expected work is O(executed statements plus initialized or
read subobjects), O(1) member access by recorded ordinal/offset, and O(1)-average reuse by
prehashed immutable object identity. Validate aggregate/array, constexpr constructor,
member-call, temporary-object, and declaration-rejection groups; then the full PA21,
prior-through-PA20, file audit, and member-count/depth scaling probes.

## Performance Evidence

Floating recursion/conversion depths 32/64/128/256 used 66/130/258/514 evaluator
steps, 66/130/258/514 peak locals, and 397/781/1,549/3,085 scratch nodes. Canonical
scopes/declarations/semantic nodes stayed fixed at 5/9/14; peak stage bytes were
108,616/193,641/363,643/701,563. Repeating depth 128 produced one call-cache hit and held
work at 258 steps. This is linear executed work and temporary storage; completed lookup is
O(1)-average, and float storage adds four indexed bytes per reached binding plus one dense
payload only per published float fact.

## Completed Checkpoints

| Checkpoint | Result | Validation |
| --- | --- | --- |
| Integral scalar invocation and demand | Recursive/defaulted/template-pack calls, locals, scoped type/using overlays, branches, loops, local mutation, demand folding, scalar declaration checks; ownership audit repaired canonical-graph leakage | PA1–20 2,185/2,185; all 41/129 handout passes plus audit regression; file audit pass; fixed canonical graph and linear scaling above |
| Tagged floating scalar widening | Target-rounded literals/conversions, mixed signed/unsigned arithmetic and comparison, floating locals/free calls, binding publication, typed call keys/results, and canonical LowIR literals | PA21 42→49/130; PA1–20 2,185/2,185; file audit pass; fixed canonical graph and linear scaling above |

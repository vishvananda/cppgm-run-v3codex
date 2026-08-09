# PA21 Implementation Plan

## Stage Design and Spec Alignment

PA21 extends the canonical PA12 graph with one typed constant layer for declaration checks,
template arguments, `static_assert`, initialization, and LowIR demand. Tagged scalars,
structurally interned immutable aggregate/array objects, compact binding indexes, local
scope facts, and block lifetimes remain evaluator-owned; only completed call/object facts
escape. Runtime ODR-use rematerializes object facts as structured initializers instead of
replaying evaluation. This follows `spec.md` §§2–6,8–10: canonical identity, indexed lookup,
demand-separated completion, fact-driven lowering, phase-local ownership, and bounded work.

## Current Failure Map

Current result: 56/130 pass, with all previous passes retained and seven object tests added;
74 failures remain. The complete remainder partitions by owner into constexpr constructors,
base/member execution, and literal-class validation; indirect/member/operator/callable calls;
pointer/reference/subobject address values; `noexcept` deduction; and function-local static
classification, guards, and LowIR emission. Aggregate/member-function tests now depend on
constructor receiver execution, while direct aggregate/array storage is complete.

## Active Checkpoint

**Constructor/member invocation over object slots.** Reuse immutable object values as
invocation receivers, initialize mutable invocation-local slots from constructor
initializer facts, execute selected constexpr constructors/member functions, and intern
only a successfully completed result. Pointer arithmetic and runtime local-static guards
remain separate checkpoints.

Owner/data flow: selected function plus layout/initializer facts -> invocation receiver and
subobject slots -> statement execution -> immutable completed object -> call, member, and
initializer consumers. Requirements: `spec.md` §§2,4,6,8–9. Expected work is O(executed
statements plus initialized/read subobjects), O(1) member access by ordinal, and O(1)-average
completed-result reuse. Validate constructors, base/member calls, temporaries, declaration
rejection, full PA21, prior PA1–20, file audit, and depth/member-count probes.

## Performance Evidence

Constexpr arrays of 32/64/128/256 elements produced 49/81/145/273 semantic nodes,
45/77/141/269 conversion checks, 5,039/6,831/10,415/17,583 typed-storage bytes, and
41,928/56,424/90,569/165,065 peak-stage bytes; lowering stayed fixed at six instructions.
This is linear construction/storage with O(1) ordinal reads. Earlier floating recursion at
32/64/128/256 was also linear (66/130/258/514 steps), with a repeated depth-128 call hitting
the completed-call cache.

## Completed Checkpoints

| Checkpoint | Result | Validation |
| --- | --- | --- |
| Integral scalar invocation and demand | Recursive/defaulted/template-pack calls, locals, scoped type/using overlays, branches, loops, local mutation, demand folding, scalar declaration checks; ownership audit repaired canonical-graph leakage | PA1–20 2,185/2,185; all 41/129 handout passes plus audit regression; file audit pass; fixed canonical graph and linear scaling above |
| Tagged floating scalar widening | Target-rounded literals/conversions, mixed signed/unsigned arithmetic and comparison, floating locals/free calls, binding publication, typed call keys/results, and canonical LowIR literals | PA21 42→49/130; PA1–20 2,185/2,185; file audit pass; fixed canonical graph and linear scaling above |
| Immutable aggregate/array values | Structural interning, zero/nested/string initialization, binding/local publication, direct projection, ODR-use rematerialization, canonical bool identities, and multidimensional strides | PA21 49→56/130; PA1–20 2,185/2,185; file audit pass; linear 32–256 element scaling above |

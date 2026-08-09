# PA21 Implementation Plan

## Stage Design and Spec Alignment

PA21 extends the canonical PA12 graph with one typed constant layer for declaration checks,
template arguments, `static_assert`, initialization, and LowIR demand. Tagged scalars,
structurally interned immutable aggregate/array objects, canonical allocation-relative
addresses, compact binding indexes, local scope facts, and block lifetimes remain
evaluator-owned; only completed call/object/address facts escape. Runtime ODR-use
rematerializes completed facts instead of replaying evaluation. This follows `spec.md`
§§2–6,8–10: canonical identity, indexed lookup, demand-separated completion, fact-driven
lowering, phase-local ownership, and bounded work.

## Current Failure Map

Current result: 76/130 pass, with all previous passes retained; 54 failures remain. The
remainder partitions by owner into base/delegating/converting and object-return execution;
overloaded/operator/callable calls; `noexcept` deduction; pointer-bearing constructor
materialization and wide literals; and function-local static classification, guards, and
LowIR emission. Direct constructors, immutable objects, canonical address evaluation,
indirect calls, and checked pointer walking are complete.

## Active Checkpoint

**Base-subobject and converting construction.** Extend immutable object completion across
base initializer order, copy/converting construction, constructor delegation, and class
results returned from constexpr functions without weakening nonliteral-result rejection.

Owner/data flow: constructor selection and ordered base/member recipes -> evaluator frame ->
immutable complete-object fact -> reference/value projection or returned class fact ->
syntax-preserving materialization and demand. Requirements: `spec.md` §§2–6,8–9. Expected
work is linear in initialized subobjects plus executed statements, with O(1)-average binding
and completed-object lookup. Validate base/copy/delegating/converting probes, nonliteral bad
cases, full PA21, prior PA1–20, file audit, and constructor-width scaling.

## Performance Evidence

Constexpr arrays of 32/64/128/256 elements produced 49/81/145/273 semantic nodes,
45/77/141/269 conversion checks, 5,039/6,831/10,415/17,583 typed-storage bytes, and
41,928/56,424/90,569/165,065 peak-stage bytes; lowering stayed fixed at six instructions.
This is linear construction/storage with O(1) ordinal reads. Earlier floating recursion at
32/64/128/256 was also linear (66/130/258/514 steps), with a repeated depth-128 call hitting
the completed-call cache. Constructor/member probes with 16/32/64/128 fields produced
101/181/341/661 semantic nodes, 31/63/127/255 scratch nodes, 20,028/36,284/68,796/133,825
typed bytes, and 68/132/260/516 LowIR instructions; constructor/member and ordinal-probe
counters doubled exactly, confirming linear initialization/read/lowering work.
Checked pointer walks of 32/64/128/256 elements used 133/261/517/1,029 constexpr steps,
one call request, 18 semantic nodes, 16,743 typed-storage bytes, and
60,409/64,921/73,899/89,753 peak-stage bytes; lowering stayed at one instruction. The
four-step-per-element growth confirms O(1) canonical stepping, bounds checks, and lookup.

## Completed Checkpoints

| Checkpoint | Result | Validation |
| --- | --- | --- |
| Integral scalar invocation and demand | Recursive/defaulted/template-pack calls, locals, scoped type/using overlays, branches, loops, local mutation, demand folding, scalar declaration checks; ownership audit repaired canonical-graph leakage | PA1–20 2,185/2,185; all 41/129 handout passes plus audit regression; file audit pass; fixed canonical graph and linear scaling above |
| Tagged floating scalar widening | Target-rounded literals/conversions, mixed signed/unsigned arithmetic and comparison, floating locals/free calls, binding publication, typed call keys/results, and canonical LowIR literals | PA21 42→49/130; PA1–20 2,185/2,185; file audit pass; fixed canonical graph and linear scaling above |
| Immutable aggregate/array values | Structural interning, zero/nested/string initialization, binding/local publication, direct projection, ODR-use rematerialization, canonical bool identities, and multidimensional strides | PA21 49→56/130; PA1–20 2,185/2,185; file audit pass; linear 32–256 element scaling above |
| Constructor/member invocation | Dump-node object facts, constructor argument frames, member-initializer execution, immutable receiver calls, default class initialization, scalar-member suitability checks, and runtime/static materialization boundaries | PA21 56→67/130; PA1–20 2,185/2,185; file audit pass; linear 16–128 field scaling above |
| Canonical addresses and indirect calls | Allocation-relative null/binding/local/string/function identities, lvalue/reference transport, subobject bounds, pointer walking/comparison/truth, address-returning calls, indirect function calls, and expanded-pack array completion | PA21 67→76/130; PA1–20 2,185/2,185; file audit pass; linear 32–256 pointer-walk scaling above |

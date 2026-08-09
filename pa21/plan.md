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

Current result: 79/130 pass, with all previous passes retained; 51 failures remain. The
remainder partitions by owner into base/delegating object shape; overloaded operators,
callable objects, and contextual conversions; `noexcept` deduction; pointer-bearing class
materialization and wide literals; constexpr declaration suitability; runtime class-return
ABI details; and function-local static classification, guards, and LowIR emission. Direct
constructors, immutable objects, canonical addresses, indirect calls, class-valued returns,
and converting call arguments are complete.

## Active Checkpoint

**Base-subobject completion.** Extend immutable object completion across ordered direct-base
initializers, base copy/conversion, derived-to-base receiver/reference projection, and
delegating constructors while retaining the completed class-return transport.

Requirements: PA21 requires valid base/member initialization and base-backed member calls;
`spec.md` §§2–6,8–9 require canonical subobject identity, indexed base edges, narrow
completion ownership, recorded conversion/layout facts, and bounded work. Owner/data flow:
selected constructor plus ordered direct-base/member recipes -> evaluator frame -> immutable
complete-object fact with explicit base projection -> member/reference reads or returned
class fact -> demand/materialization. Expected work is linear in initialized subobjects plus
executed statements, with O(1)-average member/object lookup and bounded base-path traversal.
Validate CRTP/base-reference, base-copy, delegation and base-pack probes, nonliteral bad
cases, full PA21, prior PA1–20, file audit, and inheritance-depth scaling.

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
Class returns over 16/32/64/128-member objects used one call request, three evaluator steps,
seven scratch nodes, and one LowIR instruction at every width. Layout visits were
16/32/64/128, special-member subobject visits 64/128/256/512, conversion checks
101/181/341/661, and lookups 68/116/212/404; peak-stage storage was
57,287/65,639/111,527/204,413 bytes. Work is linear and storage follows geometric growth;
returned object facts add no width-dependent call replay or lowering.

## Completed Checkpoints

| Checkpoint | Result | Validation |
| --- | --- | --- |
| Integral scalar invocation and demand | Recursive/defaulted/template-pack calls, locals, scoped type/using overlays, branches, loops, local mutation, demand folding, scalar declaration checks; ownership audit repaired canonical-graph leakage | PA1–20 2,185/2,185; all 41/129 handout passes plus audit regression; file audit pass; fixed canonical graph and linear scaling above |
| Tagged floating scalar widening | Target-rounded literals/conversions, mixed signed/unsigned arithmetic and comparison, floating locals/free calls, binding publication, typed call keys/results, and canonical LowIR literals | PA21 42→49/130; PA1–20 2,185/2,185; file audit pass; fixed canonical graph and linear scaling above |
| Immutable aggregate/array values | Structural interning, zero/nested/string initialization, binding/local publication, direct projection, ODR-use rematerialization, canonical bool identities, and multidimensional strides | PA21 49→56/130; PA1–20 2,185/2,185; file audit pass; linear 32–256 element scaling above |
| Constructor/member invocation | Dump-node object facts, constructor argument frames, member-initializer execution, immutable receiver calls, default class initialization, scalar-member suitability checks, and runtime/static materialization boundaries | PA21 56→67/130; PA1–20 2,185/2,185; file audit pass; linear 16–128 field scaling above |
| Canonical addresses and indirect calls | Allocation-relative null/binding/local/string/function identities, lvalue/reference transport, subobject bounds, pointer walking/comparison/truth, address-returning calls, indirect function calls, and expanded-pack array completion | PA21 67→76/130; PA1–20 2,185/2,185; file audit pass; linear 32–256 pointer-walk scaling above |
| Class-valued calls and conversions | Object IDs through returns/calls, converting-constructor argument facts, temporary allocation identity, constexpr hidden-friend facts, and compile-time/runtime demand separation | PA21 76→79/130; aggregate return/NTTP, hidden-friend, and reference-conversion probes pass; PA1–20 2,185/2,185; audit pass; linear 16–128 member scaling above |

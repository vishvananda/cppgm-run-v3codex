# PA21 Implementation Plan

## Stage Design and Spec Alignment

PA21 extends the canonical PA12 graph with one typed constant layer for declaration checks,
template arguments, `static_assert`, initialization, and LowIR demand. Tagged scalars,
structurally interned immutable aggregate/array objects, canonical allocation-relative
addresses, compact binding indexes, local scope facts, and block lifetimes remain
evaluator-owned. Completed calls use canonical function/receiver and typed
scalar/object/address argument identities; scalar or immutable object results and expected
failures are memoized at that owner. Invocation storage boundaries plus each object's
transitive newest-local summary prevent direct or nested local addresses from escaping.
Runtime ODR-use rematerializes completed facts and creates emission demand without replaying
evaluation. This follows `spec.md` §§2–6,8–10: canonical identity, complete cache keys,
indexed lookup, demand-separated completion, fact-driven lowering, phase-local ownership,
and bounded work.

## Current Failure Map

Current result: 87/132 pass and 45 fail, up from 80/131. The complete remaining set
partitions by owner into overloaded operators, callable objects, and contextual/user
conversions (11); `noexcept` facts (8); qualified/static object lookup, arrays, and wide
literals (11); class-valued runtime/global materialization (4); function-local static
classification, guards, and emission (10); and constexpr declaration suitability (1).
Base construction/projection, direct constructors, immutable member/array objects,
canonical addresses, indirect calls, safe class returns, complete call-result keys, and
converting call arguments are complete.

## Active Checkpoint

**Callable and contextual-conversion completion.** Keep overload lookup/ranking in the
ordinary semantic graph, then let the evaluator consume its selected canonical binding,
prepared conversion facts, receiver object/address, and converted argument identities for
overloaded operators, callable objects, and user conversions. Results return through the
existing typed scalar/object/address channel; runtime demand remains separate from a
successful compile-time fact.

Requirements: PA21's call/operator/conversion constant-expression rules and `spec.md`
§§2–6,8–9 require one canonical call owner, complete cache keys, recorded conversions,
fact-driven consumers, and demand-separated lowering. Data flow: expression/operator ->
ordinary overload set and conversion sequence -> selected call -> evaluator frame/cache ->
typed result -> contextual consumer or LowIR demand. Expected work is O(candidates ×
arguments + executed constexpr steps) per uncached call and average O(1) cache lookup;
conversion analysis must not replay in the evaluator. Validate all 11 mapped failures,
receiver/reference and shadowing probes, repeated-call scaling, PA1–20, full PA21, and audit.

## Performance Evidence

The retained width probes remain linear: 32/64/128/256-element arrays used
49/81/145/273 semantic nodes and 45/77/141/269 conversion checks; pointer walks used
133/261/517/1,029 evaluator steps; and 16/32/64/128-member constructor/member probes used
101/181/341/661 semantic nodes and 68/132/260/516 LowIR instructions. Class returns over
16/32/64/128 members stayed at one request, three evaluator steps, seven scratch nodes, and
one LowIR instruction while layout and conversion work grew linearly with width.

For repeated class returns with object-reference arguments, 1/2/4/8 identical uses produced
1/2/4/8 requests and 0/1/3/7 cache hits while evaluator steps (5), scratch nodes (17), typed
storage (5,294 bytes), and LowIR instructions (8) stayed fixed. Semantic nodes
33/41/57/89 and peak-stage storage 48,199/48,923/55,795/66,083 grew with the source uses,
showing that only required parsing/consumer work scales after the first complete call key.

Base-depth probes at 8/16/32/64 levels used 15/31/63/127 constructor-base visits,
11/19/35/67 evaluator steps, 41/81/161/321 scratch nodes, and 123/235/459/907 LowIR
instructions. The near-doubling confirms linear constructor traversal and projection work.

## Completed Checkpoints

| Checkpoint | Result | Validation |
| --- | --- | --- |
| Integral scalar invocation and demand | Recursive/defaulted/template-pack calls, locals, scoped type/using overlays, branches, loops, local mutation, demand folding, scalar declaration checks; ownership audit repaired canonical-graph leakage | PA1–20 2,185/2,185; all 41/129 handout passes plus audit regression; file audit pass; fixed canonical graph and linear scaling above |
| Tagged floating scalar widening | Target-rounded literals/conversions, mixed signed/unsigned arithmetic and comparison, floating locals/free calls, binding publication, typed call keys/results, and canonical LowIR literals | PA21 42→49/130; PA1–20 2,185/2,185; file audit pass; fixed canonical graph and linear scaling above |
| Immutable aggregate/array values | Structural interning, zero/nested/string initialization, binding/local publication, direct projection, ODR-use rematerialization, canonical bool identities, and multidimensional strides | PA21 49→56/130; PA1–20 2,185/2,185; file audit pass; linear 32–256 element scaling above |
| Constructor/member invocation | Dump-node object facts, constructor argument frames, member-initializer execution, immutable receiver calls, default class initialization, scalar-member suitability checks, and runtime/static materialization boundaries | PA21 56→67/130; PA1–20 2,185/2,185; file audit pass; linear 16–128 field scaling above |
| Canonical addresses and indirect calls | Allocation-relative null/binding/local/string/function identities, lvalue/reference transport, subobject bounds, pointer walking/comparison/truth, address-returning calls, indirect function calls, and expanded-pack array completion | PA21 67→76/130; PA1–20 2,185/2,185; file audit pass; linear 32–256 pointer-walk scaling above |
| Class-valued calls and conversions | Object IDs through returns/calls, converting-constructor argument facts, temporary allocation identity, constexpr hidden-friend facts, compile-time/runtime demand separation; audit added complete typed result keys and transitive invocation-storage escape checks | PA21 76→79/130 preserved and audit regression passes for 80/131; aggregate return/NTTP, hidden-friend, reference-conversion, and dangling-object probes pass; PA1–20 2,185/2,185; file audit pass; bounded width and repeated-call scaling above |
| Base-subobject completion | Ordered direct-base facts after direct members, base/member/delegating initialization, base projection for receivers/references, class-return transport, and initializer-local demand/slot boundaries | PA21 80/131→87/132; seven focused base/delegation tests pass; PA1–20 2,185/2,185; file audit pass; linear depth scaling above |

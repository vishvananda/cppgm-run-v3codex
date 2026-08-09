# PA21 Implementation Plan

## Stage Design and Spec Alignment

PA21 extends the canonical PA12 graph with one typed constant layer for declaration checks,
template arguments, `static_assert`, initialization, and LowIR demand. Tagged scalars,
structurally interned immutable aggregate/array objects, canonical allocation-relative
addresses, compact binding indexes, local scope facts, and block lifetimes remain
evaluator-owned. A class expression carries both its immutable complete-object ID and the
active subobject selected by typed base edges, together with the adjusted allocation-relative
address. Completed calls key canonical functions, active/complete receivers, and typed
scalar/object/address arguments; scalar, immutable object, address results, and expected
failures are memoized at that owner. Invocation storage boundaries plus each object's
transitive newest-local summary prevent direct or nested local addresses from escaping.
One semantic arrow-chain owner supplies typed pointer/object facts to every arrow consumer,
and speculative syntax attachments use an exact rollback journal released at the parser
boundary. Runtime ODR-use rematerializes completed facts and creates emission demand without
replaying evaluation. This follows `spec.md` §§1–6,8–10: bounded parser checkpoints,
canonical identity, complete cache keys, indexed lookup, demand-separated completion,
fact-driven lowering, explicit phase ownership, and observable work.

## Current Failure Map

The handout remains at its 100/133 audit-start baseline; the audit regression makes the
combined report 101/134. The 33 remaining failures group by owner into `noexcept`
declaration/expression facts (8); qualified/static references, arrays, and wide literals
(11); function-local static classification, guards, and emission (10); class-valued
runtime/global materialization (3); and constexpr declaration suitability (1). Callable
objects and surrogates, overloaded operators, recursive arrows, contextual/user conversions,
all typed call-result categories, template const-reference ordering, and the earlier
base/object/address layers are complete.

## Active Checkpoint

**`noexcept` fact completion (next).** Resolve exception specifications once at declaration
completion, including defaulted/user constructors, inherited using overloads, dependent
callables, `decltype`, and array-reference results. Expression consumers read the selected
function or constructor's canonical nonthrowing fact; LowIR uses the same fact without
re-running overload resolution.

Requirements: PA21 declaration checks plus `spec.md` §§2–6,8–9 require canonical declaration
ownership, indexed lookup, retained selected bindings, fact-driven lowering, and no semantic
replay. Data flow: declarator/defaulted-special-member completion -> canonical function fact
-> ordinary or unevaluated call selection -> `noexcept`/`decltype` scalar consumer -> LowIR
signature. Expected work is O(declarations + selected overload candidates), with average O(1)
canonical fact lookup. Validate all 8 mapped failures, nearby passing overload/defaulted
probes, PA1–20, full PA21, and audit.

## Performance Evidence

The retained width probes remain linear: 32/64/128/256-element arrays used
49/81/145/273 semantic nodes and 45/77/141/269 conversion checks; pointer walks used
133/261/517/1,029 evaluator steps; and 16/32/64/128-member constructor/member probes used
101/181/341/661 semantic nodes and 68/132/260/516 LowIR instructions. Class returns over
16/32/64/128 members stayed at one request, three evaluator steps, seven scratch nodes, and
one LowIR instruction while layout and conversion work grew linearly with width.

For 1/2/4/8 identical class-argument calls with contextual `operator bool`, requests were
2/3/5/9 with 0/1/3/7 cache hits, evaluator steps were 5/6/8/12, and scratch stayed fixed at
11 nodes. Semantic nodes were 13/23/43/83, overload candidates 12/22/42/82, and conversion
checks 36/57/99/183; all runs emitted one LowIR instruction and zero demanded functions.
Thus source-facing overload work grows linearly while completed evaluation remains cached
and compile-time-only demand stays bounded.

For 1/2/4/8 identical address-returning calls, requests were 1/2/4/8 with 0/1/3/7 cache
hits, while evaluator steps (2), scratch nodes (2), typed storage (2,151 bytes), LowIR
instructions (1), and demanded functions (0) stayed fixed; semantic nodes grew
12/18/30/54. Those sources retained 41/48/62/90 syntax edges and used
1,165/1,222/1,334/2,326 parser bytes with geometric capacity growth. The rollback journal
is one 12-byte mutation per edge while parsing and is released before semantic consumption.

## Completed Checkpoints

| Checkpoint | Result | Validation |
| --- | --- | --- |
| Integral scalar invocation and demand | Recursive/defaulted/template-pack calls, locals, scoped type/using overlays, branches, loops, local mutation, demand folding, scalar declaration checks; ownership audit repaired canonical-graph leakage | PA1–20 2,185/2,185; all 41/129 handout passes plus audit regression; file audit pass; fixed canonical graph and linear scaling above |
| Tagged floating scalar widening | Target-rounded literals/conversions, mixed signed/unsigned arithmetic and comparison, floating locals/free calls, binding publication, typed call keys/results, and canonical LowIR literals | PA21 42→49/130; PA1–20 2,185/2,185; file audit pass; fixed canonical graph and linear scaling above |
| Immutable aggregate/array values | Structural interning, zero/nested/string initialization, binding/local publication, direct projection, ODR-use rematerialization, canonical bool identities, and multidimensional strides | PA21 49→56/130; PA1–20 2,185/2,185; file audit pass; linear 32–256 element scaling above |
| Constructor/member invocation | Dump-node object facts, constructor argument frames, member-initializer execution, immutable receiver calls, default class initialization, scalar-member suitability checks, and runtime/static materialization boundaries | PA21 56→67/130; PA1–20 2,185/2,185; file audit pass; linear 16–128 field scaling above |
| Canonical addresses and indirect calls | Allocation-relative null/binding/local/string/function identities, lvalue/reference transport, subobject bounds, pointer walking/comparison/truth, address-returning calls, indirect function calls, and expanded-pack array completion | PA21 67→76/130; PA1–20 2,185/2,185; file audit pass; linear 32–256 pointer-walk scaling above |
| Class-valued calls and conversions | Object IDs through returns/calls, converting-constructor argument facts, temporary allocation identity, constexpr hidden-friend facts, compile-time/runtime demand separation; audit added complete typed result keys and transitive invocation-storage escape checks | PA21 76→79/130 preserved and audit regression passes for 80/131; aggregate return/NTTP, hidden-friend, reference-conversion, and dangling-object probes pass; PA1–20 2,185/2,185; file audit pass; bounded width and repeated-call scaling above |
| Base-subobject completion | Ordered direct-base facts after direct members, base/member/delegating initialization, active/complete object transport with adjusted addresses through receivers/references/cache facts, and initializer-local demand/slot boundaries; audit repaired repeated-base ambiguity and inherited-base ownership | PA21 handout 80/131→87/132 preserved, audit regression passes for 88/133; eight focused base/delegation tests pass; PA1–20 2,185/2,185; file audit pass; linear depth scaling above |
| Callable and contextual conversions | Local-callable shadowing, call operators/surrogates, one recursive-arrow owner, overloaded unary/binary/subscript/assignment/logical operators, semantic class-expression initialization, user/return conversions, complete cached scalar/reference/pointer/object results, template cv partial ordering, exact parser rollback, and compile-time/runtime demand separation | PA21 handout 88→100/133 preserved, audit regression passes for 101/134; PA1–20 2,185/2,185; file audit pass; repeated call/address and parser scaling above |

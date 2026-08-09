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
Exception specifications keep constant-expression demand active through one typed
contextual-bool conversion; `noexcept` consumes selected callable/lifetime facts, including
temporary destructors, and records bounded action-walk visits without creating emission
demand.
One semantic arrow-chain owner supplies typed pointer/object facts to every arrow consumer,
and speculative syntax attachments use an exact rollback journal released at the parser
boundary. Runtime ODR-use rematerializes completed facts and creates emission demand without
replaying evaluation. This follows `spec.md` §§1–6,8–10: bounded parser checkpoints,
canonical identity, complete cache keys, indexed lookup, demand-separated completion,
fact-driven lowering, explicit phase ownership, and observable work.

## Current Failure Map

The combined report is now 109/135, including the exception-ownership audit regression. The
26 remaining handout failures group by owner into
qualified/static references, arrays, and wide literals (11); function-local static
classification, guards, and emission (11, including the array-reference-return case whose
diff is entirely local-static storage); class-valued runtime/global materialization (3); and
constexpr declaration suitability (1). Canonical exception specifications and `noexcept`
expressions now join the completed callable, conversion, base/object, and address layers.

## Active Checkpoint

**Qualified static constant storage completion (next).** Unify qualified lookup and storage
publication for static constexpr scalars, references, pointers, arrays, and class objects,
including class-template and typedef-qualified owners. Preserve immutable scalar/object/
address facts for compile-time consumers while creating one stable storage/emission identity
only when ODR-use or runtime lowering demands it; wide literals use their typed element width.

Requirements: PA21's object-valued evaluation, qualified reuse, and constant-initialization
contract plus `spec.md` §§2–6,8–9 require canonical binding/type/object identities, indexed
qualified lookup, demand-separated completion, typed lowering, and bounded phase-local
storage. Owner/data flow: class/member or specialization completion -> canonical static
binding -> typed scalar/object/address initializer fact -> qualified semantic consumer or
ODR-use demand -> one global LowIR identity. Expected work is O(qualified path length +
initializer elements + demand edges), with average O(1) completed-fact access and no scans of
unrelated declarations. Validate all 11 owned failures, nearby passing qualified/object
probes, PA1–20, full PA21, file audit, and width/repeated-lookup scaling.

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

For 1/2/4/8 identical dependent `noexcept(work<int>())` consumers, semantic nodes were
10/13/19/31, nonthrowing-action visits 2/4/8/16, and overload candidates 1/2/4/8.
Specialization requests were 6/10/18/34 with 4/8/16/32 cache hits, leaving exactly two misses
at every width; conversion checks (2), typed storage (1,735 bytes), LowIR instructions (1),
constexpr calls (0), and demanded functions (0) stayed fixed. Successful temporary operands
at the same widths used 8/11/17/29 semantic nodes, 2/4/8/16 action visits, and 2/4/8/16
overload candidates while conversion checks (1), typed storage (1,735 bytes), LowIR
instructions (1), demand pushes (0), and demanded functions (0) stayed fixed. Thus action
inspection and source-facing selection grow linearly while completed template facts are
reused without emission demand.

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
| Canonical exception and `noexcept` facts | Unevaluated fold-suppressed operands consume selected canonical call/constructor/lifetime facts; dependent specifications complete per specialization; audit unified contextual-bool conversion, compile-time-only demand, temporary destruction, and action observability | PA21 101→108/134 preserved, audit regression passes for 109/135; owned probes 8/8; PA1–20 2,185/2,185; file audit pass; linear action/specialization scaling above |

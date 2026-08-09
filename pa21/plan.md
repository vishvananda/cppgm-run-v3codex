# PA21 Implementation Plan

## Stage Design and Spec Alignment

PA21 extends the canonical PA12 graph with one typed constant layer for declaration checks,
template arguments, `static_assert`, initialization, and LowIR demand. Tagged scalars,
structurally interned immutable aggregate/array objects, canonical allocation-relative
addresses, compact binding indexes, local scope facts, and block lifetimes remain
evaluator-owned. A class expression carries both its immutable complete-object ID and the
active subobject selected by typed base edges, together with the adjusted allocation-relative
address. Completed calls key canonical functions, active/complete receivers, and typed
scalar/object/address arguments; scalar or immutable object results and expected failures
are memoized at that owner. Invocation storage boundaries plus each object's transitive
newest-local summary prevent direct or nested local addresses from escaping. Runtime ODR-use
rematerializes completed facts and creates emission demand without replaying evaluation.
This follows `spec.md` §§2–6,8–10: canonical identity, complete cache keys, indexed lookup,
demand-separated completion, fact-driven lowering, explicit phase ownership, and bounded,
observable work.

## Current Failure Map

Latest result: 100/133 pass, up from 88/133 at turn start. The 33 remaining failures group by
owner into `noexcept` declaration/expression facts (8); qualified/static references, arrays,
and wide literals (11); function-local static classification, guards, and emission (10);
class-valued runtime/global materialization (3); and constexpr declaration suitability (1).
Callable objects, overloaded operators, contextual/user conversions, pointer/object paired
facts, template const-reference ordering, and the earlier base/object/address layers are
complete.

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

For repeated class returns with object-reference arguments, 1/2/4/8 identical uses produced
1/2/4/8 requests and 0/1/3/7 cache hits while evaluator steps (5), scratch nodes (17), typed
storage (5,294 bytes), and LowIR instructions (8) stayed fixed. Semantic nodes
33/41/57/89 and peak-stage storage 48,199/48,923/55,795/66,083 grew with the source uses,
showing that only required parsing/consumer work scales after the first complete call key.

For 1/2/4/8 identical class-argument calls with contextual `operator bool`, requests were
2/3/5/9 with 0/1/3/7 cache hits, evaluator steps were 5/6/8/12, and scratch stayed fixed at
11 nodes. Semantic nodes were 13/23/43/83, overload candidates 12/22/42/82, and conversion
checks 36/57/99/183; all runs emitted one LowIR instruction and zero demanded functions.
Thus source-facing overload work grows linearly while completed evaluation remains cached
and compile-time-only demand stays bounded.

Audited base-depth probes at 8/16/32/64 levels used 15/31/63/127 constructor-base visits,
9/17/33/65 object-projection visits, 11/19/35/67 evaluator steps, 41/81/161/321 scratch
nodes, and 123/235/459/907 LowIR instructions. Typed storage was
41,484/80,246/157,016/311,576 bytes and peak-stage storage was
276,966/551,103/1,135,047/2,429,119 bytes. A two-branch repeated-base probe used two call
requests, zero cache hits, 14 projection visits, 14 steps, four scratch nodes, and one LowIR
instruction. These counters show linear depth traversal and distinct cache ownership for
sibling subobjects.

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
| Callable and contextual conversions | Local-callable shadowing, callable/recursive-arrow dispatch, overloaded unary/binary/subscript/assignment/logical operators, user and return conversions, scalar/reference/pointer/object result transport, template cv partial ordering, and compile-time/runtime demand separation | PA21 88→100/133; all 12 mapped and nearby regression probes pass; PA1–20 2,185/2,185; file audit pass; repeated-call scaling above |

# PA34 Plan

## Stage Design and Spec Alignment

PA34 extends the shared source-to-object pipeline at existing boundaries. Hosted
configuration feeds the streaming preprocessor; parser extensions publish canonical
syntax/type facts; semantics owns typed operands, lookup, demand, and compact builtin
IDs; typed LowIR owns effects and value behavior; native lowering alone selects host
symbols. This follows `spec.md` §§1-3 and 6-10: interned inputs, bounded registries, no
spelling recovery after semantics, demand-driven emission, linear lowering/allocation,
and no host-compiler fallback.

## Current Failure Map

- Preprocess is complete: 45/45.
- Compile has 70/281 failures: 16 extension grammar/type cases, 18
  trait/layout/constant cases, and 36 template lookup/demand cases.
- Run has 19/41 failures: 16 stop in the compile pipeline and 3 reach linked
  backend/lifetime or forced-inline behavior.
- PA34 is 278/367 overall; PA1-PA33 remain 4387/4387.

## Active Checkpoint

Next, implement trait operand-pack replay and assignment-expression queries, bundling
the remaining pack-wrapped constructibility cases with deleted, ref-qualified,
templated, trivial, and nothrow assignment cases. Under `spec.md` §§2, 4-7, and 9,
template substitution owns expansion of each retained builtin operand pack into
canonical `TypeId`s; PA34 trait semantics then creates hypothetical lhs/rhs expressions
and delegates to member/builtin operator selection and completed special-member facts.
The constexpr result remains the only lowering input, with no spelling recovery.

Expected work is O(expanded operands + reachable assignment candidates), with
O(1)-average canonical/cache access and one expansion per specialization. Validate
empty/single/many packs, SFINAE replay, deleted and object-category overloads,
member-template assignment, trivial/nothrow facts, 1/8/64 repeated queries, then the
PA34, through-PA33, and file-audit gates.

## Performance Evidence

| Boundary | Representative scaling evidence |
| --- | --- |
| Hosted input/type queries | 1→8 preprocessing produced 8x counters in 6.98x time; 8→64 type queries produced 8x work in 6.27x time |
| Integer/memory/atomic builtins | 1/8/64 probes emitted exact proportional LowIR/MIR; 64-case times were 0.07/0.04/0.04 s |
| Floating builtins | 1/8/64 emitted 411/3,288/26,304 native instructions; 0.00/0.01/0.07 s |
| Layout/asm | 1/8/64 empty-member visits and asm LowIR were linear; layout 0.49/0.58/1.43 ms, asm RSS ~8.2 MiB |
| Vector/block/lambda | Nodes/lookups were linear; lambda requests 1/8/64 had 0/7/63 cache hits and one emitted specialization |
| Numeric scalars | 1/8/64 emitted 9/65/513 LowIR and 15/85/645 MIR; 64 cases took 0.02 s, 10,472 KiB RSS |
| GNU complex | 1/8/64 repeated compiles took 0.00/0.05/0.40 s with 8,328/8,552/8,568 KiB RSS; pair construction is two fixed stores |
| Construction/conversion traits | 1/8/64 unique template query sets compiled in 0.00/0.00/0.01 s with 8,088/8,212/9,644 KiB RSS |

## Completed Checkpoints

| Checkpoint | Result | Validation |
| --- | --- | --- |
| Hosted preprocessing boundary | Configuration, probes, directives, literals; +51 | preprocess 45/45; PA34 121/367; prior/audit pass |
| Hosted builtin type queries | Traits/transforms, structural constants, null adaptation; +46 | PA34 167/367; prior/audit pass |
| Hosted annotations | GNU aliases/int128 and declaration/type-id attributes; +20 | PA34 187/367; prior/audit pass |
| Integer bit intrinsics | Registry IDs, constexpr semantics, typed lowering; +8 | focused 8/8; PA34 195/367; prior/audit pass |
| Memory/string intrinsics | Typed effects, ABI symbols, identity lowering; +9 | PA34 204/367; linked/scaling/prior/audit pass |
| C11/GNU atomic and sync | Canonical atomic types and first-class atomic LowIR; +12 | PA34 216/367; linked/scaling/prior/audit pass |
| Scalar floating and abort | Width-aware values/predicates and nonreturning abort; +6 | PA34 222/367; scaling/prior/audit pass |
| Class layout attributes | Aligned/packed/no-unique facts and template replay; +5 | PA34 227/367; linked/scaling/prior/audit pass |
| GNU asm and labels | Structured effects and binding-owned ABI labels; +10 | PA34 237/367; linked/scaling/prior/audit pass |
| Scalar GNU vectors | Canonical lane/width identity and object lowering; +2 | PA34 239/367; focused/scaling/prior/audit pass |
| Canonical block pointers | Distinct callable type, mangling, invoke lowering; +4 | PA34 243/367; linked/scaling/prior/audit pass |
| Generic lambdas | Retained templates, cached demand, local ABI identity; +7 | PA34 250/367; linked/scaling/prior/audit pass |
| Hosted numeric scalars | Canonical `_BitInt`/extended floats, traits, ABI; +6 | PA34 256/367; linked/scaling/prior/audit pass |
| GNU complex scalars | Canonical pair type, components/builtin, `C<element>` ABI, by-address object boundary; +2 | focused 2/2; linked/invalid/traits/mangling/scaling; PA34 258/367; PA1-33 4387/4387; audit pass |
| Construction/conversion traits | `declval` categories, direct constructor/implicit conversion selection, nothrow/trivial special-member facts; +20 | focused 10/10; PA34 278/367; scaling; PA1-33 4387/4387; audit pass |

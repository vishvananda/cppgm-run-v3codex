# PA34 Plan

## Spec Alignment

PA34 extends the shared source-to-object pipeline at existing boundaries. Hosted
configuration feeds the streaming preprocessor; parser extensions publish canonical
syntax/type facts; semantics owns typed operands, lookup, demand, and compact builtin
IDs; typed LowIR owns effects and value behavior; native lowering alone selects host
symbols. This follows `spec.md` §§1-3 and 6-10: interned inputs, bounded registries, no
spelling recovery after semantics, demand-driven emission, linear lowering/allocation,
and no host-compiler fallback.

## Current Failure Map

- Preprocess is complete: 45/45.
- Handout compile has 56/281 failures: 16 extension grammar/type cases, 13
  trait/layout/constant cases, and 27 template lookup/demand cases.
- Run has 19/41 failures: 16 stop in the compile pipeline and 3 reach linked
  backend/lifetime or forced-inline behavior.
- Audit course compile is 2/2. PA34 is 294/369 overall while the handout
  baseline remains 292/367; PA1-PA33 remain 4387/4387.

## Active Checkpoint — Next Substantial Checkpoint

Next, implement retained variable-template constant replay and the remaining hosted
unary/shorthand trait facts. Under `spec.md` §§2, 4-7, and 9, variable-template demand
must replay retained constant syntax exactly once in its specialization scope; canonical
trait operands then flow through class completion into abstract/polymorphic/destructor,
literal/layout, rank, and nothrow facts before publishing the specialization-owned
constant. Lookup and demand own specialization identity; lowering receives only the
resolved constant.

Expected work is O(unique specialization + trait operands), with O(1)-average retained
recipe/cache access and demand-once replay. Validate dependent variable templates,
virtual-destructor/abstract/polymorphic/literal/rank traits, nothrow shorthands, SFINAE,
1/8/64 specialization demand, then the PA34, through-PA33, and file-audit gates.

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
| Trait packs and assignment | 1/8/64 unique pack/operator sets recorded 5/40/320 candidates, 8/64/512 template requests, 13/69/517 nodes, and 90,239/467,809/3,661,927 peak semantic bytes; 0.690/2.400/16.499 ms semantic time, 8,264/8,680/10,060 KiB RSS, constant backend output |

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
| Trait packs and assignment | Ordered operand expansion, indexed member-template/ref-qualified selection, and distinct user-provided/trivial/body-copy facts; +14 handout, +2 audit regressions | focused 24/24; PA34 handout 292/367 and total 294/369; PA1-33 4387/4387; proportional scaling; audit/file gates pass |

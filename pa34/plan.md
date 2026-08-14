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
- Handout compile has 51/281 failures: 16 parser/declaration/type cases, 8
  semantic trait/layout/constant cases, and 27 template lookup/demand/pack cases.
- Run has 19/41 failures: 16 stop in the compile pipeline and 3 reach linked
  backend/lifetime or forced-inline behavior.
- Audit course compile is 2/2. PA34 is 299/369 overall and 297/367 in the
  handout; PA1-PA33 remain 4387/4387.

## Active Checkpoint — Retained Fold and Pack Expressions

Implement fold expressions, `sizeof...`, and indexed pack selection as retained typed
template recipes. Under `spec.md` §§1-5 and 9, the parser owns structured fold/pack
syntax; template substitution binds compact pack slices; semantic replay applies the
correct fold direction and empty identity, publishes canonical constant/type results,
and demands only the selected alias/member specialization. Under §§6-8, lowering sees
resolved scalar or type facts rather than source spellings.

Ownership/data flow is parser fold/pack node -> retained template registry ->
specialization substitution scope -> typed expression/type result -> ordinary lowering.
Expected work is O(pack length) per unique specialization with O(1)-average indexed
lookup/cache access and no whole-program retry. Validate the three fold-expression cases,
integer/value/type pack aliases, nested tuple indices, pack constructors and SFINAE,
1/8/64 pack widths, then the PA34, through-PA33, and file-audit gates.

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
| Retained variable-template traits | 1/8/64 unique groups recorded 7/56/448 requests and 3/24/192 candidates; semantic time 0.535/1.319/8.313 ms, peak 73,899/250,858/1,911,348 bytes, RSS 8,560/8,408/9,552 KiB, and one backend instruction |

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
| Retained variable-template traits | Demand-state replay, final/rank/virtual-destructor facts, compiler-identifier probing, and typed global nothrow shorthands; +5 | focused 7/7; PA34 299/369; PA1-33 4387/4387; proportional scaling; audit pass |

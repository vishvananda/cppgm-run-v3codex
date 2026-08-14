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
- Dependent callable/type replay is complete: all 4 reducers pass.
- Canonical declaration types/demand is complete: compatible extern arrays, distinct GNU
  zero-length arrays, and unused IA32 intrinsic wrappers pass with ordinary errors preserved.
- Typed constant objects are complete: wait-status constexpr object reinterpretation and
  full-width int128 initializer/member cases pass.
- Runtime object actions are complete: canonical always-inline expansion and throwing
  large-array destruction pass in compile-only, direct-link, and staged-link paths.
- ABI/source identity is complete: nested-template owner mangling and canonical
  inline/owner-template pretty-function presentation pass.
- Audit course compile is 2/2. PA34 is 369/369 tests, while
  PA1-PA33 remain 4387/4387.

## Active Checkpoint — ABI and Source Identity Completion

The canonical-name boundary is complete under `spec.md` §§1-4 and 6-7. Parsed lexical
identity must retain inline-namespace transparency and elaborated/template-owner arguments;
canonical semantic identity must attach declaration attributes exactly once without deriving ABI
or pretty-function text from backend spellings.

Parser/template replay owns structured source names and arguments, semantic bindings own canonical
owners and ABI tags, and lowering only materializes the resulting string/object identity. Data flows
syntax name/template arguments -> instantiated owner and canonical binding -> ABI/pretty-function
fact -> typed constant or object symbol.

Work is O(name path + template argument count) per demanded entity with interned lookup and no
namespace/template rescans during rendering. Validation covers all 4 reducers, redeclared
out-of-class and nested specialized members, aliases and inline namespaces, defaulted owner/function
template arguments, malformed and out-of-scope forms, and 1/8/64 identity batches.

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
| Fold and indexed pack replay | Width 1/8/64 mixed fold/integer/type-pack cases recorded 31/66/346 semantic nodes, 51/128/744 lookups, constant 3 template requests and 6 candidates; semantic time 0.587/0.915/3.216 ms, peak 78,971/187,906/928,290 bytes, RSS 8,376/8,460/9,216 KiB, and one backend instruction |
| Aggregate designation/decomposition | 1/8/64-member trailing-designator plus `auto&` decomposition emitted 19/26/82 semantic nodes, 17/31/143 declarations, 24/45/213 lookups, 17/38/206 LowIR and 24/53/277 MIR; semantic time 0.314/0.356/0.619 ms, peak 38,024/44,762/190,388 bytes, RSS 8,228/8,216/9,056 KiB; all linked probes passed |
| Compiler function builtins/invoke | 1/8/64 mixed overflow/member-invoke/`offsetof` batches emitted 61/243/1,699 semantic nodes, 63/280/2,016 lookups, 55/237/1,693 LowIR and 76/321/2,281 MIR; semantic time 0.529/0.850/4.250 ms, peak 56,176/124,227/756,406 bytes, RSS 8,468/9,356/14,552 KiB; all linked probes passed |
| Hosted configuration/runtime | 1/8/64 cmath/cstring/cstdlib batches emitted 4,419/4,706/7,002 semantic nodes, 5,772/6,157/9,237 lookups, 45/339/2,691 LowIR and 87/633/5,001 MIR; 0.32/0.32/0.36 s wall, 18,012/18,160/22,772 KiB RSS; all linked probes passed |
| Hosted declarations | 1/8/64 guide/conditional-explicit/placeholder groups produced 14/77/581 declarations and 9/37/261 lookups, zero template requests or demands; semantic time 0.254/0.760/4.631 ms, RSS 7,880/8,436/8,956 KiB, constant backend output |
| Hosted selection replay | 1/8/64 selected/discarded specializations produced 25/130/970 semantic nodes, 25/130/970 lookups, and 3/24/192 template requests; semantic time 0.434/0.842/4.224 ms, RSS 8,568/8,388/9,048 KiB; linked probes passed and invalid discarded branches created no work |
| Hosted type formation | 1/8/64 unique 8-element sequence specializations produced 1/8/64 requests, 12/61/453 semantic nodes, and 43/224/1,714 KiB peak semantic storage; semantic time 0.408/1.251/8.398 ms, RSS 8,360/8,352/9,496 KiB, constant backend output |
| Dependent callable/type replay | Recursive 1/8/64 vector-alias/partial requests compiled in 0.00/0.00/0.01 s with 8,340/8,216/9,824 KiB RSS; assertions passed, while zero lanes and unequal-width conversion rejected |
| Canonical declarations | 1/8/64 extern-array/zero-member/intrinsic-wrapper groups produced 20/83/587 semantic nodes, 19/110/838 declarations, and 21/112/840 lookups; semantic time 0.304/0.805/4.966 ms, peak 40,408/156,054/1,187,387 bytes, RSS 8,472/8,248/8,864 KiB, with constant 10-instruction LowIR |
| Typed constant objects | 1/8/64 wide-constant batches compiled in 0.00/0.00/0.01 s with 8,644/8,348/9,872 KiB RSS; boundary/static linked probes passed and overflow/out-of-bounds probes rejected |
| Runtime object actions | 1/8/64 template/action batches produced 99/274/1,611 semantic nodes and 79/611/602 LowIR instructions in 0.00/0.01/0.02 s with 8,600/9,740/9,868 KiB RSS; linked probes passed, and counted arrays remain fixed-size above the 8-element inline threshold |
| ABI and source identity | 1/8/64 nested-owner/defaulted-template batches produced 60/326/2,454 semantic nodes, 147/917/7,077 lookups, and 22/106/778 LowIR instructions in 0.00/0.01/0.04 s with 8,732/9,228/12,988 KiB RSS; all linked probes passed |

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
| Fold and indexed pack replay | Structured left/right/binary folds, bounded integer/type-pack builtins, and multi-argument direct-initializer recovery; +12 | focused 12/12; PA34 311/369; PA1-33 4387/4387; proportional scaling; audit pass |
| Aggregate designation and decomposition | Binding-owned hidden objects and member projections, ordered designators, zero-filled holes, active unions, and compound-literal storage; +7 | focused 7/7; linked/template/range/reference/invalid probes pass; PA34 318/369; PA1-33 4387/4387; proportional scaling; audit pass |
| Compiler function builtins and invoke | Typed `offsetof`, source/predefined values, string/format/allocation aliases, widened exact overflow, and direct/pointer-like invocation; +10 | focused compile/link/invalid probes; PA34 328/369; PA1-33 4387/4387; proportional scaling; audit pass |
| Hosted configuration and runtime adapters | Mode-aware exception/RTTI facts, host scalar/keyword normalization, standard-layout/POD, GNU null/typeof/restrict forms, and declaration-backed C/math aliases; +13 | all 3 compile and 6 linked reducers pass; malformed suffix/alias reject; PA34 341/369; PA1-33 4387/4387; proportional scaling; audit pass |
| Hosted declaration compatibility | Structured guide declarations and conditional `explicit`; canonical deleted placeholder overloads; +5 | guide/placeholder/instantiation reducers pass; malformed forms reject; PA34 346/369; proportional scaling; prior/audit gates pass |
| Hosted selection and contextual control | Typed `if constexpr` branch selection, alias/declaration init scopes, ordinary runtime init lowering, and template-only contextual coroutine recipes; +3 | focused 3/3 plus linked/negative probes; PA34 349/369; PA1-33 4387/4387; proportional scaling; audit pass |
| Hosted type formation and primary shims | Canonical `__make_integer_seq`, re-entrant partial selection, pointed-class conversion demand, and undefined `char_traits` primary members; +3 | focused 3/3 plus malformed-count rejection; PA34 352/369; PA1-33 4387/4387; proportional scaling; audit pass |
| Dependent callable and type replay | Structured destructor template-ids, static call operators, lexical lambda enum facts, dependent vector lanes/deduction, and scalarized vector builtins; +4 | reducers 4/4; malformed vector probes reject; PA34 356/369; PA1-33 4387/4387; proportional scaling; audit pass |
| Canonical declaration types and demand | Distinct zero-length arrays, compatible array composites, canonical object symbol types, and typed IA32 `emms` recognition; +3 | focused/negative/link/scaling probes; PA34 359/369; PA1-33 4387/4387; audit pass |
| Typed constant objects | Two-limb int128 facts/operators/static data and bounded binding-address scalar loads; +4 | reducers 4/4; boundary/link/negative/scaling probes; PA34 363/369; PA1-33 4387/4387; audit pass |
| Runtime object actions | Canonical always-inline facts with per-TU CFG expansion; EH-resumable counted array destruction; +2 | reducers 2/2; redeclaration/standard/recursive/EH and 8/9/64 probes; PA34 365/369; PA1-33 4387/4387; scaling/audit pass |
| ABI and source identity | Template-safe predefined function names, canonical source type/substitution rendering, and typed nested-specialization owners; +4 | reducers 4/4; composite/default/inline/nested/negative/scaling probes; PA34 369/369; PA1-33 4387/4387; audit pass |

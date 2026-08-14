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
- Hosted configuration/builtin calls own 7 compile and 12 run failures:
  `builtin-vsnprintf-va-list`, wait-status constants, direct/builtin `offsetof`,
  exception/RTTI/`__func__` predefines, standard-layout/POD, invoke (direct and
  pointer-like), overflow/new-delete/null, cmath/constructor/csignal/cstdio/cstring,
  and source-location builtins.
- Template declaration/deduction/replay owns 17 compile and 1 run failure:
  placeholder return, the three deduction-guide forms, vector deduction,
  special-member/control-flow replay, integer-sequence casts, template-id
  destructors, instantiation compatibility, alias conversion, GNU `decltype`/
  `typeof`, char-traits/coroutine contexts, local aliases, static call operators,
  and local-lambda invoke-result packs.
- Object/layout/constant/lifetime owns 7 compile and 2 run failures: deferred GNU
  inline emission, anonymous no-unique layout, extern arrays, local/global/static
  int128 constants, zero-length members, always-inline codegen, and large-array
  lifecycle.
- ABI/source spelling owns 1 compile and 4 run failures: constexpr
  `__PRETTY_FUNCTION__`, nested-template ABI-tag suppression, and the three inline/
  owner-template pretty-function cases.
- Audit course compile is 2/2. Handout compile is 249/281 and run is 22/41;
  PA34 is 318/369 overall (316/367 handout), while PA1-PA33 remain 4387/4387.

## Active Checkpoint — Compiler Function Builtins and Invoke Adaptation

Implement the remaining compiler-function builtin family at the shared call boundary:
direct/template `offsetof`, source-context functions, overflow operations, address/string
helpers, and `__builtin_invoke` for functions, call objects, and member pointers. Under
`spec.md` §§1-3 and 6, preprocessing publishes exact builtin availability and source facts;
semantic analysis owns canonical builtin IDs, typed operands, member paths, invocation
selection, and constant evaluation. Under §§7-10, typed LowIR owns effects/results and the
native boundary selects ABI operations without spelling-based rediscovery.

Ownership/data flow is builtin probe/call syntax -> registry ID plus typed call/member facts
-> constant or first-class LowIR action -> ABI lowering. Expected work is O(arguments +
member-path length), with O(1)-average registry lookup and one overload/invocation pass; no
header-text replay or per-use linear builtin scan. Validate the direct and variable-template
`offsetof` cases, invoke reducers (including pointer-like member access), overflow families,
source-location contexts, invalid signatures, 1/8/64 call batches, then all stage gates.

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

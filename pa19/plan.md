# PA19 Full-Stage Plan

## Stage Design and Spec Alignment

PA19 extends the PA11/PA12 canonical semantic graph and PA15-PA18 typed LowIR
path with retained template patterns and demanded specializations. Patterns own
one PA10 syntax body; specializations use canonical `(pattern, TypeId...)` keys,
parent-linked argument scopes, indexed lexical/using/associated relations, and
the ordinary class/function, lifetime, and lowering owners. This follows
`spec.md` sections 2-4 and 9: identity/cache access is O(1) average, completion
is monotonic, retained syntax is shared, and lowering consumes selected bindings
and conversions without lookup replay.
Native IR and ELF remain later-stage boundaries.

## Current Failure Map

Retained-definition validation raised PA19 from 214 to 225/293. The complete
remaining 68-test set is:

| Failures | Shared behavior | Owner |
|---:|---|---|
| 15 rejected valid inputs | Function-template deduction/returns, enum or member operators, and target candidate ranking. | PA19 function-template deduction and PA12 overload selection |
| 16 rejected valid inputs | Dependent/local type and member lookup, nested completion, and retained replay. | PA12 retained scopes and lookup provenance; PA19 completion |
| 5 rejected valid inputs | Explicit instantiation demand, defaults, variable templates, and declaration forms. | PA19 template declaration and demand owners |
| 32 LowIR mismatches | Selected instantiated facts diverge in special members/lifetimes, static storage, null constants, or overload/control-flow lowering. | PA12 selected facts and PA15-PA19 typed lowering |

## Active Checkpoint

Normalize function-template deduction and selected result shapes. PA19 owns
top-level cv adjustment, reference/array matching, target/member candidates and
return substitution; PA12 overload selection ranks the canonical candidates,
and PA15 materializes the selected class result. This applies `spec.md` sections
2-4 and 9 in O(pattern type nodes plus viable candidates), with O(1)-average
specialization probes and no pattern-set rescans. Validate by-value const,
cv/reference/array parameters, pair-vs-range ordering, target member templates,
class returns, private-base ADL, PA1-PA18, file audit, and candidate scaling.

## Performance Evidence

| Probe | Result |
|---|---|
| One demanded function-template specialization, 128/256/512 calls | Requests 128/256/512, hits 127/255/511, one demand push; semantic nodes 783/1,551/3,087 and instructions 519/1,031/2,055. |
| One class-template specialization, 128/256/512 object uses | Requests 128/256/512, hits 127/255/511, exactly one class layout/member visit; semantic nodes 1,544/3,080/6,152, typed bytes 132,519/263,463/525,351, semantic time 1.82/3.62/7.16 ms. |
| One renamed out-of-class member specialization, 128/256/512 calls | Requests 128/256/512, hits 127/255/511, one class layout and one demanded body emission; semantic nodes 2,064/4,112/8,208, typed bytes 144,482/285,794/568,418, semantic time 2.41/4.78/9.45 ms. |
| Indexed class-argument ADL of one function template, 128/256/512 calls | Scope visits 128/256/512 and declaration visits 256/512/1,024; one demanded body, semantic nodes 797/1,565/3,101, typed bytes 131,240/258,344/512,552, semantic time 1.22/2.08/3.95 ms. |
| Target-deduced function-template address, 128/256/512 uses | Overload visits 128/256/512, requests 256/512/1,024 with 255/511/1,023 cache hits, one demanded body; semantic nodes 922/1,818/3,610, typed bytes 145,770/285,546/565,098, semantic time 1.41/2.67/5.08 ms. |
| Namespace-qualified class template with use-site argument, 128/256/512 uses | Scope visits 139/267/523, zero using-edge visits, requests 128/256/512 with 127/255/511 cache hits; semantic nodes 911/1,807/3,599, typed bytes 47,487/92,415/182,271, semantic time 1.19/2.01/3.79 ms. |
| One function template through 128/256/512 nested using-directives | Scope visits 781/1,549/3,085 and relation visits 512/1,024/2,048; exactly one specialization request, semantic nodes 141/269/525, constant 3,284 typed bytes, semantic time 1.68/3.07/3.74 ms. |
| 128/256/512 retained direct initializers with one qualified template call each | Semantic nodes 795/1,563/3,099, lookup scope visits 690/1,330/2,610, requests 515/1,027/2,051 with 513/1,025/2,049 cache hits, typed bytes 160,143/314,127/622,095, semantic time 6.51/12.41/26.73 ms. |
| 128/256/512 qualified `decltype` class-template arguments | Semantic nodes 914/1,810/3,602, lookup scope visits 278/534/1,046, requests 128/256/512 with 127/255/511 cache hits and one specialization; typed bytes 46,518/91,446/181,302. |
| 128/256/512 incomplete shells followed by object-definition demand | Requests 256/512/1,024 with 128/256/512 cache hits, layout member visits 128/256/512, semantic nodes 1,285/2,565/5,125, typed bytes 153,074/304,626/607,730, semantic time 19.5/40.6/74.9 ms. |
| One retained function body with 128/256/512 local declarations | Constant six lookup queries and no specialization requests; semantic peak bytes 126,695/236,775/458,983 and semantic time 1.64/3.15/5.79 ms. |

## Completed Checkpoints

| Checkpoint | Final result | Principal evidence |
|---|---|---|
| PA18 handoff | Pass | PA1-PA18 through report clean |
| Demanded function-template definitions | Pass | Canonical merge/deduction/cache; PA19 14 to 32, prior 1713/1713 |
| Canonical class-template identity/layout | Pass | Retained patterns, defaults, forward upgrade, canonical specialization shells and one-time layout; PA19 32 to 129, 13/13 focused, prior 1713/1713, audit pass |
| Current-instantiation and retained member owners | Pass | Canonical injected-name bridge, structured nested owner paths, renamed definition scopes, deferred functions/special members, static/nested definitions; PA19 129 to 166, prior 1713/1713, audit pass |
| Indexed dependent ADL and using replay | Pass | Canonical class arguments, structural class-template deduction, direct associated template candidates and using aliases; PA19 166 to 177, linear candidate probe, prior 1713/1713, audit pass |
| Explicit and target-context specialization | Pass | Partial explicit completion, deferred overload-set arguments, target-shape deduction, canonical template ABI/symbol identity, parameter-scoped unevaluated trailing returns; PA19 177 to 183, linear target probe, prior 1713/1713, audit pass |
| Structured qualified specialization replay | Pass | Indexed using/member lookup, separate template-owner and use-site argument scopes, canonical replay through dependent bases and namespace owners; PA19 183 to 190, linear qualified probe, prior 1713/1713, audit pass |
| Namespace-visible function-template replay | Pass | Template-owner provenance in ordinary lookup, name-specific using/inline traversal and hiding, owner-local deduction aliases, parenthesized call reclassification, scalar reference-return slot; PA19 190 to 195, linear relation probe, prior 1713/1713, audit pass |
| Indexed retained-syntax reclassification | Pass | Lexical type/callable hiding, retained direct calls and constructor initialization, parenthesized value operators and parameter names; PA19 195 to 202, six focused cases, linear shape probe, prior 1713/1713, audit pass |
| Structural dependent-type replay | Pass | Qualified `decltype`, parameter-visible array references, deferred template shape returns, member trailing-return object context, elaborated template-ids and keyword boundaries; PA19 202 to 208, linear type/path probe, prior 1713/1713, audit pass |
| Indexed specialization completion demand | Pass | Canonical incomplete shells, O(1) entity-to-pattern/argument recovery, layout/member demand, incomplete-cycle nested retention, and rebound out-of-class nested definitions; PA19 208 to 214, six focused cases, linear demand probe, prior 1713/1713, audit pass |
| One-pass retained-definition validation | Pass | Indexed lexical scopes, nondependent value/type classification, parameter and member redeclaration checks, dependent-name deferral, and retained special-member exception matching; PA19 214 to 225, all 11 accepted-invalid cases, linear body probe, prior 1713/1713, audit pass |

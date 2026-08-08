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

Indexed namespace-visible function templates raised PA19 from 190 to 195/293.
The complete remaining 98-test set is:

| Failures | Shared behavior | Owner |
|---:|---|---|
| 23 rejected valid inputs | Remaining declaration-shaped function template-ids, complex parameter/result deduction, and operator candidate ranking. | PA19 function-template deduction and PA12 overload selection |
| 31 rejected valid inputs | Remaining dependent type/member, qualified/local replay, and syntactic ambiguity reclassification. | PA12 retained scopes, lookup provenance, and completion |
| 4 rejected valid inputs | Explicit demand, defaults, variable templates, and declaration forms. | PA19 template declaration and demand owners |
| 11 accepted invalid inputs | Definition-time template parameter/name/redeclaration and exception-spec diagnostics. | PA12 retained-definition validation |
| 29 LowIR mismatches | Selected instantiated facts diverge in special members/lifetimes, static storage, null constants, or overload/control-flow lowering. | PA12 selected facts and PA15-PA19 typed lowering |

## Active Checkpoint

Reclassify retained declaration/cast-shaped ambiguities from indexed semantic
categories. PA10 continues to retain one syntax shape; PA12 owns the decision
between a type declaration, initializer, qualified value, and callable after
substitution, then passes only selected bindings and conversions to lowering.
Expected work is O(ambiguous syntax size plus qualified path/relation visits),
with O(1)-average owner/name and specialization probes and no reparsing or
declaration scan. Validate declaration-vs-initializer forms, parenthesized
qualified values/calls, local shadowing and scope isolation, PA1-PA18, file
audit, and shape/path scaling.

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

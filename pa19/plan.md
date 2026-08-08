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

Indexed ADL/using replay raised PA19 from 166 to 177/293. The complete remaining
116-test set is:

| Failures | Shared behavior | Owner |
|---:|---|---|
| 31 rejected valid inputs | Explicit/partial template-ids, target/function-address context, complex parameter/result deduction, and operator candidate ranking. | PA19 function-template deduction and PA12 overload selection |
| 40 rejected valid inputs | Remaining dependent base/type/member and qualified/local/inline-namespace replay. | PA12 retained scopes, lookup provenance, and completion |
| 4 rejected valid inputs | Explicit demand, defaults, variable templates, and declaration forms. | PA19 template declaration and demand owners |
| 11 accepted invalid inputs | Definition-time template parameter/name/redeclaration and exception-spec diagnostics. | PA12 retained-definition validation |
| 30 LowIR mismatches | Selected instantiated facts diverge in special members/lifetimes, static storage, null constants, or overload/control-flow lowering. | PA12 selected facts and PA15-PA19 typed lowering |

## Active Checkpoint

Complete explicit and target-context function-template selection. The PA19
deduction owner must accept partial explicit arguments and function/array/member
shapes, combine them with argument or target types, and publish one canonical
specialization binding to PA12 overload selection; retained syntax flows only
through specialization demand and selected facts flow lookup-free to lowering.
Expected work is O(pattern shape plus viable candidate/conversion visits), with
O(1)-average specialization lookup and no overload-set scan outside the indexed
name set. Validate explicit/partial ids, function addresses, return/member
shapes, cv/reference forms, ordinary-vs-template ranking, PA1-PA18, audit, and
candidate scaling.

## Performance Evidence

| Probe | Result |
|---|---|
| One demanded function-template specialization, 128/256/512 calls | Requests 128/256/512, hits 127/255/511, one demand push; semantic nodes 783/1,551/3,087 and instructions 519/1,031/2,055. |
| One class-template specialization, 128/256/512 object uses | Requests 128/256/512, hits 127/255/511, exactly one class layout/member visit; semantic nodes 1,544/3,080/6,152, typed bytes 132,519/263,463/525,351, semantic time 1.82/3.62/7.16 ms. |
| One renamed out-of-class member specialization, 128/256/512 calls | Requests 128/256/512, hits 127/255/511, one class layout and one demanded body emission; semantic nodes 2,064/4,112/8,208, typed bytes 144,482/285,794/568,418, semantic time 2.41/4.78/9.45 ms. |
| Indexed class-argument ADL of one function template, 128/256/512 calls | Scope visits 128/256/512 and declaration visits 256/512/1,024; one demanded body, semantic nodes 797/1,565/3,101, typed bytes 131,240/258,344/512,552, semantic time 1.22/2.08/3.95 ms. |

## Completed Checkpoints

| Checkpoint | Final result | Principal evidence |
|---|---|---|
| PA18 handoff | Pass | PA1-PA18 through report clean |
| Demanded function-template definitions | Pass | Canonical merge/deduction/cache; PA19 14 to 32, prior 1713/1713 |
| Canonical class-template identity/layout | Pass | Retained patterns, defaults, forward upgrade, canonical specialization shells and one-time layout; PA19 32 to 129, 13/13 focused, prior 1713/1713, audit pass |
| Current-instantiation and retained member owners | Pass | Canonical injected-name bridge, structured nested owner paths, renamed definition scopes, deferred functions/special members, static/nested definitions; PA19 129 to 166, prior 1713/1713, audit pass |
| Indexed dependent ADL and using replay | Pass | Canonical class arguments, structural class-template deduction, direct associated template candidates and using aliases; PA19 166 to 177, linear candidate probe, prior 1713/1713, audit pass |

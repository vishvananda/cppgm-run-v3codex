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

Explicit/target-context selection raised PA19 from 177 to 183/293. The complete
remaining 110-test set is:

| Failures | Shared behavior | Owner |
|---:|---|---|
| 26 rejected valid inputs | Remaining qualified template-ids, complex parameter/result deduction, and operator candidate ranking. | PA19 function-template deduction and PA12 overload selection |
| 40 rejected valid inputs | Remaining dependent base/type/member and qualified/local/inline-namespace replay. | PA12 retained scopes, lookup provenance, and completion |
| 4 rejected valid inputs | Explicit demand, defaults, variable templates, and declaration forms. | PA19 template declaration and demand owners |
| 11 accepted invalid inputs | Definition-time template parameter/name/redeclaration and exception-spec diagnostics. | PA12 retained-definition validation |
| 29 LowIR mismatches | Selected instantiated facts diverge in special members/lifetimes, static storage, null constants, or overload/control-flow lowering. | PA12 selected facts and PA15-PA19 typed lowering |

## Active Checkpoint

Replay dependent qualified type/member lookup across retained definition and
specialization scopes. The PA12 lookup owner must preserve each structured name
path and definition owner, substitute its canonical type/entity through the
PA19 argument scope, then traverse only indexed member/base/using relations;
the selected binding/type flows into existing demand and lookup-free lowering.
Expected work is O(path plus required base/using/member visits), with
O(1)-average canonical/cache probes and no global declaration scan or retry.
Validate dependent bases and `typename`, aliases/enumerators/static values,
nested and out-of-class forms, local/inline namespaces, class-scope template
arguments, PA1-PA18, file audit, and relation-visit scaling.

## Performance Evidence

| Probe | Result |
|---|---|
| One demanded function-template specialization, 128/256/512 calls | Requests 128/256/512, hits 127/255/511, one demand push; semantic nodes 783/1,551/3,087 and instructions 519/1,031/2,055. |
| One class-template specialization, 128/256/512 object uses | Requests 128/256/512, hits 127/255/511, exactly one class layout/member visit; semantic nodes 1,544/3,080/6,152, typed bytes 132,519/263,463/525,351, semantic time 1.82/3.62/7.16 ms. |
| One renamed out-of-class member specialization, 128/256/512 calls | Requests 128/256/512, hits 127/255/511, one class layout and one demanded body emission; semantic nodes 2,064/4,112/8,208, typed bytes 144,482/285,794/568,418, semantic time 2.41/4.78/9.45 ms. |
| Indexed class-argument ADL of one function template, 128/256/512 calls | Scope visits 128/256/512 and declaration visits 256/512/1,024; one demanded body, semantic nodes 797/1,565/3,101, typed bytes 131,240/258,344/512,552, semantic time 1.22/2.08/3.95 ms. |
| Target-deduced function-template address, 128/256/512 uses | Overload visits 128/256/512, requests 256/512/1,024 with 255/511/1,023 cache hits, one demanded body; semantic nodes 922/1,818/3,610, typed bytes 145,770/285,546/565,098, semantic time 1.41/2.67/5.08 ms. |

## Completed Checkpoints

| Checkpoint | Final result | Principal evidence |
|---|---|---|
| PA18 handoff | Pass | PA1-PA18 through report clean |
| Demanded function-template definitions | Pass | Canonical merge/deduction/cache; PA19 14 to 32, prior 1713/1713 |
| Canonical class-template identity/layout | Pass | Retained patterns, defaults, forward upgrade, canonical specialization shells and one-time layout; PA19 32 to 129, 13/13 focused, prior 1713/1713, audit pass |
| Current-instantiation and retained member owners | Pass | Canonical injected-name bridge, structured nested owner paths, renamed definition scopes, deferred functions/special members, static/nested definitions; PA19 129 to 166, prior 1713/1713, audit pass |
| Indexed dependent ADL and using replay | Pass | Canonical class arguments, structural class-template deduction, direct associated template candidates and using aliases; PA19 166 to 177, linear candidate probe, prior 1713/1713, audit pass |
| Explicit and target-context specialization | Pass | Partial explicit completion, deferred overload-set arguments, target-shape deduction, canonical template ABI/symbol identity, parameter-scoped unevaluated trailing returns; PA19 177 to 183, linear target probe, prior 1713/1713, audit pass |

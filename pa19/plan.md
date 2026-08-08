# PA19 Full-Stage Plan

## Stage Design and Spec Alignment

PA19 extends the PA11/PA12 canonical semantic graph and PA15-PA18 typed LowIR
path with retained template patterns and demanded specializations. Patterns own
one PA10 syntax body; specializations use canonical `(pattern, TypeId...)` keys,
parent-linked argument scopes, and the ordinary class/function, lifetime, and
lowering owners. This follows `spec.md` sections 2-4 and 9: identity and cached
fact lookup are O(1) average, completion is monotonic, retained syntax is shared,
and lowering consumes selected bindings and typed facts without lookup replay.
Native IR and ELF remain later-stage boundaries.

## Current Failure Map

The retained-member checkpoint raised PA19 from 129 to 166/293. The complete
remaining 127-test set is:

| Failures | Shared behavior | Owner |
|---:|---|---|
| 99 exit mismatches | Dependent-base/ADL/using replay, remaining complex function deduction and type forms, explicit instantiation syntax/demand, variable templates, and definition-time diagnostics are incomplete. | PA12 template lookup, overload, declaration, and demand facts |
| 28 LowIR mismatches | Selected instantiated facts still diverge for special members/lifetimes/polymorphism, constant static members, or overload/ADL/control-flow cases. | PA12 selected semantic facts and PA15-PA19 typed lowering |

## Active Checkpoint

Add dependent lookup provenance and point-of-instantiation ADL/using replay.
Each retained dependent call/name records its definition scope, required
ordinary/using edges, and substituted associated scopes; specialization demand
combines only those indexed candidates, memoizes the selected declaration and
conversions, and leaves lowering lookup-free. Lookup is O(lexical path plus
required relation/candidate visits), cache access is O(1) average, and no global
retry is allowed. Validate dependent bases, hidden friends, using declarations
and directives, enum/operator fallback, local and inline namespaces, PA1-PA18
preservation, file audit, and candidate-visit scaling.

## Performance Evidence

| Probe | Result |
|---|---|
| One demanded function-template specialization, 128/256/512 calls | Requests 128/256/512, hits 127/255/511, one demand push; semantic nodes 783/1,551/3,087 and instructions 519/1,031/2,055. |
| One class-template specialization, 128/256/512 object uses | Requests 128/256/512, hits 127/255/511, exactly one class layout/member visit; semantic nodes 1,544/3,080/6,152, typed bytes 132,519/263,463/525,351, semantic time 1.82/3.62/7.16 ms. |
| One renamed out-of-class member specialization, 128/256/512 calls | Requests 128/256/512, hits 127/255/511, one class layout and one demanded body emission; semantic nodes 2,064/4,112/8,208, typed bytes 144,482/285,794/568,418, semantic time 2.41/4.78/9.45 ms. |

## Completed Checkpoints

| Checkpoint | Final result | Principal evidence |
|---|---|---|
| PA18 handoff | Pass | PA1-PA18 through report clean |
| Demanded function-template definitions | Pass | Canonical merge/deduction/cache; PA19 14 to 32, prior 1713/1713 |
| Canonical class-template identity/layout | Pass | Retained patterns, defaults, forward upgrade, canonical specialization shells and one-time layout; PA19 32 to 129, 13/13 focused, prior 1713/1713, audit pass |
| Current-instantiation and retained member owners | Pass | Canonical injected-name bridge, structured nested owner paths, renamed definition scopes, deferred functions/special members, static/nested definitions; PA19 129 to 166, prior 1713/1713, audit pass |

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

The class-template checkpoint raised PA19 from 32 to 129/293. The complete
remaining 164-test set is:

| Failures | Shared behavior | Owner |
|---:|---|---|
| 143 exit mismatches | Dependent/current-instantiation lookup, out-of-class and nested definitions, complex function deduction/type forms, ADL/using/operator replay, variable templates, and definition-time diagnostics are incomplete. | PA12 template scope, lookup, declaration, and demand facts |
| 21 LowIR mismatches | Selected instantiated facts still diverge for special members/lifetimes/polymorphism or overload/ADL/control-flow cases. | PA12 selected semantic facts and PA15-PA19 typed lowering |

## Active Checkpoint

Add current-instantiation/dependent owner resolution and out-of-class class
template member definitions. A retained member definition resolves its
structured template-id owner through the canonical class-specialization cache,
rebinds declaration parameter names in a parent-linked scope, and attaches to
the existing member binding before body demand. Owner lookup is O(path length),
specialization lookup is O(1) average, and each member definition/body remains
single-owner and monotonic. Validate renamed/swapped parameters, nested owners,
constructors/destructors, static data/function members, inherited/member aliases,
PA1-PA18 preservation, file audit, and specialization reuse scaling.

## Performance Evidence

| Probe | Result |
|---|---|
| One demanded function-template specialization, 128/256/512 calls | Requests 128/256/512, hits 127/255/511, one demand push; semantic nodes 783/1,551/3,087 and instructions 519/1,031/2,055. |
| One class-template specialization, 128/256/512 object uses | Requests 128/256/512, hits 127/255/511, exactly one class layout/member visit; semantic nodes 1,544/3,080/6,152, typed bytes 132,519/263,463/525,351, semantic time 1.82/3.62/7.16 ms. |

## Completed Checkpoints

| Checkpoint | Final result | Principal evidence |
|---|---|---|
| PA18 handoff | Pass | PA1-PA18 through report clean |
| Demanded function-template definitions | Pass | Canonical merge/deduction/cache; PA19 14 to 32, prior 1713/1713 |
| Canonical class-template identity/layout | Pass | Retained patterns, defaults, forward upgrade, canonical specialization shells and one-time layout; PA19 32 to 129, 13/13 focused, prior 1713/1713, audit pass |

# PA17 Implementation Plan

## Stage Design and Spec Alignment

PA17 extends the PA16 typed semantic graph and LowIR lowering path; it does not
add a parallel value-semantics frontend. `spec.md` sections 2, 3, 4, 6, and 9
require canonical O(1)-average identity/lookup, retained overload/conversion
facts, demand-only emission, typed lowering without semantic reconstruction,
and work proportional to candidates plus emitted IR. PA17 therefore records
special-member/ref-qualifier/value-category facts in canonical source types and
bindings, selects calls once, and passes selected identities and lifetime
actions into the existing lowering graph.

## Current Failure Map

Current result: **54/228**, up from the 42/228 turn-start baseline. The
complete 174-test failure set is grouped once by primary owner (ordered keyword
classification, so groups do not overlap):

| Failures | Shared behavior | Primary owner |
| ---: | --- | --- |
| 63 | class-value ABI, copy/move construction and assignment, pass/return | PA12 initialization + PA15/PA16 lowering |
| 47 | lookup, ADL, overloaded operators, conversion operators | PA12 calls/operator resolution |
| 34 | scalar/array allocation, deletion, arrays, unions | PA12 object actions + PA16 lifetime lowering |
| 22 | temporary lifetime and cleanup across control flow | PA12 lifetime facts + PA16 lowering |
| 2 | ref-qualified cross-cases blocked after selection by class-value lowering | PA12 initialization + PA15/PA16 lowering |
| 6 | residual initialization/lowering interactions | mixed owners after the above boundaries |

The completed ref checkpoint accounts for 11 mapped tests; generalized xvalue
member-call support also passes `200-prvalue-method-call-temporary`.

## Active Checkpoint

**Next: synthesized class-value transfer boundary.** Add canonical copy/move
special-member facts and demand states, select those declarations during
initialization/assignment, and lower trivial complete-object transfers and
indirect class parameters/returns without changing source-level types. Owner
and flow: completed class layout + special-member state -> PA12 initialization
action/selected binding -> PA15 boundary slots and `copyobj`/helper demand.
Expected complexity is O(base/member actions) per demanded helper, O(1)-average
special-member identity lookup, and O(IR) lowering. Validate the 100/200-level
copy/move/pass/return cluster first, then the two ref-qualified cross-cases,
the full PA17 report, prior-through report, audit, and doubled member-count
probes.

## Performance Evidence

Five-run release medians with `CPPGM_FRONTEND_STATS=1` used one class containing
64/128/256 same-name parameter shapes, each declared in `&` and `&&` forms.
Signature lookups were 646/1,286/2,566; lookup queries 452/900/1,796; dependency
edges 128/256/512; peak semantic bytes 372,305/742,629/1,483,493; semantic time
1.241/2.375/5.381 ms. The flat signature-without-ref index avoids a same-name
overload rescan; all work counters and storage scale linearly. A separate
64/128/256 selected-call probe produced 384/768/1,536 candidates and
2.866/5.727/11.765 ms semantic medians.

## Completed Checkpoints

| Checkpoint | Result | Validation |
| --- | --- | --- |
| Ref-qualified member identity and selection | 42 -> 54 passing | 12 new passes; through PA16 1,436/1,436; linear probes; file audit pass |

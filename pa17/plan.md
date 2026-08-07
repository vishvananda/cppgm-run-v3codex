# PA17 Implementation Plan

## Stage Design and Spec Alignment

PA17 extends the PA16 typed semantic graph and LowIR. Canonical `TypeId` and
selected semantic actions remain the only phase boundary: lowering performs no
lookup or text round trip. Direct complete-object transfers now flow as PA12
`class-value-transfer` actions into PA15/PA16 destination-owned `copyobj`
instructions. Function boundaries keep source types canonical and choose a
typed object value or hidden indirect-result slot only during ABI lowering, in
line with `spec.md` sections 2, 6, 8, and 9.

## Current Failure Map

Current result: **68/231**, up from **57/231** at checkpoint start. The
non-overlapping remaining-failure map is:

| Failures | Shared behavior | Primary owner |
| ---: | --- | --- |
| 52 | nontrivial class-value ABI, copy/move construction and assignment, pass/return | PA12 initialization + PA15/PA16 lowering |
| 47 | lookup, ADL, overloaded operators, conversion operators | PA12 calls/operator resolution |
| 34 | scalar/array allocation, deletion, arrays, unions | PA12 object actions + PA16 lifetime lowering |
| 22 | temporary lifetime and cleanup across control flow | PA12 lifetime facts + PA16 lowering |
| 2 | ref-qualified cross-cases blocked after selection by class-value lowering | PA12 initialization + PA15/PA16 lowering |
| 6 | residual initialization/lowering interactions | mixed owners after the above boundaries |

## Active Checkpoint

**Next: selected nontrivial copy/move special members.** Compute canonical
implicit/defaulted/deleted facts once per completed class, select copy versus
move during initialization, assignment, and return, and demand helper bodies
monotonically. Ownership is class completion -> PA12 selected special-member
action -> PA16 helper call/lifetime lowering. Expected cost is O(base/member
actions) once per demanded helper and O(1)-average selection lookup; validation
starts with the 100/200 deleted/defaulted/move-only cluster, then the two
ref-qualified cross-cases, full PA17, through PA16, audit, and doubled-member
probes.

## Performance Evidence

Five-run 64/128/256 same-name declaration probes record
646/1,286/2,566 signature lookups, 452/900/1,796 lookup queries,
128/256/512 dependency edges, 372,305/742,629/1,483,493 peak semantic bytes,
and 1.269/2.401/4.830 ms semantic medians. Separate selected-call probes record
384/768/1,536 candidates, 1,155/2,307/4,611 instructions,
979,138/1,956,614/3,911,950 peak semantic bytes, and
2.920/5.944/11.815 ms semantic medians. Counters and storage double with input;
the declaration check does not rescan the same-name overload set.

Five-run direct-transfer probes for local copy, return/call transfer, and a
24-byte indirect result emit 1/4/3 `copyobj` instructions with 25/34/50
semantic nodes and 11/20/58 LowIR instructions. Median semantic/lowering times
were 0.107/0.038, 0.122/0.085, and 0.138/0.122 ms. Object width is instruction
metadata, so transfer lowering emits one instruction rather than a byte/member
loop.

## Completed Checkpoints

| Checkpoint | Result | Validation |
| --- | --- | --- |
| Ref-qualified member identity and selection | Original suite 42/228 -> 54/228; audit suite 57/231 | Three audit defects closed; focused 15/15; through PA16 1,436/1,436; proportional probes; Valgrind/process trace and file audit pass |
| Direct trivial class-value transfer and ABI | PA17 57/231 -> 68/231 | Exact-copy focus 3/3; 24-byte ABI/copy boundary matches apart from preserved PA16 conversion spelling; through PA16 1,436/1,436; proportional transfer probes; file audit pass |

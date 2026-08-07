# PA17 Implementation Plan

## Stage Design and Spec Alignment

PA17 extends the PA16 typed semantic graph and LowIR. Canonical `TypeId`,
selected function identity, and typed semantic actions remain the phase
boundary: lowering performs no lookup or text round trip. Complete-object
transfers use destination-owned `copyobj`; synthesized assignments use
class-completion facts and demanded ordered subobject actions. Function
boundaries preserve canonical source types and choose object values or hidden
result slots only during ABI lowering, per `spec.md` sections 2, 3, 4, 6, 8,
and 9.

## Current Failure Map

Current result: **79/231**, up from **68/231** at checkpoint start. The
non-overlapping remaining-failure map is:

| Failures | Shared behavior | Primary owner |
| ---: | --- | --- |
| 41 | nontrivial class-value ABI, copy/move construction and assignment, pass/return | PA12 initialization + PA15/PA16 lowering |
| 47 | lookup, ADL, overloaded operators, conversion operators | PA12 calls/operator resolution |
| 34 | scalar/array allocation, deletion, arrays, unions | PA12 object actions + PA16 lifetime lowering |
| 22 | temporary lifetime and cleanup across control flow | PA12 lifetime facts + PA16 lowering |
| 2 | ref-qualified cross-cases blocked after selection by class-value lowering | PA12 initialization + PA15/PA16 lowering |
| 6 | residual initialization/lowering interactions | mixed owners after the above boundaries |

## Active Checkpoint

**Active: synthesized copy/move construction and materialization.** Extend the
class-completion facts to canonical implicit/defaulted/deleted copy and move
constructors. PA12 initialization selects that identity and emits one demanded,
typed construction action; PA15/PA16 materialization projects its ordered base
and member actions into destination storage or calls a selected helper. This
applies `spec.md` sections 2, 3, 4, 6, 8, and 9: monotonic demand, no name-based
lowering fallback, O(1)-average fact lookup, O(base/member count) one-time
completion/action construction, and O(emitted IR) lowering. Validate the
implicit/defaulted/deleted construction cluster, direct/copy initialization,
pass/return materialization, full PA17, through PA16, file audit, and
32/64/128-member scaling probes.

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

Five-run synthesized-assignment probes with 32/64/128 class members recorded
96/192/384 special-member fact lookups and subobject visits, 81/113/177
semantic nodes, and 184/344/664 LowIR instructions. Median semantic/lowering
times were 0.243/0.203, 0.284/0.245, and 0.449/0.395 ms. Work counters are
exactly proportional to members; completion and demanded action construction
visit each subobject once per required special-member fact.

## Completed Checkpoints

| Checkpoint | Result | Validation |
| --- | --- | --- |
| Ref-qualified member identity and selection | Original suite 42/228 -> 54/228; audit suite 57/231 | Three audit defects closed; focused 15/15; through PA16 1,436/1,436; proportional probes; Valgrind/process trace and file audit pass |
| Direct trivial class-value transfer and ABI | PA17 57/231 -> 68/231 | Exact-copy focus 3/3; 24-byte ABI/copy boundary matches apart from preserved PA16 conversion spelling; through PA16 1,436/1,436; proportional transfer probes; file audit pass |
| Synthesized copy/move assignment | PA17 68/231 -> 79/231 | Canonical implicit/defaulted/deleted facts and demanded actions; assignment/qualified-base focus; through PA16 1,436/1,436; linear 32/64/128 probes; file audit pass |

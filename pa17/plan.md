# PA17 Implementation Plan

## Stage Design and Spec Alignment

PA17 extends the PA16 typed semantic graph and LowIR. Canonical `TypeId`,
completed special-member facts, selected function identity, and typed actions
remain the phase boundary; lowering performs no lookup or text round trip.
Class completion owns copy/move/assignment facts, PA12 owns selection and
demand, and PA15-PA17 materialize into destination storage or ABI result slots.
This follows `spec.md` sections 2, 3, 4, 6, 8, and 9: monotonic demand,
O(1)-average fact access, O(candidate count) selection, O(subobject count)
synthesis, and O(emitted IR) lowering.

## Current Failure Map

Current result: **97/231**, up from the **79/231** turn baseline. The
non-overlapping remaining-failure map is:

| Failures | Shared behavior | Primary owner |
| ---: | --- | --- |
| 23 | residual class-value construction/ABI/initialization shapes | PA12 initialization + PA15-PA17 lowering |
| 47 | ADL, overloaded operators, and conversion sequences | PA12 calls/operator resolution |
| 34 | scalar/class allocation, deletion, arrays, and unions | PA12 object actions + PA16 lifetime lowering |
| 22 | temporary lifetime and cleanup across control flow | PA12 lifetime facts + PA16 lowering |
| 2 | ref-qualified class-value cross-cases | PA12 selection + PA16 lowering |
| 6 | residual initialization/lowering interactions | mixed owners after the above boundaries |

## Active Checkpoint

**Next: conversion-function and overloaded-operator viability.** Record
canonical conversion candidates and standard/user-defined sequence ranks in
PA12, resolve the selected callable before the typed graph boundary, and let
existing call/lifetime lowering consume the result. Validate the focused
conversion/operator cluster, full PA17, through PA16, file audit, and
candidate-count scaling. Expected work is O(associated scopes + candidates)
per unresolved call with memoized canonical type and declaration indexes.

## Performance Evidence

- Same-name declaration and selected-call probes scale proportionally from
  64/128/256 declarations; medians were 1.269/2.401/4.830 ms and
  2.920/5.944/11.815 ms respectively, with doubling counters and storage.
- Direct-transfer probes emit constant-count `copyobj` operations; a 24-byte
  indirect result remains one width-annotated instruction rather than a loop.
- Synthesized assignment at 32/64/128 members recorded 96/192/384 fact and
  subobject visits and 184/344/664 instructions.
- Synthesized construction at 32/64/128 members recorded 160/320/640 fact
  lookups, 161/321/641 subobject visits, and 271/527/1,039 instructions.
  Five-run semantic medians were 0.216/0.281/0.405 ms, confirming linear work.

## Completed Checkpoints

| Checkpoint | Result | Validation |
| --- | --- | --- |
| Ref-qualified member identity and selection | 42/228 -> 57/231 | Focused 15/15; through PA16; audit and proportional probes pass |
| Direct trivial class-value transfer and ABI | 57/231 -> 68/231 | Exact-copy focus; through PA16; constant-count transfer probes and audit pass |
| Synthesized copy/move assignment | 68/231 -> 79/231 | Implicit/defaulted/deleted focus; through PA16; linear probes and audit pass |
| Synthesized copy/move construction and materialization | 79/231 -> 97/231 | Deleted/defaulted/move-only and call/return focus; named return-slot reuse; through PA16 1,436/1,436; linear probes and audit pass |

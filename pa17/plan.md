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

Current result: **124/231**, up from the **110/231** checkpoint baseline. The
non-overlapping remaining-failure map is:

| Failures | Shared behavior | Primary owner |
| ---: | --- | --- |
| 24 | temporary lifetime and cleanup across control flow | PA12 lifetime facts + PA16 lowering |
| 28 | allocation, deletion, arrays, and unions | PA12 object actions + PA16 lifetime lowering |
| 21 | residual operators, conversions, lookup, and ref qualification | PA12 calls/operator resolution |
| 32 | class-value construction, initialization, copy/move, and ABI shapes | PA12 initialization + PA15-PA17 lowering |
| 2 | residual namespace/control interactions | PA12 lookup and statement analysis |

## Active Checkpoint

**Next: allocation/deallocation selection and typed object actions.** Apply
`spec.md` sections 3, 6, 8, and 9 at the PA12 object-action boundary: indexed
lookup selects and retains allocation/deallocation identities, element type,
array count, initialization action, and cleanup path; PA16 lowering consumes
those facts without lookup. Expected compile-time work is O(relevant overload
candidates + emitted action kinds), and runtime array work is O(element count).
Validate focused scalar/class/array new-delete cases, full PA17, through PA16,
file audit, and overload/action-count scaling.

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
- Conversion indexes at 32/64/128 candidates recorded 42/74/138 overload
  visits and 71/135/263 conversion checks. Five-run semantic medians were
  0.999/1.625/3.165 ms, proportional to the indexed candidate set.
- Built-in comparison over inherited conversion indexes at 32/64/128 entries
  recorded 67/131/259 overload visits and 134/262/518 conversion checks;
  five-run semantic medians were 0.948/1.800/3.658 ms and semantic peak storage
  was 268,397/532,493/1,060,770 bytes, proportional to the indexed input.

## Completed Checkpoints

| Checkpoint | Result | Validation |
| --- | --- | --- |
| Ref-qualified member identity and selection | 42/228 -> 57/231 | Focused 15/15; through PA16; audit and proportional probes pass |
| Direct trivial class-value transfer and ABI | 57/231 -> 68/231 | Exact-copy focus; through PA16; constant-count transfer probes and audit pass |
| Synthesized copy/move assignment | 68/231 -> 79/231 | Implicit/defaulted/deleted focus; through PA16; linear probes and audit pass |
| Synthesized copy/move construction and materialization | 79/231 -> 97/231 | Deleted/defaulted/move-only and call/return focus; named return-slot reuse; through PA16 1,436/1,436; linear probes and audit pass |
| Indexed conversion functions and retained selection | 97/231 -> 110/231 | Implicit/explicit, inherited, ref-qualified, second-rank, alias-ID, and qualified-definition focus 10/10; through PA16 1,436/1,436; linear probes and audit pass |
| Built-in operators after class conversion | 110/231 -> 124/231 | Unary/arithmetic/pointer/comparison/logical/subscript/compound focus 14/15; rank-based overloaded-vs-built-in choice; through PA16 1,436/1,436; proportional probes and audit pass |

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

Current result: **160/231**, up from the **124/231** checkpoint baseline. The
non-overlapping remaining-failure map is:

| Failures | Shared behavior | Primary owner |
| ---: | --- | --- |
| 16 | temporary cleanup across conditional and loop control flow | PA12 lifetime facts + PA16 lowering |
| 1 | namespace class-array copy initialization | PA12 class-value initialization + static lowering |
| 21 | residual operators, conversions, lookup, and ref qualification | PA12 calls/operator resolution |
| 32 | class-value construction, initialization, copy/move, and ABI shapes | PA12 initialization + PA15-PA17 lowering |
| 1 | residual namespace/control interaction | PA12 lookup and statement analysis |

## Active Checkpoint

**Active: branch-local full-expression cleanup.** Apply `spec.md` sections 2,
6, 8, and 9 at conditional-expression and short-circuit CFG boundaries. PA12
owns construction order, branch membership, result materialization, and the
ordered destructor suffix; PA16 carries the completed-prefix fact across each
selected branch and unwind edge, preserving enclosing temporaries until the
full-expression merge without lookup. Analysis is O(expression syntax +
obligations), and lowering is O(CFG + emitted cleanup IR). Validate enclosing
temporary preservation, selected conditional prvalues/member access, return
branches, and both logical short-circuit paths; then run full PA17, through
PA16, audit, and branch-depth scaling. Loop full-expression regions remain the
following CFG checkpoint.

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
- Scalar `new` with 16/32/64 unrelated placement overloads recorded 17/33/65
  overload visits, a fixed 4 conversion checks after arity filtering, and
  19,401/36,137/69,609 typed bytes. Five-run semantic medians were
  0.504/0.938/1.723 ms, proportional to the actual candidate set.
- Nontrivial `new[]`/`delete[]` at constant bounds 16/1,024/65,536 retained
  fixed loop-shaped LowIR: 16 blocks, 65 instructions, and 16,444 typed bytes.
  Five-run semantic medians were 0.118/0.117/0.119 ms and lowering medians were
  0.113/0.111/0.115 ms; compile work is independent of runtime element count.
- Unions with 32/64/128 variants recorded 32/64/128 layout visits and
  128/256/512 special-member subobject visits. Semantic medians were
  0.165/0.229/0.374 ms, while whole-storage copy kept lowering fixed at 21
  instructions and 6,197 typed bytes rather than emitting per-variant IR.
- Full expressions with 16/32/64 materialized temporaries emitted
  97/161/289 LowIR lines (3,013/5,333/9,973 bytes). Five 500-compile batch
  medians were 1.60/1.76/2.06 s, confirming proportional analysis and output.
- Nested condition declarations at depth 16/32/64 emitted 560/1,056/2,048
  LowIR lines (12,473/23,950/46,990 bytes). Five 300-compile batch medians were
  1.10/1.28/1.65 s, proportional to reached scopes and emitted cleanup edges.

## Completed Checkpoints

| Checkpoint | Result | Validation |
| --- | --- | --- |
| Ref-qualified member identity and selection | 42/228 -> 57/231 | Focused 15/15; through PA16; audit and proportional probes pass |
| Direct trivial class-value transfer and ABI | 57/231 -> 68/231 | Exact-copy focus; through PA16; constant-count transfer probes and audit pass |
| Synthesized copy/move assignment | 68/231 -> 79/231 | Implicit/defaulted/deleted focus; through PA16; linear probes and audit pass |
| Synthesized copy/move construction and materialization | 79/231 -> 97/231 | Deleted/defaulted/move-only and call/return focus; named return-slot reuse; through PA16 1,436/1,436; linear probes and audit pass |
| Indexed conversion functions and retained selection | 97/231 -> 110/231 | Implicit/explicit, inherited, ref-qualified, second-rank, alias-ID, and qualified-definition focus 10/10; through PA16 1,436/1,436; linear probes and audit pass |
| Built-in operators after class conversion | 110/231 -> 124/231 | Unary/arithmetic/pointer/comparison/logical/subscript/compound focus 14/15; rank-based overloaded-vs-built-in choice; through PA16 1,436/1,436; proportional probes and audit pass |
| Scalar allocation/deallocation typed actions | 124/231 -> 136/231 | Ordinary/class-specific/placement/nothrow/global new and destructor/usual/global delete focus 12/12; through PA16 1,436/1,436; proportional candidate probe and audit pass |
| Dynamic array allocation/deallocation typed actions | 136/231 -> 145/231 | Scalar/class/multidimensional arrays and direct array allocation calls 9/9; scalar compatibility 15/15; through PA16 1,436/1,436; constant-size extent probe and audit pass |
| Union declaration and object actions | 145/231 -> 151/231 | Anonymous injection, active/default variant validation, constructor precedence, empty inactive lifetime, and whole-storage copy 6/6; through PA16 1,436/1,436; linear member probe and audit pass |
| Typed temporary identity and linear lifetime regions | 151/231 -> 157/231 | Discarded direct/indirect class results, noexcept argument and throwing base-init suffixes, local reference extension, and rvalue-reference materialization 6/6; proportional 16/32/64-temporary probe |
| Condition-declaration lifetime regions | 157/231 -> 160/231 | Nested false-path containment, reference-extended temporary, class-to-bool and switch-to-int conversion 4/4; through PA16 1,436/1,436; audit and proportional depth probe pass |

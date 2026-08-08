# PA17 Implementation Plan

## Stage Design and Spec Alignment

PA17 extends the PA16 typed semantic graph and LowIR. Canonical `TypeId`,
completed special-member facts, selected function identity, and typed actions
remain the phase boundary; lowering performs no lookup or text round trip.
Class completion owns copy/move/assignment facts. PA12 owns selection, demand,
stable temporary identity, conditional-construction facts, and ordered
destructor actions, including materialization of class-valued discarded calls,
conditionals, casts, and comma wrappers. PA15-PA17 materialize into destination
storage or ABI result slots, using compact action/slot IDs and function-local
flat tables so normal and unwind exits share one typed cleanup chain. Small
cleanup sequences use a bounded exact path; wider sequences intern one linked
suffix per constructed temporary, and flat maps clear only occupied slots. This
follows `spec.md` sections 2, 3, 4, 6, 8, and 9: monotonic demand, O(1)-average
fact access, O(candidate count) selection, O(subobject count) synthesis, and
analysis, lowering, storage, and output proportional to owned obligations and
emitted IR.

## Current Failure Map

Post-checkpoint result: **186/231**. The turn-start checkpoint pass set and all
earlier stages remain intact; the non-overlapping failure map contains 45 tests:

| Failures | Shared behavior | Primary owner |
| ---: | --- | --- |
| 11 | temporary cleanup across residual control flow | PA12 lifetime facts + PA17 lowering |
| 1 | namespace class-array copy initialization | PA12 class-value initialization + static lowering |
| 19 | residual operators, conversions, lookup, and ref qualification | PA12 calls/operator resolution |
| 13 | class-value construction, initialization, copy/move, and ABI shapes | PA12 initialization + PA15-PA17 lowering |
| 1 | residual namespace/control interaction | PA12 lookup and statement analysis |

## Active Checkpoint

**Active: delegating and out-of-class special-member definitions.** Apply
`spec.md` sections 2, 3, 4, 6, and 9 to retain one canonical member identity
from declaration through qualified definition, delegation selection, cycle
detection, and lowering. Parsing/PA12 own definition completion, selected
delegate identity, and ordered construction facts; PA17 lowers those facts
without lookup or retry. Work is O(delegation chain + indexed candidates +
emitted actions), with cycle checks bounded by in-progress state. Validate the
basic, overloaded, and external delegating-constructor cases; out-of-class
constructor/destructor/defaulted definitions; cycle rejection; full PA17,
through PA16, file audit, and 32/64/128 delegation/candidate scaling.

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
- Conditionally evaluated right-hand temporary chains at depth 16/32/64
  recorded 16/32/64 lifetime slots, marks, dispatch probes, and dispatch
  entries; 171/331/651 blocks; 619/1,211/2,395 instructions; and
  30,359/59,815/119,238 output bytes. Five 100-compile batch medians were
  0.43/0.50/0.65 s with 7.2-7.9 MiB peak RSS, confirming one cleanup node per
  obligation rather than repeated constructed-prefix expansion.
- Nested loop iteration cleanup at depth 16/32/64 recorded 16/32/64 indexed
  unwind-scope visits and actions, 32/64/128 dispatch probes, 437/853/1,685
  instructions, and 81,548/153,710/297,966 typed bytes. Five-run semantic
  medians were 0.629/1.056/1.884 ms and lowering medians 0.387/0.592/1.011 ms.
- Direct discarded loop-iteration temporaries at width 16/32/64 recorded
  17/33/65 cleanup probes and entries, 26/42/74 blocks, 139/251/475
  instructions, and 32,560/58,320/109,840 typed bytes. Five-run semantic
  medians were 0.330/0.389/0.577 ms and lowering medians
  0.247/0.357/0.399 ms, confirming one linked cleanup suffix per obligation.
- Direct-list construction with 32/64/128 indexed constructor candidates
  recorded 34/66/130 candidate visits, a fixed 6 conversion checks, and
  269,164/532,868/1,060,340 semantic peak bytes. Five-run semantic medians
  were 1.134/2.046/4.009 ms; lowering remained 0.144/0.142/0.173 ms with a
  fixed 9,828 typed bytes, confirming proportional selection and one retained
  destination recipe.

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
| Branch-local class values and full-expression cleanup | 160/231 -> 173/231 | Branch lifetime plus right-hand short-circuit construction-state probes pass; original pass set intact; through PA16 1,436/1,436; file audit and linear 16/32/64 cleanup probes pass |
| Loop full-expression regions | 173/231 -> 174/231 | Direct/cast/comma/conditional discarded materialization; exact normal/unwind cleanup; focused 17/17; through PA16 1,436/1,436; file audit and linear nested/width probes pass |
| Class direct-initialization recipes | 174/231 -> 186/231 | Cast/braced/default/member/return focus 11/11; converting by-value case reaches residual scalar canonicalization; through PA16 1,436/1,436; audit and proportional candidate probes pass |

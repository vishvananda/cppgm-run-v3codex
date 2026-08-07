# PA17 Implementation Plan

## Stage Design and Spec Alignment

PA17 extends the PA16 typed semantic and LowIR graph rather than adding a
parallel value frontend. In line with `spec.md` sections 2, 3, 6, 8, 9, and 10,
ref-qualifiers are packed into canonical `TypeId` identity; declaration
compatibility uses a flat complete-key index; overload resolution visits only
the direct candidate set and records the chosen binding/object conversion; and
typed lowering consumes that identity before terminal ABI rendering. No source
spelling, rendered signature, whole-program scan, external tool, or text round
trip participates in this path. The next value-semantics checkpoint must keep
the same rule: canonical special-member state and demand reasons flow into
typed initialization/lifetime actions and direct LowIR exactly once.

## Current Failure Map

Current result: **57/231**. The original checkpoint suite remains **54/228**
with the same 174 future-stage failures; three audit regressions were added to
the passing side. The non-overlapping primary-owner map is:

| Failures | Shared behavior | Primary owner |
| ---: | --- | --- |
| 63 | class-value ABI, copy/move construction and assignment, pass/return | PA12 initialization + PA15/PA16 lowering |
| 47 | lookup, ADL, overloaded operators, conversion operators | PA12 calls/operator resolution |
| 34 | scalar/array allocation, deletion, arrays, unions | PA12 object actions + PA16 lifetime lowering |
| 22 | temporary lifetime and cleanup across control flow | PA12 lifetime facts + PA16 lowering |
| 2 | ref-qualified cross-cases blocked after selection by class-value lowering | PA12 initialization + PA15/PA16 lowering |
| 6 | residual initialization/lowering interactions | mixed owners after the above boundaries |

## Active Checkpoint

**Next: synthesized class-value transfer boundary.** Establish canonical
copy/move special-member facts with monotonic demand states, select them during
initialization and assignment, and lower complete-object transfers plus
indirect class parameters/returns without changing source types. The ownership
flow is completed class layout/special-member state -> PA12 selected action ->
PA15 boundary slot and `copyobj` or demanded helper. Target O(1)-average fact
lookup, O(base/member actions) per demanded helper, and O(IR) lowering. Start
with the 100/200 copy/move/pass/return cluster, then close the two class-value
ref-qualified cross-cases before the full report and doubled-size probes.

## Performance Evidence

Five-run 64/128/256 same-name declaration probes record
646/1,286/2,566 signature lookups, 452/900/1,796 lookup queries,
128/256/512 dependency edges, 372,305/742,629/1,483,493 peak semantic bytes,
and 1.269/2.401/4.830 ms semantic medians. Separate selected-call probes record
384/768/1,536 candidates, 1,155/2,307/4,611 instructions,
979,138/1,956,614/3,911,950 peak semantic bytes, and
2.920/5.944/11.815 ms semantic medians. Counters and storage double with input;
the declaration check does not rescan the same-name overload set.

## Completed Checkpoints

| Checkpoint | Result | Validation |
| --- | --- | --- |
| Ref-qualified member identity and selection | Original suite 42/228 -> 54/228; audit suite 57/231 | Three audit defects closed; focused 15/15; through PA16 1,436/1,436; proportional probes; Valgrind/process trace and file audit pass |

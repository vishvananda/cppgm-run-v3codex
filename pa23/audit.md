# PA23 Checkpoint Audit

## Current Checkpoint Review

The direct array-extent increment is sound after two audit repairs. A dependent
array bound remains a canonical `TypeRecord` fact; deduction normalizes the
concrete extent into the candidate-local binding environment, canonical
specialization creation retains the selected declaration, and lowering sees
only that typed selection. The constructor trace for `N == 17` emits the bound
as the ordinary `i32 17` initializer operand.

The landed candidate filter was initially too late: ordinary lookup copied all
previous implicit specializations and removed them only after collection, so
the reported overload counter hid a quadratic ownership path. Declaration
publication already maintains parallel all-function and nontemplate indexes.
Call lookup now selects the nontemplate index before copying when template
patterns participate, retained lookup snapshots that same set, and completion
merges only the current call's deduced canonical specializations. The new
`function_candidate_index_visits` counter measures the actual compact-index
entries traversed.

The landed LowIR fix also inspected `BIND_PARAMETER` in lowering. Cast analysis
now records the general integral-narrowing conversion on the typed semantic
operand, and ordinary initializer conversion consumes that fact; the
template-specific lowering branch was removed. This preserves the PA23 handoff
in which instantiated declarations use the normal LowIR path.

On generated 1,024/2,048/4,096-call unique-bound probes, candidate-index visits
are 0/0/0, overload candidates are 1,024/2,048/4,096, deduction visits are
4,096/8,192/16,384, semantic peak bytes are
20,109,886/40,244,801/80,547,396, and three-run semantic medians are
85.2/177.5/369.0 ms. Before the source-index repair, the 4,096 case took
1,141.8 ms. The counters, storage, and repaired timings now track the produced
calls and specializations rather than accumulated prior specializations.

The exact PA23 pass/fail set remains 177/400, including all seven landed tests;
PA1-PA22 remain 2,639/2,639. The PA23 file audit passes with the same 13
inherited advisory header-division warnings, and `git diff --check` passes.

## Checkpoint Audit Ledger

| Checkpoint | Evidence and disposition |
| --- | --- |
| Direct array-extent NTTP deduction (`b6d38290`, this audit) | Canonical bound deduction and ordinary typed lowering pass; candidate ownership was repaired at the source index and scales linearly; baseline preserved. |

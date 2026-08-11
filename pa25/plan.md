# PA25 Plan

## Stage Design and Spec Alignment

PA25 extends the shared PA12 semantic graph and PA15 lowering path; it does not
add a source or LowIR transport. Placeholder deduction publishes canonical
`TypeId` facts on the owning variable/function binding, and lowering consumes
those facts without lookup or textual reconstruction. This follows `spec.md`
sections 2 (canonical identity), 4 (separate declaration/body demand), 6
(typed lowering), 8 (phase ownership), and 9 (work proportional to analyzed
syntax).

## Current Failure Map

Current baseline: 46/121. The remaining 75 failures group by owner: deferred
template placeholder deduction (1), list/aggregate/array initialization (12),
class conversions and value transfer (5), lambda closure synthesis/calls (41),
range-for desugaring (13), and retained-template recovery regressions (3).
Lambda/range overlap is charged to lambda where closure synthesis fails first.

## Active Checkpoint

Implement range-for at the statement semantic/lowering boundary, first
materializing bounded arrays and braced lists, then sharing the same loop fact
with member/ADL `begin`/`end` ranges. The owner is PA12 statement semantics plus
PA15 control-flow lowering; data flows through canonical range/iterator types,
selected begin/end/operator bindings, loop-variable initialization, and
lifetime actions. Construction and lowering must be O(range syntax + emitted
loop IR), with indexed lookup limited to actual member/ADL candidates. Validate
the 13 range-owned failures, array/reference loop declarations, ADL shadowing,
and prvalue-range destruction order without perturbing ordinary loops.

## Performance Evidence

For generated families of 101 and 401 ordinary auto functions, functions grew
3.97x, semantic nodes 406->1606 (3.96x), signature probes 210->810 (3.86x),
and semantic time 1.20ms->4.26ms (3.56x). This is consistent with the intended
linear body analysis and O(1) publication per deduced result.

## Completed Checkpoints

| Checkpoint | Result | Validation |
|---|---|---|
| Ordinary placeholder deduction | Condition/static-member auto and visible non-template function/member results; deferred members retain one analyzed body | PA25 40->46/121; focused 10/10; PA1-24 3471/3471; audit pass |

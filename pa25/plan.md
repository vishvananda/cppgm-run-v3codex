# PA25 Plan

## Stage Design and Spec Alignment

PA25 extends the shared PA12 semantic graph and PA15 typed lowering path; it
does not add a source or LowIR transport. Variable placeholder deduction
analyzes the initializer once, derives a canonical `TypeId`, and returns the
typed initializer directly to its declaration owner. Ordinary placeholder
functions publish one canonical result type on their `FunctionInfo` and
binding, retain one analyzed body, and attach that body by identity if emission
is demanded. Cv-qualified rvalue references collapse only for the
cv-unqualified forwarding-reference form, and local volatile placeholders use
the same runtime-initializer demand as equivalent explicit declarations.

These decisions align with `spec.md` sections 2 (canonical identity), 4
(separate deduction/body/emission demand), 6 (typed lowering), 8 (direct
ownership without hot node-based handoff maps or deep copies), and 9 (work
proportional to declarations and emitted IR). Analyzer and syntax scratch die
before the borrowed semantic graph is lowered; text remains an output view.

## Current Failure Map

Current local baseline: 50/125, comprising the unchanged 46/121 shipped-test
baseline plus four passing audit regressions. The remaining 75 shipped failures
group by first owner: deferred template placeholder deduction (1),
list/aggregate/array initialization (12), class conversions and value transfer
(5), lambda closure synthesis/calls (41), range-for desugaring (13), and
retained-template recovery regressions (3). Lambda/range overlap is charged to
lambda where closure synthesis fails first.

## Next Substantial Checkpoint

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

Seven-run medians for classes with 25, 100, and 400 ordinary placeholder-result
members show semantic nodes 144/519/2,019, signature probes 97/322/1,222,
temporary-dependency visits 29/104/404, peak stage bytes
140,200/459,189/1,531,297, and semantic time 0.551/1.441/4.830 ms. Each width
kept two demand pushes, one demanded member emission, two lowered functions,
and six instructions; lowering stayed 0.119-0.179 ms. The affected semantic
work scales with member count while emitted work stays constant.

## Completed Checkpoints

| Checkpoint | Result | Validation |
|---|---|---|
| Ordinary placeholder deduction and checkpoint audit | Canonical variable/function results; condition/static-member auto; one retained member body; cv-reference correctness; direct initializer ownership; no placeholder-path deep copies | Shipped PA25 46/121 preserved; audit regressions 4/4; local PA25 50/125; PA1-24 3,471/3,471; file audit pass |

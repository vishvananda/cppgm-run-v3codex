# PA12 Plan

## Stage Design and Spec Alignment

`--emit-semantics` now lowers each PA10 syntax arena into a translation-unit
semantic graph backed by PA11 canonical names, types, scopes, entities, and
bindings. Declaration/type/template resolution is owned by
`pa12_semantic_declarations.cpp`; expression, conversion, statement, and dump
construction by `pa12_semantic.cpp`; the shared internal contract is in
`pa12_semantic_detail.h`. This aligns the stage with `spec.md` §§1–3 and §6:
lookup is scope/name indexed, overloads retain canonical bindings and ranked
conversions, selected deferred definitions/instantiations are materialized on
demand, and rendering is a deterministic view. Expected work is O(syntax
nodes + indexed scope visits + viable candidates), with translation-unit
ownership and function/block-local bindings preserved for later lowering.

## Current Failure Map

No PA12 failures remain. The complete baseline set grouped into: driver and
canonical declaration/expression ownership (tier 100, 37); statement scopes,
target-directed calls, and indirect calls (tier 200, 42); and rejection rules,
enum/class compatibility, reference/pointer ranking, member demand, and
function-template selection (tier 300, 87). All groups now pass their checked
success/exit-status oracles without changing tests or references.

## Active Checkpoint

Checkpoint complete: canonical PA12 declarations, types, expressions,
statements, lookup, overload resolution, target conversions, class/enum facts,
and demand-driven member/template output. Data flows from syntax node + scope +
optional target type to a resolved type/value-category/binding fact and then to
the dump arena. Validation is the 166-test PA12 report, all 646 PA1–PA11 tests,
and the PA12 file audit. Final post-refactor validation is complete.

## Performance Evidence

A generated chain of one-argument function definitions/calls was measured
three times with `CPPGM_FRONTEND_STATS=1`. At 128/256 links: syntax nodes were
2,449/4,881; semantic nodes 902/1,798; lookup scope visits 1,284/2,564;
overload candidates 128/256; peak stage storage 312,366/623,278 bytes; median
analysis time 1.061/2.031 ms. Doubling input produced 1.99–2.00x structural
work/storage and 1.91x median analysis time, consistent with the expected
linear path.

## Completed Checkpoints

| Checkpoint | Result | Validation |
|---|---|---|
| Full PA12 semantic stage | Canonical model, lookup/conversions/statements, class/enum handling, overload/template demand, deterministic output | PA12 166/166; through PA11 646/646; file audit pass |

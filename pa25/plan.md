# PA25 Plan

## Stage Design and Spec Alignment

PA25 extends the shared PA10 syntax, PA12 semantic graph, and PA15 typed
lowering path without adding a transport representation. Range delimiter
lookahead is bounded and allocation-free; selected syntax regions are parsed
once. Placeholder/range initializers and conversion targets are analyzed once;
canonical `TypeId`, selected-operation, conversion, and class-boundary ABI facts
remain at their semantic owner, and PA15 consumes those identities directly.
Demanded placeholder-template bodies use a canonical monotonic state, are
analyzed only after specialization cache publication, and republish their
completed function type/ABI before lowering demand.
Full-expression and scope lifetime actions use the shared typed cleanup path;
LowIR text remains only the requested output view.

This aligns with `spec.md` sections 1 (one parse and bounded checkpoints), 2
(canonical type/declaration identity), 3 (indexed member/ADL lookup with chosen
conversions retained), 4 (dependent retained bodies instantiated on demand), 6
(direct typed lowering without lookup replay), 8 (explicit phase-local
ownership), and 9 (work proportional to syntax, actual candidates, and emitted
IR).

## Current Failure Map

PA25 is 87/130, up from the turn-start 85/130. The complete 43-failure set now
groups by first owner: lambda closure synthesis, capture binding, call/conversion,
and closure special-member facts (41); retained nested class-template member
lookup (1); and dependent-owner class result ABI publication (1). The
lambda/range case remains charged to lambda because closure synthesis fails
before its range body is analyzed.

## Active Checkpoint

The next substantial checkpoint is canonical lambda closure synthesis and call
ownership. PA12 will own one closure entity per lambda syntax/environment,
capture fields, synthesized call operator, conversion/special-member facts, and
selected call; PA19 will retain template-dependent environments and instantiate
only demanded call bodies; PA15 will consume closure layout, capture projection,
and direct call/conversion facts. This applies `spec.md` sections 2-4 (canonical
identity, indexed lookup, demand), 5 (monotonic cached facts), 6 (typed direct
lowering), 8 (explicit owner boundaries), and 9 (observable proportional work).
Expected complexity is O(lambda syntax + captures + actual candidates + demanded
body syntax + emitted IR), with O(1)-average closure/specialization lookup and no
translation-unit scan. Validate captureless calls/conversions first, then local,
member, nested, capturing, template, range-body, and trait cases; rerun PA1-24,
the full PA25 report, scaling counters, and file audit.

## Performance Evidence

For 16/64/256 demanded `constexpr auto` function-template specializations,
tokens were 135/471/1,815 and semantic nodes 179/707/2,819. Specialization
requests were 48/192/768 with 32/128/512 cache hits; demand pushes and demanded
emissions were exactly 16/64/256, functions 17/65/257, and instructions
48/192/768. Typed storage was 22,872/89,352/355,428 bytes. Five-run median
semantic time was 0.893/2.715/10.364 ms and lowering time 0.304/0.856/3.031 ms.
Demand, retained-body analysis, storage, and emitted IR scale with requested
specializations without a translation-unit scan or quadratic trend.

## Completed Checkpoints

| Checkpoint | Result | Validation |
|---|---|---|
| Ordinary placeholder deduction and checkpoint audit | Canonical variable/function results; condition/static-member auto; one retained member body; cv-reference correctness; direct initializer ownership; no placeholder-path deep copies | Shipped PA25 46/121 preserved; audit regressions 4/4; local PA25 50/125; PA1-24 3,471/3,471; file audit pass |
| Range-for statement closure and audit | Single-parse dispatch; category-correct one-time range materialization; counted array/braced paths; selected member/ADL and iterator facts; retained templates; complete condition/iteration lifetimes; direct typed CFG lowering | Range-owned 13/13 plus audit 3/3; PA25 67/128 (+17 from pre-range); PA1-24 3,471/3,471; reducer executables, file audit, and diff checks pass |
| Target-directed list, aggregate, and array initialization | Fundamental `T{...}`; adjacent strings; braced char arrays and reference viability; direct/omitted aggregate plans; canonical helper-prefix reuse; nested array members; class boundary copies; array temporary identity | Checkpoint 12/12; PA25 79/128 (+12); PA1-24 3,471/3,471; file audit and diff checks pass; 16/64/256 scaling is linear in elements and emitted actions |
| Selected class conversions and typed value boundaries plus checkpoint audit | One-class conditional construction; single-parse canonical conversion targets; modifiable lvalue-reference increment; semantic-owned direct derived parameter ABI | Checkpoint and neighbors 10/10; PA25 baseline 83/128 preserved plus audit 2/2 (85/130); PA1-24 3,471/3,471; file audit passes; 16/64/256 candidates scale linearly |
| Function-template placeholder results and deduced class-value locals | Canonical four-state retained-body demand; cache-before-analysis; completed result/ABI publication; selected same-type class transfer and source-order storage facts | Checkpoint and neighbors 5/5; PA25 87/130 (+2); PA1-24 3,471/3,471; file audit and diff checks pass; 16/64/256 specializations scale linearly |

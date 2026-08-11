# PA25 Plan

## Stage Design and Spec Alignment

PA25 extends the shared PA10 syntax, PA12 semantic graph, and PA15 typed
lowering path without adding a transport representation. Range delimiter
lookahead is bounded and allocation-free; selected syntax regions are parsed
once. Placeholder and range initializers are analyzed once, canonical `TypeId`
and selected-operation facts remain at their semantic owner, and PA15 consumes
those identities directly. Full-expression and scope lifetime actions use the
shared typed cleanup path; LowIR text remains only the requested output view.

This aligns with `spec.md` sections 1 (one parse and bounded checkpoints), 2
(canonical type/declaration identity), 3 (indexed member/ADL lookup with chosen
conversions retained), 4 (dependent retained bodies instantiated on demand), 6
(direct typed lowering without lookup replay), 8 (explicit phase-local
ownership), and 9 (work proportional to syntax, actual candidates, and emitted
IR).

## Current Failure Map

This checkpoint started at 67/128 and now passes 79/128 with every baseline
pass preserved. The 12 list/aggregate/array failures are closed. The remaining
49 failures group by first owner: deferred function-template result deduction
(1), class conversions and value transfer (4), lambda closure synthesis/calls
(41), and retained-template recovery (3). The lambda/range case remains charged
to lambda because closure synthesis fails before its range body is analyzed.

## Active Checkpoint

Close the four class-conversion/value-transfer failures at the selected
conversion and typed value-boundary interface. PA12 owns canonical constructor
and conversion-function lookup, reference result categories, alias-resolved
types, and the selected conversion sequence; PA15-PA17 consume those facts to
lower direct-object parameters and copy boundaries without replaying lookup.
Relevant `spec.md` requirements are sections 2 (canonical identities), 3
(indexed lookup and retained selected conversions), 6 (typed lowering), 8
(phase-local ownership), and 9 (work proportional to actual candidates and
emitted actions). Expected complexity is O(actual candidates + selected
conversion steps + emitted actions), with no whole-program scan. Validate the
four failures, adjacent passing constructor/conversion/value-boundary cases,
PA1-24, PA25-local progress, and file audit.

## Performance Evidence

For 16/64/256-element aggregate-array cases, tokens were 266/938/3,626,
semantic nodes 177/657/2,577, conversion checks 87/327/1,287, and emitted
instructions 186/666/2,586; function-signature lookups stayed at 22. Five-run
median semantic time was 0.424/0.984/3.192 ms and lowering time
0.151/0.286/1.212 ms. Typed storage was 39,536/143,360/558,656 bytes and peak
semantic storage 88,362/241,194/895,531 bytes. Work and storage follow expanded
elements and emitted actions without an aggregate-member rescan trend.

## Completed Checkpoints

| Checkpoint | Result | Validation |
|---|---|---|
| Ordinary placeholder deduction and checkpoint audit | Canonical variable/function results; condition/static-member auto; one retained member body; cv-reference correctness; direct initializer ownership; no placeholder-path deep copies | Shipped PA25 46/121 preserved; audit regressions 4/4; local PA25 50/125; PA1-24 3,471/3,471; file audit pass |
| Range-for statement closure and audit | Single-parse dispatch; category-correct one-time range materialization; counted array/braced paths; selected member/ADL and iterator facts; retained templates; complete condition/iteration lifetimes; direct typed CFG lowering | Range-owned 13/13 plus audit 3/3; PA25 67/128 (+17 from pre-range); PA1-24 3,471/3,471; reducer executables, file audit, and diff checks pass |
| Target-directed list, aggregate, and array initialization | Fundamental `T{...}`; adjacent strings; braced char arrays and reference viability; direct/omitted aggregate plans; canonical helper-prefix reuse; nested array members; class boundary copies; array temporary identity | Checkpoint 12/12; PA25 79/128 (+12); PA1-24 3,471/3,471; file audit and diff checks pass; 16/64/256 scaling is linear in elements and emitted actions |

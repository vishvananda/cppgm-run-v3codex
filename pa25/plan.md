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

The audit turn started at 64/125. The checkpoint now passes 67/128: every
turn-start pass remains, and all three audit regressions pass. The unchanged 61
failures group by first owner: deferred function-template result deduction (1),
list/aggregate/array initialization (12), class conversions and value transfer
(4), lambda closure synthesis/calls (41), and retained-template recovery (3).
The lambda/range case remains charged to lambda because closure synthesis fails
before its range body is analyzed.

## Next Substantial Checkpoint

Close the 12 list/aggregate/array initialization failures as one ownership
checkpoint. PA12 should canonicalize direct-braced targets, bounded array and
nested aggregate element plans, omitted-tail zero initialization, and string
literal array facts once; PA15 should consume those plans without reconstructing
shape or replaying initialization lookup. Preserve the ordinary placeholder and
range paths. Complexity must be O(initializer syntax + aggregate elements +
emitted actions), with no per-element whole-aggregate rescans. Validate the
current 12-case cluster plus nested arrays, omitted class tails, and direct
braced scalar expressions before moving to class conversions or lambdas.

## Performance Evidence

Seven-run medians for 16/64/256 repeated member ranges produced 358/982/3,478
tokens, 701/2,573/10,061 semantic nodes, 203/731/2,843 overload candidates,
450/1,554/5,970 lookup queries, and 465/1,665/6,465 instructions. Semantic time
was 1.482/4.391/16.269 ms, lowering time 0.522/1.400/5.083 ms, and peak semantic
storage 353,997/1,229,663/4,637,012 bytes. Work and storage track expanded
syntax, actual candidates, and emitted-loop growth without a superlinear trend.

## Completed Checkpoints

| Checkpoint | Result | Validation |
|---|---|---|
| Ordinary placeholder deduction and checkpoint audit | Canonical variable/function results; condition/static-member auto; one retained member body; cv-reference correctness; direct initializer ownership; no placeholder-path deep copies | Shipped PA25 46/121 preserved; audit regressions 4/4; local PA25 50/125; PA1-24 3,471/3,471; file audit pass |
| Range-for statement closure and audit | Single-parse dispatch; category-correct one-time range materialization; counted array/braced paths; selected member/ADL and iterator facts; retained templates; complete condition/iteration lifetimes; direct typed CFG lowering | Range-owned 13/13 plus audit 3/3; PA25 67/128 (+17 from pre-range); PA1-24 3,471/3,471; reducer executables, file audit, and diff checks pass |

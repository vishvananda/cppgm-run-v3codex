# PA25 Plan

## Stage Design and Spec Alignment

PA25 extends the shared PA10 syntax, PA12 semantic graph, and PA15 typed
lowering path; it adds no transport representation. Placeholder deduction and
range-for each analyze source expressions once, publish canonical `TypeId` and
selected-operation facts at their semantic owner, and lower those facts
directly. Syntax remains source-faithful, semantic identities cross the phase
boundary, and LowIR text remains only the requested output view.

This aligns with `spec.md` sections 1 (one parse and compact retained syntax),
2 (canonical type/declaration identity), 3 (indexed member/ADL lookup with
chosen conversions retained), 4 (demanded bodies only), 6 (direct typed
lowering without lookup replay), 8 (phase-local ownership), and 9 (semantic
work proportional to syntax, actual candidates, and emitted IR).

## Current Failure Map

Turn-start baseline was 50/125. The completed range checkpoint is 64/125; its
13 owned failures and the shared conversion-reference/range case now pass. The
61 remaining failures group by first owner: deferred function-template result
deduction (1), list/aggregate/array initialization (12), class conversions and
value transfer (4), lambda closure synthesis/calls (41), and retained-template
recovery (3). The lambda/range case remains charged to lambda because closure
synthesis fails before its range body is analyzed.

## Active Checkpoint

Status: complete. PA10 owns one parsed `range-for-statement`; PA12 materializes
the range once, retains canonical range/element/iterator types and selected
member-or-ADL operations, and records loop-variable and lifetime actions. PA15
consumes those facts directly as an ordinary loop CFG. Bounded arrays and
braced lists use counted ranges; class ranges retain selected `begin`, `end`,
comparison, dereference, and increment bindings. Scalar prvalues bound to a
loop reference receive one typed temporary. Complexity is O(range syntax +
actual lookup candidates + emitted loop IR), without lowering-time lookup or
syntax replay.

## Performance Evidence

Seven-run medians for 16/64/256 repeated member ranges produced 362/986/3,482
tokens, 703/2,575/10,063 semantic nodes, 203/731/2,843 overload candidates,
450/1,554/5,970 lookup queries, and 469/1,669/6,469 instructions. Semantic time
was 1.515/4.486/16.415 ms, lowering time 0.542/1.434/5.206 ms, and peak semantic
storage 354,070/1,229,768/4,637,154 bytes. Work and storage track input and
emitted-loop growth without a superlinear candidate or lookup trend. The prior
placeholder checkpoint's 25/100/400-member probe likewise remained linear.

## Completed Checkpoints

| Checkpoint | Result | Validation |
|---|---|---|
| Ordinary placeholder deduction and checkpoint audit | Canonical variable/function results; condition/static-member auto; one retained member body; cv-reference correctness; direct initializer ownership; no placeholder-path deep copies | Shipped PA25 46/121 preserved; audit regressions 4/4; local PA25 50/125; PA1-24 3,471/3,471; file audit pass |
| Range-for statement closure | One syntax node; counted array/braced paths; selected member/ADL and iterator facts; explicit/placeholder references; retained templates; prvalue and cleanup lifetimes; direct typed CFG lowering | Range-owned 13/13; PA25 64/125 (+14); PA1-24 3,471/3,471; file audit and diff checks pass |

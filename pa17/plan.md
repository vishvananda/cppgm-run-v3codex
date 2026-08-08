# PA17 Implementation Plan

## Stage Design and Spec Alignment

PA17 extends the PA16 semantic graph and typed LowIR with non-polymorphic class
value semantics. PA12 owns canonical class types, selected special members,
conversion facts, class-prvalue recipes, temporary identities, and destructor
actions. PA15-PA17 lowering consumes those facts once and writes directly into
the ABI result, parameter, local, member, or allocation destination. It does
not repeat lookup or serialize IR. Class completion owns synthesized
copy/move/assignment recipes; function-local lowering owns storage and cleanup
state. This applies `spec.md` sections 2, 3, 6, 8, and 9: stable identities,
retained overload results, typed one-way lowering, phase-local storage, and
work proportional to selected candidates, owned subobjects, lifetime actions,
and emitted IR. PA17 remains demand-driven and leaves virtual dispatch to
PA18.

## Current Failure Map

Latest audit: **230/241** PA17 tests pass, up from the turn-start **224/241**;
all earlier stages and file audit pass. The complete 11-test failure set is
grouped without overlap:

| Failures | Shared behavior | Primary owner |
| ---: | --- | --- |
| 3 | residual implicit/defaulted move and inherited value-initialization classification | PA12 class completion + PA16/PA17 special-member lowering |
| 3 | aggregate appertainment, bit-field transfer order, and global class-array copy shape | PA12 aggregate initialization + PA16/PA17 storage lowering |
| 2 | shadowed local identity and cleanup rebinding across control flow | PA12 scope facts + PA17 lifetime lowering |
| 1 | converting-constructor staging for a selected by-value parameter | PA12 call conversion + PA17 argument lowering |
| 1 | redundant integer-immediate conversion after class-pointer subtraction | PA15 scalar lowering |
| 1 | functional class cast used as the current member-call object | PA12 expression analysis |

## Active Checkpoint

**Residual synthesized construction and move classification (3 failures).**
Apply `spec.md` sections 2, 3, 6, and 8: class completion owns one canonical
implicit/defaulted special-member fact, and initialization retains the exact
selected constructor rather than retrying lookup during lowering. Data flows
`completed class -> selected copy/move identity -> subobject recipe -> typed
LowIR`. PA12 class completion and special-member synthesis own the fix;
PA16/PA17 lowering only consumes retained steps. Expected work is O(direct
bases + members), with each selected dependency demanded once.

Validate the three move/inherited-value failures, deleted/private and
copy-fallback controls, full PA17, through PA16, and file audit. Measure
16/32/64-subobject copy/move chains; visits, emitted actions, time, and retained
storage must scale linearly.

## Performance Evidence

- Existing 16/32/64 conditional-temporary probes retain 16/32/64 lifetime
  slots and marks and 619/1,211/2,395 instructions; semantic medians were
  0.43/0.50/0.65 ms per 100-compile batch unit, demonstrating linear cleanup
  ownership before this checkpoint.
- Existing nontrivial array transfer probes at extents 32/1,024/65,536 emit a
  fixed 12 blocks and 74 instructions, so compile work is independent of the
  runtime element count.
- Destination-forwarding chain at 16/32/64 functions produced semantic nodes
  251/475/923, lowered nodes 96/176/336, instructions 175/335/655, typed
  storage 53,478/101,238/197,270 bytes, and peak stage storage
  122,241/236,465/465,553 bytes. Nine-run median semantic/lowering/render
  times were 0.541/0.381/0.094, 0.924/0.520/0.156, and
  1.600/0.904/0.270 ms: all retained work and phase time remain proportional.

## Completed Checkpoints

| Checkpoint | Result | Validation |
| --- | --- | --- |
| Ref-qualified member identity and selection | 42/228 -> 57/231 | Focus 15/15; through PA16; proportional probes |
| Direct trivial class-value transfer and ABI | 57/231 -> 68/231 | Exact-copy focus; through PA16; constant transfer probes |
| Synthesized copy/move assignment | 68/231 -> 79/231 | Implicit/defaulted/deleted focus; linear probes |
| Synthesized copy/move construction and materialization | 79/231 -> 97/231 | Value call/return focus; through PA16; linear probes |
| Indexed conversion functions and retained selection | 97/231 -> 110/231 | Conversion focus 10/10; through PA16; linear probes |
| Built-in operators after class conversion | 110/231 -> 124/231 | Operator focus 14/15; through PA16; proportional probes |
| Scalar allocation/deallocation typed actions | 124/231 -> 136/231 | Focus 12/12; through PA16; proportional probes |
| Dynamic array allocation/deallocation typed actions | 136/231 -> 145/231 | Focus 9/9; fixed-size lowering probe |
| Union declaration and object actions | 145/231 -> 151/231 | Focus 6/6; linear member probe |
| Typed temporary identity and linear lifetime regions | 151/231 -> 157/231 | Focus 6/6; 16/32/64 probes |
| Condition-declaration lifetime regions | 157/231 -> 160/231 | Focus 4/4; through PA16; depth probe |
| Branch-local class values and cleanup | 160/231 -> 173/231 | Branch focus; through PA16; linear cleanup probes |
| Loop full-expression regions | 173/231 -> 174/231 | Focus 17/17; through PA16; loop probes |
| Class direct-initialization recipes | 174/231 -> 188/233 | Focus 12/12 plus audits; list/candidate probes |
| Constructor delegation and qualified defaults | 188/233 -> 199/239 | Positive/rejection/default focus; chain probes |
| Composite subobject copy/move transfer | 199/239 -> 207/239 | Focus 8/8; bounded array-loop probes |
| Value-category and reference-binding closure | 207/239 -> 216/239 | Focus plus adjacent gains; ancestry probes |
| Canonical lookup and candidate identity | 216/239 -> 224/241 | Focus plus audits; through PA16; lookup probes |
| Class-prvalue destination propagation | 224/241 -> 230/241 | Focus 6/6; no new failures; linear 16/32/64 forwarding probe |

# PA17 Implementation Plan

## Stage Design and Spec Alignment

PA17 extends the PA16 semantic graph and typed LowIR with non-polymorphic class
value semantics. PA12 owns canonical class types, selected special members,
conversion facts, class-prvalue recipes, temporary identities, and destructor
actions. `DUMP_CLASS_VALUE_TRANSFER` retains the selected constructor;
PA15-PA17 lowering sends only indirect results a destination and copies direct
typed results once. Temporary collection retains control dependency once for
O(1) cleanup decisions. Class completion owns synthesized recipes, while
function-local lowering owns storage and cleanup state. This applies `spec.md`
sections 2, 3, 6, 8, and 9: stable identities, retained overload results,
one-way typed lowering, phase-local ownership, and work proportional to
selected candidates, owned subobjects, lifetime actions, and emitted IR. PA17
remains demand-driven and leaves virtual dispatch to PA18.

## Current Failure Map

Latest audit: **235/241** PA17 tests pass, up from this turn's **230/241**
baseline; all earlier stages and file audit pass. The complete current
6-test failure set is grouped without overlap:

| Failures | Shared behavior | Primary owner |
| ---: | --- | --- |
| 1 | bit-field constructor read/modify/write presentation order | PA16 constructor storage lowering |
| 2 | shadowed local identity and cleanup rebinding across control flow | PA12 scope facts + PA17 lifetime lowering |
| 1 | converting-constructor staging for a selected by-value parameter | PA12 call conversion + PA17 argument lowering |
| 1 | redundant integer-immediate conversion after class-pointer subtraction | PA15 scalar lowering |
| 1 | functional class cast used as the current member-call object | PA12 expression analysis |

## Active Checkpoint

**Shadowed automatic-object identity and cleanup closure (2 failures).** Apply
`spec.md` sections 2, 8, and 9: PA12 scope facts assign each declaration a
stable binding; PA17 storage and lifetime lowering must index slots and cleanup
obligations by that binding across nested control flow. Data flows `lexical
declaration -> binding-owned storage/lifetime fact -> branch/return cleanup ->
LowIR`. Expected work is O(declarations + traversed cleanup edges + emitted
actions), with direct binding lookups and no name-based rescan.

Validate both shadowing failures plus nested condition, early-return,
non-shadowed class-local, and loop cleanup controls; then full PA17, through
PA16, and file audit. Measure 16/32/64 nested scopes; binding probes, cleanup
visits, retained storage, and emitted actions must scale linearly.

## Performance Evidence

- Audited destination-forwarding chains at 16/32/64 functions recorded
  158/318/638 temporary-dependency visits, 251/475/923 semantic nodes,
  96/176/336 lowered nodes, 175/335/655 instructions, and
  53,478/101,238/197,270 typed bytes. Nine-run median semantic/lowering/render
  times were 0.545/0.357/0.093, 0.922/0.536/0.156, and
  1.614/0.891/0.267 ms; all work, storage, and phase time remain proportional.
- Synthesized construction at 16/32/64 scalar members recorded 48/96/192
  special-member subobject visits, fixed 27 semantic nodes, 14 lowered nodes,
  and 17 instructions, plus 39,738/54,282/89,178 peak stage bytes. Nine-run
  semantic medians were 0.206/0.229/0.323 ms; lowering medians were
  0.122/0.125/0.124 ms. Completion work and storage are linear in subobjects,
  while retained bulk-copy lowering remains constant-size.
- Aggregate appertainment at 16/32/64 direct class-member clauses recorded
  83/163/323 conversion checks, 32/64/128 cached target misses, 145/273/529
  semantic nodes, 107/203/395 lowered nodes, 138/266/522 instructions, and
  35,113/67,609/132,601 typed bytes. Nine-run semantic/lowering/render medians
  were 0.422/0.186/0.065, 0.655/0.259/0.118, and 1.080/0.418/0.193 ms;
  conversion, graph, storage, emission, and phase time remain linear.

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
| Class-prvalue destination propagation | 224/241 -> 230/241 | Focus 6/6 plus controls; direct/indirect and cleanup audit fixes; through PA16; linear forwarding probe |
| Synthesized construction and move classification | 230/241 -> 233/241 | Focus 3/3 plus six controls; through PA16; file audit; linear subobject probe |
| Non-braced aggregate appertainment and namespace copies | 233/241 -> 235/241 | Focus 6/6 plus four PA16 controls; linear 16/32/64 probe |

# PA26 Plan

## Stage Design and Spec Alignment

PA26 remains a monotonic extension of the shared PA25 pipeline. PA12 owns
canonical types, declarations, overload decisions, template deductions,
lifetimes, storage duration, and demand; PA15 consumes that typed graph directly.
Initializer-list backing arrays are explicit owned temporaries or namespace
objects with selected element destructors. Aggregate helpers carry their member
destruction plan, and lowering maps backing identity to final stack/global
storage, uses a bounded exact cleanup frontier for small lists, and a shared
progress loop for large lists. This follows `spec.md` sections 2-6 and 8-10:
canonical identity, explicit ownership, direct typed lowering, and bounded work.

## Current Failure Map

Current result is **67/110**, up from **63/110** at this turn's start.

| Owner | Failing | Shared behavior |
|---|---:|---|
| EH and lifetime lowering | 39 | throws/handlers, unwind snapshots, branch/full-expression cleanup, construction failure |
| lambda/RTTI presentation integration | 2 | closure ABI spelling in RTTI name objects |
| template emission ordering | 1 | stable anonymous ordering across member-template specializations |
| object construction lowering | 1 | nontrivial copy in the cv/reference `typeid` fixture |

## Active Checkpoint

Build the source exception/handler core at the typed-action boundary. Semantic
analysis owns throw operand normalization, handler matching facts, and lexical
cleanup snapshots; lowering consumes those actions into EH regions and runtime
throw/catch operations without syntax recovery. Data flows from typed throw and
handler actions through scope obligations to shared unwind suffixes. Expected
work is O(active obligations + handler clauses), with cached shared cleanup
tails. Validate the grouped source throw/catch and rethrow cases, measure nested
scope/handler scaling, then run the PA26 report, through-PA25 report, and audit.

## Performance Evidence

One release run per point; exact counters are the primary complexity evidence.
A generated `initializer_list<item>` call with nontrivial class elements
exercises conversion, demand, backing construction, and shared cleanup:

| Elements | Conversions | Temp visits | Instructions | Semantic | Lowering | Typed storage | RSS |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 64 | 641 | 133 | 337 | 0.89 ms | 0.31 ms | 77,729 B | 6,604 KiB |
| 128 | 1,281 | 261 | 657 | 1.25 ms | 0.40 ms | 147,617 B | 6,860 KiB |
| 256 | 2,561 | 517 | 1,297 | 1.99 ms | 0.59 ms | 287,393 B | 7,048 KiB |
| 512 | 5,121 | 1,029 | 2,577 | 3.75 ms | 1.07 ms | 566,945 B | 7,644 KiB |
| 1,024 | 10,241 | 2,053 | 5,137 | 6.66 ms | 1.94 ms | 1,126,049 B | 8,868 KiB |

From 512 to 1,024, conversion, visit, instruction, and storage counts all grow
about 2x; semantic/lowering time grows 1.78/1.81x. The large-list cleanup path
therefore preserves O(elements) emitted work and memory.

## Completed Checkpoints

| Checkpoint | Result | Validation |
|---|---|---|
| Canonical RTTI demand and query lowering | Canonical query/cast identity and ABI RTTI | RTTI 14/17; PA26 30/110; through PA25 3,607/3,607; audit pass |
| Lambda capture ownership | Explicit/default captures, closure members, projected access, cycle-safe identity | 12 new passes; PA26 42/110; timeout removed; through PA25 3,607/3,607; audit pass |
| Scalar initializer-list interoperation | Canonical specialization, two-phase construction, scalar backing, references, `auto`, range-for | 14 new passes; PA26 56/110; through PA25 3,607/3,607; audit pass; linear to 512 |
| List overload and class-backing boundary | Separate list/element ranks, whole-list template deduction, selected source, typed class recipes and compact backing addresses | 7 new passes; PA26 63/110; focused 7/7; through PA25 3,607/3,607; audit pass; class scaling linear to 512 |
| Initializer-list and aggregate lifecycle | Namespace backing globals/finalization, exact local element frontier, aggregate parameter teardown, direct nested destination | 4 new passes; PA26 67/110; focused 4/4; through PA25 3,607/3,607; audit pass; nontrivial lists linear to 1,024 |

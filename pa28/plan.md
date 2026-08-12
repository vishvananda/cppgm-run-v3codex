# PA28 Implementation Plan

## Stage Design and Spec Alignment

`canonical classes -> complete-object views -> demanded ABI units -> typed LowIR`

The stage keeps canonical indexed identities and explicit semantic/lowering
ownership required by `spec.md`. Layout chooses a primary polymorphic base;
semantic facts own physical and logical views, slots, final overriders, and
receiver adjustments; lowering emits only demanded vtables, RTTI, and thunks.
PA27 single-view programs retain their existing path.

## Current Failure Map

Current: **27/42 PA28 tests pass** (turn baseline 24/42); PA1-PA27 and audit pass.

| Shared behavior | Owner | Remaining |
|---|---|---:|
| Virtual-base value forwarding and hidden-address boundaries | semantic actions + value lowering | 8 |
| Multi-base/diamond destructor sequencing | lifecycle facts + deleting lowering | 4 |
| Inverse-cast presentation ordering | cast lowering | 2 |
| Sibling dynamic cast | RTTI lowering | 1 |

## Active Checkpoint

**Virtual-base construction boundaries and value forwarding.** Make complete
constructors select VTT address points while base-object entries forward the
canonical virtual-base address set exactly once. Semantic lifecycle actions own
complete/base entry identity and required virtual-base unions; call lowering
maps those facts to hidden addresses, and vptr lowering consumes the selected
construction view. Construction and forwarding remain O(B+V) per boundary and
O(V) scratch for direct bases and unique virtual bases. Validate the eight
constructor/reference/pointer forwarding failures, then PA28, PA1-PA27, audit,
and increasing-base-count work counters.

## Performance Evidence

`CPPGM_FRONTEND_STATS=1` virtual-row witnesses:

| Case | Bytes | Vbase edge visits/facts | Slots | Offset rows | RTTI edges | semantic/lowering ns | Wall/RSS |
|---|---:|---:|---:|---:|---:|---:|---:|
| rich vbase slots | 405 | 1/1 | 4 | 3 | 1 | 302,446 / 214,204 | 0.00 s / 7,008 KiB |
| inherited converted receiver | 241 | 2/1 | 2 | 3 | 2 | 436,176 / 257,382 | 0.00 s / 6,700 KiB |

Rows are emitted once from indexed view facts; edge, slot, row, and RTTI work is
bounded by the represented ABI graph without hierarchy-path enumeration.

## Completed Checkpoints

| Checkpoint | Evidence |
|---|---|
| Baseline and ownership map | 1/42; all failures grouped by phase owner |
| Shared virtual layout and hidden address boundary | 13/42; PA1-PA27 + audit pass |
| Multi-view facts, adjusted dispatch, primary layout, and inverse casts | 24/42; secondary vtables/vptrs/thunks, multi-base RTTI, logical aliases; PA1-PA27 + audit pass |
| Virtual-base rows, VTT address points, and converted receivers | 27/42; three focused dispatch/table tests, 3814/3814 prior tests, audit pass |

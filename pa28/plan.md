# PA28 Implementation Plan

## Stage Design and Spec Alignment

`canonical classes -> complete-object views -> demanded ABI units -> typed LowIR`

The stage keeps canonical indexed identities and explicit semantic/lowering
ownership required by `spec.md`. Layout chooses a primary polymorphic base;
semantic facts own physical and logical views, slots, final overriders, and
receiver adjustments; lowering emits only demanded vtables, RTTI, and thunks.
PA27 single-view programs retain their existing path.

## Current Failure Map

Current: **31/42 PA28 tests pass** (turn baseline 27/42); PA1-PA27 and audit pass.

| Shared behavior | Owner | Remaining |
|---|---|---:|
| Demand-shaped virtual-base value/copy forwarding | lifecycle facts + value/call lowering | 4 |
| Multi-base/diamond destructor sequencing | lifecycle facts + deleting lowering | 4 |
| Inverse-cast presentation ordering | cast lowering | 2 |
| Sibling dynamic cast | RTTI lowering | 1 |

## Active Checkpoint

**Demand-shaped value contract and copy forwarding.** Per `spec.md` §2/§4,
derive virtual-base address operands from the demanded constructor/call boundary
instead of relying on a complete-object static offset. Lifecycle facts own the
canonical subobject actions; value/call lowering forwards the recorded address
through reference, pointer, and prvalue materialization into constructor copies.
Build each binding contract once in O(P+V), then consume it in O(V) per boundary
with O(V) scratch, preserving §6 stable identities and §9 proportional work.
Validate the four constructor/reference/prvalue/layout-union witnesses, then
PA28, PA1-PA27, audit, and boundary-fact/call counters.

## Performance Evidence

`CPPGM_FRONTEND_STATS=1` boundary/construction witnesses:

| Case | Bytes | Layout edges/facts | Boundary facts/args | Globals/rows/slots | semantic/lowering ns | Wall/RSS |
|---|---:|---:|---:|---:|---:|---:|
| member-pointer reference | 468 | 3/2 | 2/2 | 0/0/0 | 700,158 / 319,533 | 0.00 s / 6,800 KiB |
| construction VTT | 238 | 4/2 | 1/1 | 20/9/5 | 564,181 / 329,460 | 0.00 s / 6,620 KiB |

Contracts are cached once per binding and construction rows are emitted once
from indexed view facts; observed work is bounded by represented parameters,
virtual bases, and demanded ABI units without hierarchy-path enumeration.

## Completed Checkpoints

| Checkpoint | Evidence |
|---|---|
| Baseline and ownership map | 1/42; all failures grouped by phase owner |
| Shared virtual layout and hidden address boundary | 13/42; PA1-PA27 + audit pass |
| Multi-view facts, adjusted dispatch, primary layout, and inverse casts | 24/42; secondary vtables/vptrs/thunks, multi-base RTTI, logical aliases; PA1-PA27 + audit pass |
| Virtual-base rows, VTT address points, and converted receivers | 27/42; three focused dispatch/table tests, 3814/3814 prior tests, audit pass |
| Cached hidden contracts and construction views/VTT forwarding | 31/42; five focused witnesses exact, 3814/3814 prior tests, audit pass |

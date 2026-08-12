# PA28 Implementation Plan

## Stage Design and Spec Alignment

`canonical classes -> complete-object views -> demanded ABI units -> typed LowIR`

The stage keeps canonical indexed identities and explicit semantic/lowering
ownership required by `spec.md`. Layout chooses a primary polymorphic base;
semantic facts own physical and logical views, slots, final overriders, and
receiver adjustments; lowering emits only demanded vtables, RTTI, and thunks.
PA27 single-view programs retain their existing path.

## Current Failure Map

Current: **35/42 PA28 tests pass** (turn baseline 31/42); PA1-PA27 and audit pass.

| Shared behavior | Owner | Remaining |
|---|---|---:|
| Multi-base/diamond destructor sequencing | lifecycle facts + deleting lowering | 4 |
| Inverse-cast presentation ordering | cast lowering | 2 |
| Sibling dynamic cast | RTTI lowering | 1 |

## Active Checkpoint

**Multi-view destructor ABI and lifecycle forwarding.** Per `spec.md` §2/§6,
publish complete/base/deleting destructor entry identities once and make every
direct lifecycle action consume the selected entry's virtual-base/VTT contract.
Lifecycle facts own sequencing and final-overrider choice; polymorphism facts
own view slots and receiver adjustments; destructor/deleting lowering forwards
typed addresses without name lookup. Build merged slots and lifecycle edges in
O(S+E+V), then lower each action in O(V), with O(V) function-local scratch, as
required by §9. Validate the diamond, implicit/pure multi-base, and multi-level
lifecycle witnesses, then PA28, PA1-PA27, audit, and lifecycle/contract counters.

## Performance Evidence

`CPPGM_FRONTEND_STATS=1` demand-shaped boundary witnesses:

| Case | Bytes | Lowered nodes | Scan nodes | Carried facts/args | Functions/instructions | semantic/lowering ns | Wall/RSS |
|---|---:|---:|---:|---:|---:|---:|---:|
| direct-use layout union | 382 | 24 | 19 | 4/4 | 4/50 | 840,080 / 301,517 | 0.00 s / 6,840 KiB |
| forwarded prvalue | 369 | 29 | 29 | 9/9 | 5/67 | 964,650 / 307,188 | 0.00 s / 6,848 KiB |
| reference-to-pointer | 468 | 35 | 30 | 10/10 | 8/82 | 679,697 / 266,302 | 0.00 s / 6,720 KiB |

Only definitions with a virtual-base boundary are scanned once; the layout-union
callee carries one of three available ordinals while its forwarding caller
carries all three. Observed work stays bounded by relevant typed body nodes and
selected ABI facts.

## Completed Checkpoints

| Checkpoint | Evidence |
|---|---|
| Baseline and ownership map | 1/42; all failures grouped by phase owner |
| Shared virtual layout and hidden address boundary | 13/42; PA1-PA27 + audit pass |
| Multi-view facts, adjusted dispatch, primary layout, and inverse casts | 24/42; secondary vtables/vptrs/thunks, multi-base RTTI, logical aliases; PA1-PA27 + audit pass |
| Virtual-base rows, VTT address points, and converted receivers | 27/42; three focused dispatch/table tests, 3814/3814 prior tests, audit pass |
| Cached hidden contracts and construction views/VTT forwarding | 31/42; five focused witnesses exact, 3814/3814 prior tests, audit pass |
| Demand-shaped value/copy ABI and minimal address frontiers | 35/42; four focused witnesses exact, 3814/3814 prior tests, audit pass |

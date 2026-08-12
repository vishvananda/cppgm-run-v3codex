# PA28 Implementation Plan

## Stage Design and Spec Alignment

`canonical classes -> complete-object views -> demanded ABI units -> typed LowIR`

The stage keeps canonical indexed identities and explicit semantic/lowering
ownership required by `spec.md`. Layout chooses a primary polymorphic base;
semantic facts own physical and logical views, slots, final overriders, and
receiver adjustments; lowering emits only demanded vtables, RTTI, and thunks.
PA27 single-view programs retain their existing path.

## Current Failure Map

Current: **42/42 PA28 tests pass** (turn baseline 38/42); PA1-PA27 and audit pass.

| Shared behavior | Owner | Remaining |
|---|---|---:|
| None | — | 0 |

## Active Checkpoint

**Stage complete.** The complete/base lifecycle boundary now follows
`spec.md` §2/§6: semantic facts own canonical physical views, distinct
destructor identities, and ordered actions; lowering recursively emits demanded
VTT subtrees and forwards their indexed slices. The most-derived destructor
alone tears down virtual bases. Discovery remains O(V + E) per class and
lowering O(A) in emitted actions per §9. The diamond witness, PA28, PA1-PA27,
audit, and scaling counters all pass.

## Performance Evidence

`CPPGM_FRONTEND_STATS=1` demand-shaped boundary witnesses:

| Case | Bytes | Lowered nodes | Scan nodes | Carried facts/args | Functions/instructions | semantic/lowering ns | Wall/RSS |
|---|---:|---:|---:|---:|---:|---:|---:|
| direct-use layout union | 382 | 24 | 19 | 4/4 | 4/50 | 840,080 / 301,517 | 0.00 s / 6,840 KiB |
| forwarded prvalue | 369 | 29 | 29 | 9/9 | 5/67 | 964,650 / 307,188 | 0.00 s / 6,848 KiB |
| reference-to-pointer | 468 | 35 | 30 | 10/10 | 8/82 | 679,697 / 266,302 | 0.00 s / 6,720 KiB |
| pure destructor ownership | 140 | 17 | 0 | 0/0 | 4/42 | 375,140 / 255,750 | 0.00 s / 6,816 KiB |
| diamond deleting sequence | 234 | 33 | 0 | 0/0 | 18/288 | 403,623 / 457,160 | 0.00 s / 6,828 KiB |
| sibling dynamic cast | 210 | 27 | 43 | 10 misses/12 hits | 6/55 | 478,992 / 316,179 | 0.00 s / 6,732 KiB |
| inverse static cast | 323 | 41 | 66 | 4 misses/13 hits | 7/87 | 540,821 / 335,970 | 0.00 s / 6,880 KiB |
| multi-level lifecycle | 288 | 54 | 30 | 4 layout/6 boundary | 14/243 | 637,895 / 507,742 | 0.00 s / 6,816 KiB |

Only definitions with a virtual-base boundary are scanned once; the layout-union
callee carries one of three available ordinals while its forwarding caller
carries all three. Adjusted destructor thunks are interned by target/adjustment
in expected O(1); reverse-base EH suffixes scale with emitted cleanup volume.
The multi-level diamond visits 11 layout edges for 4 unique virtual-base facts
and emits 4 constructor-base plus 4 destructor-subobject actions, 24 vptr
stores, and 28 vtable offset rows.

## Completed Checkpoints

| Checkpoint | Evidence |
|---|---|
| Baseline and ownership map | 1/42; all failures grouped by phase owner |
| Shared virtual layout and hidden address boundary | 13/42; PA1-PA27 + audit pass |
| Multi-view facts, adjusted dispatch, primary layout, and inverse casts | 24/42; secondary vtables/vptrs/thunks, multi-base RTTI, logical aliases; PA1-PA27 + audit pass |
| Virtual-base rows, VTT address points, and converted receivers | 27/42; three focused dispatch/table tests, 3814/3814 prior tests, audit pass |
| Cached hidden contracts and construction views/VTT forwarding | 31/42; five focused witnesses exact, 3814/3814 prior tests, audit pass |
| Demand-shaped value/copy ABI and minimal address frontiers | 35/42; four focused witnesses exact, 3814/3814 prior tests, audit pass |
| Pure and multi-view deleting destructor ABI | 38/42; three focused witnesses exact, hidden lifecycle calls arity-correct, 3814/3814 prior tests, audit pass |
| Canonical cast control and RTTI hint | 41/42; three focused witnesses exact, 3814/3814 prior tests, audit pass |
| Complete/base lifecycle split and recursive VTTs | 42/42; diamond lifecycle exact, 3814/3814 prior tests, audit pass |

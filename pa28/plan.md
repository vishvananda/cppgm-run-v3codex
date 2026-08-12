# PA28 Implementation Plan

## Stage Design and Spec Alignment

PA28 extends the typed PA27 path in place:

`canonical classes -> complete-object/view facts -> semantic ABI facts -> typed LowIR`

`spec.md` requires canonical indexed identities, explicit phase ownership,
demanded emission, and lowering that consumes recorded layout/conversion facts.
Virtual-base closure is iterative and edge-proportional; PA28 behavior remains
source-driven so non-virtual PA27 programs retain their existing path.

## Current Failure Map

Current: **13/42 PA28 tests pass** (baseline 1/42); PA1-PA27 and file audit pass.

| Shared behavior | Owner | Remaining tests |
|---|---|---:|
| Constructor/value forwarding and richer virtual-base lifecycle | semantic actions + boundary/lifetime lowering | 6 |
| Multi-root virtual slots, secondary vptrs, adjusted calls/thunks | polymorphism facts + vtable lowering | 17 |
| Primary-base choice and inverse non-primary static casts | layout + cast lowering | 4 |
| Sibling cross-cast and dynamic-type recovery | RTTI facts + RTTI lowering | 2 |

## Active Checkpoint

**Multi-view polymorphism facts and adjusted dispatch.** Record every active
polymorphic base view, its complete-object offset, logical slot identity, final
overrider, and `this` adjustment. The semantic polymorphism model owns these
facts; demanded vtable/thunk units consume them directly, and call lowering
selects a view from the recorded conversion. Build facts once per class by an
iterative, deduplicated base-edge walk, O(V+E+S) time and O(V+S) storage for
views and slots. Validate non-primary dispatch/vptr tests first, then the full
PA28 report, PA1-PA27, file audit, and edge/slot counters.

## Performance Evidence

`CPPGM_FRONTEND_STATS=1` witnesses after the shared-layout checkpoint:

| Case | Source | Edge visits | Facts | semantic_ns | Wall/RSS |
|---|---:|---:|---:|---:|---:|
| virtual diamond | 173 B | 6 | 3 | 407,979 | 0.00 s / 6,700 KiB |
| nested virtual hierarchy | 468 B | 16 | 9 | 681,710 | 0.00 s / 6,776 KiB |

The larger witness increases traversal work with the reachable base graph; no
path enumeration or translation-unit rescan appears.

## Completed Checkpoints

| Checkpoint | Result | Evidence |
|---|---|---|
| Baseline and ownership map | complete | 1/42; all 42 tests grouped by semantic owner |
| Shared virtual layout and hidden address boundary | complete | 13/42; canonical shared offsets, complete/base-entry sequencing, null/reference forwarding; PA1-PA27 and audit pass |

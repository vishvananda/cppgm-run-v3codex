# PA28 Implementation Plan

## Stage Design and Spec Alignment

`canonical classes -> complete-object views -> demanded ABI units -> typed LowIR`

The stage keeps canonical indexed identities and explicit semantic/lowering
ownership required by `spec.md`. Layout chooses a primary polymorphic base;
semantic facts own physical and logical views, slots, final overriders, and
receiver adjustments; lowering emits only demanded vtables, RTTI, and thunks.
PA27 single-view programs retain their existing path.

## Current Failure Map

Current: **24/42 PA28 tests pass** (turn baseline 13/42); PA1-PA27 and audit pass.

| Shared behavior | Owner | Remaining |
|---|---|---:|
| Virtual-base value forwarding and hidden-address boundaries | semantic actions + value lowering | 8 |
| Multi-base/diamond destructor sequencing | lifecycle facts + deleting lowering | 4 |
| Virtual-base vtable rows and converted receivers | polymorphism facts + vtable/call lowering | 3 |
| Inverse-cast presentation ordering | cast lowering | 2 |
| Sibling dynamic cast | RTTI lowering | 1 |

## Active Checkpoint

**Virtual-base vtable rows and converted receivers.** Extend each view with
virtual-base offset rows and an address-point index, then make dispatch consume
the receiver selected by semantic conversion. Semantic polymorphism owns row
identity and offsets; demanded lowering owns vtable/VTT symbols; call lowering
only reads those facts. Construction is output-proportional, O(E+S+R) time and
O(S+R) storage for direct edges, slots, and emitted rows. Validate the three
virtual-base dispatch tests, then the complete PA28 report, PA1-PA27, audit, and
edge/slot/row scaling witnesses.

## Performance Evidence

`CPPGM_FRONTEND_STATS=1` multi-view witnesses:

| Case | Bytes | Base edges | Slots | Vptr stores | semantic/lowering ns | Wall/RSS |
|---|---:|---:|---:|---:|---:|---:|
| two roots + override | 251 | 2 | 4 | 4 | 582,552 / 319,921 | 0.00 s / 6,772 KiB |
| inherited two-view interface | 347 | 5 | 6 | 6 | 542,473 / 363,870 | 0.00 s / 6,772 KiB |

The larger view graph grows edge, slot, RTTI-dependency, and vptr work
proportionally; no hierarchy-path enumeration or translation-unit rescan occurs.

## Completed Checkpoints

| Checkpoint | Evidence |
|---|---|
| Baseline and ownership map | 1/42; all failures grouped by phase owner |
| Shared virtual layout and hidden address boundary | 13/42; PA1-PA27 + audit pass |
| Multi-view facts, adjusted dispatch, primary layout, and inverse casts | 24/42; secondary vtables/vptrs/thunks, multi-base RTTI, logical aliases; PA1-PA27 + audit pass |

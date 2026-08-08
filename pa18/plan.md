# PA18 Implementation Plan

## Stage Design and Spec Alignment

PA18 extends the canonical PA11/PA12 class and function identities with one
per-class polymorphism fact: inherited virtual slots followed by source-ordered
new slots, with overrides replacing a slot by binding identity.  The same fact
drives class layout, constructor/destructor vpointer actions, vtable globals,
and direct-versus-virtual call lowering.  This follows `spec.md` sections 2, 3,
4, 6, 8, and 9: canonical O(1) identity, indexed base lookup, separately
demanded vtable facts/emission, direct typed LowIR, translation-unit ownership,
and work proportional to classes plus virtual declarations and slots.

## Current Failure Map

Closed. Semantic virtual identity/validation, polymorphic layout, LowIR
dispatch/global emission, stable local-class identity, and complete/base/deleting
lifetime ABI behavior are all covered. Turn-start baseline was 2/29 PA18 tests.

## Active Checkpoint

Complete. The stable boundary is syntax declarator -> canonical binding ->
per-entity slot vector -> layout and borrowed `SemanticGraphView` -> dedicated
typed polymorphism lowering. Validation is 29/29 PA18, 1677/1677 through PA17,
and a passing PA18 file audit.

## Performance Evidence

Generated single-inheritance chains with one overridden slot per class compiled
at 800 classes in 0.08 s / 18,160 KiB RSS and 1,600 classes in 0.17 s / 30,512
KiB RSS. Doubling the chain approximately doubled time and added 68% memory;
lookup remains indexed and no call-site source scan or retry loop was added.

## Completed Checkpoints

| Checkpoint | Result | Validation |
| --- | --- | --- |
| PA17 handoff baseline | 2/29 PA18; PA1-PA17 clean | Ralph baseline and prior-through check |
| Canonical PA18 polymorphism pipeline | Full semantic, layout, dispatch, RTTI/vtable, and lifetime ABI support | PA18 29/29; through PA17 1677/1677; file audit pass |

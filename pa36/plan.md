# PA36 Implementation Plan

## Stage Design and Spec Alignment

PA36 retains the `spec.md` semantic graph -> typed LowIR -> per-function MIR ->
direct ELF pipeline. Canonical entities own specialization, layout, constexpr,
selected-callable, and ABI-entry facts; demand completes those facts once, and
lowering consumes recorded `TypeId`/`BindingId` facts without semantic lookup.

Hosted ABI entries now preserve canonical owner types for nested members,
complete/base/deleting lifecycle relationships, vtable-derived weak ownership,
and explicit native object-publication policy. Distinct virtual-base lifecycle
entries publish one object symbol, while weak shared-entry families retain the
local aliases needed for coherent per-TU selection.

## Current Failure Map

PA36 is 77/80. Recursive `std::function` violates enclosing-lifetime
monotonicity; contained-virtual-base tuple `get` is ambiguous; hosted floating
`signbit` classification returns the wrong runtime result. These have separate
semantic lifetime, lookup, and native builtin owners.

## Active Checkpoint

Next: recursive callable enclosing-lifetime closure. `spec.md` §§2-4 require
canonical specialization state, monotonic demand, and no semantic lookup after
lowering begins. The owner is function-template/lambda instantiation: recursive
call selection must reuse the in-progress specialization while its closure and
captured callable type flow through demand into typed LowIR. Expected work is
O(instantiated bindings + demand edges), with O(1) in-progress identity lookup.
Validate the recursive `std::function` fixture, neighboring function/lambda
tests, PA19/25 template-capture coverage, then the stage, prior, and audit gates.

## Performance Evidence

The new owner predicate and publication index are O(1) per mangled owner and
O(exported symbols). One-shot evidence: locale 1.55 s/73,248 KiB RSS and
iostream 1.35 s/65,472 KiB RSS; the iostream object has exactly one published
complete constructor. The broader prior profiles remain stable.

## Completed Checkpoints

| Checkpoint | Result | Validation |
| --- | --- | --- |
| Builtin ownership before hosted alias fallback | Removed observed `alloca` relocations; PA36 26 -> 43/80 | Collision and PA33/34 focus; through PA35; audit |
| Canonical structured `std` owner identity | Removed substitution pollution; PA36 43 -> 56/80 | Standard-owner focus 16/16; stable profile; through PA35; audit |
| Canonical trivial explicit-destructor calls | Preserved evaluation while pruning trivial host calls; PA36 56 -> 65/80 | Destructor focus 9/9; PA17/32 focus; through PA35; audit |
| Hosted completion, callable, and virtual-base boundaries | Normalized type/call identities and followed virtual anchors; PA36 65 -> 70/80 | Checkpoint 5/5; PA28 focus 3/3; through PA35; audit |
| Hosted container specialization and preserved native forwarding | Completed conversion/layout/constexpr/inherited-constructor facts and preserved call-live values; PA36 70 -> 75/80 | Container focus 5/5; PA16/17/29 focus; through PA35 4,907/4,907; audit |
| Canonical hosted special-member entry ownership | Unified nested-owner substitutions, D0 weak ownership, and complete-entry publication; PA36 75 -> 77/80 | ABI/TLS focus 3/3; PA36 77/80; through PA35 4,907/4,907; audit |

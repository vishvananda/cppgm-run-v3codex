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
local aliases needed for coherent per-TU selection. Hosted floating predicates
lower from their retained operand `LowType`; sign classification stages the
value once and extracts the f32/f64 IEEE or f80 x87 sign field directly.

## Current Failure Map

PA36 is 80/80. No current-stage failures remain.

## Active Checkpoint

Complete: typed floating sign classification. In alignment with `spec.md`
§§2, 6, 7, and 9, the retained builtin operation and operand `LowType` flow
directly into typed LowIR and bounded native lowering. Numeric builtin lowering
stages the value once, selects its format-owned sign word, and returns the
integer predicate consumed by the call expression in O(1) work and storage.
Validation covers positive/negative zero and negative NaN for f32/f64/f80,
neighboring PA34 predicates, PA36, all prior stages, and file audit.

## Performance Evidence

The new owner predicate and publication index are O(1) per mangled owner and
O(exported symbols). One-shot evidence: locale 1.55 s/73,248 KiB RSS and
iostream 1.35 s/65,472 KiB RSS; the iostream object has exactly one published
complete constructor. Recursive `std::function` changed from a 0.40 s/
23,788 KiB semantic failure to a passing 0.42 s/23,812 KiB compile. A reduced
two-word union swap now passes and emits direct reference calls with no argument
materialization slots. The tuple fixture passes in 1.13 s/59,224 KiB; normalized
reference comparison adds constant work per candidate pair, and a complete
virtual-base temporary now emits one static projection instead of a vptr load,
row lookup, and dynamic projection. The nine-call signbit fixture compiles in
0.00 s/8,472 KiB RSS and emits nine fixed shifts, one per call; only f80 adds a
constant byte-index step. The broader prior profiles remain stable.

## Completed Checkpoints

| Checkpoint | Result | Validation |
| --- | --- | --- |
| Builtin ownership before hosted alias fallback | Removed observed `alloca` relocations; PA36 26 -> 43/80 | Collision and PA33/34 focus; through PA35; audit |
| Canonical structured `std` owner identity | Removed substitution pollution; PA36 43 -> 56/80 | Standard-owner focus 16/16; stable profile; through PA35; audit |
| Canonical trivial explicit-destructor calls | Preserved evaluation while pruning trivial host calls; PA36 56 -> 65/80 | Destructor focus 9/9; PA17/32 focus; through PA35; audit |
| Hosted completion, callable, and virtual-base boundaries | Normalized type/call identities and followed virtual anchors; PA36 65 -> 70/80 | Checkpoint 5/5; PA28 focus 3/3; through PA35; audit |
| Hosted container specialization and preserved native forwarding | Completed conversion/layout/constexpr/inherited-constructor facts and preserved call-live values; PA36 70 -> 75/80 | Container focus 5/5; PA16/17/29 focus; through PA35 4,907/4,907; audit |
| Canonical hosted special-member entry ownership | Unified nested-owner substitutions, D0 weak ownership, and complete-entry publication; PA36 75 -> 77/80 | ABI/TLS focus 3/3; PA36 77/80; through PA35 4,907/4,907; audit |
| Callable cleanup domains and union-reference preservation | Separated lexical lookup from cleanup ownership and retained union xvalue calls as addresses; PA36 77 -> 78/80 | Recursive `std::function`; two-word union swap; PA36 78/80; through PA35 4,907/4,907; audit |
| Canonical tuple reference ordering and complete temporary vbase projection | Ranked xvalue cv bindings, normalized forwarding packs, and used static complete-object offsets; PA36 78 -> 79/80 | Tuple/reference/tie focus; PA36 79/80; through PA35 4,907/4,907; audit |
| Typed floating sign-field extraction | Replaced comparison classification with single-evaluation f32/f64/f80 sign extraction; PA36 79 -> 80/80 | Signbit 1/1; PA34 predicates 1/1; PA36 80/80; through PA35 4,907/4,907; audit |

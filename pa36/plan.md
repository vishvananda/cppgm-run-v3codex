# PA36 Implementation Plan

## Stage Design and Spec Alignment

PA36 retains the `spec.md` semantic graph -> typed LowIR -> per-function MIR ->
direct ELF pipeline. Canonical entities own specialization, layout, constexpr,
selected-callable, and ABI-entry facts; demand completes those facts once, and
lowering consumes recorded `TypeId`/`BindingId` facts without semantic lookup.

Hosted containers now cross that pipeline through monotonic class completion:
conversion demand may complete an object type once, specialization replay resets
provisional layout, constant-expression roots do not inherit suppression, and an
inherited default constructor is materialized only when the derived class's own
declarations suppress its implicit default. Native forwarding preserves values
across calls and keeps zero-offset aliases under their parameter register owner.

## Current Failure Map

PA36 is 75/80. Two semantic failures have separate owners: recursive
`std::function` violates enclosing-lifetime monotonicity, and contained-virtual-
base tuple `get` is ambiguous. Two ABI-entry failures share callable identity and
object ownership: locale `_Impl` leaves its qualified copy constructor undefined,
while stringbuf emits a deleting destructor strongly in two objects. The final
runtime failure is hosted floating `signbit` classification.

## Active Checkpoint

Next: canonical hosted special-member entry ownership for the locale and
stringbuf link failures. `spec.md` §§2-3 and §§6-8 require one canonical callable
identity, explicit complete/base/deleting entry relationships, selected bindings
carried into lowering, and at most one owning object definition per emitted
symbol. Data flows from semantic special-member selection -> canonical lifecycle
entry -> demand -> LowIR function metadata -> ELF binding/coalescing. Expected
cost is O(demanded entries + relocations), with O(1) canonical entry lookup.
Validate both failures, neighboring locale/iostream fixtures, PA17 lifecycle and
PA32 weak-ODR coverage, then the PA36, through-PA35, and audit gates.

## Performance Evidence

Current representative one-shot compiles measured recursive map/`unique_ptr` at
1.28 s and 56,356 KiB RSS, and unordered-set construction at 1.41 s and 73,120
KiB RSS. The checkpoint adds one terminal completion retry, constant-root state
save/restore, and indexed constructor fact per affected identity; native
live-across-call and alias-ownership tests are O(1) over precomputed facts. The
earlier vector/pair and iostream profiles remained stable across repeated runs
(1.39-1.41 s/30.6 MiB and 3.21-3.76 s/75.6 MiB, with identical objects).

## Completed Checkpoints

| Checkpoint | Result | Validation |
| --- | --- | --- |
| Builtin ownership before hosted alias fallback | Removed observed `alloca` relocations; PA36 26 -> 43/80 | Collision and PA33/34 focus; through PA35; audit |
| Canonical structured `std` owner identity | Removed substitution pollution; PA36 43 -> 56/80 | Standard-owner focus 16/16; stable profile; through PA35; audit |
| Canonical trivial explicit-destructor calls | Preserved evaluation while pruning trivial host calls; PA36 56 -> 65/80 | Destructor focus 9/9; PA17/32 focus; through PA35; audit |
| Hosted completion, callable, and virtual-base boundaries | Normalized type/call identities and followed virtual anchors; PA36 65 -> 70/80 | Checkpoint 5/5; PA28 focus 3/3; through PA35; audit |
| Hosted container specialization and preserved native forwarding | Completed conversion/layout/constexpr/inherited-constructor facts and preserved call-live values; PA36 70 -> 75/80 | Container focus 5/5; PA16/17/29 focus; through PA35 4,907/4,907; audit |

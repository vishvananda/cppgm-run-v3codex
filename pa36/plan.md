# PA36 Implementation Plan

## Stage Design and Spec Alignment

PA36 keeps the semantic graph -> typed LowIR -> per-function MIR -> direct ELF
pipeline. Per `spec.md` §§2-6 and §8, canonical entities own class/template and
selected-callable facts; semantic demand completes each fact once; lowering
consumes recorded `TypeId`, `BindingId`, base-path, and ABI facts without lookup.
Builtin spelling and ABI `std` identity remain semantic-ingress classifications.

Type identity does not demand class layout, so `is_same` compares canonical
types without completing its operands. Indirect calls normalize pointer/reference
callees to one function `TypeId` before hidden-argument lowering. A virtual-base
projection preserves the first carried virtual anchor; when the target is a
non-virtual suffix, runtime projection reads that anchor from the vtable and
adds the recorded direct-edge offsets. This keeps semantic ownership distinct
from lowering and preserves the PA28 boundary contract.

## Current Failure Map

PA36 is 70/80. Four hosted-container instantiation failures share template
completion/selection ownership: unordered pointer insertion and unordered range
construction report no viable overload, recursive map/unique_ptr reports no
zero-argument constructor, and recursive vector reports a nonconstant enumerator.
Two remaining semantic failures are separate: recursive `std::function` rejects
its lifetime prefix and tuple `get` is ambiguous. Two ABI/link failures are the
unowned locale `_Impl` copy constructor and duplicate strong stringbuf destructor.
Two runtime failures remain: floating `signbit` returns 1 and braced vector
temporary destruction aborts with 134.

## Active Checkpoint

Next: close the four hosted-container instantiation failures at the canonical
specialization/completion boundary. `spec.md` §§2-5 require one specialization
identity, memoized completion/demand, and candidate facts derived from substituted
types; §6 requires the selected constructor/call to flow into typed lowering.
Ownership is template request -> canonical class/function specialization ->
completed constexpr/overload facts -> selected typed expression. Expected work
is O(new specialization facts + visited candidates + constexpr steps), with no
completion retry after a terminal state. Validate all four cases, neighboring
vector/pair and PA35 container fixtures, then full gates. Profile recursive map
and vector representatives, recording template requests/cache hits, lookup
visits, constexpr steps, demand pushes, and peak memory.

## Performance Evidence

The vector/pair representative ran in 1.40/1.41/1.39 s at
30,608/30,744/30,836 KiB RSS. All runs retained 66,517 tokens, 21,141 lookup
queries, 1,088 template requests/593 hits, 11 demand pushes, 138 LowIR
instructions, and 28,567,431 semantic peak bytes; objects were SHA-256 identical
(`3d080b...aa58`). The iostream representative ran in 3.21/3.23/3.76 s at
75,584/75,700/75,588 KiB RSS with stable 147,006 tokens, 48,195 lookups, 3,317
template requests/2,413 hits, 291 pushes, 6,409 LowIR and 9,301 MIR instructions,
and 77,557,789 semantic peak bytes; objects were identical (`24ed40...ea6`).
Identity handling is O(operands) without layout demand, callable normalization is
O(1), and runtime base projection is one cached base query plus O(path length +
owner virtual bases).

## Completed Checkpoints

| Checkpoint | Result | Validation |
| --- | --- | --- |
| Closed builtin ownership before hosted alias fallback (`1f918c84` plus audit repair) | Removed observed `alloca` relocations and raised PA36 26 -> 43/80 | Collision and PA33/34 focus pass; through PA35 4,907/4,907; audit pass |
| Canonical structured `std` owner identity (`03e47d62` plus audit repair) | Emitted canonical `St`, removed substitution pollution, and raised PA36 43 -> 56/80 | Standard-owner focus 16/16; stable profile; through PA35 and audit pass |
| Canonical trivial explicit-destructor calls (`79dce1ac`) | Preserved object evaluation, pruned trivial host calls, and raised PA36 56 -> 65/80 | Destructor focus 9/9; PA17/32 focus; stable profiles; through PA35 and audit pass |
| Canonical hosted completion, callable, and virtual-base boundaries | Removed false identity completion, normalized indirect callees, followed virtual anchors through non-virtual suffixes, and raised PA36 65 -> 70/80 | Checkpoint 5/5; PA28 focus 3/3; stable profiles; PA36 70/80; through PA35 4,907/4,907; audit pass |

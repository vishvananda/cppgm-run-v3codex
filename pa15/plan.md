# PA15 Implementation Plan

## Stage Design and Spec Alignment

PA12 owns canonical names, types, bindings, constants, conversions, value categories,
and control facts. Its synchronous graph callback lends one semantic arena while PA15
builds a separately owned LowIR model; no semantic pointer escapes. PA15 lowers each
expression directly through typed value/address results, uses strong IDs for LowIR
cross-links, and keeps flat canonical indexes for cross-source identity. String and
aggregate storage, static addresses, deferred initialization actions, function
boundaries, blocks, labels, and generated slots each have one owner.

This applies `spec.md` §§2 and 6: lowering consumes recorded facts by identity,
coalesces each emission unit once, and neither parses a qualified/mangled spelling nor
serializes and reparses LowIR. PA14's typed encoder supplies object spellings without
its fact-text adapter. Small child collections stay inline, deep type modifiers are
flat and iterative, dominant indexes use geometric storage, and output streams
directly. The LowIR model is isolated from graph traversal in
`pa15_lowir_model.h`, aligning with §§8-9 and the file-size audit.

## Current Failure Map

No PA15 failures remain: the required report passes 108/108, including both
compile-fail oracles. The prior-stage report passes 1,037/1,037 and the file audit
passes. The former groups—semantic conversions/overloads/enums/labels and lowering
for storage, arrays, calls, globals, expressions, and control flow—are closed.

## Active Checkpoint

**Full-stage closure (complete).** The stable boundary is semantic expression ID ->
typed value/address -> ordered LowIR action. PA12 records promotion, conversion,
default-argument, label, extent, and constant facts; PA15 owns storage selection,
addressing, lowering, deferred global actions, block formation, and rendering. Lookup
is average `O(1)`, graph/block traversal is `O(nodes + edges)`, and array/string
work is `O(elements)`. Validation covers all PA15 clusters, PA1-PA14 preservation,
the file audit, and the representative linear probes below.

## Performance Evidence

A generated scalar function with 5,000 versus 10,000 assignments produced exactly
15,003 versus 30,003 instructions, 25,006 versus 50,006 lowering visits, and 10,002
versus 20,002 binding probes. Typed storage was 5,039,080 versus 10,076,600 bytes.
Across three runs, elapsed time was 0.07 s versus 0.13-0.14 s and median lowering time
was 12.58 ms versus 24.97 ms. Deep declarators at 8,000/16,000/32,000 pointer levels
took 0.02/0.04/0.08 s and 9,024/13,240/22,112 KiB maximum RSS. Counts, time, and
storage are representative linear evidence; no recursive modifier ownership,
qualified-name parsing, global retry, or complete output buffer remains.

## Completed Checkpoints

| Checkpoint | Result | Validation |
|---|---|---|
| Semantic handoff and scalar procedural spine | 0 -> 27/108 PA15 | scalar lowering, ABI metadata, identity/dedup, ASan+UBSan success set, 32k deep types, and linear 5k/10k counters |
| Procedural value/address, control, calls, and globals | 27 -> 108/108 PA15 | references/arrays/pointers, enums/casts/defaults, calls, loops/switch/goto, strings/aggregate globals/deferred init; through-PA14 1,037/1,037; file audit pass |

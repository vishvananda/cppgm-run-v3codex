# PA15 Implementation Plan

## Stage Design and Spec Alignment

PA12 owns canonical names/types/bindings plus the constants, common operand types,
value categories, storage, language linkage, exception state, and owner/name paths
needed by PA15. Its synchronous graph callback borrows one semantic arena only while
building a PA15-owned LowIR program; no semantic pointer escapes the callback. PA15
uses strong IDs for LowIR cross-links, typed operations, and flat canonical tables for
cross-source paths, types, signatures, and emission units. Internal identity includes
the source owner, while external-C identity is name-based and checked against the
canonical source type.

This applies `spec.md` §§2 and 6: lowering consumes recorded facts by identity,
coalesces each emission unit once, and neither parses a qualified/mangled spelling nor
serializes and reparses LowIR. PA14's typed encoder supplies object spellings without
its fact-text adapter. Small child collections stay inline, deep type modifiers are
flat and iterative, dominant indexes use geometric open-addressed storage, and the
explicit `--emit-lowir` adapter streams output directly, aligning with §§8-9.

## Current Failure Map

The required report is 27/108 and both compile-fail oracles pass. Of 81 remaining
success-oracle failures, 19 stop in PA12: comparison/conversion rules (8), overload
selection (4), labels (2), and five enum/reference/conditional/braced-return cases.
The other 62 reach PA15: unsupported statements (12), scalar conversions (11),
aggregate globals (11), expression forms (10), short-circuit/comma control (6),
indirect calls (3), increment/address unary forms (3), and six missing
address/constant/operand facts. This inventory supersedes the pre-audit failure map.

## Active Checkpoint

**Addressable storage and lvalue boundary.** Add one typed address result that covers
references, pointers, arrays, subscripts, decay, conditional/comma lvalues, and
address-valued global constants. PA12 must record any missing pointer-comparison,
compound-assignment, decay, and constant-address facts; PA15 must consume selected
bindings and value categories once and emit `addr`/`load`/`store`/`index` without
lookup or reevaluation. Per-binding/per-expression access remains average `O(1)` and
array initialization remains `O(elements)`. Validate reference aliasing,
array-init/subscript, pointer arithmetic, global addresses, and evaluate-LHS-once
clusters plus all checkpoint gates.

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
| Semantic handoff and scalar procedural spine | 0 -> 27/108 PA15; audit pass | scalar truth/conversion, ABI metadata, global declaration, cross-source identity/dedup, and 32k deep-type probes; ASan+UBSan current success set; through-PA14 1,037/1,037; clean file audit; linear 5k/10k counters |

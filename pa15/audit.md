# PA15 Checkpoint Audit

## Current Checkpoint Review

**Checkpoint:** Semantic handoff and scalar procedural spine (`1735bfbe`)

**Result:** Pass after audit fixes. The review was bounded to the landed PA15
increment; PA1-PA14 remain 1,037/1,037 and PA15 remains at its 27/108 checkpoint
baseline.

The landed path had typed top-level records but still encoded operations and
cross-links with interchangeable integers or strings, reconstructed constants and
arithmetic operand types during lowering, keyed cross-source emission through
rendered names, and rebuilt deep ABI modifier chains recursively. It also emitted
incorrect truth tests for wide integers and floating point, treated `extern` objects
as definitions, and could emit duplicate declaration/definition units across source
files.

The durable ownership boundary is now PA12 semantic facts -> PA15 typed LowIR ->
PA14 typed ABI spelling. PA12 records constants, common operand types, storage,
language linkage, exception state, and owner/name paths. PA15 consumes those facts by
identity; strong symbol/parameter/slot/block/temp IDs and typed operations prevent
cross-kind assignment, flat canonical tables own cross-source paths/types/symbols,
and internal entities include their source owner. Definitions are emitted once,
external-C declarations conflict by source type, and text is streamed only by the
explicit LowIR output adapter. No mangled or qualified spelling is an emission key.

Focused probes cover boolean constants, wide-integer and floating logical-not,
negative-zero conditions, external object declarations, C/noexcept metadata,
cross-source declaration/definition coalescing, duplicate-definition rejection, and
source-local internal symbols. A 5,000/10,000-assignment profile produced
15,003/30,003 instructions, 25,006/50,006 lowering visits, and 10,002/20,002 binding
probes; typed storage was 5,039,080/10,076,600 bytes and median lowering time was
12.58/24.97 ms. Flat ABI chains at 8,000/16,000/32,000 pointer levels completed in
0.02/0.04/0.08 s with 9,024/13,240/22,112 KiB maximum RSS. ASan+UBSan passed all 25
currently successful success-oracle fixtures and the 32,000-level stress case. The
PA15 report remains 27/108, the through-PA14 report is 1,037/1,037, and the PA15 file
audit is clean across 61 files. A process-only trace contains the compiler's initial
`execve` and `exit_group(0)` with no child tool or host compiler.

## Checkpoint Audit Ledger

| Checkpoint | Audit result | Evidence |
|---|---|---|
| Semantic handoff and scalar procedural spine | Pass after ownership, identity, truth-conversion, linkage, and scaling fixes | PA1-PA14 1,037/1,037; PA15 27/108 baseline preserved; clean file audit; linear 5k/10k profile; sanitized 32k deep-type probe |

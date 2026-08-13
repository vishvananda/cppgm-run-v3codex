# PA31 Implementation Plan

## Stage Design and Spec Alignment

PA31 now follows the completed-compiler path required by `spec.md` sections 2,
6--10: source semantics produce typed LowIR; each function is lowered once to
MIR and encoded immediately; final function layouts feed host-EH call sites,
LSDA/CFI, symbols, relocations, and a directly serialized ELF64 relocatable.
The backend consumes recorded ABI/runtime identities and never emits textual
LowIR or assembly or invokes a host compiler. The PA30 compiler payload remains
in a non-allocating ELF section so earlier internal linking stays compatible.

Ownership is split at stable boundaries: `lowir_native_host_eh` derives MIR
landing-pad clauses, the x86 encoder owns frame/code layout, and
`lowir_native_object_elf` owns ELF sections, host runtime imports, RTTI cells,
CFI, LSDA, symbols, and relocations. Work is O(IR + labels + relocations +
output), with hash-indexed symbols and geometrically growing buffers.

## Current Failure Map

No open PA31 failures. PA31 passes 17/17, PA1--PA30 pass 4132/4132, and the
PA31 file audit passes. The turn-start groups (host object format, runtime/EH
imports, protected regions, unwind frame facts, and local symbol binding) are
all closed at their owning boundaries.

## Active Checkpoint

PA31 full-stage is complete. Validation is the required PA31 report, the full
through-PA30 report, file audit, host link/run behavior, and exact normalized
ELF/LSDA/CFI/symbol facts. No later-assignment behavior was pulled forward.

## Performance Evidence

`CPPGM_DRIVER_STATS=1` plus `/usr/bin/time` measured representative PA31
objects. The 56-byte unhandled-throw case had 2 functions, 6 LowIR / 16 MIR
instructions, 5 fixups, 8,984 output bytes, 0.099 ms native lowering,
0.070 ms encoding, and 7,664 KiB RSS. The 3,980-byte compact-unwind case had
23 functions, 671 LowIR / 1,059 MIR instructions, 233 fixups, 326,648 output
bytes, 2.798 ms lowering, 1.464 ms encoding, and 9,872 KiB RSS. Counters,
time, and memory remain consistent with linear traversal; the direct writer
retains final function layouts but not whole-program MIR function bodies.

## Completed Checkpoints

| Checkpoint | Status | Evidence |
| --- | --- | --- |
| Host ELF64 object, SysV EH/unwind, RTTI/linkage, and compatibility boundary | Complete | PA31 17/17; through-PA30 4132/4132; file audit pass; scaling measurements above |

# PA29 Implementation Plan

## Stage Design and Spec Alignment

PA29 owns the direct `LowIR -> typed MIR -> x86-64 ELF` boundary. `lowir_parse`
owns PA13 syntax and structural validation; `lowir_native` owns one-pass
per-function instruction selection, bounded register/frame state, and startup/data
lowering; `mir_model` owns the deterministic debug view; `mir_elf` owns encoding,
fixups, and executable layout. This follows `spec.md` sections 6-9: typed facts
cross phase boundaries, MIR is serialized only as a view, native emission consumes
the same MIR, per-function work is O(instructions + operands), and final fixup/data
layout is O(output + relocations).

## Current Failure Map

| Group | Tests | Shared behavior and owner |
| --- | ---: | --- |
| Advanced strict MIR | 27 | Exact floating, atomic, object ABI, TLS/section, and stack-address forms; selector and encoder |
| Structural MIR | 59 | Width-aware scalar/floating selection, ABI classification, direct branches, slots, and calls; selector and ABI lowering |
| Behavior/pressure | 87 | Calls, loops, spills, object chunks, atomics, and liveness under pressure; allocator, frame planner, and encoder |

Ten of 183 tests now pass. The 173 failures above are divided by their first
stable owning boundary; unsupported forms fail normally rather than stopping at
the driver scaffold.

## Active Checkpoint

Implement the integer selection boundary: `copy`, width-aware integer/pointer
unary and binary operations, signed/unsigned comparisons, compare-as-value, and
direct compare-fed branches, including narrow normalization and pointer index /
difference forms. The owner is per-function LowIR-to-MIR selection with typed
width/predicate facts flowing to the existing encoder; the allocator may choose
locations but must preserve direct-branch shape.

This applies `spec.md` 6-7 and PA29 requirements 6-8, 11, and 13-15. Selection,
last-use updates, and encoding remain O(instructions + operands), with one typed
predicate/width decision per operation. Validate the strict unsigned anchors,
the structural integer/pointer/direct-branch families, full PA29 report,
through-PA28 report, and file audit.

## Performance Evidence

Generated independent leaf programs at 100/1,000/5,000 functions produced
205/2,005/10,005 MIR instructions and 1,756/16,156/80,156-byte executables.
Lowering took 0.175/1.859/9.349 ms, wall time 0.00/0.01/0.04 s, and peak RSS
4,084/6,380/16,180 KiB. Counts and observed time/space scale approximately
linearly; each case had one final startup fixup.

## Completed Checkpoints

| Checkpoint | Result | Evidence |
| --- | --- | --- |
| Foundation typed MIR and direct ELF | Complete | Strict `100-*` plus malformed input 9/9; full PA29 10/183 (one additional behavior pass); 5,000-function scaling evidence above |

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
| Advanced strict MIR | 24 | Exact floating, atomic, object ABI, TLS/section, and call/frame forms; selector and encoder |
| Structural MIR | 35 | Floating selection, ABI classification, call liveness, and frame promotion; selector and ABI lowering |
| Behavior/pressure | 77 | Calls, loops, spills, object chunks, atomics, and liveness under pressure; allocator, frame planner, and encoder |

Forty-seven of 183 tests now pass. The 136 failures above are divided by their first
stable owning boundary; unsupported forms fail normally rather than stopping at
the driver scaffold.

## Active Checkpoint

Implement the GPR call/allocation boundary: classify direct scalar and supported
object chunks across register/stack arguments and results; plan parameter homes,
parallel call moves, caller-clobber preservation, and reactive spills; and retain
values correctly across calls, branches, joins, and loop backedges. MIR frame
metadata and native prologue/epilogue emission must consume one shared frame plan.

This applies `spec.md` 7-9 and PA29 requirement 9. Liveness and allocation must be
linear or near-linear in MIR size, with block worklists/edge facts rather than
whole-function retries; frame growth is geometric/monotonic and each value gets
at most one stable spill home. Validate strict six-register/stack/object-call
anchors, structural call/frame cases, behavior pressure families, full PA29,
through-PA28, and file audit.

## Performance Evidence

Generated independent leaf programs at 100/1,000/5,000 functions produced
205/2,005/10,005 MIR instructions and 1,756/16,156/80,156-byte executables.
Lowering took 0.175/1.859/9.349 ms, wall time 0.00/0.01/0.04 s, and peak RSS
4,084/6,380/16,180 KiB. Counts and observed time/space scale approximately
linearly; each case had one final startup fixup.

The integer/frame selector was measured with 100/1,000/5,000 functions, each
containing a slot, narrow load/add, compare, and branch: 700/7,000/35,000 LowIR
instructions became 1,303/13,003/65,003 MIR instructions and
9,540/94,140/470,140-byte executables. Lowering took 1.248/11.781/61.354 ms,
wall time 0.00/0.04/0.21 s, and RSS 5,136/15,516/61,780 KiB, providing linear
count and approximately linear time/space evidence for the new analysis paths.

## Completed Checkpoints

| Checkpoint | Result | Evidence |
| --- | --- | --- |
| Foundation typed MIR and direct ELF | Complete | Strict `100-*` plus malformed input 9/9; full PA29 10/183 (one additional behavior pass); 5,000-function scaling evidence above |
| Integer, pointer, and frame selection | Complete | 27 focused integer/frame anchors pass; full PA29 47/183; 5,000-function mixed slot/branch scaling evidence above |

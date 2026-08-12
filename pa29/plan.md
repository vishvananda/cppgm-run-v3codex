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
| Unsupported lowering | 112 | Floating/XMM/x87, atomic, object ABI, TLS, and pressure forms rejected before MIR/ELF; selector, ABI classifier, and encoder |
| MIR conformance | 18 | Seven exact and eleven canonical frame, forwarding, and CFG-liveness mismatches; selector and allocator |
| Runtime correctness | 2 | Resident pointer/index and narrow call-result failures; allocator and integer normalizer |

Fifty-one of 183 tests now pass. The 132 failures are grouped by first stable
owning boundary; unsupported forms fail normally rather than at the driver.

## Active Checkpoint

Implement the scalar floating boundary: carry f32/f64 class facts from parsed
LowIR through MIR, allocate XMM values independently of GPRs, lower arithmetic,
ordered comparisons and integer/float conversions, and classify mixed SysV
register arguments and scalar returns. `lowir_native` owns selection/allocation;
the shared MIR then flows unchanged to `lowir_native_elf` for SSE encoding.

This applies `spec.md` sections 6-9 and PA29 requirements 6-9. Analysis and
selection remain O(instructions + operands); XMM allocation uses bounded pools
and per-call parallel moves, with monotonic stable frame homes under pressure.
Validate strict/structural f32/f64 leaves, calls, comparisons and conversions,
mixed-call ABI cases, full PA29, through-PA28, and file audit. f80/x87 and object
classification remain later checkpoints.

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

The GPR ABI path was measured with 100/1,000/5,000 wrappers making eight-argument
calls. Lowering took 0.00/0.03/0.14 s, peak RSS was 4,844/12,692/47,608 KiB, and
the executables were 12,372/121,272/605,272 bytes. Register parallelization is
bounded by six ABI GPRs and stack placement is linear in excess arguments, matching
the observed approximately linear scaling.

## Completed Checkpoints

| Checkpoint | Result | Evidence |
| --- | --- | --- |
| Foundation typed MIR and direct ELF | Complete | Strict `100-*` plus malformed input 9/9; full PA29 10/183 (one additional behavior pass); 5,000-function scaling evidence above |
| Integer, pointer, and frame selection | Complete | 27 focused integer/frame anchors pass; full PA29 47/183; 5,000-function mixed slot/branch scaling evidence above |
| GPR ABI and stack calls | Complete | Parallel six-register moves, stack arguments/homes, call-safe values, stable temp homes, and slot-address rematerialization; strict stack-address anchor passes; full PA29 51/183 (+4); call-heavy scaling above |

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
| Unsupported lowering | 23 | x87/f80, atomics, wide integers, TLS, and varargs rejected before complete MIR/ELF; selector and encoder |
| MIR conformance | 21 | Five exact, thirteen canonical, and three behavioral allocation/frame/section shape mismatches; selector and allocator |

One hundred thirty-nine of 183 tests now pass. The 44 failures are grouped by first
stable owning boundary; no aggregate ABI runtime failures remain.

## Active Checkpoint

Implement the x87/f80 scalar boundary. Typed f80 facts flow from LowIR through
dedicated frame-backed MIR operations and call/return classification; the selector
owns conversions, arithmetic, comparisons, literals, and x87 stack balance while
the encoder consumes the same MIR. Work remains O(instructions + operands), with
bounded x87 stack state and monotonic frame allocation. Validate all f80 anchors,
mixed GPR/f80 calls, full PA29, through-PA28, and file audit. Atomics, TLS,
variadics, wide integers, and residual MIR allocation shape remain later checkpoints.

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

The scalar SSE path was measured with 100/1,000/5,000 f64-heavy functions, each
performing a constant, add, multiply, comparison, and return. Lowering took
0.00/0.03/0.13 s, peak RSS was 4,864/11,116/40,100 KiB, and executables were
10,556/104,156/520,156 bytes, providing approximately linear time, space, and
output-size evidence for XMM selection and direct SSE encoding.

The CFG allocator was measured on 100/1,000/5,000-block value chains. Dirty
predecessor propagation plus edge-safe allocation took 0.00/0.01/0.08 s, peak
RSS was 4,340/8,496/26,112 KiB, and executables were 1,659/15,159/75,159 bytes.
Observed time, space, and output size scale approximately linearly; work is
proportional to instructions, CFG edges, and newly propagated live facts.

The aggregate ABI path was measured with 100/1,000/5,000 groups of a two-eightbyte
return, straddling stack argument, and wrapper call: 1,002/10,002/50,002 LowIR
instructions became 3,805/38,005/190,005 MIR instructions. Lowering took
4.43/46.73/259.91 ms, wall time 0.01/0.10/0.62 s, RSS was
6,716/31,968/143,336 KiB, and output was 31,151/310,151/1,550,151 bytes,
showing approximately linear classification, lowering, and encoding.

## Completed Checkpoints

| Checkpoint | Result | Evidence |
| --- | --- | --- |
| Foundation typed MIR and direct ELF | Complete | Strict `100-*` plus malformed input 9/9; full PA29 10/183 (one additional behavior pass); 5,000-function scaling evidence above |
| Integer, pointer, and frame selection | Complete | 27 focused integer/frame anchors pass; full PA29 47/183; 5,000-function mixed slot/branch scaling evidence above |
| GPR ABI and stack calls | Complete | Parallel six-register moves, stack arguments/homes, call-safe values, stable temp homes, and slot-address rematerialization; strict stack-address anchor passes; full PA29 51/183 (+4); call-heavy scaling above |
| Scalar f32/f64 and XMM ABI | Complete | SSE arithmetic, ordered/value comparisons, conversions, globals, direct/indirect mixed calls, returns, call-crossing homes, and exact decimal rounding; focused scalar anchors pass; full PA29 94/183 (+43); float-heavy scaling above |
| CFG liveness and reactive spilling | Complete | Dirty predecessor worklist, call/fixed-clobber facts, incoming-register ownership, edge-safe reuse, stable GPR/XMM homes, variable indices, narrow reloads, and parallel XMM cycles; full PA29 118/183 (+24); 5,000-block scaling above |
| SysV aggregate ABI and bulk storage | Complete | Typed one/two-eightbyte register classification, whole-object stack rollback, parameter/result homes and aliases, pass-mode address materialization, slot-safe bulk operations, and padded returns; all 19 focused object/pass-mode anchors pass; full PA29 139/183 (+21); aggregate scaling above |

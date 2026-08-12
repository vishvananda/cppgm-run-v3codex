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
| Wide scalar and atomic ABI | 2 | i128 call chunks and compare-exchange; ABI classifier, selector, encoder |
| Variadic register-save state | 2 | GPR/XMM `va_start` state and caller vector count; ABI and frame lowering |
| Global metadata and storage | 2 | Read-only extra sections and direct TLS; global lowering and ELF layout |
| Exact entry/call/slot shape | 3 | Constructor entry, six-register indirect call, and promoted slot; selector |
| CFG/allocation conformance | 16 | Thirteen canonical control-flow/liveness cases and three behavioral spill/home shapes; analysis and allocator |

One hundred fifty-eight of 183 tests now pass. The 25 failures are grouped by first
stable owning boundary; scalar atomics, aggregate ABI, and floating runtime are complete.

## Active Checkpoint

Implement the SysV variadic register-save boundary required by `spec.md` sections 6-9
and PA29's ABI contract. LowIR call-boundary arity and argument types flow through
`lowir_native_abi` classification: callers set the XMM-argument count in `al`, while
variadic callees materialize bounded GPR/XMM save areas and initialize `va_start`'s
`gp_offset`, `fp_offset`, `overflow_arg_area`, and `reg_save_area`. ABI classification
owns register/stack positions, the selector owns frame bindings and prologue stores,
and existing MIR/ELF typed moves own encoding. Work remains O(parameters + arguments +
instructions), with constant-size SysV register-save areas. Validate both remaining
variadic anchors, adjacent indirect/mixed-call and register-home cases, full PA29,
through-PA28, and file audit; measure a generated variadic-call family. i128, globals,
and residual CFG/allocation conformance remain later checkpoints.

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

The x87 path was measured with 100/1,000/5,000 f80-heavy functions performing
constant, add, unsigned conversion, compare, and return: 604/6,004/30,004 LowIR
instructions became 918/9,018/45,018 MIR instructions. Lowering took
1.56/14.64/76.75 ms, wall time 0.00/0.04/0.23 s, RSS was
5,440/15,072/58,044 KiB, and output was 34,449/342,249/1,710,249 bytes.
All generated executables returned zero, and count/time/space scaled approximately
linearly with bounded x87 depth.

The scalar atomic path was measured with 100/1,000/5,000 functions exercising load,
store, exchange, fetch-add, compare-exchange, and a fence. 1,001/10,001/50,001 LowIR
instructions became 2,705/27,005/135,005 MIR instructions and
14,464/143,168/715,168-byte executables. Lowering took 4.06/33.48/172.00 ms, wall time
was 0.01/0.07/0.39 s, and peak RSS was 6,172/24,252/103,468 KiB, showing linear counts
and approximately linear time/space with O(1) fixed-register work per operation.

## Completed Checkpoints

| Checkpoint | Result | Evidence |
| --- | --- | --- |
| Foundation typed MIR and direct ELF | Complete | Strict `100-*` plus malformed input 9/9; full PA29 10/183 (one additional behavior pass); 5,000-function scaling evidence above |
| Integer, pointer, and frame selection | Complete | 27 focused integer/frame anchors pass; full PA29 47/183; 5,000-function mixed slot/branch scaling evidence above |
| GPR ABI and stack calls | Complete | Parallel six-register moves, stack arguments/homes, call-safe values, stable temp homes, and slot-address rematerialization; strict stack-address anchor passes; full PA29 51/183 (+4); call-heavy scaling above |
| Scalar f32/f64 and XMM ABI | Complete | SSE arithmetic, ordered/value comparisons, conversions, globals, direct/indirect mixed calls, returns, call-crossing homes, and exact decimal rounding; focused scalar anchors pass; full PA29 94/183 (+43); float-heavy scaling above |
| CFG liveness and reactive spilling | Complete | Dirty predecessor worklist, call/fixed-clobber facts, incoming-register ownership, edge-safe reuse, stable GPR/XMM homes, variable indices, narrow reloads, and parallel XMM cycles; full PA29 118/183 (+24); 5,000-block scaling above |
| SysV aggregate ABI and bulk storage | Complete | Typed one/two-eightbyte register classification, whole-object stack rollback, parameter/result homes and aliases, pass-mode address materialization, slot-safe bulk operations, and padded returns; all 19 focused object/pass-mode anchors pass; full PA29 139/183 (+21); aggregate scaling above |
| x87/f80 scalar and ABI path | Complete | Frame-backed f80 SSA, aligned stack parameters, `st0` returns, exact literals/globals, balanced arithmetic/comparisons, signed/unsigned conversions, implicit width conversion, and shared ABI ownership; all eight f80 anchors plus adjacent u32 conversion pass; full PA29 148/183 (+9); x87 scaling above |
| Scalar atomics and fences | Complete | Typed TSO loads/stores, seq-cst exchange/store, locked fetch-add/compare-exchange, expected writeback, narrow normalization, fixed-clobber analysis, direct encoding, and bounded allocator/MIR modules; all 10 scalar atomic anchors pass; full PA29 158/183 (+10); atomic scaling above |

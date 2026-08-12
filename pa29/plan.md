# PA29 Implementation Plan

## Stage Design and Spec Alignment

PA29 owns `LowIR -> typed MIR -> x86-64 ELF`. `lowir_parse` validates the PA13
text boundary; ABI, function/storage analysis, selection utilities, wide-value,
varargs, and program modules own bounded facts; `lowir_native` selects per
function; `mir_model` is the deterministic view; and `lowir_native_elf` encodes
the same MIR. This follows `spec.md` sections 6-9: typed facts cross explicit
phase boundaries, allocation is demand-driven, and lowering plus fixups is
O(instructions + operands + output).

## Current Failure Map

| Group | Tests | Status |
| --- | ---: | --- |
| Current PA | 183 | No failures; strict, structural, and behavior suites pass |

PA29 is complete at 183/183. PA1-PA28 remain clean at 3,857/3,857.

## Active Checkpoint

Complete. Per `spec.md` sections 7-9, analysis now owns typed parameter identity,
width-safe compare demand, and clobber intervals; selection owns ABI setup and
pressure materialization; `RegisterPool` retains constant-size physical choices.
The flow remains O(instructions + CFG edges). Validation covers every PA29 test,
through-PA28, file audit, and 100/1,000/5,000 pressure scaling.

## Performance Evidence

| Path | 100 / 1,000 / 5,000 workload evidence | Result |
| --- | --- | --- |
| Foundation leaves | 0.175 / 1.859 / 9.349 ms lower; 4,084 / 6,380 / 16,180 KiB RSS | Linear counts/time/space |
| Integer/frame | 1.248 / 11.781 / 61.354 ms; 1,303 / 13,003 / 65,003 MIR | Linear mixed slot/branch lowering |
| GPR ABI | 0.00 / 0.03 / 0.14 s; 12,372 / 121,272 / 605,272-byte output | Six-register work bounded; stack work linear |
| SSE | 0.00 / 0.03 / 0.13 s; 4,864 / 11,116 / 40,100 KiB RSS | Linear scalar-float lowering |
| CFG allocator | 0.00 / 0.01 / 0.08 s; 1,303 / 13,003 / 65,003 MIR | Linear block/edge propagation |
| Aggregate ABI | 4.43 / 46.73 / 259.91 ms; 3,805 / 38,005 / 190,005 MIR | Linear classification and encoding |
| x87/f80 | 1.56 / 14.64 / 76.75 ms; 34,449 / 342,249 / 1,710,249-byte output | Linear with bounded x87 depth |
| Scalar atomics | 4.06 / 33.48 / 172.00 ms; 2,705 / 27,005 / 135,005 MIR | O(1) fixed-register work per operation |
| Variadics | 2.99 / 31.77 / 157.66 ms; 3,005 / 30,005 / 150,005 MIR | Linear save-area/classification growth |
| i128 values | 2.08 / 19.85 / 100.12 ms; 2,305 / 23,005 / 115,005 MIR; 5,412 / 16,268 / 64,296 KiB RSS | Linear two-limb lowering; O(1) work per value |
| Storage facts | 1.00 / 9.42 / 52.77 ms lower; 305 / 3,005 / 15,005 MIR; 7,028 / 12,244 / 47,104 KiB RSS | Linear readonly/TLS plus parameter-shadow workload |
| Branch demand | 0.65 / 5.54 / 34.81 ms lower; 410 / 4,010 / 20,010 MIR; 6,824 / 9,980 / 35,640 KiB RSS | Linear CFG worklist and deferred compare lowering |
| Wide call pressure | 0.03 / 0.30 / 1.69 s; 10,162 / 101,062 / 505,062 MIR lines; 9,496 / 57,848 / 267,844 KiB RSS | Linear per-function liveness, homes, and setup |

The i128 family used 100/1,000/5,000 two-limb parameter, constant, equality, and
return functions. Outputs were 14,356/142,156/710,156 bytes and wall times were
0.00/0.03/0.19 s, corroborating the internal timings.

## Completed Checkpoints

| Checkpoint | Result | Evidence |
| --- | --- | --- |
| Foundation typed MIR and direct ELF | Complete | Full PA29 10/183; 5,000-function scaling |
| Integer, pointer, and frame selection | Complete | Full PA29 47/183; mixed slot/branch scaling |
| GPR ABI and stack calls | Complete | Full PA29 51/183; call-heavy scaling |
| Scalar f32/f64 and XMM ABI | Complete | Full PA29 94/183; float-heavy scaling |
| CFG liveness and reactive spilling | Complete | Full PA29 118/183; 5,000-block scaling |
| SysV aggregate ABI and bulk storage | Complete | Full PA29 139/183; all 19 anchors and aggregate scaling |
| x87/f80 scalar and ABI path | Complete | Full PA29 148/183; all eight anchors and x87 scaling |
| Scalar atomics and fences | Complete | Full PA29 158/183; all 10 anchors and atomic scaling |
| SysV variadic register-save state | Complete | Full PA29 160/183; both anchors and variadic scaling |
| i128 scalar/atomic ABI | Complete | Exact MIR and runtime for both anchors; full PA29 162/183; i128 scaling above |
| Storage identity and deferred materialization | Complete | Five anchors exact/runtime; full PA29 167/183; through-PA28 3,857/3,857; storage scaling above |
| Direct branch demand and result forwarding | Complete | All seven anchors exact/runtime; full PA29 174/183; branch scaling above |
| Allocation, parameter identity, and call setup | Complete | All nine final anchors exact/runtime; full PA29 183/183; through-PA28 3,857/3,857; pressure scaling above |

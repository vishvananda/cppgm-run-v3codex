# PA38 Plan

## Stage Design and Spec Alignment

The PA38 path is `typed LowIR -> function-local physical-register MIR ->
machine_opt(level) -> direct ELF encoding`. `lowir2native` retains a complete
MIR program only when its explicit dump/executable interface requires both
views; `cppgm++` lowers, optimizes, encodes, and releases one function at a
time. The optimized MIR is authoritative: `ret`, bulk-memory, branch, frame,
and call operands consumed by the encoder are the same facts serialized by
`--dump-machine-ir`.

This applies `spec.md` §6's typed one-way lowering boundary; §7's small,
explicit O1/O2 budgets, function-local lifetime, near-linear allocation, and
worklist fixed points; §8's short-lived per-function analysis ownership; §9's
O(n) or O(n log n) backend bound and work counters; and §10's self-contained
object path. PA38 changes no LowIR semantics and introduces no later-stage
interprocedural behavior.

## Current Failure Map

The turn-start provider baseline was 2/32 and the checked primary lane was
2/24. All 32 primary/debug cases now pass. The closed failures shared three
owners:

| Behavior group | Failing surface | Owner/data flow |
| --- | --- | --- |
| Local value cleanup | integer/float copies, return and call-result shuffles, immediate rematerialization, frame-address folding | per-block MIR value facts -> explicit operand rewrite -> register liveness |
| CFG and ABI safety | fallthrough jumps, conditional tails, zero tests, bulk-copy setup, cross-block live copies, ordinary call arguments | block successor index -> fixed-register liveness; encoder consumes rewritten operands |
| O2 finalization | jump-trace layout, unused callee-save pruning, final frame reservation | whole-function block order and surviving physical/frame facts |

The missing level consumer, structural mismatches, behavior checks, and debug
locations are closed. No current-PA failure remains.

## Active Checkpoint

Completed: the shared post-lowering machine optimizer is wired with the explicit
level through `lowir2native` and both streaming `cppgm++` object/executable
paths. O1 uses fixed-size physical-register facts, indexed CFG edges, and a
monotonic predecessor worklist to coalesce safe local copies, rematerialize
supported constants, fold frame addresses, preserve call/bulk/cross-block
requirements, form direct zero tests, and clean branch fallthroughs. O2 first
builds deterministic unconditional-jump traces, then runs O1 and prunes
unused preservation/frame state. Debug/source-position facts from removed
instructions are retained on a surviving instruction.

The implemented complexity is O(I + (B + E)R), where physical register count
R is the fixed x86-64 constant. Each liveness bit and predecessor causes only
bounded work. The encoder now consumes rewritten return and bulk-memory
operands, and MIR lowering/serialization preserves source locations through
fused and removed instructions. Validation covers all O1/O2 structural and
executable cases, both debug lanes, the shared-driver path, the cumulative
through-PA37 report, file audit, and controlled scaling.

## Performance Evidence

The release `lowir_native_opt.o` was exercised directly on a deterministic
four-instruction-per-block jump chain. `/usr/bin/time` measured process RSS;
optimizer telemetry measured only the pass:

| Blocks | Input / output MIR | Visits / CFG edges / pushes | Rewrites | Optimizer time | RSS |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1,000 | 4,000 / 2,001 | 10,002 / 1,998 / 1,000 | 2,999 | 1.62 ms | 5,360 KiB |
| 2,000 | 8,000 / 4,001 | 20,002 / 3,998 / 2,000 | 5,999 | 3.91 ms | 7,152 KiB |
| 4,000 | 16,000 / 8,001 | 40,002 / 7,998 / 4,000 | 11,999 | 7.08 ms | 10,480 KiB |
| 8,000 | 32,000 / 16,001 | 80,002 / 15,998 / 8,000 | 23,999 | 19.33 ms | 17,132 KiB |

Every work counter and live-memory measurement grows linearly; the small
wall-time variation does not hide repeated scans. A `cppgm++ -O1` LowIR-input
link also reported one machine-optimizer function, 2 input/2 output MIR
instructions, 6 visits, one worklist push, and a successful executable,
confirming reuse outside `lowir2native`.

## Completed Checkpoints

| Checkpoint | Result |
| --- | --- |
| PA38 shared machine optimizer | O1 local value/ABI/CFG cleanup, O2 trace/frame finalization, explicit encoder operands, debug preservation, production telemetry, shared-driver integration, and linear scaling completed; 24/24 primary, 8/8 debug, and 5,065/5,065 prior tests pass. |

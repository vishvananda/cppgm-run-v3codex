# PA30 Plan

## Stage Design and Spec Alignment

PA30 owns a driver boundary around the existing front end and PA29 backend.
Each source is lowered once from the semantic graph through typed PA15 LowIR,
adapted directly to backend LowIR, and stored in a versioned binary object.
Link mode indexes strong, weak, and internal symbols across objects, aggregates
ordered TU lifecycle hooks, lowers the linked program to MIR, imports supported
x86-64 ELF relocatables, and writes one executable without a host linker. This
matches `spec.md` sections 1, 6-10: typed phase ownership, no production text
round trip, demand-preserving linkage, indexed lookup, and direct ELF output.

Data flows `source/options -> semantic graph -> typed PA15 LowIR -> compiler
object -> indexed link -> backend LowIR/MIR -> ELF`. Compile work is O(source +
emitted IR); link work is O(objects + symbols + relocations + output bytes),
using hash indexes and geometric buffers.

## Current Failure Map

The current result is 72/88, up 10 from the checkpoint's 62/88 baseline. The
16 remaining failures group by owner:

- 7 are numeric/helper lowering: four calculator/float80 mismatches, builtin
  `isnan`, a discarded float80 result, and comprehensive i128 value lowering.
- 7 are semantic region formation: two GNU statement expressions, three
  function-try forms, local-friend access, and ordered template handlers.
- 2 are aggregate representations: trivial-union value initialization and a
  multiple-inheritance virtual member pointer.

## Active Checkpoint

Next, close the numeric/helper boundary. The spec's typed phase contracts and
backend legality rules require float80 and i128 values, helper calls, and
discarded results to retain explicit widths and ABI roles through lowering.
Data flows `typed expression -> PA15 LowIR -> PA30 adapter -> MIR legalization
and allocation -> x86/helper emission`. Ownership is split between numeric
lowering and the native MIR/backend; expected work is O(instructions + live
ranges), with indexed helper lookup and bounded-width legalization. Validate
the seven numeric/helper failures, then the PA30, through-PA29, and audit gates.

## Performance Evidence

Representative direct links show proportional work: 1/2/3 objects produced
1/3/9 symbols, 2/5/16 symbol probes, 1/2/7 definitions, 5/8/26 MIR
instructions, and 156/170/297-byte executables. Each measured under 0.01 s at
7.4-7.7 MiB maximum RSS. This supports the expected linear indexed-link path;
no repeated whole-program scan appeared. EH samples with nested copy/rethrow and
incremental constructor cleanup each compiled under 0.01 s at 7.4-7.6 MiB RSS;
the PA13 catch/cleanup/resume executables were 776-984 bytes. The VTT and
sibling-cast cases each compiled under 0.01 s at 7.1-7.9 MiB RSS. Repeating VTT
LowIR emission 100/200 times took 0.53/1.07 s wall and 0.28/0.58 s user time at
7.0-7.3 MiB RSS, evidence of proportional semantic/lowering work.

## Completed Checkpoints

| Checkpoint | Result | Validation |
| --- | --- | --- |
| Typed object/driver/link boundary | 0 -> 49/88; direct source/object/mixed links, options, lifecycle aggregation, strong/weak rejection, ELF64 helper import, and focused i128 operations implemented | 10/10 focused boundary tests; helper group 7/11; through PA29 4040/4040; file audit passes |
| Native exception regions and constructor unwind | 49 -> 62/88; role-indexed EH runtime, typed/catch-all dispatch, by-value copy lifetime, nested rethrow, and prefix subobject cleanup implemented | PA13 catch/end/cleanup exits 7/3/14; focused PA30 EH/constructor cases pass; through PA29 4040/4040; file audit passes |
| Polymorphic support closure and link ownership | 62 -> 72/88; typed ABI runtime roles, coalescible special entries, all-view key ownership, VTT retention, RTTI casts, and multibase return/vptr layout implemented | 9/9 focused cases; PA30 72/88; through PA29 4040/4040; file audit passes |

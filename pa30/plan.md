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

The current result is 49/88, up from 0/88. The 39 failures group by owner:

- 14 reach unselected EH/cleanup LowIR instructions, including destructor
  cleanup, throws/catches, local-class construction, and pure-virtual cleanup.
- 10 are front-end language gaps: GNU statement expressions, function try
  blocks, multiple template handlers, local-friend access, builtin `nanl`, and
  one VTT name lookup.
- 8 are polymorphic linkage/runtime gaps: three duplicate vtable/thunk/special
  member definitions, four missing RTTI/dynamic-cast runtime symbols, and two
  remaining multibase lowering paths (one overlaps cleanup).
- 4 helper-backed calculator/float80 programs link and run but produce incorrect
  bytes; one float80 discard and one trivial-union value-init case return the
  wrong status; the comprehensive i128 case reaches runtime but fails its
  struct-array shift loop (other focused i128 cases pass).

## Active Checkpoint

Next, connect typed EH regions at the LowIR-to-MIR boundary. The PA26 region
instructions and runtime symbol roles own the input; native function lowering
must maintain a bounded handler/cleanup stack, lower exception value/selector
operations, and route throw/resume through runtime entries while preserving
normal control flow. Expected work is O(instructions + EH edges), with one
indexed region record per active handler. Validate destructor-only cleanup,
catch-all, typed catch-by-value, constructor unwind, and function-try-block
cases, followed by the full PA30 and through-PA29 reports.

## Performance Evidence

Representative direct links show proportional work: 1/2/3 objects produced
1/3/9 symbols, 2/5/16 symbol probes, 1/2/7 definitions, 5/8/26 MIR
instructions, and 156/170/297-byte executables. Each measured under 0.01 s at
7.4-7.7 MiB maximum RSS. This supports the expected linear indexed-link path;
no repeated whole-program scan appeared.

## Completed Checkpoints

| Checkpoint | Result | Validation |
| --- | --- | --- |
| Typed object/driver/link boundary | 0 -> 49/88; direct source/object/mixed links, options, lifecycle aggregation, strong/weak rejection, ELF64 helper import, and focused i128 operations implemented | 10/10 focused boundary tests; helper group 7/11; through PA29 4040/4040; file audit passes |

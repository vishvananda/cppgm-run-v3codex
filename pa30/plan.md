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

The current result is 62/88, up from 0/88. The 26 failures group by owner:

- 9 are polymorphic identity/link ownership: vtable/VTT/key-function selection,
  cross-TU initialization, and multiple-inheritance RTTI/return adjustment.
- 8 are source semantic/region formation: GNU statement expressions, three
  function-try forms, ordered multiple handlers, local-friend access, and
  pure-virtual body classification.
- 7 are numeric/helper lowering: four calculator/float80 byte mismatches,
  builtin `nanl`, discarded float80 results, and one comprehensive i128 loop.
- 2 are aggregate representations: trivial-union value initialization and a
  multiple-inheritance virtual member pointer.

## Active Checkpoint

Next, reconcile polymorphic definition ownership at the semantic-to-link
boundary. Per `spec.md` sections 2, 4-7, canonical class/function identities and
demand own vtable, VTT, RTTI, thunk, and special-member emission; the object
index must coalesce weak definitions while retaining the key-function TU and
required construction tables. Data flows `canonical identity + demand -> object
symbol provenance -> indexed link resolution -> native relocation`; expected
work is O(polymorphic facts + symbols + relocations), without pairwise TU scans.
Validate the nine grouped failures plus representative PA27-PA29 multibase and
cross-TU cases, then PA30 and through-PA29 reports.

## Performance Evidence

Representative direct links show proportional work: 1/2/3 objects produced
1/3/9 symbols, 2/5/16 symbol probes, 1/2/7 definitions, 5/8/26 MIR
instructions, and 156/170/297-byte executables. Each measured under 0.01 s at
7.4-7.7 MiB maximum RSS. This supports the expected linear indexed-link path;
no repeated whole-program scan appeared. EH samples with nested copy/rethrow and
incremental constructor cleanup each compiled under 0.01 s at 7.4-7.6 MiB RSS;
the PA13 catch/cleanup/resume executables were 776-984 bytes.

## Completed Checkpoints

| Checkpoint | Result | Validation |
| --- | --- | --- |
| Typed object/driver/link boundary | 0 -> 49/88; direct source/object/mixed links, options, lifecycle aggregation, strong/weak rejection, ELF64 helper import, and focused i128 operations implemented | 10/10 focused boundary tests; helper group 7/11; through PA29 4040/4040; file audit passes |
| Native exception regions and constructor unwind | 49 -> 62/88; role-indexed EH runtime, typed/catch-all dispatch, by-value copy lifetime, nested rethrow, and prefix subobject cleanup implemented | PA13 catch/end/cleanup exits 7/3/14; focused PA30 EH/constructor cases pass; through PA29 4040/4040; file audit passes |

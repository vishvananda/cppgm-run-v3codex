# PA30 Plan

## Stage Design and Spec Alignment

PA30 owns a driver boundary around the existing front end and PA29 backend.
Each source is lowered once through typed PA15 LowIR, adapted directly to
backend LowIR, and stored in a versioned object. Link mode indexes symbols,
ordered TU lifecycle hooks, and relocations before MIR/native ELF emission.
This follows `spec.md` sections 1 and 6-10: typed phase ownership, no production
text round trip, demand-preserving linkage, indexed lookup, and direct output.

Data flows `source/options -> semantic graph -> typed PA15 LowIR -> compiler
object -> indexed link -> backend LowIR/MIR -> ELF`. Compile work is O(source +
emitted IR); link work is O(objects + symbols + relocations + output bytes),
using hash indexes and geometric buffers.

## Current Failure Map

The current result is 79/88, up from this checkpoint's 72/88 baseline. The nine
remaining failures group by shared behavior and owner:

- 7 are semantic region formation: two GNU statement expressions, three
  function-try forms, local-friend access, and ordered template handlers.
- 2 are aggregate representations: trivial-union value initialization and a
  multiple-inheritance virtual member pointer.

## Active Checkpoint

Next, close semantic region formation at the syntax-to-semantic-graph boundary.
The spec's canonical-fact and phase-ownership rules require statement-expression
values, function-try handlers, and access/handler ordering to be explicit before
lowering. Data flows `syntax -> scoped semantic facts -> typed dump regions ->
control/EH lowering`; semantic analysis owns the facts and lowering consumes
them without source-name recovery. Expected work is O(nodes + scope/region
edges) with indexed lookup. Validate the seven semantic-region failures, then
the PA30, through-PA29, and audit gates.

## Performance Evidence

Indexed direct-link samples over 1/2/3 objects produced 1/3/9 symbols and
2/5/16 probes in under 0.01 s at 7.4-7.7 MiB RSS. Repeating VTT LowIR emission
100/200 times took 0.53/1.07 s wall and 0.28/0.58 s user. Repeating the i128
compile/native path 30/60 times took 0.27/0.58 s wall and 0.18/0.34 s user at
8.3 MiB RSS. These ratios support the expected near-linear indexed lowering,
allocation, and link paths; no repeated whole-program scan appeared.

## Completed Checkpoints

| Checkpoint | Result | Validation |
| --- | --- | --- |
| Typed object/driver/link boundary | 0 -> 49/88; direct source/object/mixed links, options, lifecycle aggregation, strong/weak rejection, ELF64 helper import, and focused i128 operations implemented | 10/10 focused boundary tests; helper group 7/11; through PA29 4040/4040; file audit passes |
| Native exception regions and constructor unwind | 49 -> 62/88; role-indexed EH runtime, typed/catch-all dispatch, by-value copy lifetime, nested rethrow, and prefix subobject cleanup implemented | PA13 catch/end/cleanup exits 7/3/14; focused PA30 EH/constructor cases pass; through PA29 4040/4040; file audit passes |
| Polymorphic support closure and link ownership | 62 -> 72/88; typed ABI runtime roles, coalescible special entries, all-view key ownership, VTT retention, RTTI casts, and multibase return/vptr layout implemented | 9/9 focused cases; PA30 72/88; through PA29 4040/4040; file audit passes |
| Numeric values, calls, and native allocation | 72 -> 79/88; call-live and wide-parameter homes, x87-compatible f64/f80 handling, i128 values, discarded f80 results, and typed NaN builtins implemented | 7/7 focused; PA30 79/88; through PA29 4040/4040; file audit passes; 30/60 compile scaling measured |

# PA30 Plan

## Stage Design and Spec Alignment

PA30 owns a driver boundary around the existing front end and PA29 backend.
Each source is lowered once through typed PA15 LowIR, adapted directly to
backend LowIR, and stored in a versioned object. Link mode indexes symbols,
ordered TU lifecycle hooks, and relocations before MIR/native ELF emission.
This follows `spec.md` sections 1-10: canonical identity and typed phase
ownership, no production text round trip, demand-preserving linkage, indexed
lookup, bounded per-function lowering, and direct output. Scoped regions retain
structured syntax once, publish typed statement-result, handler-order, and
function-try body-role facts, and lower with one geometric task scheduler.

Data flows `source/options -> semantic graph -> typed PA15 LowIR -> compiler
object -> indexed link -> backend LowIR/MIR -> ELF`. Compile work is O(source +
emitted IR); link work is O(objects + symbols + relocations + output bytes),
using hash indexes and geometric buffers.

## Current Failure Map

The current result is 88/90 after two audit regressions were added and passed,
preserving the turn-start 86/88 baseline. Both remaining failures share
aggregate representation ownership: trivial-union value initialization and a
multiple-inheritance virtual member pointer. The audited scoped-region path is
closed and does not own either failure.

## Next Substantial Checkpoint

Close aggregate representation at semantic initialization and ABI lowering.
`spec.md` sections 2-3 and 8-10 require canonical class/layout identities,
explicit value-initialization and member-pointer facts, and target-independent
typed lowering before native allocation. Semantic initialization owns the
selected union member and zero-initialization fact; member-pointer lowering owns
the canonical path/adjustment representation; native emission only allocates
those typed values. Data flows `aggregate/type facts -> typed semantic action ->
LowIR value -> ABI allocation/native code`. Expected work is O(members + base
path), using existing indexed layout/path facts. Validate both remaining cases,
then PA30, through-PA29, and audit gates.

## Performance Evidence

Indexed direct-link samples over 1/2/3 objects produced 1/3/9 symbols and
2/5/16 probes in under 0.01 s at 7.4-7.7 MiB RSS. Repeating VTT LowIR emission
100/200 times took 0.53/1.07 s wall and the i128 compile/native path at 30/60
repetitions took 0.27/0.58 s. Audited statement-expression widths
512/1,024/2,048 produce 523/1,035/2,059 scheduler tasks, peak task depth two,
and 47,210/96,362/194,666 output bytes. Five-run medians for 20 compilations are
0.20/0.32/0.57 s versus 0.21/0.34/0.58 s before audit, with byte-identical
LowIR. The evidence supports linear indexed lowering/link work and no repeated
whole-program scan or per-prefix scheduler allocation.

## Completed Checkpoints

| Checkpoint | Result | Validation |
| --- | --- | --- |
| Typed object/driver/link boundary | 0 -> 49/88; direct source/object/mixed links, options, lifecycle aggregation, strong/weak rejection, ELF64 helper import, and focused i128 operations implemented | 10/10 focused boundary tests; helper group 7/11; through PA29 4040/4040; file audit passes |
| Native exception regions and constructor unwind | 49 -> 62/88; role-indexed EH runtime, typed/catch-all dispatch, by-value copy lifetime, nested rethrow, and prefix subobject cleanup implemented | PA13 catch/end/cleanup exits 7/3/14; focused PA30 EH/constructor cases pass; through PA29 4040/4040; file audit passes |
| Polymorphic support closure and link ownership | 62 -> 72/88; typed ABI runtime roles, coalescible special entries, all-view key ownership, VTT retention, RTTI casts, and multibase return/vptr layout implemented | 9/9 focused cases; PA30 72/88; through PA29 4040/4040; file audit passes |
| Numeric values, calls, and native allocation | 72 -> 79/88; call-live and wide-parameter homes, x87-compatible f64/f80 handling, i128 values, discarded f80 results, and typed NaN builtins implemented | 7/7 focused; PA30 79/88; through PA29 4040/4040; file audit passes; 30/60 compile scaling measured |
| Scoped semantic regions and ordered EH | 79 -> 86/88, audited at 88/90 with two passing regressions; statement-expression values/reachability, typed ordinary/constructor/destructor function-try roles, inherited local-friend access, and ordered derived/catch-all dispatch implemented | 9/9 focused; PA30 baseline preserved with only the two aggregate failures; through PA29 4040/4040; file audit passes; 512/1,024/2,048 region scaling measured |

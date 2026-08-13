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

PA30 is 90/90. The former aggregate-representation group is closed:
non-user-provided defaulted union construction now retains value-initialization,
and polymorphic member-function pointers retain slot, adjustment, and dispatch
facts across runtime calls. No current-stage failures remain.

## Active Checkpoint

PA30 full-stage closure. Aggregate representation is complete at semantic
initialization and ABI lowering.
`spec.md` sections 2-3 and 8-10 require canonical class/layout identities,
explicit value-initialization and member-pointer facts, and target-independent
typed lowering before native allocation. Semantic initialization owns the
non-user-provided default-constructor/value-initialization fact; polymorphism
semantics records the selected virtual slot on a member-pointer constant; and
member-pointer lowering owns its `{target-or-slot, this-adjustment}` ABI value.
Data flows `class/layout facts -> typed semantic action -> LowIR value -> ABI
allocation/native code`. Construction remains O(1) after existing indexed
layout completion; member-pointer formation and invocation add O(1) arithmetic
and one virtual-slot load, with no class/member scan. Validation is 2/2 focused,
90/90 PA30, 4040/4040 through PA29, 4130/4130 through PA30, and a passing file
audit.

## Performance Evidence

Indexed direct-link samples over 1/2/3 objects produced 1/3/9 symbols and
2/5/16 probes in under 0.01 s at 7.4-7.7 MiB RSS. Repeating VTT LowIR emission
100/200 times took 0.53/1.07 s wall and the i128 compile/native path at 30/60
repetitions took 0.27/0.58 s. Audited statement-expression widths
512/1,024/2,048 produce 523/1,035/2,059 scheduler tasks, peak task depth two,
and 47,210/96,362/194,666 output bytes. Five-run medians for 20 compilations are
0.20/0.32/0.57 s versus 0.21/0.34/0.58 s before audit, with byte-identical
LowIR. The evidence supports linear indexed lowering/link work and no repeated
whole-program scan or per-prefix scheduler allocation. Repeated polymorphic
member-pointer LowIR compilation took 0.13/0.26 s for 20/40 runs at 7.5 MiB
peak RSS; 40 union-value-init runs took 0.22 s at 7.7 MiB. The exact 2x PMF
timing and fixed per-call dispatch CFG support O(source + emitted IR) behavior.

## Completed Checkpoints

| Checkpoint | Result | Validation |
| --- | --- | --- |
| Typed object/driver/link boundary | 0 -> 49/88; direct source/object/mixed links, options, lifecycle aggregation, strong/weak rejection, ELF64 helper import, and focused i128 operations implemented | 10/10 focused boundary tests; helper group 7/11; through PA29 4040/4040; file audit passes |
| Native exception regions and constructor unwind | 49 -> 62/88; role-indexed EH runtime, typed/catch-all dispatch, by-value copy lifetime, nested rethrow, and prefix subobject cleanup implemented | PA13 catch/end/cleanup exits 7/3/14; focused PA30 EH/constructor cases pass; through PA29 4040/4040; file audit passes |
| Polymorphic support closure and link ownership | 62 -> 72/88; typed ABI runtime roles, coalescible special entries, all-view key ownership, VTT retention, RTTI casts, and multibase return/vptr layout implemented | 9/9 focused cases; PA30 72/88; through PA29 4040/4040; file audit passes |
| Numeric values, calls, and native allocation | 72 -> 79/88; call-live and wide-parameter homes, x87-compatible f64/f80 handling, i128 values, discarded f80 results, and typed NaN builtins implemented | 7/7 focused; PA30 79/88; through PA29 4040/4040; file audit passes; 30/60 compile scaling measured |
| Scoped semantic regions and ordered EH | 79 -> 86/88, audited at 88/90 with two passing regressions; statement-expression values/reachability, typed ordinary/constructor/destructor function-try roles, inherited local-friend access, and ordered derived/catch-all dispatch implemented | 9/9 focused; PA30 baseline preserved with only the two aggregate failures; through PA29 4040/4040; file audit passes; 512/1,024/2,048 region scaling measured |
| Aggregate value initialization and member-pointer ABI | 88 -> 90/90; explicitly defaulted non-user-provided constructors retain zero-initialization, virtual PMFs encode canonical slots plus `this` adjustment, dispatch through the adjusted vptr, and direct targets remain untagged via aligned entries | 2/2 focused plus 5 PA17/PA27 controls; PA30 90/90; through PA29 4040/4040; file audit passes; 20/40 PMF scaling measured |

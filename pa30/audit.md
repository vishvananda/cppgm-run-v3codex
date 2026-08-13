# PA30 Checkpoint Audit

## Current Checkpoint Review

The scoped-semantic-regions checkpoint (`7e02cff3`) is closed after audit.
The review followed statement expressions, ordinary/constructor/destructor
function-try-blocks, ordered handlers, and local-class access from structured
PA10 syntax through canonical semantic facts, typed PA15 LowIR, MIR, and direct
ELF emission. Syntax and retained template bodies remain arena-owned and parsed
once; lowering consumes node, type, binding, handler, and body-role identities
without rendering or reparsing text. No host/reference invocation, fixture
dispatch, filename/source shortcut, global retry, or name-based lowering
fallback was found in the affected path.

Three checkpoint-local findings were fixed. Destructor function-try syntax was
retained in `FunctionInfo` but omitted when demanded destructor semantics were
emitted. Function-try regions now carry an explicit ordinary, constructor, or
destructor body role; the semantic owner builds the region for constructors and
destructors, and lowering selects the matching typed lifecycle path. Reaching a
constructor or destructor handler still rethrows through the recorded role.
Statement expressions previously suspended the iterative statement scheduler
by swapping a fresh vector around every prefix statement, and an unconditional
enclosing return could leave its caller attempting to emit the unreachable
result store. Nested scheduler frames now share one geometric task vector with
a size boundary, preserve reachable labels, and stop unreachable result/store
lowering. The PA30 region owners also absorb the new shell/scheduler logic, so
the two implementation files exposed by the fresh file audit remain below
their limits.

Representative 512/1,024/2,048-prefix statement expressions record exactly
512/1,024/2,048 nested scheduler entries, 523/1,035/2,059 processed tasks, and
a peak of two retained tasks. LowIR grows 47,210/96,362/194,666 bytes and is
byte-identical to the landed checkpoint at every scale. Five-run medians for
20 compilations improve from 0.21/0.34/0.58 seconds to
0.20/0.32/0.57 seconds; peak RSS remains comparable at about
7.7/8.4/10.1 MiB. The counters and output slope demonstrate linear work while
the ownership change removes the former per-prefix task-vector allocation.

Focused validation is 9/9, including new destructor-function-try and
unreachable-statement-expression regressions. The PA30 report is 88/90; the
same trivial-union value-initialization and multiple-inheritance virtual
member-pointer cases remain for the already-planned aggregate checkpoint.
Through PA29 remains 4,040/4,040. The PA30 file audit and `git diff --check`
pass. No relevant correctness, architecture, performance, shortcut, timeout,
or fatal file-audit issue remains in the landed scoped-region ownership path.

## Checkpoint Audit Ledger

| Checkpoint | Audit result |
|---|---|
| Scoped semantic regions and ordered EH (`7e02cff3`) | Pass after typed destructor-region ownership, bounded nested scheduling, abrupt-result reachability, focused regressions, scaling evidence, and required gates. |

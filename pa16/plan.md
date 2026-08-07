# PA16 Plan

## Stage Design and Spec Alignment

PA16 extends the shared compiler in place: PA10 publishes scoped interned syntax
facts; PA11 owns canonical entities, types, bindings, and ABI identity; PA12
resolves lookup, access, declarators, conversions, initialization, lifetime, and
demand; PA15/PA16 lowering consumes those typed facts without repeating lookup.
This matches `spec.md` sections 2, 3, 5, 6, 8, and 9. Composed/trailing-return
types, exception compatibility, builtin kind, selected calls, and integer-
narrowing conversions cross boundaries as compact facts. ABI identity/effects
derive from the selected binding or bounded enum tables; no spelling-keyed
lowering, textual reconstruction, whole-program retry, or external tool enters
the path.

## Current Failure Map

Current state is **249/286 PA16 tests**: every original 246/283 pass plus three
audit regressions, with PA1-PA15 at **1,145/1,145**. The unchanged 37-test
remainder is assigned once by primary owner:
11 aggregate/value-initialization/temporary-materialization cases; 13 friend,
access, ADL, and inherited-overload cases; 11 member-expression, destructor,
declarator, or parser cases; and 2 conversion-constraint cases.

## Active Checkpoint

**Aggregate/value-initialization and temporary materialization closure (11
tests).** Apply `spec.md` sections 5, 6, and 8: semantic initialization owns the
destination identity, ordered subobject actions, omitted-member value-init, and
temporary lifetime; lowering only replays typed actions. Data flow is `braces/new/
call syntax -> typed initialization target -> ordered base/member/array actions ->
materialized storage and cleanup -> LowIR`. Expected work is O(E + A), for
initializer elements E and emitted actions A, with one monotonic traversal and no
aggregate-prefix replay. Validate the owned failures, 1x/2x nested aggregate
probes, full PA16, through-PA15, and file audit.

## Performance Evidence

| Boundary | Representative 1x / 2x evidence |
|---|---|
| Bit-field layout | 512/1,024 fields: 512/1,024 visits; 1.166/2.446 ms median semantic time |
| Inherited constructors | 64/128 overloads: 582/1,158 signature lookups; 67/131 access checks |
| Members/calls/initialization | 5k/10k fields: 5k/10k visits; 1,001/2,001 candidates: 2,006/4,006 checks |
| Inheritance/lifetime | 250/500 base edges: 250/500 actions; 1k/2k namespace objects: 1k/2k actions |
| Operator/access indexes | Dense 128/256 ADL: 258/514 candidates; sparse 512/1,024 friends: 2/2 candidates |
| Composed declarators/calls | 32/64 methods: 1,318/2,598 tokens, 1,654/3,254 syntax nodes, 104/200 fact changes, 291/579 conversions, 261/517 access checks, 112,179/222,099 typed bytes, 455/903 instructions; 0.521/0.986 ms parse, 0.908/1.678 ms semantic, 0.532/1.040 ms lowering medians |

The deterministic counters establish proportional work at each scaling-sensitive
owner; current composed-declarator work, storage, and output ratios are
1.89-1.99 for 2x input.

## Completed Checkpoints

| Checkpoint | Closure evidence |
|---|---|
| Direct-member object spine | Canonical layout/projection; 42/247; linear field/use curves |
| Resolved member-call spine | Object-aware ranking, hidden `this`, stable ABI IDs; 51/247 |
| Local aggregate-action spine | C++11 aggregate/union rules and bounded projections; 61/248 |
| Special-member initialization spine | Ordered typed init actions and exception/reference facts; 91/255 |
| Single-base construction spine | Canonical base/access edges and retained projections; 120/259 |
| Destruction and lexical cleanup | Reverse lifetime, lexical exits, shared cleanup suffixes; 132/265 |
| Namespace/static lifetime | TLS/linkage facts and one ordered lifecycle pair; 154/269 |
| Operator/ADL callable spine | Indexed ordinary/hidden-friend union and typed ranking; 186/269 |
| Access/base-path closure | Indexed grants/signatures and object-correct protected access; 202/275 |
| Layout/bit-field/inherited-ctor closure | Focused 29/29; 231/283; prior 1,145/1,145; audit pass |
| Typed declarator/call/boundary closure | Pass after audit fixes: interned scoped parameter facts, exception/builtin ABI ownership, and retained narrowing conversions; focused 15/15; **249/286** with original 246/283 preserved; prior 1,145/1,145; file audit passes |

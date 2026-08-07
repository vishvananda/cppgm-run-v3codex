# PA16 Plan

## Stage Design and Spec Alignment

PA16 extends the shared compiler in place: PA10 publishes scoped syntax facts;
PA11 owns canonical entities, types, bindings, and ABI identity; PA12 resolves
lookup, access, declarators, conversions, initialization, lifetime, and demand;
PA15/PA16 lowering consumes those typed facts without repeating lookup. This
matches `spec.md` sections 2, 3, 5, 6, 8, and 9. Function bindings now retain
composed/trailing-return shape and builtin kind, resolved calls retain converted
arguments, and LowIR boundaries derive identity/effects from the selected binding.
Work is indexed by canonical IDs or bounded enum tables; no textual reconstruction,
whole-program retry, or lowering-time overload search is introduced.

## Current Failure Map

Current state is **246/283 PA16 tests**, up 15 from 231/283, with PA1-PA15 at
**1,145/1,145**. The 37-test remainder is assigned once by primary owner:
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
| Composed declarators | 32/64 declarations: 1,009/2,001 tokens, 1,395/2,771 syntax nodes, 68/132 signature lookups, 128/256 access checks, 21,958/42,150 semantic bytes, 0.630/1.044 ms analysis |

The deterministic counters establish proportional work at each scaling-sensitive
owner; the composed-declarator 2x ratios are 1.94-2.00 for owned operations and
1.92 for semantic storage.

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
| Typed declarator/call/boundary closure | Focused 12/12; trailing/access/shadowing, canonical ABI identity, builtin effects, and immediate conversion policy; **246/283 (+15)**; prior 1,145/1,145; audit pass |

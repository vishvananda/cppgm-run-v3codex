# PA16 Plan

## Stage Design and Spec Alignment

PA16 extends the shared compiler in place: PA10 publishes syntax, PA11 owns
canonical entities/types/bindings, PA12 resolves lookup, access, conversions,
initialization, lifetime, and demanded functions, and PA15/PA16 lowering consumes
typed IDs and actions without repeating lookup. This follows `spec.md` sections
2, 3, 5, 6, 8, and 9: class entities own completed layout and special-member
facts; bindings own identity, access, offsets, bit slices, and ABI entry flavor;
resolved nodes retain selected bindings, value categories, conversions, and
subobject paths. Indexes and generation marks bound work to relevant entities,
members, grants, and candidates.

The audited layout slice carries pack directives and `alignas` as typed syntax
facts into one monotonic class-completion pass. That pass records natural,
requested, and effective alignment, direct-base/member offsets, and separate
declared-width, value-width, and storage-slice bit-field facts. Per-object
initialization retains only the current storage identity, and inherited
constructors use indexed derived signatures, source access ownership, and
distinct typed C1/C2 bindings. No lowering lookup, textual reconstruction,
per-unit hash allocation, or candidate-vector retry remains on this path.

## Current Failure Map

Current state is **231/283 PA16 tests**: every original 223/275 pass plus eight
audit regressions, with PA1-PA15 at **1,145/1,145**. The unchanged 52-test
remainder is assigned once by primary owner: 13 procedural cast/function-boundary
metadata; 14 aggregate, value-initialization, and temporary-lifetime cases; 12
call/declarator and parameter-type metadata cases; 10 lookup-shaped cross-feature
cases blocked in call conversion, parsing, or presentation; and 3 residual
operator cases blocked by materialization or user-defined conversion.

## Active Checkpoint

**Typed call/declarator and function-boundary metadata closure (12 tests).** Apply
`spec.md` sections 2, 3, 5, 6, 8, and 9 at the declarator-to-call boundary.
Canonical function/parameter bindings own nested declarator shape, reference and
alias facts, `noexcept`, static/member flavor, and return conversion; resolved
calls retain the selected function and explicit ABI argument facts. Data flow is
`declarator syntax -> canonical function/parameter type -> overload conversion ->
resolved call/return -> typed LowIR metadata`. Expected work is O(D + C*A), for
declarator nodes D, eligible candidates C, and arguments A, with indexed signature
lookup. Validate all 12 owned failures, 1x/2x nested-parameter and candidate
counters, full PA16, through-PA15, and file audit.

## Performance Evidence

| Boundary | Representative 1x / 2x evidence |
|---|---|
| Bit-field layout | 512/1,024 fields: 512/1,024 member visits, 1.166/2.446 ms median semantic time, and constant 3-instruction output |
| Inherited constructors | 64/128 overloads: 582/1,158 indexed signature lookups, 67/131 access checks, 0.861/1.596 ms median semantic time, and constant 3-instruction output |
| Members/calls/initialization | 5k/10k fields: 5,000/10,000 visits; 1,001/2,001 candidates: 2,006/4,006 conversion checks; 1k/2k aggregate actions: 1,000/2,000 visits |
| Inheritance/lifetime | 250/500 base edges: 250/500 actions and 500/1,000 edge visits; 1k/2k namespace objects: 1,000/2,000 actions and 4,016/8,016 instructions |
| Operator/access indexes | Dense 128/256 ADL classes: 258/514 candidates; sparse 512/1,024 unrelated friends: 2/2 candidates; 50/100 friend classes: 250/500 grant probes |

Exact counters establish proportional work at each scaling-sensitive owner; the
new layout and inherited-constructor probes also keep emitted LowIR constant.

## Completed Checkpoints

| Checkpoint | Closure evidence |
|---|---|
| Direct-member object spine | Canonical member layout/projection; 42/247; linear field/use curves |
| Resolved member-call spine | Object-aware ranking, hidden `this`, stable ABI IDs; 51/247; linear candidates |
| Local aggregate-action spine | C++11 aggregate/union rules and bounded typed projections; 61/248 |
| Special-member initialization spine | Ordered typed init actions and exception/reference facts; 91/255 |
| Single-base construction spine | Canonical base/access edges and retained projections; 120/259 |
| Destruction and lexical cleanup | Reverse lifetime, lexical exits, shared cleanup suffixes; 132/265 |
| Namespace/static lifetime | TLS/linkage/static facts and one ordered lifecycle pair; 154/269 |
| Operator/ADL callable spine | Indexed ordinary/hidden-friend union and typed ranking; 186/269 |
| Access/base-path closure | Indexed grants/signatures and object-correct protected access; 202/275 |
| Layout/bit-field/inherited-ctor closure | Pass after audit fixes: physical/value-width split, scalar per-object init identity, indexed inheritance/access, and C2 demand; focused 29/29; **231/283** with original 223/275 preserved; prior 1,145/1,145; file audit passes |

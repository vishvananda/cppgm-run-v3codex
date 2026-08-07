# PA16 Plan

## Stage Design and Spec Alignment

PA16 extends the shared compiler in place: PA10 publishes scoped interned syntax
facts; PA11 owns canonical entities, types, bindings, and ABI identity; PA12
resolves lookup, access, declarators, conversions, initialization, lifetime, and
demand; PA15/PA16 lowering consumes those typed facts without repeating lookup.
This matches `spec.md` sections 2, 3, 5, 6, 8, and 9. Composed/trailing-return
types, exception compatibility, builtin kind, selected calls, and integer-
narrowing conversions cross boundaries as compact facts. Aggregate construction
helpers likewise have canonical typed identities indexed by object and boundary
type; they are lowering records, never C++ declarations or lookup candidates,
and each is registered and emitted once. Literal decoding has one shared typed
owner. ABI identity/effects derive from selected bindings or bounded enum
tables; no spelling-keyed lowering, textual reconstruction, whole-program retry,
or external tool enters the path.

## Current Failure Map

PA16 is **271/288** after closing the 9-test friend/ADL boundary; PA1-PA15
remain **1,145/1,145**. The 17 remaining failures are assigned once by primary
owner after tracing their semantic graphs and canonical LowIR diffs:

- Constructor/base/layout/static-overload edges (4): `friend-derived-private-base-defaulted-constructor`, `friend-intermediate-derived-protected-base-method`, `callable-field-hides-private-base-method`, `using-base-static-same-signature-derived-preferred`.
- Member/destructor/declarator/parser (11): `mutable-member-const-method`, `nested-out-of-class-constructor-enclosing-type`, `reference-indexed-pointer-member-access`, `static-thread-local-member-object-call`, `const-pointer-explicit-destructor-call`, `explicit-destructor-call-enclosing-namespace-type`, `late-member-subscript-shadows-type`, `member-function-pointer-field-call`, `reference-member-same-name-as-class`, `scalar-pseudo-destructor-call`, `decltype-qualified-nested-type-local`.
- Conversion constraints (2): `string-literal-does-not-convert-to-mutable-void-pointer`, `list-init-narrowing-bad`.

## Active Checkpoint

**Next: constructor/base/layout/static-overload closure (4 tests).** Apply
`spec.md` sections 2, 3, 5, 6, and 9: canonical class records own defaulted
base construction demand, zero-offset base projections, empty-base layout, and
derived-versus-imported member preference. The owner/data flow is `class
declarations and syntax -> canonical base/layout and function indexes ->
selected constructor/member facts -> lowering`. Work is O(M + B + C), for
members M, traversed base edges B, and bounded candidates C, with no semantic
retry. Validate the 4 owned failures, representative 1x/2x base/member cases,
full PA16, through-PA15, and file audit.

## Performance Evidence

| Boundary | Representative 1x / 2x evidence |
|---|---|
| Bit-field layout | 512/1,024 fields: 512/1,024 visits; 1.166/2.446 ms median semantic time |
| Inherited constructors | 64/128 overloads: 582/1,158 signature lookups; 67/131 access checks |
| Members/calls/initialization | 5k/10k fields: 5k/10k visits; 1,001/2,001 candidates: 2,006/4,006 checks |
| Inheritance/lifetime | 250/500 base edges: 250/500 actions; 1k/2k namespace objects: 1k/2k actions |
| Operator/access indexes | Dense 128/256 ADL: 258/514 candidates; sparse 512/1,024 friends: 2/2 candidates |
| Composed declarators/calls | 32/64 methods: 1,318/2,598 tokens, 1,654/3,254 syntax nodes, 104/200 fact changes, 291/579 conversions, 261/517 access checks, 112,179/222,099 typed bytes, 455/903 instructions; 0.521/0.986 ms parse, 0.908/1.678 ms semantic, 0.532/1.040 ms lowering medians |
| Nested aggregate initialization | 32/64 members: 333/653 semantic nodes, 35/67 layout visits, 65/129 conversions, 328/648 instructions, 63,078/124,518 typed bytes; 0.291/0.422 ms semantic and 0.165/0.288 ms lowering medians |
| Aggregate helper demand/deduplication | 64/128 explicit elements: one helper definition and 6/6 signature lookups; 398/782 semantic nodes, 460/908 edges, 148/276 instructions, 43,120/82,288 typed bytes; 0.224/0.374 ms semantic and 0.102/0.141 ms lowering medians |
| Friend/ADL converting-call boundary | 64/128 target constructors: 68/132 candidates, 276/532 conversions, 396/780 signature lookups, 206/398 access checks; output stays at 23 instructions and 9,582 typed bytes; 1.134/1.949 ms median semantic time |

The deterministic counters establish proportional work at each scaling-sensitive
owner, generally 1.86-1.99x for 2x input; helper identity and selected-call
output stay constant where the added declarations are nonviable.

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
| Aggregate/value-init/materialization closure | Pass after audit fixes: typed lowering-only helper identities, shared literal decoding, complete omitted-member actions, and ordinary demand ownership; all 11 landed gains plus 2/2 audit regressions; **262/288** with the original **260/286** preserved; proportional 1x/2x probes; prior 1,145/1,145; file audit passes |
| Friend/ADL call-boundary closure | Canonical anonymous-namespace emission identity, demand-indexed hidden friends, bounded converting-constructor selection, and typed argument staging/base projection; focused 9/9; **271/288** (+9); prior 1,145/1,145; audit pass; proportional 64/128 probe |

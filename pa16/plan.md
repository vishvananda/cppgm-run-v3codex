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

The checkpoint's original suite remains **260/286 PA16 tests**; two audit
regressions pass for a current report of **262/288**. PA1-PA15 remain
**1,145/1,145**. The same 26 original failures are assigned once by primary
owner:

- Friend/access/ADL/inherited overload (13): `friend-derived-private-base-defaulted-constructor`, `friend-function-member-access`, `friend-intermediate-derived-protected-base-method`, `qualified-friend-function-member-access`, `unnamed-namespace-hidden-friend-single-definition`, `adl-associated-namespace-does-not-climb-parents`, `adl-using-declaration-source-point`, `callable-field-hides-private-base-method`, `friend-function-definition-skip`, `hidden-friend-definition-adl-call`, `hidden-friend-operator-nullptr-compare`, `prvalue-derived-base-friend-operator`, `using-base-static-same-signature-derived-preferred`.
- Member/destructor/declarator/parser (11): `mutable-member-const-method`, `nested-out-of-class-constructor-enclosing-type`, `reference-indexed-pointer-member-access`, `static-thread-local-member-object-call`, `const-pointer-explicit-destructor-call`, `explicit-destructor-call-enclosing-namespace-type`, `late-member-subscript-shadows-type`, `member-function-pointer-field-call`, `reference-member-same-name-as-class`, `scalar-pseudo-destructor-call`, `decltype-qualified-nested-type-local`.
- Conversion constraints (2): `string-literal-does-not-convert-to-mutable-void-pointer`, `list-init-narrowing-bad`.

## Active Checkpoint

**Next: friend/access/ADL/inherited-overload closure (13 tests).** Apply
`spec.md` sections 5 and 6: declarations publish canonical friendship, using,
and base/access edges; semantic lookup unions indexed ordinary and associated
candidates, then performs object-aware access and overload ranking once. Data
flow is `declaration facts -> scope/entity indexes -> candidate IDs -> access and
conversion facts -> selected binding -> lowering`. Expected work is O(C + B)
per lookup, for candidates C and traversed base edges B, with no namespace-wide
rescans. Validate the 13 owned failures, sparse/dense 1x/2x lookup probes, full
PA16, through-PA15, and file audit.

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

The deterministic counters establish proportional work at each scaling-sensitive
owner. Current aggregate work, storage, and output ratios are 1.86-1.99 for 2x
input, while helper identity and declaration-index work stay constant.

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

# PA16 Plan

## Stage Design and Spec Alignment

PA16 extends the shared compiler in place: PA10 publishes scoped interned syntax
facts; PA11 owns canonical entities, types, bindings, and ABI identity; PA12
resolves lookup, access, declarators, conversions, initialization, lifetime, and
demand; PA15/PA16 lowering consumes those typed facts without repeating lookup.
This matches `spec.md` sections 2, 3, 5, 6, 8, and 9. Composed/trailing-return
types, exception compatibility, builtin kind, selected calls, and integer-
narrowing conversions cross boundaries as compact facts. Selected calls retain
their standard/user-defined conversion rank and converting-constructor binding;
a selection-local flat cache has the complete argument-ordinal/target-type key,
and lowering sees only typed materialization and projection records. Aggregate
helpers have canonical typed identities and one emission owner, while literal,
scope/emission-path, and call-argument lowering each have one bounded owner. ABI
effects derive from binding/type identity and bounded enum tables; no spelling-
keyed lowering, textual reconstruction, whole-program retry, or external tool
enters the path. Completed class records also retain canonical empty-layout
facts. PA12 separates nested same-type zero-offset subobjects through direct
`EntityId` edges with generation-marked, phase-local traversal state; semantic
base distance remains distinct from the selected one-step physical LowIR
projection.

## Current Failure Map

PA16 is **276/291**, preserving the **275/290** turn-start baseline and adding
one audit regression; PA1-PA15 are **1,145/1,145**. The remaining 15-test
failure set is assigned by primary owner after inspecting source diagnostics
and canonical LowIR diffs:

- Constructor access/demand (1): `friend-derived-private-base-defaulted-constructor`.
- Member candidate/name selection (2): `using-base-static-same-signature-derived-preferred`, `late-member-subscript-shadows-type`.
- Declarator and qualified-type construction (4): `nested-out-of-class-constructor-enclosing-type`, `member-function-pointer-field-call`, `reference-member-same-name-as-class`, `decltype-qualified-nested-type-local`.
- Cv/member-object and explicit-lifetime expressions (6): `mutable-member-const-method`, `reference-indexed-pointer-member-access`, `static-thread-local-member-object-call`, `const-pointer-explicit-destructor-call`, `explicit-destructor-call-enclosing-namespace-type`, `scalar-pseudo-destructor-call`.
- Conversion constraints (2): `string-literal-does-not-convert-to-mutable-void-pointer`, `list-init-narrowing-bad`.

## Active Checkpoint

**Next: declarator and qualified-type construction closure (4 tests).** Apply
`spec.md` sections 1-3, 6, and 9: parser-produced declarator structure and
canonical scoped type identities must survive into declaration/member facts;
lowering consumes those facts without reparsing qualified spellings. Ownership
and flow are `PA10 declarator nodes -> PA12 scoped type/declaration facts ->
typed call/storage lowering`. Expected work is O(D + Q) for declarator nodes D
and qualified lookup edges Q, with O(1)-average canonical identity probes and
no whole-scope scan. Validate the four owned failures, neighboring nested-type
and function-pointer fixtures, 1x/2x declarator probes, full PA16,
through-PA15, and file audit.

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
| Friend/ADL converting-call boundary | Adversarial 32/64 overloads plus shared target constructors: 64/128 candidates, 162/322 conversions, 31/63 cache hits, 32/64 misses, 129/257 access checks, 104/200 instructions, 69,590/136,790 typed bytes; 1.017/1.980 ms median semantic time |
| Physical single-base layout/projection | 256/512 members over an empty base: 256/512 layout visits and 0.557/1.028 ms median semantic time. A 64/128-edge chain with a nested zero-offset collision: 67/131 layouts, 67/131 collision visits, 394/778 path visits, 0.701/1.317 ms semantic medians, and one projection/11 LowIR instructions at both sizes |

The deterministic counters establish proportional work at each scaling-sensitive
owner. The audited nested-collision path is 1.96x in zero-offset visits and
1.97x in base-path visits for 2x depth while physical output stays constant.
The shared-target call path remains 1.98-2.00x in work counters, replacing the
pre-audit Cartesian constructor rescans.

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
| Friend/ADL call-boundary closure | Pass after audit fixes: canonical internal ABI identity, indexed friendship/ADL, retained conversion/constructor facts, complete-key local cache, and typed argument lowering; focused 11/11; **273/290** with the original **271/288** preserved and the same 17 failures; prior 1,145/1,145; file audit and proportional probe pass |
| Physical single-base layout/projection closure | Pass after audit fixes: canonical empty-layout facts, identity-indexed nested zero-offset separation, retained semantic distance, and one typed physical projection; focused 7/7; **276/291** with **275/290** preserved; prior 1,145/1,145; file audit and proportional probes pass |

# PA16 Audit

## Current Checkpoint Review

**Checkpoint:** `f77bcbf8` (layout, bit-field, and inherited-constructor
closure).

**Result:** Pass after audit fixes. Typed pack/alignment events and member/base
IDs feed one monotonic class-layout fact containing natural/requested/effective
alignment, offsets, declared bit widths, value widths, and storage slices.
Resolved field actions consume those slices directly. Inherited constructors
are keyed by the derived signature, retain the source constructor's access
owner, and forward to a distinct canonical base-subobject entry.

The complete affected path is closed:

1. Bit-field lowering had conflated promoted expression type with physical
   storage width, zero-extended signed values, returned positioned assignment
   bits, and used a per-unit hash whose key aliased repeated subobjects. Reads
   and writes now use the canonical storage width, signed fields normalize by
   their value width, assignment returns the unpositioned stored value, and
   per-object initialization uses constant scalar state and one projected
   destination. Constructor aggregate leaves use the same typed helper.
2. Layout rejected legal over-width fields, let an alignment-only unnamed
   zero-width separator strengthen the containing class, and accepted
   `alignas` on bit-fields. Declared allocation width is now separate from the
   representable value width, zero-width separators affect only the next
   offset, and forbidden bit-field alignment is rejected.
3. Constructor inheritance could replace a same-signature derived constructor,
   re-anchor private/protected access on the derived class, hide transitive
   inherited constructors as aliases, and scan the growing derived candidate
   vector. An indexed signature precheck suppresses collisions, source access
   ownership survives transitively, self-owned inherited bindings remain in
   the callable index, and each new candidate is appended once. All base
   initialization, inherited or ordinary, selects the C2 entry before demand.

Validation is 29/29 focused cases: the 21 landed cases plus eight audit
regressions. The required full-stage report is 231/283, preserving every one of
the original 223/275 passes and leaving the same 52 pre-existing failures;
PA1-PA15 remain 1,145/1,145. File audit passes with four warnings: the three
pre-existing shared-header warnings and the reviewed cohesive CRTP lowering
header. Signed-field fixtures were corrected to the README's explicit
negative-value contract.

Five-run 512/1,024-field probes record 512/1,024 layout visits, 1.166/2.446 ms
median semantic time, and constant three-instruction output. Five-run 64/128
inherited-overload probes record 582/1,158 indexed signature lookups, 67/131
access checks, 0.861/1.596 ms medians, and constant three-instruction output.

## Checkpoint Audit Ledger

| Checkpoint | Result | Closure evidence |
|---|---|---|
| Direct-member object spine | Pass | Canonical layout/member facts, typed fields, linear curves, gates preserved |
| Local aggregate-action spine | Pass | Aggregate/union rules, borrowed cursor, bounded projections, gates preserved |
| Special-member initialization spine | Pass | Mode-correct selection, canonical actions, typed subobjects, linear curves |
| Single-base construction spine | Pass | Naming-class access, selected base actions, recorded projections, linear edges |
| Destruction and lexical-cleanup spine | Pass | Reverse lifetime, lexical exits, shared EH suffixes, linear cleanup curves |
| Namespace/static lifetime spine | Pass | Independent TLS/linkage, sparse identity, one lifecycle pair, linear curves |
| Operator/ADL callable spine | Pass after audit fixes | Typed operator/null/UDL facts, direct ordinary/hidden-friend indexes, 186/269 with no losses, dense-linear and sparse-constant candidate evidence, gates preserved |
| Access/base-path closure | Pass after audit fixes | Exact/canonical lookup identities, indexed grants/signatures, object-correct protected access, retained projections; 202/275 with no losses; linear counters; gates preserved |
| Layout/bit-field/inherited-ctor closure | Pass after audit fixes | Physical/value-width split, per-object scalar init state, indexed signature/access ownership, C2 demand; focused 29/29; original 223/275 preserved; linear probes; gates preserved |

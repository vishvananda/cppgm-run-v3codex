# PA16 Plan

## Stage Design and Spec Alignment

PA16 extends the shared PA11/PA12 graph and PA15 typed LowIR path in place:
`class syntax -> canonical entity/binding facts -> resolved object expressions ->
typed object/address LowIR`. Class entities own completion and layout; member and
function binding IDs own offsets, static category, signatures, and object class.
Resolved expressions retain selected bindings and conversions, and lowering
consumes those facts without name lookup, semantic reconstruction, or text
transport. This follows `spec.md` sections 2, 3, 6, 8, and 9: canonical identity,
indexed candidate sets, one selected-call fact, typed lowering, bounded temporary
storage, and work proportional to relevant members/candidates.

## Current Failure Map

Current state is 51/247 PA16 tests, up from the 42/247 turn baseline. The complete
196-failure set, assigned once by primary semantic owner, is: 22 member-function/
call-boundary; 38 class lookup/access/inheritance; 72 initialization/lifetime;
42 operator/ADL/callable; 10 layout/object representation; and 12 procedural
interaction/metadata failures. By result, 173 are expected-success exits, 3 are
missing rejections, and 20 are LowIR differences.

## Active Checkpoint

**Class initialization action spine.** PA12 will classify aggregate/default/direct
class initialization once, select constructors from the class-owned overload set,
and materialize an ordered typed action list for bases, fields, default member
initializers, and explicit initializers. PA15 will consume object addresses,
canonical offsets, and selected constructor IDs directly; helper demand remains a
separate monotonic fact. This applies `spec.md` sections 2, 3, 4, 6, 8, and 9.

Ownership/data flow is `EntityId layout + constructor/member BindingIds -> resolved
initializer actions -> typed stores/calls`. Aggregate planning is O(M + E) for M
members and E initializer elements; constructor selection is O(C * (A + 1)); each
action lowers once. Validate scalar/class default member initializers, aggregate
brace elision, direct/default construction and override precedence, then PA16,
through-PA15, file audit, and doubled member/element curves.

## Performance Evidence

| Workload | Scale | Evidence |
|---|---:|---|
| Class layout | 5k / 10k fields | 5,000 / 10,000 visits; 7.91 / 16.49 ms semantic |
| Repeated field use | 5k / 10k uses | 5,003 / 10,003 probes; 19.87 / 39.26 ms semantic; 10.73 / 23.48 ms lowering |
| Member overload selection | 1,001 / 2,001 candidates | 2,000 / 4,000 order comparisons; 2,006 / 4,006 conversion checks; 6.57 / 13.07 ms semantic; 8 instructions at both sizes |

Exact counter doubling and near-2x semantic time establish work proportional to
the indexed candidate set; unchanged lowering size shows no class-wide rescan.

## Completed Checkpoints

| Checkpoint | Final state | Evidence |
|---|---|---|
| Direct-member object spine | Complete | Canonical size/alignment/offset and default-construction facts; typed `.`/`->` fields; duplicate rejection; 42/247 PA16 and 1,145/1,145 through PA15; linear field/use curves; audit pass |
| Resolved member-call spine | Complete | Canonical method/access/cv facts, implicit-object ranking, out-of-class hidden `this`, stable overload/ABI IDs, typed calls, and LowIR counters; 9 product tests gained to 51/247 with no losses; 1,145/1,145 through PA15; linear candidate curve; audit pass |

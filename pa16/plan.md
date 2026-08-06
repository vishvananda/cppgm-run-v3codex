# PA16 Plan

## Stage Design and Spec Alignment

PA16 extends the shared PA11/PA12 graph and PA15 typed LowIR path in place:
`class syntax -> canonical entity/binding facts -> resolved object expressions and
initialization actions -> typed object/address LowIR`. Class entities own
completion, layout, and separate user-declared/user-provided constructor facts;
member/function bindings own offsets, static category, signatures, and object
class. Resolved nodes retain selected bindings and conversions, and lowering
consumes them without name lookup, semantic reconstruction, or text transport.

This follows `spec.md` sections 2, 3, 6, 8, and 9. Aggregate planning walks the
borrowed syntax-edge sequence and canonical member index once. Local lowering
keeps shallow paths inline and retains typed subobject addresses after bounded
depth, so action work is linear while existing shallow presentation remains
stable. The semantic graph remains translation-unit-owned and synchronously
borrowed by typed lowering.

## Current Failure Map

Current state is 61/248 PA16 tests, up from the 58/247 turn baseline with one
passing audit regression. The complete 187-failure set, assigned once by primary
semantic owner, is: 22 member-function/call-boundary; 38 class lookup/access/
inheritance; 63 initialization/lifetime; 42 operator/ADL/callable; 10 layout/
object representation; and 12 procedural interaction/metadata failures. By
result, 164 are expected-success exits, 4 are missing rejections, and 19 are
LowIR differences.

## Next Substantial Checkpoint

**Special-member initialization action spine.** PA12 will give constructors stable
`BindingId` identity, select one constructor at each direct/copy/list/default-init
site, and order explicit/default member actions by canonical subobject identity.
Demand-owned helper emission remains keyed by entity/binding; PA15 consumes only
selected calls and member actions, with no name lookup or constructor reselection.
This applies `spec.md` sections 2, 3, 6, 8, and 9.

Ownership/data flow is `EntityId special-member facts + constructor/member
BindingIds -> resolved initialization actions -> typed calls/stores`. Selection is
O(C + E), for C indexed candidates and E explicit expressions; action planning is
O(M) for M initialized subobjects, demand deduplication is expected O(1), and
lowering is O(A). Validate direct/default/list initialization, default member
initializers and explicit overrides, nested class subobjects, overload/default-
argument selection, PA16, through-PA15, file audit, and doubled candidate/member
curves.

## Performance Evidence

| Workload | Scale | Evidence |
|---|---:|---|
| Class layout | 5k / 10k fields | 5,000 / 10,000 visits; 7.91 / 16.49 ms semantic |
| Repeated field use | 5k / 10k uses | 5,003 / 10,003 probes; 19.87 / 39.26 ms semantic; 10.73 / 23.48 ms lowering |
| Member overload selection | 1,001 / 2,001 candidates | 2,000 / 4,000 order comparisons; 2,006 / 4,006 conversion checks; 6.57 / 13.07 ms semantic; 8 instructions at both sizes |
| Local aggregate actions | 1k / 2k members | 2,009 / 4,009 semantic nodes; 2,006 / 4,006 lowered nodes; 3,005 / 6,005 instructions; 1.96 / 3.93 ms semantic; 1.08 / 2.06 ms lowering |
| Nested aggregate actions | 100x100 / 200x200 depth/leaves | After audit: 408 / 808 semantic nodes; 303 / 603 instructions; 63,066 / 124,506 typed bytes; pre-fix instructions were 10,302 / 40,602 |

Exact counter scaling establishes work proportional to owned fields, indexed
candidates, resolved actions, and emitted IR; the nested audit curve closes the
former depth-by-leaf projection product.

## Completed Checkpoints

| Checkpoint | Final state | Evidence |
|---|---|---|
| Direct-member object spine | Complete | Canonical size/alignment/offset and default-construction facts; typed `.`/`->` fields; duplicate rejection; 42/247 PA16 and 1,145/1,145 through PA15; linear field/use curves; audit pass |
| Resolved member-call spine | Complete | Canonical method/access/cv facts, implicit-object ranking, out-of-class hidden `this`, stable overload/ABI IDs, typed calls, and LowIR counters; 9 product tests gained to 51/247 with no losses; 1,145/1,145 through PA15; linear candidate curve; audit pass |
| Local aggregate-action spine | Pass after audit fixes | C++11 aggregate eligibility, one union active member, member-ID action trees, borrowed brace cursor, typed scalar/reference stores, and bounded projection reuse; 61/248 PA16 with no losses; 1,145/1,145 through PA15; flat and nested linear curves; file audit pass |

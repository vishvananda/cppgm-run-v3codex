# PA16 Plan

## Stage Design and Spec Alignment

PA16 extends the shared PA11/PA12 graph and PA15 typed LowIR path in place:
`class syntax -> canonical entity/binding facts -> resolved object expressions and
initialization actions -> typed object/address LowIR`. Class entities own
completion, layout, and separate user-declared/user-provided constructor facts;
member/function bindings own declaration ordinals, offsets, static category,
signatures, and object class. Resolved nodes retain selected bindings,
initialization mode, conversions, and conservative exception facts, and lowering
consumes them without name lookup, semantic reconstruction, or text transport.

This follows `spec.md` sections 2, 3, 6, 8, and 9. Aggregate planning walks the
borrowed syntax-edge sequence and canonical member index once. Local lowering
keeps shallow paths inline and retains typed subobject addresses after bounded
depth, so action work is linear while existing shallow presentation stays
stable. Inherited lookup results retain the compact naming-class `EntityId`, and
derived conversions/member/base actions retain an explicit projection count, so
access is decided once and lowering never walks inheritance edges. Constructor
overload sets and class-local action scratch are borrowed from their canonical
owners rather than copied or sized by unrelated bindings. The semantic graph
remains translation-unit-owned and synchronously borrowed by typed lowering;
textual output is only the final view.

## Current Failure Map

Current state is 120/259 PA16 tests: all 116/255 checkpoint-entry passes remain
and four audit regressions pass. The complete 139-failure set, assigned once by
primary semantic owner, is: 16 member-function/call-
boundary; 26 class lookup/access/inheritance; 39 initialization/lifetime; 41
operator/ADL/callable; 8 layout/object representation; and 9 procedural
interaction/metadata failures.

## Next Substantial Checkpoint

**Next: destruction and cleanup action spine (queued).** Give each class a
canonical user or implicit destructor `BindingId`, derive ordered reverse member/
base actions once, attach local-scope lifetime obligations to resolved variables,
and lower normal/early-return cleanup plus demanded helper emission. Ownership
and flow are `class EntityId -> destructor/subobject BindingIds -> lexical
lifetime action sequence -> typed cleanup calls`. This applies `spec.md`
sections 2, 4, 6, 8, and 9: stable lifetime facts, reasoned demand, no lowering
lookup, phase-local action scratch, and work proportional to live objects and
subobjects. Expected work is O(S + E) per scope exit and O(A) per demanded
destructor, with one monotonic demand transition per helper. Validate direct and
implicit destructors, reverse member/base order, block/return/loop exits,
member/array recursion, through-PA15, audit, and doubled action curves.

## Performance Evidence

| Workload | Scale | Evidence |
|---|---:|---|
| Class layout | 5k / 10k fields | 5,000 / 10,000 visits; 7.91 / 16.49 ms semantic |
| Repeated field use | 5k / 10k uses | 5,003 / 10,003 probes; 19.87 / 39.26 ms semantic; 10.73 / 23.48 ms lowering |
| Member overload selection | 1,001 / 2,001 candidates | 2,000 / 4,000 order comparisons; 2,006 / 4,006 conversion checks; 6.57 / 13.07 ms semantic; 8 instructions at both sizes |
| Local aggregate actions | 1k / 2k members | 2,009 / 4,009 semantic nodes; 2,006 / 4,006 lowered nodes; 3,005 / 6,005 instructions; 1.96 / 3.93 ms semantic; 1.08 / 2.06 ms lowering |
| Nested aggregate actions | 100x100 / 200x200 depth/leaves | After audit: 408 / 808 semantic nodes; 303 / 603 instructions; 63,066 / 124,506 typed bytes; pre-fix instructions were 10,302 / 40,602 |
| Constructor candidates | 1,001 / 2,001 candidates | 1,001 / 2,001 candidate visits; 1,004 / 2,004 conversion checks; 5.71 / 12.60 ms semantic; 10 instructions and 3 binding probes at both sizes |
| Constructor member actions | 1k / 2k initialized members | 1,000 / 2,000 action visits; 2,012 / 4,012 semantic nodes; 2,007 / 4,007 lowered nodes; 3,005 / 6,005 instructions; 2.34 / 5.72 ms semantic; 1.84 / 2.22 ms lowering |
| Nested constructor aggregate actions | 40x40 / 80x80 depth/leaves | Pre-audit 1,765 / 6,725 instructions, 248,820 / 986,100 typed bytes, 0.78 / 2.62 ms lowering; after bounded retention 127 / 247 instructions, 18,420 / 33,780 typed bytes, 0.13 / 0.15 ms lowering |
| Constructor isolation from unrelated declarations | 5k / 10k globals plus one member | 1 / 1 constructor-member action visits, 1 / 1 candidate visits, and 8 instructions at both sizes while required global/output work scales |
| Single-base chain construction and lookup | 250 / 500 base edges | Post-audit: 250 / 500 base actions; 500 / 1,000 lookup-edge visits; 251 / 501 candidates; 1,511 / 3,011 instructions; 511,781 / 1,021,943 typed bytes; five-run median 2.11 / 4.58 ms semantic and 1.71 / 3.27 ms lowering |

Exact counter scaling establishes work proportional to owned fields, indexed
candidates, resolved actions, and emitted IR; the nested audit curve closes the
former depth-by-leaf projection product.

## Completed Checkpoints

| Checkpoint | Final state | Evidence |
|---|---|---|
| Direct-member object spine | Complete | Canonical size/alignment/offset and default-construction facts; typed `.`/`->` fields; duplicate rejection; 42/247 PA16 and 1,145/1,145 through PA15; linear field/use curves; audit pass |
| Resolved member-call spine | Complete | Canonical method/access/cv facts, implicit-object ranking, out-of-class hidden `this`, stable overload/ABI IDs, typed calls, and LowIR counters; 9 product tests gained to 51/247 with no losses; 1,145/1,145 through PA15; linear candidate curve; audit pass |
| Local aggregate-action spine | Pass after audit fixes | C++11 aggregate eligibility, one union active member, member-ID action trees, borrowed brace cursor, typed scalar/reference stores, and bounded projection reuse; 61/248 PA16 with no losses; 1,145/1,145 through PA15; flat and nested linear curves; file audit pass |
| Special-member initialization action spine | Pass after audit fixes | Init-mode-aware selection, declaration-ordinal member actions, conservative exception facts, typed reference/subobject lowering, and bounded projection reuse; 91/255 with seven audit regressions and no existing loss; 1,145/1,145 through PA15; candidate/member/nested curves and file audit pass |
| Single-base construction spine | Pass after audit fixes | Canonical base/access edges, naming-class-aware lookup, selected base actions, conversion ranking, and recorded typed projection counts with no lowering lookup; 120/259 with four audit regressions and no existing loss; 1,145/1,145 through PA15; exact linear 250/500-edge curves; file audit pass |

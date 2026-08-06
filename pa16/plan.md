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

Destruction uses the same identity path: each class owns one destructor binding
and explicit destructible/trivial facts; scopes own compact automatic-object
obligations; demanded destructor bodies own reverse member/base actions. Access,
deletion, object identity, order, and projection are fixed semantically before
typed lowering. Destructor returns share one subobject epilogue, and bounded
arrays flatten to one leaf constructor fact with shared reverse exception
suffixes after an eight-element inline case; union destructors never synthesize
ordinary-member actions for inactive variants. This applies `spec.md` sections
2, 3, 4, 5, 6, 8, and 9: stable identity, monotonic demand, no lowering lookup,
cohesive phase-local ownership, and work linear in emitted actions.

Namespace objects now use the same action model. The translation unit owns one
source-ordered `NamespaceObjectAction` per definition, keyed by canonical
`BindingId` and retaining the resolved initializer plus optional destructor.
Static-data serialization, dynamic init/fini, static-member identity, and TLS
wrapper/guard emission consume those facts directly; references remain storage
bindings rather than lifetime owners. Static initializer serialization and
source-type lowering have separate compiled owners, while the shared graph is
still synchronously borrowed through the lowering driver.

## Current Failure Map

Current state is 150/265 PA16 tests, +18 over checkpoint entry. The complete
115-failure set, assigned once by primary owner, is: 16 member-function/call-
boundary; 22 class lookup/access/inheritance; 20 initialization/lifetime; 41
operator/ADL/callable; 8 layout/object representation; and 8 procedural
interaction/metadata failures.

## Active Checkpoint

**Next: operator/ADL callable spine.** Semantic expression analysis will own one
candidate union for member, ordinary, and argument-dependent lookup; each
candidate retains canonical function identity, implicit-object mode, conversion
sequence, access/deletion state, and selected operator token/result type. Data
flow is `operator/call syntax -> associated-class/namespace IDs -> indexed
candidate union -> ranked selected BindingId -> existing typed call lowering`.
This applies `spec.md` sections 2, 3, 5, 6, and 9: stable IDs, monotonic demand,
lookup completed before lowering, borrowed candidate storage, and
O(associated scopes + candidates log candidates + conversions) selection.
Validation will cover member/nonmember competition, hidden friends, enum ADL,
callable fields, operator fallback/rejection, through-PA15, file audit, and
1x/2x candidate/associated-scope counters.

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
| Lexical cleanup exits | 1k / 2k locals plus normal/return exits | 2,000 / 4,000 cleanup-action visits; 3,007 / 6,007 instructions; 593,407 / 1,182,871 typed bytes; 17.24 / 33.25 ms semantic and 6.77 / 13.19 ms lowering |
| Throwing array construction cleanup | 100 / 200 elements | Post-audit 1,505 / 3,005 instructions, 303 / 603 binding probes, 312,756 / 620,976 typed bytes, and 0.52 / 0.94 ms five-run median lowering; pre-fix was 21,007 / 82,007 instructions, 5,154 / 20,304 probes, and 3,977,181 / 15,535,017 bytes |
| Destructor EH suffixes | 100 / 200 nontrivial members | Shared large-case cleanup chain: 100 / 200 subobject visits; 1,015 / 2,015 instructions; 198,924 / 391,392 typed bytes; 0.32 / 0.63 ms five-run median lowering; pre-fix was 15,961 / 61,911 instructions and 3,112,135 / 12,029,251 bytes |
| Namespace dynamic actions | 1k / 2k objects | 1,000 / 2,000 actions; 2,004 / 4,004 instructions; 2,001 / 4,001 binding probes; 605,026 / 1,208,130 typed bytes; five-run median 3.79 / 7.62 ms semantic and 2.89 / 5.55 ms lowering |

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
| Destruction and lexical-cleanup spine | Pass after audit fixes | Demand-correct user/implicit destructor facts, access/deletion checks, union/reverse nested-array lifetime, normal/return/break/continue cleanup, return preservation, and shared typed EH suffixes; 132/265 with no prior loss; 1,145/1,145 through PA15; exact linear array/destructor curves; file audit pass with no PA16 warning |
| Namespace/static lifetime spine | Complete | Canonical ordered namespace actions; constant aggregate/string serialization; ordered init and reverse fini; reference/static-member identity; isolated TLS wrappers/guards; 150/265 (+18) with no baseline loss; 1,145/1,145 through PA15; 1k/2k linear counter/timing curves; file audit pass |

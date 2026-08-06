# PA16 Audit

## Current Checkpoint Review

**Checkpoint:** `79e34477` (`Implement PA16 destruction and cleanup spine`)

**Result:** Pass after audit fixes. The increment is bounded to canonical
destructor facts, reverse member/base/array actions, local automatic-object
obligations, normal and early-exit cleanup, demanded destructor emission, and
typed exception cleanup. Namespace/static lifetime and explicit destructor
expressions remain assigned to later checkpoints.

The durable ownership path is class `EntityId` -> canonical destructor
`BindingId` plus destructible/trivial facts -> scope-owned `LifetimeObligation`
or class-owned reverse subobject actions -> typed calls and shared cleanup CFG.
Semantic analysis decides access, deletion, order, object identity, and demand;
lowering consumes those facts without lookup, rendered-name recovery, or a text
round trip. Constructor-array, destructor-action, and lifetime-control lowering
have separate cohesive owners.

Audit findings are closed:

1. Trivial destructors bypassed access checks, and a deleted subobject caused
   eager class-definition failure. All destructor requirements now check access
   and deletion; unusable implicit destructors are represented as deleted and
   fail only when demanded.
2. `return;` in a destructor returned before member/base actions. Return cleanup
   now exits the protected body into one shared typed subobject epilogue.
3. Local arrays were destroyed forward, and nested default-construction omitted
   partial-construction cleanup. Array lowering now uses one flattened leaf
   action for construction cleanup and reverse recursive destruction; the
   affected checked fixture now records the required reverse order.
4. Union destructors treated every variant as a live ordinary member. Defaulted
   union destructors now become unusable for nontrivial variants, while a
   user-provided union destructor performs no implicit variant destruction.
5. Throwing construction of N array elements emitted every prior-element suffix,
   producing quadratic IR. Beyond a bounded eight-element small case, each call
   targets one of N shared reverse cleanup blocks.
6. The landed lifetime header carried mixed array/action/control implementation
   weight. Cohesive array-lifetime, destructor-action, constructor-action, and
   lifetime-control mixins remove all PA16 file-audit warnings.
7. Default-constructor attachment retained a `TypeRecord` reference across type
   interning. The compact record is now copied before recursive action building,
   so type-table growth cannot invalidate the later array-kind read.

Validation:

- The focused lifetime set and six audit regressions pass, including trivial
  destructor access, inaccessible base destruction, unused deleted subobjects,
  explicit destructor return, nested class arrays, and union variant lifetime.
- `make test-report ACTIVE_TEST_REPORT_PAS='pa16'`: expected full-stage failure,
  132/265. All 126/259 checkpoint-entry passes remain, the six audit cases pass,
  and the same 133 mapped later-checkpoint failures remain with no timeout.
- Through PA15 remains 1,145/1,145 and 15/15 stages. File audit passes with only
  the two pre-existing shared-header advisories.
- Throwing arrays at 100/200 elements now record 1,505/3,005 instructions,
  303/603 binding probes, and 312,756/620,976 typed bytes; before the audit they
  recorded 21,007/82,007 instructions, 5,154/20,304 probes, and
  3,977,181/15,535,017 bytes. Five-run median lowering is 0.52/0.94 ms.
- Nontrivial 100/200-member destructor suffixes remain exact-linear at 100/200
  action visits, 1,015/2,015 instructions, 198,924/391,392 typed bytes, and
  five-run median lowering of 0.32/0.63 ms.
- Valgrind on default construction plus explicit-return destruction reports zero
  errors, zero leaks, and 779/779 allocations freed.

## Checkpoint Audit Ledger

| Checkpoint | Audit result | Closure evidence |
|---|---|---|
| Direct-member object spine | Pass after audit fixes | One-shot class completion, non-mergeable member IDs, sound implicit-construction conditions, typed field projection, linear curves, and all checkpoint gates preserved |
| Local aggregate-action spine | Pass after audit fixes | C++11 user-provided eligibility, one union active member, borrowed edge cursor, bounded typed projection reuse, 61/248 PA16 with no losses, and all audit gates preserved |
| Special-member initialization action spine | Pass after audit fixes | Init-mode-correct constructor selection, canonical member ordinals/actions, truthful exception facts, typed nested references, bounded projections, 91/255 with no existing loss, and all audit gates preserved |
| Single-base construction spine | Pass after audit fixes | Naming-class access, hiding-correct base initializers, recorded projection counts, no lowering lookup, 120/259 with no existing loss, exact linear chain evidence, and all audit gates preserved |
| Destruction and lexical-cleanup spine | Pass after audit fixes | Demand-correct destructor access/deletion, union/reverse nested-array lifetime, shared return/EH suffixes, 132/265 with no prior loss, exact linear array/destructor curves, and all audit gates preserved |

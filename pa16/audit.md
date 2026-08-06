# PA16 Audit

## Current Checkpoint Review

**Checkpoint:** `76410798` (`Implement PA16 single-base construction spine`)

**Result:** Pass after audit fixes. The landed increment is bounded to one
canonical direct-base edge, inherited indexed lookup/access, base-at-zero layout,
selected base-constructor actions, derived-to-base conversions, and typed base
projection. Destruction, friends/using declarations, inheriting constructors,
and broader layout remain assigned to later checkpoints.

The ownership path is base syntax -> canonical derived/base `EntityId`s plus
access -> lookup result with its compact naming-class identity -> selected
constructor/conversion and explicit projection-count facts -> typed base/member
addresses and calls. Semantic construction performs lookup and path validation;
lowering consumes the recorded count and never walks inheritance edges, reruns
lookup, or reconstructs a conversion from types or names.

Audit findings are closed:

1. Lookup discarded the naming class, allowing public types and static functions
   inherited through a private base to bypass transformed access. Lookup results
   now retain that `EntityId`; access follows the single-base chain in the source
   context, including class member declarations and injected class names.
2. A constructor mem-initializer fell back to base-type lookup after finding a
   same-named static member. Any direct ordinary declaration now hides the base,
   while aliases that genuinely denote the direct base remain valid.
3. Lowering rediscovered base relationships with `IsBaseOf` and `direct_base`
   walks. Derived conversions, inherited fields, explicit reference casts, and
   base actions now record `base_projection_count`; lowering emits exactly that
   typed fact and has no inheritance lookup path.
4. Publishing a fresh member scope/base edge unnecessarily invalidated the whole
   lookup cache, and the new base action pushed implementation weight back into
   the constructor header. Base edges are fixed before scope publication, so no
   invalidation is needed; the base action has a normal `.cpp` owner and the
   affected header no longer triggers the file audit.

Validation:

- Focused inherited construction/conversion set: 17/17 existing tests pass;
  the four naming-class/hiding audit regressions also pass.
- `make test-report ACTIVE_TEST_REPORT_PAS='pa16'`: expected full-stage failure,
  120/259. All 116/255 checkpoint-entry passes remain, the four audit cases pass,
  and the same 139 mapped later-checkpoint failures remain.
- Through PA15: 1,145/1,145 and 15/15 stages pass. File audit passes; its sole
  advisory is the pre-existing declaration-weight warning in `pa11_model.h`.
- A 250/500-edge constructor chain records 250/500 base actions, 500/1,000
  lookup-edge visits, 251/501 candidates, 1,511/3,011 instructions, and
  511,781/1,021,943 typed bytes. Five-run median semantic/lowering times are
  2.11/4.58 ms and 1.71/3.27 ms.
- Valgrind on the conditional derived/base reference path reports zero errors,
  zero leaks, and 854/854 allocations freed.

## Checkpoint Audit Ledger

| Checkpoint | Audit result | Closure evidence |
|---|---|---|
| Direct-member object spine | Pass after audit fixes | One-shot class completion, non-mergeable member IDs, sound implicit-construction conditions, typed field projection, linear curves, and all checkpoint gates preserved |
| Local aggregate-action spine | Pass after audit fixes | C++11 user-provided eligibility, one union active member, borrowed edge cursor, bounded typed projection reuse, 61/248 PA16 with no losses, and all audit gates preserved |
| Special-member initialization action spine | Pass after audit fixes | Init-mode-correct constructor selection, canonical member ordinals/actions, truthful exception facts, typed nested references, bounded projections, 91/255 with no existing loss, and all audit gates preserved |
| Single-base construction spine | Pass after audit fixes | Naming-class access, hiding-correct base initializers, recorded projection counts, no lowering lookup, 120/259 with no existing loss, exact linear chain evidence, and all audit gates preserved |

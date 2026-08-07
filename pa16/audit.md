# PA16 Audit

## Current Checkpoint Review

**Checkpoint:** `53c686fd` (physical single-base layout/projection closure).

**Result:** Pass after audit fixes. PA11 owns canonical class/type identity and
the completed empty-class layout fact; PA12 owns layout, semantic base distance,
access, and typed projection facts; PA15/PA16 lowering consumes those facts
without lookup or name recovery.

The complete affected ownership path is closed:

1. Class completion records object size/alignment and whether the class has no
   nonzero data subobject. An empty direct base contributes alignment but no
   physical extent; every complete object still has size at least one.
2. The landed same-type separation checked only a member's outer named type.
   It could therefore overlap an empty base with the same type nested at offset
   zero inside a differently named member. The layout owner now walks explicit
   zero-offset direct-base and data-member edges, keyed by canonical `EntityId`.
   Generation marks deduplicate the walk, a reusable semantic-phase vector owns
   temporary traversal state, and an identity intersection advances the member
   to its next aligned address. No layout spelling, copied semantic graph, or
   whole-scope scan enters the decision.
3. Overload/access ranking retains full semantic base distance. Once a selected
   conversion is known to traverse the PA16 single-inheritance chain, its
   physical projection is one zero-offset LowIR operation. Member, cast, call,
   and operator paths publish that typed fact; lowering emits it once and does
   not repeat inheritance analysis. Constructor/destructor direct-base actions
   remain independently typed one-edge actions.
4. Release telemetry now exposes zero-offset-subobject visits in addition to
   layout and access-path visits. A 64/128-edge empty-base chain with the nested
   collision records 67/131 layouts, 67/131 zero-offset visits, 394/778 base-
   path visits, and 0.701/1.317 ms five-run semantic medians. Both sizes emit
   one base projection and 11 instructions, establishing proportional semantic
   work and constant physical projection work.

The two landed gains, the audit regression, and four neighboring layout/base
cases pass (7/7). The required stage report is 276/291 with the same 15
pre-existing failures, preserving and improving the 275/290 turn-start
baseline. PA1-PA15 pass 1,145/1,145. File audit passes with the same four
pre-existing header-division warnings. No relevant timeout, external tool,
reference-binary dependency, source/test shortcut, textual round trip,
spelling-keyed fallback, or unresolved affected-path issue remains.

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
| Typed declarator/call/boundary closure | Pass after audit fixes | Interned scoped parameter facts, canonical binding/ABI and builtin metadata, retained narrowing conversions; focused 15/15; original 246/283 preserved; proportional 1x/2x probes; gates preserved |
| Aggregate/value-init/materialization closure | Pass after audit fixes | Typed non-declaration helper IDs, complete omitted-member actions, shared literal decoding, ordinary demand; original 260/286 preserved, 2/2 audit regressions pass; proportional probes and gates preserved |
| Friend/ADL call-boundary closure | Pass after audit fixes | Indexed friends/ADL and internal ABI identity; retained cached conversion facts and post-selection constructor checks; focused 11/11; 273/290 with the same 17 failures; proportional probe and gates preserved |
| Physical single-base layout/projection closure | Pass after audit fixes | Canonical empty-layout fact, identity-indexed nested zero-offset separation, retained semantic distance and one typed physical projection; focused 7/7; 276/291; proportional probe and gates preserved |

# PA16 Audit

## Current Checkpoint Review

**Checkpoint:** operator/ADL callable spine.

**Result:** Complete. Semantic analysis now collects canonical associated
classes and scopes, exposes hidden friends only through eligible associated
classes, deduplicates ordinary/member/ADL functions, and ranks the mixed set
without throwing when built-in fallback remains valid. `operator()` uses the
same resolved-call path. Friend lexical/granting-class facts and operator ABI
terminals remain canonical through typed lowering.

Audit closure:

1. Qualified ordinary calls suppress ADL, while unqualified calls union indexed
   ADL candidates unless ordinary lookup found a member.
2. Hidden friends retain granting-class identity and are not ordinarily visible;
   malformed nonmember operators are rejected without misclassifying conversion,
   allocation, or deallocation functions.
3. `nullptr_t` remains a distinct semantic/ABI type. Pointer contexts materialize
   typed nulls without changing PA12 semantic output or prior LowIR presentation.
4. Discarded reference-valued operator calls preserve side effects without
   introducing invalid object loads.
5. Literal helpers and operator resolution have separate compiled owners, keeping
   the file audit within limits.

Validation is 185/269 PA16, a gain of 31 with no baseline loss; PA1-PA15 are
1,145/1,145. File audit passes with the same three pre-existing shared-header
warnings. At 128/256 associated classes, measured scope visits are 128/256,
candidates 258/514, conversion checks 836/1,668, instructions 220/412, and typed
bytes 47,373/86,541; five-run semantic/lowering medians are 0.544/1.000 ms and
0.441/0.749 ms.

## Checkpoint Audit Ledger

| Checkpoint | Result | Closure evidence |
|---|---|---|
| Direct-member object spine | Pass | Canonical layout/member facts, typed fields, linear curves, gates preserved |
| Local aggregate-action spine | Pass | Aggregate/union rules, borrowed cursor, bounded projections, gates preserved |
| Special-member initialization spine | Pass | Mode-correct selection, canonical actions, typed subobjects, linear curves |
| Single-base construction spine | Pass | Naming-class access, selected base actions, recorded projections, linear edges |
| Destruction and lexical-cleanup spine | Pass | Reverse lifetime, lexical exits, shared EH suffixes, linear cleanup curves |
| Namespace/static lifetime spine | Pass | Independent TLS/linkage, sparse identity, one lifecycle pair, linear curves |
| Operator/ADL callable spine | Pass | Canonical ADL/hidden friends, mixed ranking/fallback and out-of-class operators, 185/269 with no losses, linear candidate curve |

# PA16 Audit

## Current Checkpoint Review

**Checkpoint:** `0d60dc0e` (access/base-path closure).

**Result:** Pass after audit fixes. The durable path is class/friend/using syntax
to canonical class and declaration IDs -> indexed enclosing/base, grant, direct-
signature, and imported-signature facts -> lookup's exact binding, canonical
declaration, and naming class -> one access/path decision using the actual object
class -> retained selected object conversion and projection -> typed LowIR.

Audit findings are closed:

1. Type lookup canonicalized away a class-using alias before access checking,
   and candidate deduplication similarly discarded callable alias access. Lookup
   now retains exact and canonical declaration IDs separately; selected aliases
   survive canonical deduplication until emission consumes the canonical target.
2. Using-function selection preferred a same-named tag, while hiding depended on
   declaration order and scanned the existing overload sequence. Ordinary names
   now take their language-required priority; separate direct/imported canonical
   signature indexes make both class orders O(1) average per signature and reject
   namespace conflicts. Qualified friends require a prior ordinary-visible fact.
3. Protected access omitted the object-expression restriction. Access now checks
   the actual object class, records path/grant work, and carries the selected
   object conversion/projection into the typed call instead of rediscovering it.

Validation is 202/275 PA16: all 195 turn-start passes, one existing using/tag
case, and six audit regressions, with no original loss; PA1-PA15 are
1,145/1,145. File audit passes with the same three shared-header warnings. A
50/100-class friend/access probe records 551/1,101 access checks, 100/200 path
visits, 250/500 grant probes, 306/606 signature lookups, 860/1,710 instructions,
and 275,983/550,731 typed bytes. Five-run semantic/lowering medians are
1.770/3.503 ms and 1.099/2.078 ms.

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

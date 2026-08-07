# PA16 Audit

## Current Checkpoint Review

**Checkpoint:** `ac6acf41` (aggregate/value-initialization and temporary
materialization closure).

**Result:** Pass after audit fixes. PA12 owns recursive aggregate actions,
value-initialization, selected construction, and temporary demand. PA15/PA16
lowering consumes those typed records for local arrays, placement construction,
and temporaries. Synthetic aggregate helpers now have stable typed emission
identities without pretending to be source-language constructors.

The complete affected ownership path is closed:

1. The landed aggregate-array path manufactured a constructor declaration,
   binding, function scope, parameters, and body solely to reuse constructor
   lowering. That fake declaration entered semantic function indexes and could
   affect lookup. It is replaced by `AggregateHelperInfo`, a canonical typed
   helper index, and an action carrying only a helper ID and converted values.
   A dedicated lowering owner registers and emits each helper exactly once;
   lowering performs no name lookup or semantic reconstruction.
2. The manufactured-helper shortcut also mapped elements with omitted members
   to trivial default construction, dropping required zero initialization.
   Incomplete or nested elements now retain their full typed action lists for
   direct lowering, while eligible explicit scalar elements share one helper.
   The array list is updated in place, target-directed aggregate analysis avoids
   abandoned temporary nodes, and edge replacement is sequenced safely across
   arena growth.
3. Semantic and static lowering had separate partial string decoders that
   accepted a user-defined suffix and did not share raw-literal handling. Both
   now use the post-tokenizer's narrow ordinary/UTF-8 decoder, which supports raw
   forms and rejects suffixes before aggregate character-array initialization.
4. Temporary construction now uses the ordinary binding demand worklist; the
   parallel bound-default emission state is gone. Helper records, member lists,
   and the canonical index are included in typed-storage accounting. No helper
   is a declaration, no names cross into lowering as semantic identity, and no
   reference binary or external compiler participates.

The aggregate path is O(A + M) for A initializer actions and M emitted member
stores. Canonical helper lookup is indexed, each eligible signature has one
definition, and projection replay is bounded by the existing depth-eight PA16
limit. Five-run 32/64 nested-member probes record 333/653 semantic nodes, 35/67
layout visits, 65/129 conversions, 328/648 instructions, and
63,078/124,518 typed bytes; semantic medians are 0.291/0.422 ms and lowering
medians 0.165/0.288 ms. Separate 64/128-element helper probes retain one helper
definition and 6/6 signature lookups while scaling 398/782 semantic nodes,
460/908 edges, 148/276 instructions, and 43,120/82,288 typed bytes; semantic
medians are 0.224/0.374 ms and lowering medians 0.102/0.141 ms.

The required stage report is 262/288: all 11 landed checkpoint gains and both
audit regressions pass, preserving the original 260/286 baseline and the same
26 pre-existing failures. PA1-PA15 pass 1,145/1,145. File audit passes with the
same four pre-existing shared/CRTP-header warnings.

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

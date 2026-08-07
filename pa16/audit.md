# PA16 Audit

## Current Checkpoint Review

**Checkpoint:** `3f6d32d5` (friend/ADL call-boundary closure).

**Result:** Pass after audit fixes. PA11 owns canonical namespace, entity,
binding, type, and ABI identity; PA12 owns indexed friendship/ADL, overload and
conversion selection, access, and demand; PA15/PA16 lowering consumes the
selected binding, conversion, materialization, and projection facts directly.

The complete affected ownership path is closed:

1. Unnamed namespaces retain their lookup identity while publishing a separate
   canonical emission path. Functions and variables carry explicit internal-
   linkage facts, and cross-source lowering keys internal symbols by source
   ordinal plus typed path/signature identity. Hidden friends and grants remain
   compact `(EntityId, NameId)` / `(EntityId, BindingId)` indexes; ADL visits only
   associated entities, bases, enclosing classes, and direct namespace indexes.
2. The landed call path ranked a converting constructor, discarded its binding,
   and searched the complete constructor set again while building the selected
   call. `CallConversionFact` now retains the conversion rank, constructor
   binding, and source-to-constructor rank. A selection-local flat table caches
   positive and negative constructor results by argument ordinal and canonical
   target `TypeId`; the key is complete for its fixed lookup/access context and
   dies with that overload operation. All ordinary, member, operator, literal,
   and placement-call owners pass the selected facts into call construction.
3. Constructor viability no longer filters deleted or inaccessible candidates
   before ranking: the best constructor is retained and diagnosed when the
   conversion is materialized. A class temporary is not proposed for a
   non-const/volatile lvalue reference or to bypass a failed related-class
   reference binding. Focused regressions cover `X&` rejection and a deleted
   exact constructor beating an available standard-conversion constructor.
4. Lowering receives typed temporary/staging/projection flags and performs no
   lookup, mangled-name recovery, or semantic retry. Call-argument lowering and
   scope/emission-path ownership are isolated in bounded source/header units;
   the checkpoint-added header-weight warning is gone. Cache hit/miss and all
   constructor-candidate work are exposed by ordinary release telemetry.

The adversarial shared-target probe doubles both overloads and converting
constructors from 32 to 64. Candidate visits are 64/128, conversion checks
162/322, cache hits 31/63, cache misses 32/64, access checks 129/257,
instructions 104/200, and typed output 69,590/136,790 bytes; five-run semantic
medians are 1.017/1.980 ms. This is proportional in declarations and emitted
work; before the audit cache, the smaller 16/32 form required 343/1,191
conversion checks and 336/1,184 access checks.

All 9 landed gains and 2 audit regressions pass. The required stage report is
273/290 with the same 17 pre-existing failures, preserving the turn-start
271/288 baseline. PA1-PA15 pass 1,145/1,145. File audit passes with only the same
four pre-existing shared/CRTP-header warnings; no timeout, external compiler,
reference binary, source-text shortcut, or spelling-keyed semantic fallback is
present on this path.

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

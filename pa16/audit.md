# PA16 Audit

## Current Checkpoint Review

**Checkpoint:** `3a19722e` (`Implement PA16 namespace object lifetime spine`)

**Result:** Pass after audit fixes. The durable path is declaration-owned
linkage/TLS/constant facts -> source-ordered `NamespaceObjectAction` definitions
-> static-data or typed dynamic action lowering -> one program-owned init/fini
role pair. References remain storage bindings rather than lifetime owners;
qualified static-member definitions merge by canonical `BindingId`; TLS storage
duration is independent of `static`/`extern` linkage; and lowering consumes the
resolved initializer/destructor actions without lookup or text transport.

Audit findings are closed:

1. Encoding `thread_local` as an exclusive storage class erased `static` and
   `extern`, made class TLS members layout fields, and misclassified extern TLS
   declarations as definitions. TLS is now a separate canonical binding fact;
   in-class static/constexpr initializers are validated and constants propagate
   across redeclarations.
2. Synthetic symbols made the next translation unit violate a falsely dense
   semantic-symbol index. Dense identity entries now map to sparse emitted
   `SymbolId`s, preserving canonical lookup across synthetic emissions.
3. Each translation unit emitted its own `role=init`/`role=fini`, violating the
   one-role LowIR contract. Typed TU bodies are now coalesced once into forward
   initialization and reverse finalization calls, with singleton role ordering
   normalized and coalescing time included in lowering telemetry.
4. Constant address serialization did not retain its target symbol, allowing a
   required extern declaration to disappear. The static-initializer owner now
   marks resolved function/global targets referenced.
5. Variable linkage/TLS publication pushed the parser owner over both file
   limits. Canonical declaration publication now lives in the declaration unit;
   the file audit has no PA16 fatal or checkpoint-created warning.

Validation:

- `make test-report ACTIVE_TEST_REPORT_PAS='pa16'` remains the expected
  full-stage failure at 154/269: all original 150/265 passes plus four audit
  regressions, with the same 115 later-checkpoint failures.
- Through PA15 is 1,145/1,145. File audit passes with the same three pre-existing
  shared-header advisories.
- A two-TU dynamic-lifetime probe emits exactly one init and one fini role,
  calls two TU init helpers forward and two fini helpers backward, and is
  accepted by `lowir2cy86`; init-only, fini-only, and reversed-role source order
  also validate.
- At 1k/2k namespace objects, counters are 1,000/2,000 actions,
  4,016/8,016 instructions, 2,003/4,003 binding probes, and
  976,176/1,946,960 typed bytes. Representative semantic/lowering times are
  3.56/7.02 ms and 3.85/7.61 ms, respectively.

## Checkpoint Audit Ledger

| Checkpoint | Audit result | Closure evidence |
|---|---|---|
| Direct-member object spine | Pass after audit fixes | One-shot class completion, non-mergeable member IDs, sound implicit-construction conditions, typed field projection, linear curves, and all checkpoint gates preserved |
| Local aggregate-action spine | Pass after audit fixes | C++11 user-provided eligibility, one union active member, borrowed edge cursor, bounded typed projection reuse, 61/248 PA16 with no losses, and all audit gates preserved |
| Special-member initialization action spine | Pass after audit fixes | Init-mode-correct constructor selection, canonical member ordinals/actions, truthful exception facts, typed nested references, bounded projections, 91/255 with no existing loss, and all audit gates preserved |
| Single-base construction spine | Pass after audit fixes | Naming-class access, hiding-correct base initializers, recorded projection counts, no lowering lookup, 120/259 with no existing loss, exact linear chain evidence, and all audit gates preserved |
| Destruction and lexical-cleanup spine | Pass after audit fixes | Demand-correct destructor access/deletion, union/reverse nested-array lifetime, shared return/EH suffixes, 132/265 with no prior loss, exact linear array/destructor curves, and all audit gates preserved |
| Namespace/static lifetime spine | Pass after audit fixes | Independent TLS/linkage facts, sparse emission identity, one ordered program lifecycle pair, retained address targets, 154/269 with no baseline loss, linear namespace curves, and all audit gates preserved |

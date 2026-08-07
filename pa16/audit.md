# PA16 Audit

## Current Checkpoint Review

**Checkpoint:** `f905d52f` (operator/ADL callable spine).

**Result:** Pass after audit fixes. The durable path is operator/literal syntax
to interned name and canonical `BindingId` -> declaration-owned typed operator
kind/literal suffix plus ordinary-scope and granting-class candidate indexes ->
generation-deduped ordinary/member/ADL set -> one selected call with conversion
facts -> typed LowIR/ABI adapters. Lowering no longer recovers operator or null
semantics from source spelling.

Audit findings are closed:

1. ADL formerly scanned every same-named namespace function to filter hidden
   friends, including declarations that could not belong to the result. Separate
   `(scope, name)` ordinary and `(granting class, name)` hidden-friend indexes now
   visit only eligible edges and support multiple granting classes.
2. ABI lowering reconstructed operator terminals from function-name text, and
   null lowering recognized the word `nullptr`. Canonical bindings now retain a
   typed operator kind/suffix; all pointer conversion sites consume canonical
   `nullptr_t`, including initialization, assignment, calls, returns, casts, and
   comparisons.
3. String UDL syntax was treated as an ordinary string and never reached its
   literal operator. It now builds the standard string/length arguments, resolves
   the operator through the shared overload path, records the generated size
   conversion, and emits the typed literal ABI terminal.

Validation is 186/269 PA16, one audit gain with all 185 turn-start passes
preserved; PA1-PA15 are 1,145/1,145. File audit passes with only the same three
pre-existing shared-header warnings. A multiple-grant hidden-friend probe passes.
For 512/1,024 unrelated hidden friends, associated scope/declaration visits stay
1/1 and candidates 2/2; instructions and typed bytes scale 1,028/2,052 and
867,623/1,734,513. Five-run semantic/lowering medians are 7.348/14.762 ms and
4.436/8.734 ms.

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

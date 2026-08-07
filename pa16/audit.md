# PA16 Audit

## Current Checkpoint Review

**Checkpoint:** `7727472a` (typed declarator, call, and function-boundary
closure).

**Result:** Pass after audit fixes. Parameter and trailing-return syntax now
publish interned IDs and canonical types; function bindings own redeclaration,
exception, builtin, and ABI identity; resolved expressions retain selected
bindings and implicit narrowing facts; typed lowering consumes those IDs and
facts into LowIR boundary records. Rendering is the only textual step.

The complete affected path is closed:

1. Function-parameter shadowing collected copied strings and re-interned them
   while entering a body. The parser now traverses the immediate parameter
   clause once into a reusable, measured `TextId` scratch sequence, applies
   scoped name facts, and restores them at the body boundary.
2. Call, variable, and assignment lowering inferred immediate narrowing again
   from LowIR widths and merely retagged out-of-range values. PA12 now records
   the selected integer-narrowing conversion; lowering consumes that fact and a
   typed helper canonicalizes the represented value exactly once.
3. The builtin memory bindings used `char*` signatures, rejecting valid
   `void*` calls, and function redeclarations merged incompatible direct
   `noexcept` facts with boolean OR. Builtins now retain their real
   `void*`/`const void*` semantic types, and conflicting exception
   specifications fail before canonical binding merge. Builtin effects and
   parameter attributes remain keyed by the bounded builtin enum, not names.
4. Canonical function ABI identity comes from the selected binding type,
   including member adaptation and the deliberate PA16 incomplete-result view;
   no lowered name, rendered signature, lookup retry, or external tool is used.

Validation is 15/15 focused cases: all 12 landed cases plus three audit
regressions. The required full-stage report is 249/286, preserving every one of
the original 246/283 passes and the same 37 original failures; PA1-PA15 remain
1,145/1,145. File audit passes with the four reviewed shared/CRTP-header
warnings.

Five-run 32/64-method composed-declarator probes record 104/200 parser fact
changes, 1,654/3,254 syntax nodes, 291/579 conversion checks, 261/517 access
checks, 112,179/222,099 typed bytes, and 455/903 LowIR instructions. Median
parse/semantic/lowering times are 0.521/0.986 ms, 0.908/1.678 ms, and
0.532/1.040 ms; all deterministic work/storage ratios are 1.89-1.99 for 2x
input.

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

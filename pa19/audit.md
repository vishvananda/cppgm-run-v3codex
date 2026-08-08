# PA19 Checkpoint Audit

## Current Checkpoint Review

Checkpoint `28035796` landed scalar/control conversion replay and raised PA19
from a reproduced parent result of 280/298 to 288/298 while PA1-PA18 remained
1,713/1,713. The eight-case gain is valid, but its binary-immediate policy used
whether the translation unit's shared template-argument pool was empty. An
unrelated demanded specialization could therefore change an ordinary
function's LowIR. That was an incomplete semantic key and violated `spec.md`
sections 2, 4-6, and 9 as well as PA19's monotonic-extension rule. Pointer
value-initialization was also recognized only in call-argument lowering, even
though the semantic fact belongs to the literal and applies in every context.

The repaired ownership path is retained expression -> PA12 typed literal,
selected operand type, constant, and template-layout provenance -> one shared
semantic `DumpNode` -> PA15 typed operand/conversion/branch construction.
Signedness-changing immediate materialization now depends only on the local
source and selected target types plus parent-local constant/layout facts; it no
longer reads a translation-unit template registry. Template-layout provenance
is propagated with semantic graph edges, so an ordinary layout constant
keeps its established PA18 form while a demanded specialization layout retains
its selected conversion. Pointer value-initialization now becomes a typed null
operand in generic literal lowering, covering calls, initialization, and
returns. Direct constant branches continue to consume the PA12 constant fact
without lookup or expression replay.

Two audit controls prove both repaired boundaries: demanding an unrelated
function specialization leaves ordinary `sizeof(holder) - 1` unchanged, and a
pointer value-initialization lowers as `nullptr` in both return and local
initialization contexts. The original eight passing cases remain passing. A
bounded changed-source scan found no host/reference invocation, filename or
test branch, cached output, textual semantic reconstruction, full-program
retry, new unindexed lookup, or per-use allocation. Work is O(replayed semantic
nodes plus emitted typed nodes).

## Durable Architecture Decisions

- A class-specialization `EntityRecord` owns one slice in the shared canonical
  template-argument pool; function-specialization `BindingRecord`s use the same
  pool. Analyzer-only duplicate argument stores are removed.
- Explicit-instantiation state, weak ODR linkage, and object-emission roots are
  semantic facts. Lowering consumes them by compact identity and combines them
  monotonically when translation-unit symbols merge.
- ABI construction represents template owners and nested template argument
  types structurally. Synthetic specialization spellings remain presentation
  only and are not symbol-identity or mangling inputs.
- `object_root` is part of the documented textual LowIR adapter and is accepted
  by the shared parser; the in-process production boundary remains typed.
- Enum-only non-member operator candidates are indexed by scope, interned name,
  canonical enum type, and operand position. Ordinary visibility and ADL select
  index owners first; unrelated same-name declarations never reach conversion
  ranking.
- Standard enum conversions and source-selected class-value constructor actions
  are semantic facts. Lowering and call staging consume those facts rather than
  rediscovering type or constructor intent.
- Supported explicit type template arguments are retained as ordinary PA10
  `type-id` trees beneath compact qualified-name components and hidden only at
  the public PA10 serialization boundary. Rendered names are presentation
  payloads, not semantic inputs.
- Parser current-class state stores the terminal interned identifier. Nested
  constructors and destructors are classified by compact identity rather than
  by parsing a qualified class spelling.
- Structured template-name resolution adopts existing interned IDs and feeds
  argument trees through `BuildTypeId` and the canonical specialization cache;
  unresolved argument types cannot enter that cache.
- Scalar immediate conversion policy is owned by local typed source/target,
  constant-expression, and template-layout facts; translation-unit template
  registries are not conversion keys.
- Pointer value-initialization is literal-owned provenance. Typed lowering
  constructs one null-pointer operand from that fact in every use context.

## Performance Evidence

For 128/256/512 repeated mixed replay operations (`sizeof(T) - 1`, a direct
constant branch, and pointer value-initialization), semantic nodes were
2,069/4,117/8,213, lowered nodes 1,421/2,829/5,645, conversion checks
1,288/2,568/5,128, and peak semantic storage
1,165,394/2,318,866/4,625,810 bytes. Five-run median semantic-plus-lowering
times were 6.515/12.769/25.157 ms. Work, storage, and time follow the doubled
input/output; the audit fix adds no registry scan or repeated semantic work.

## Validation

- PA19: 290/300 including two audit controls; the original 288/298 pass set and
  exact ten-test residual set are intact.
- The eight landed scalar/control cases and both audit controls pass.
- PA1-PA18: 1,713/1,713.
- PA19 file audit: pass with the 11 pre-existing advisory header warnings.

## Checkpoint Audit Ledger

| Checkpoint | Audit disposition | Final evidence |
|---|---|---|
| Explicit class-instantiation completion and member demand (`5ca3aed9`) | Pass after audit fix | Canonical entity arguments, structured ABI identity, weak/root propagation and parser support, N3485 legality controls, linear member-demand probe, prior baseline retained |
| Canonical enum builtin competition and class default arguments (`6c1a56be`) | Pass after audit fix | Exact enum-parameter index, comma fallback, typed conversion/constructor facts, two regressions, constant unrelated-candidate work, linear required work, prior baseline retained |
| Declaration-owned local and qualified type replay (`e10d5439`) | Pass after audit fix | Retained type-argument trees, interned component/class identity, canonical specialization path, bounded parser scans, one-completion function-pointer probe, 272/298 PA19 and prior baseline retained |
| Selected scalar/control replay (`28035796`) | Pass after audit fix | Local conversion/layout keys, literal-owned null provenance, two audit controls, linear mixed replay, original 288/298 pass set and prior baseline retained |

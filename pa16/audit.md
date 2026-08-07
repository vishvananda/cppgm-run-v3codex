# PA16 Final Audit

## Checkpoint Audit Ledger

| Checkpoint | Result | Closure evidence |
|---|---|---|
| Direct member / member call | Pass | Canonical member identity, object-aware ranking, typed address/call lowering |
| Aggregate / initialization | Pass | Aggregate and union rules, ordered base/member actions, retained conversions |
| Construction / destruction | Pass | Monotonic demand, ABI entry identities, reverse subobject and lexical lifetime |
| Namespace / TLS lifetime | Pass | Independent storage/linkage facts, one ordered initializer/finalizer pair |
| Operator / ADL / friends | Pass | Direct ordinary, associated-scope, and hidden-friend indexes; selected typed calls |
| Access / inheritance | Pass | Indexed grants, canonical base paths, protected-object checks, retained projections |
| Layout / bit-fields / empty bases | Pass | Shared layout facts, physical/value width split, identity-safe zero-offset placement |
| Declarators / conversions | Pass | Scoped canonical types, complete conversion facts, narrowing and cv constraints |
| Full-stage lifetime closure | Pass | Mutable members, TLS demand, explicit/pseudo destruction, unevaluated-demand rules |
| Final architecture audit | Pass after fixes | Precise lookup dependency invalidation, inline reverse edges, flat lowering maps, terminal-only text |

## Final Findings

1. **Architecture blocker fixed:** `Program::LookupCache` used one TU-wide
   revision. Every declaration, namespace, alias, type-name, or using-edge
   insertion made every lookup cold, including unrelated names and scopes.
2. **Allocation blocker fixed:** the first reverse-index implementation and
   existing lowering helpers used singleton heap lists / node-based hash maps
   for cache dependents, labels, constructor substitutions, and string-literal
   pooling.
3. **Observability gap fixed:** LowIR mode did not publish lookup-cache,
   specialization, demand, or semantic peak-storage evidence, and duplicated
   the semantic counter schema when those fields were first added.
4. **Text-boundary cleanup fixed:** borrowed semantic lowering constructed an
   unused `ostringstream`; it now uses a non-retaining null sink. No semantic or
   LowIR text is a production transport.

Independent review found no other PA16 correctness, semantic reconstruction,
whole-program retry, fallback lookup, external-tool, test-specific, or
scaling blocker.

## Changes

- Replaced global cache generations with stable `(ScopeId, NameId,
  LookupKind)` entries, flat `(scope,name)` dependency owners, direct visited-
  scope dependencies, cache-fact edges, generation-qualified reverse links,
  and iterative dependency-cone invalidation.
- Name insertion now invalidates only its exact owner; using-edge insertion
  retains deliberate whole-scope invalidation because it may affect any name.
  Negative results use the same complete key and dependency model.
- Added two-entry inline reverse/dependency lists with geometric overflow and
  stale-link compaction; cache keys remain unique and invalid entries are
  refreshed in place.
- Added hits, misses, invalidations, dependency edges, and invalidation pushes
  to PA11/PA12 telemetry. LowIR statistics now own one nested semantic schema
  and expose specialization requests/hits, demand pushes/emissions, semantic
  peak bytes, typed bytes, IR sizes, and phase timers without duplicate fields.
- Replaced PA16 lowering's node-based maps with `FlatIdMap`, sorted compact
  constructor substitutions, and direct interned-literal-to-symbol storage.
- Replaced the unused semantic text buffer on the borrowed graph path with a
  null stream buffer.

## Performance Evidence

- Nested lookup, 2,000/4,000 scopes (five-run medians): 4,007/8,007 queries,
  2,006/4,006 scope visits, 4,002/8,002 hits, 4,005/8,005 dependency edges,
  zero unrelated invalidations, 1,902,379/3,801,195 peak semantic bytes,
  6.697/13.835 ms semantic, and 0.494/0.935 ms lowering.
- Same-name shadowing: one invalidation and one worklist push; generated LowIR
  stores through global `@g` before the declaration and local `$g` afterward.
- Cv/member/lifetime probe, 64/128 members (five-run medians): 64/128 layout
  visits, 271/527 access checks, 384/768 path visits, 471/919 instructions,
  72,163/133,603 typed bytes, 0.745/1.408 ms semantic, 0.411/0.729 ms lowering,
  and 0.192/0.342 ms rendering. Fixed demand remains 4 pushes, 3 demanded
  functions, and 1 default constructor at both sizes.
- Two calls to `hello<int>` record 2 specialization requests, 1 specialization
  hit, 1 demand push, and 1 declaration emission.
- Final Callgrind totals for the 2,000/4,000 lookup probe are
  80,111,465/157,901,321 instructions (1.97x). Token interning is the largest
  named self-cost at 25.76%; lookup-cache routines are below 0.01% self cost.

No timing ratio lacks matching source, semantic, dependency, or output growth,
so no unexplained slow path remains.

## Validation

- `make test-pa11`: 70/70 pass.
- `make test-pa15`: 108/108 pass.
- `make test-pa16`: 291/291 pass.
- `perl scripts/cppgm_file_audit.pl --stage pa16 --paths dev/src`: pass; six
  non-blocking header-division warnings.
- `make test-report-through-pa16`: **1,436/1,436 tests and 16/16 stages pass**.
- Forbidden-path scan: no implementation shell-out, reference binary, previous
  compiler, filename/source/test recognition, expected-output table, or text
  round trip.

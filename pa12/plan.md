# PA12 Final Audit

## Stage Design and Spec Alignment

`cppgm++ --emit-semantics` retains the immutable source buffer while the shared
preprocessor and PA10 parser build one syntax arena. `SemanticAnalyzer` consumes
that arena in process and records canonical PA11 `NameId`, `TypeId`, `ScopeId`,
`EntityId`, and `BindingId` facts plus a compact PA12 dump arena. Rendering is a
terminal view; no rendered name, type, signature, or dump text is reparsed.

Ownership is divided by responsibility: PA11 owns canonical identities and
indexed lookup; `pa12_semantic_declarations.cpp` owns declarations, declarators,
class/enum compatibility, and the limited template extension;
`pa12_semantic.cpp` owns expressions, conversions, statements, demand closure,
and rendering; `pa12_semantic_model.h` owns compact semantic/dump records; and
`pa12_semantic_tables.*` owns dense identity-keyed side indexes. All state has
translation-unit lifetime. This matches the PA12 surface of `spec.md` §§2–5 and
§§8–10. The complete PA10 syntax arena and PA12 semantic view coexist while this
staged tool runs; integrated parsing/semantic construction and the LowIR,
backend, and ELF ownership checks are future production-path work, not surfaces
provided by PA12.

Representative declaration trace: a qualified overloaded call is parsed to
syntax node IDs, its qualifier is resolved to one namespace owner, and the
owner/name index returns only that overload sequence. Each viable candidate is
ranked against canonical parameter `TypeId`s; the selected `BindingId`, result
`TypeId`, value category, and expression edges are retained in the dump arena
and rendered without a second lookup.

Representative demand trace: a limited function-template pattern retains its
parsed `NodeId`s once. Canonical template-pattern identity plus canonical
argument `TypeId`s key the specialization table. Selection marks the resulting
`BindingId` in a monotonic demand state; the typed worklist emits it once. PA12
does not implement general template-body substitution, which the assignment
explicitly leaves out of scope.

## Performance Evidence

Fresh release measurements used `CPPGM_FRONTEND_STATS=1`, three runs per size,
with output sent to `/dev/null`:

| Workload | Smaller | Larger | Scaling evidence |
|---|---:|---:|---|
| One-argument call chain | 1,024 links: 7.703 ms median, 10,234 lookup-scope visits, 899,392 semantic bytes | 2,048 links: 16.212 ms median, 20,474 visits, 1,798,464 bytes | 2.10x time; about 2.00x work/storage |
| Flat overload set with one exact `nullptr_t` match | 2,000 overloads: 14.132 ms median, 2,001 candidates, 4,000 ordering comparisons | 4,000 overloads: 28.053 ms median, 4,001 candidates, 8,000 comparisons | 1.99x time; 2.00x comparisons/storage |

The overload path previously measured 95.278/344.805 ms at 2,000/4,000
candidates. Typed signature indexing and linear champion verification reduce
those same sizes by about 6.7x/12.3x. Candidate, conversion, lookup, worklist,
specialization-cache, storage, and phase-time counters now explain the measured
work; no unexplained superlinear path remains.

## Architecture Review

- Representation/ownership: one source buffer, one post-token sequence, the
  shared PA10 syntax arena, canonical PA11 state, and a compact semantic view;
  deterministic text exists only at the tool boundary. No semantic text round
  trip, external compiler, reference executable, test-name dispatch, or cached
  answer exists.
- Identity/lookup: names, types, declarations, scopes, overload signatures,
  specialization arguments, function facts, and demand items use compact IDs.
  Qualified lookup stays within its nominated owner; lexical lookup follows
  explicit parent/using edges. No unrelated declaration registry is scanned.
- Templates/repeated work: the PA12 extension uses typed specialization keys,
  cache hits, stable specialization bindings, and monotonic deduplicated demand
  states. Non-template/template same-signature entities remain distinct, and an
  ordinary call prefers the non-template candidate while explicit template-id
  lookup returns only template candidates.
- Allocation/scaling: hot indexes use vectors and flat open-address tables;
  overload sequences keep two entries inline. There are no ordered node maps,
  `shared_ptr`s, per-fact `new`/`delete`, global retry loops, or whole-program
  fallback scans in PA12. Translation-unit storage is included in telemetry.
- Lowering/backend: not present at PA12. The selected declaration, conversion
  result, type, value category, and constructor action are retained by identity
  for a later typed consumer; no claim is made here about later LowIR/ELF stages.

## Final Architecture Review

The final tree has no correctness, performance, self-containment, ownership, or
file-division blocker for the PA12 contract. The audit corrected owner leakage
in qualified lookup, redeclaration identity, ordinary-name conflicts, demand
closure, cast/condition/operator validation, specialization identity, and two
quadratic registries. The remaining production-architecture gaps are exactly
the later-stage surfaces identified above, not hidden fallbacks in PA12.

## Checkpoint Ledger

| Checkpoint | Result | Evidence |
|---|---|---|
| Stage design and source-to-view trace | Pass | Canonical typed IDs throughout; deterministic rendering only at exit |
| Spec alignment and architecture checklist | Pass for PA12 surface | Ownership, identity, lookup, demand, allocation, observability, and self-containment reviewed |
| Correctness review | Pass | 166 handout tests plus 8 focused audit regressions |
| Scaling review | Pass | Call-chain and overload workloads scale with measured semantic work |
| File audit | Pass | `perl scripts/cppgm_file_audit.pl --stage pa12 --paths dev/src` |
| Through-stage validation | Pass | `make test-report-through-pa12` |

## Findings

The audit found eight ownership-path defects: qualified lookup could escape its
nominated namespace; member cv was omitted from redeclaration identity;
variable/function conflicts were accepted; overload declaration/selection and
demand deduplication had quadratic paths; deferred functions could discover
constructors after the constructor pass had ended; specialization keys used
rendered strings and merged with non-templates; anonymous-union injection
scanned unrelated bindings; and several switch, compound-assignment, and cast
constraints were under-validated.

## Changes

Lookup now has an owner-bounded qualified path. Canonical function signatures
include parameter normalization, variadic state, and member cv. Dense side
indexes replace string/unordered registries; overload selection uses one flat
conversion table and linear champion verification. Template specializations
have typed keys and distinct identity. Constructor/function demand shares one
monotonic closure loop. Anonymous-union members are entity-indexed. Switch
conditions, compound assignments, and explicit cast families enforce their
supported semantic categories. Eight regressions cover every corrected path,
and telemetry exposes the new work/cache/emission counters.

## Validation

Final required gates pass from the audited tree: the PA12 file audit reports no
findings, and the through-PA12 report passes all tracked tests and all 12 stages.

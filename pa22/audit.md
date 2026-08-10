# PA22 Final Audit

## Findings

1. **Specialization keys owned variable-size semantic payloads.**
   `TemplateSpecializationKey` copied and recursively hashed complete
   `vector<TemplateArgument>` values and pack-partition offsets in every class,
   function, alias, and variable-template cache entry. Results were correct,
   but completed-key equality was not O(1), duplicate payload survived for the
   translation unit, and the implementation contradicted the plan's claimed
   canonical argument-list identity.

2. **The lowering boundary retained earlier-phase owners.**
   `GraphLowerer` was invoked from `SemanticAnalyzer::Consume`, inside the PA10
   syntax call. Tokens, parser state, `SyntaxArena`, retained template patterns,
   substitution/lookup caches, and demand scratch therefore coexisted with the
   growing `TypedProgram`. `Program::NameTable` also borrowed the syntax-local
   string interner, preventing a simple early release.

3. **The checkpoint documents were not a final-stage audit.**
   They described only the last local-static and hidden-friend checkpoints and
   omitted the complete stage architecture, final checkpoint ledger, and
   comparative specialization-key evidence.

No functional correctness failure, missing PA22 semantic fact, fallback lookup,
global retry, textual phase transport, external compiler/reference dependency,
or test-specific branch was found in the final PA-wide trace.

## Changes

- Added `TemplateArgumentListId` and a translation-unit-owned flat interner.
  Canonical argument payload is stored once, collisions receive full typed
  equality checks, and owning entities/bindings retain both identity and the
  legacy typed range needed by ABI/lowering consumers.
- Added `TemplateArgumentPartitionId` and a flat partition interner. Empty
  partitions use identity zero; non-empty function-pack shapes are stored once.
- Reduced `TemplateSpecializationKey` to fixed-size pattern/list/partition IDs
  and converted function, class, alias, variable, explicit-specialization, and
  default-argument cache paths to construct canonical keys.
- Added list/partition request, hit, and probe counters to semantic stats,
  multi-source aggregation, storage accounting, and `CPPGM_FRONTEND_STATS`.
- Added `SemanticGraphStorage` as the explicit owner of interned names,
  `Program`, `DumpArena`, namespace/local-static actions, aggregate helpers, and
  polymorphism facts. The internal syntax driver may populate an externally
  retained interner; semantic analysis then ends, its analyzer is destroyed,
  and only the borrowed graph view is passed to lowering.
- Replaced the stale checkpoint-only plan/audit with the consolidated final
  architecture review and ledger.

## Performance Evidence

The detached `1b35885a` comparison and repaired 16/32/64 key workload had
identical LowIR and typed storage. Semantic peak bytes changed from
739,473/1,471,137/2,905,105 to
718,241/1,428,529/2,819,745, a linear
21,232/42,608/85,360-byte reduction. Repaired seven-run semantic medians were
3.551/6.901/13.478 ms versus baseline 6.635/17.357/25.463 ms; lowering medians
were 0.148/0.238/0.486 ms.

The two-pack partition probe stored one partition for 64/128/256 requests:
hits were 63/127/255 and probes exactly matched requests. Semantic medians were
5.648/11.981/25.929 ms and lowering medians 2.463/4.489/8.530 ms. A final
post-ownership-split sweep again doubled semantic nodes, edges, requests,
output, and storage with each doubling of N; semantic medians were
1.502/2.625/5.038 ms and lowering medians 0.111/0.168/0.262 ms. There was no
unexplained superlinear counter or timing path to profile.

## Validation

- `make test-pa22`: pass, 308/308 handout tests and 2/2 course tests.
- `make test-report-through-pa22`: pass, 2,639/2,639 tests and 22/22 stages.
- `perl scripts/cppgm_file_audit.pl --stage pa22 --paths dev/src`: pass with
  13 inherited advisory header-division warnings and no fatal issue.
- `git diff --check`: pass.
- Production source-set self-containment scan: pass; process spawning is
  confined to the test runner and no compiler/reference/test recognizer is on
  the PA22 compiler path.
- Final worktree after the audit commit: clean.

## Checkpoint Audit Ledger

| Checkpoint | Evidence and disposition |
|---|---|
| Canonical partial selection (`5d70a120`, `26eafabe`) | Owner/revision/substitution survives completion; canonical selected identity reaches lowering; pass. |
| Member-template identity (`da807b9f`, `07d7ba8b`) | Distinct template heads and current specialization sets remain owner-indexed; pass. |
| Entity and nested ownership (`b0f34797`-`f3799486`) | Alias, explicit, friend, partial-member, nested, replacement, and callable paths use stable owner/entity IDs; pass. |
| Dependent replay and packs (`4d266b9d`-`6ca7ac46`) | Parsed nodes are retained once, nested scopes are overlays, pack/proxy/alias paths use typed facts, and demand is bounded; pass. |
| Qualified replay through value materialization (`b17a9d97`-`1e30d5ab`) | Qualified lookup, callable shape, static initialization, conversions, and direct class values retain selected identities; pass. |
| Retained calls (`c230676a`, `0ab2a3f2`) | Comparable typed parameter patterns and aggregate-prvalue lifetime facts; pass after audit repair. |
| Captureless closures (`cc87146e`, `2223f1e0`) | Closure entities and callable-owned ABI exceptions are canonical; pass after audit repair. |
| Discarded demand (`605d7e99`, `c71d08de`) | Typed provenance and source-order deduplicated demand; pass after telemetry repair. |
| Scalar conversions (`3c0c79e2`, `742d8f6c`) | One packed assignment-owned immediate fact; pass after compact-fact repair. |
| Local statics (`5c2f9691`, `4981a933`) | ABI identity is separate from presentation and recursive analysis snapshots stable typed facts; pass after ownership repair. |
| Hidden-friend class result (`1b35885a`) | Selected friend binding, direct-result call, and destination transfer are preserved; pass. |
| Final PA-wide audit (this commit) | Canonical specialization IDs, strict graph lifetime boundary, telemetry, comparative scaling, full suite, and file audit all pass. |

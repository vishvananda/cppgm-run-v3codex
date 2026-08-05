# PA7 Final Semantic Audit

## Stage Design and Spec Alignment

The PA7 path is `immutable source buffer -> PA1-PA5 PreprocessFile callbacks ->
one compact phase-7 token array -> recursive-descent parse with immediate
semantic construction -> canonical translation-unit graph -> nsdecl view`.
The token array is the one retained parser cursor for this staged tool; it is
destroyed when construction returns. There is no syntax-tree copy, textual
semantic transport, or parse-tree owner. Each command-line source owns a fresh
model and is rendered before that source, its transient tokens, and its graph
are released.

Identifiers use interned `NameId`s; types, namespaces, declarations, and using
edges use stable compact IDs. Canonical types live in one flat hash-consed
table. A central `(scope, name)` flat binding index records the relevant name
kinds without per-scope node maps. Using directives and implicit unnamed/inline
edges are indexed once in a central edge table with explicit outgoing and
reverse links. Graph lookup facts are keyed by `(start scope, NameId, kind)`;
only graph-traversing results are retained, binding changes invalidate the same
name through reverse reachability, and edge changes invalidate affected scope
generations. Output order is independent linked source order for variables,
functions, and child namespaces.

This is the PA7 surface of `spec.md` sections 1-3 and 8-10: one forward typed
path, parse-time semantic construction, canonical identity, direct scope/name
indexes, explicit using edges, compact centralized ownership, linear parser
state, bounded declarator memoization, precise cache invalidation, counters,
and self-containment. PA7 has no
templates, expressions requiring instantiation, lowering, machine IR, backend,
or ELF path, so sections 4-7 and the checklist's demanded-template-to-ELF trace
are genuinely unavailable and deferred rather than approximated.

Representative trace: `int (*fpif(int))(int)` reaches PA7 as compact token
kinds plus interned names; declarator frames compose the inner function,
pointer, outer function, and return type once; the type table interns each
compound node by child/parameter IDs; the global binding maps `fpif` to one
`EntityId`; rendering follows that identity to produce `function fpif function
of (int) returning pointer to function of (int) returning int`. No spelling is
used as semantic equality and no structured data is rendered and reparsed.

## Findings

1. **Lookup/repeated work — closed.** Repeated uses of one type through a deep
   using-directive chain re-walked every reachable edge. At depth/use count
   1,600 this produced 2,561,600 edge visits and 38.37 ms stage time.
2. **Indexing/allocation — closed.** Duplicate using-edge insertion scanned the
   owning scope's vector, and every namespace owned eager hash/vector objects.
   A wide set of directives therefore had a quadratic insertion bound and
   namespace-heavy input caused many small allocations.
3. **Parser/lifetime robustness — closed.** Declarator, namespace rendering,
   and type rendering used the native call stack. A valid 20,000-level
   parenthesized declarator segfaulted even though token work was linear.
4. **Parser repeated work — closed.** Nested abstract function parameters
   retried named and abstract branches at every level: depth 20 built 2,097,150
   declarator frames and depth 25 exceeded five seconds. Session-scoped memo
   facts now compute each `(token position, declarator mode)` once.
5. **Observability — closed.** Lookup/declarator caches, declarator frame work,
   parser memo/scratch, and the source contribution to peak stage storage were
   not exposed.
6. **Correctness protection — closed.** Precise lookup-cache invalidation needed
   proof for both later declarations in a nominated namespace and later edges in an
   already-reachable graph. A reference-generated regression now covers both.

No correctness, self-containment, timeout, file-audit, architecture, or
performance finding remains open.

## Changes

- Centralized bindings and using edges into flat composite-key indexes; replaced
  per-namespace entity/child/edge vectors with compact linked IDs and added
  reverse using-edge ownership.
- Added graph-result caching with narrow admission, one-entry positive reuse,
  and reverse-reachability invalidation. Direct lookups remain a single binding
  probe and one-off results do not inflate the cache.
- Replaced recursive declarator grouping with explicit frames and made
  namespace parsing/rendering and type rendering iterative. Qualified names
  keep their common four-segment case inline. Semantically nested function
  declarators use a session-scoped flat memo and an explicit 4,096-level
  translation limit, converting deeper input to a diagnostic instead of stack
  overflow.
- Added lookup and declarator cache, declarator-frame, parser memo/scratch, and
  source-inclusive stage-storage telemetry.
- Added `500-lookup-cache-invalidation.t` and `600-deep-function-type.t`; both
  fixture sets were generated only with the documented PA7 reference target.

## Performance Evidence

Release `dev/nsdecl`, `/dev/null`, `CPPGM_FRONTEND_STATS=1`:

| Workload | Representative final evidence |
| --- | --- |
| Repeated using chain, N=1,600/3,200/6,400 | 1,601/3,201/6,401 edge visits; 1,599/3,199/6,399 cache hits; exactly one cache miss and one retained entry; 15.29/30.10/66.72 ms; 1.18/2.37/4.73 MB peak accounted stage storage |
| Wide using fan-out, N=4k/8k/16k/32k | exactly N edge insertions, N direct scope visits, zero graph-edge visits and zero cache entries; 30.04/54.54/94.56/219.51 ms; 1.41/2.81/5.64/11.30 MB |
| Parenthesized declarator depth 4k/8k/16k/32k | 4,001/8,001/16,001/32,001 frames; 6.57/14.85/28.41/76.53 ms; explicit scratch 0.56/1.11/2.23/4.46 MB; all accepted |
| Nested abstract function depth 500/1k/2k/4k | 1,998/3,998/7,998/15,998 frames; 996/1,996/3,996/7,996 memo hits; 2.69/4.85/9.61/19.47 ms; the 4,096 limit fails deeper input cleanly |
| Namespace depth 10k/20k/40k | 45.31/87.52/167.22 ms; explicit scratch 65,672/131,208/262,280 bytes; all parsed and rendered without native-stack recursion |

The pre-fix profile assigned 83.33% of sampled time on a 3,200 chain to
`ResolvePrefix`, matching the quadratic counter growth. The final 20,000-chain
profile attributes the largest share to 60,002 required `SearchGraph` calls;
counters prove those calls perform only 20,001 total edge visits and retain one
graph result. There is no retry pass, unrelated declaration scan, or hidden
fallback. On alternating 12,800-chain runs telemetry-off and telemetry-on were
both 0.41-0.45 s wall; RSS was 11.2-11.3 MiB off and 11.6-11.7 MiB on, so the
instrumentation cost is visible and separable.

## Architecture Review

- **Representation/ownership:** one immutable source, one compact transient
  PA7 token array, one canonical semantic graph, and short-lived parser/output
  stacks. The token owner dies after construction; IDs, not earlier-phase
  pointers, cross into the graph. No syntax graph or text round trip exists.
- **Identity/lookup:** interned `NameId`, `TypeId`, `NamespaceId`, `EntityId`,
  and `UsingEdgeId` are the hot keys. Type equality is ID equality after flat
  hash interning; direct bindings are indexed by `(scope, name)` and kind fields.
  Lookup visits only direct scopes and explicit reachable using edges.
- **Repeated work/invalidation:** expensive graph results are cached at their
  start scope. Reverse edges invalidate only graph predecessors; binding
  mutation invalidates only the changed name. Cycles use generation marks and
  an explicit worklist. There is no whole-program retry or global cold-cache
  generation. Declarator ambiguity is memoized only within one fixed semantic
  session, with position and mode as the complete key.
- **Allocation/scaling:** namespaces, entities, bindings, edges, cache entries,
  types, and parameters are centralized geometric vectors and flat indexes.
  Common qualified names are inline. Deep grouping, namespace, and renderer
  state uses explicit geometric stacks; function-declarator recursion is
  memoized and bounded; destruction is non-recursive.
- **Determinism/self-containment:** source ordinals and linked insertion order
  define the required view independently of hash layout. Static searches and
  the full call path show only the shared in-process PA1-PA5 implementation and
  PA7 semantic engine: no compiler/reference subprocess, fixture branch,
  filename shortcut, cached answer, or process-global mutable TU state.

## Final Architecture Review

**PASS.** All applicable PA7 requirements are represented by the actual owner
path and measured counters. The prior Cartesian lookup work, exponential
declarator retries, linear edge deduplication, per-scope allocation, unbounded
grouping/namespace/type traversal, and telemetry gaps are closed across their
owners. The stage remains a self-contained semantic tool boundary and does not
pull unavailable template/lowering/backend behavior forward.

## Validation

- PA7 focused suite: PASS, 43/43 including both reference-generated audit
  regressions.
- ASan+UBSan PA7 suite: PASS, 43/43 with leak detection; a 40,000-level
  declarator stress also passes under sanitizers.
- Multi-translation-unit order and per-source semantic/stat isolation: PASS.
- `-Wall -Wextra -Werror` audit compile: PASS.
- `perl scripts/cppgm_file_audit.pl --stage pa7 --paths dev/src`: PASS, 23
  files checked.
- `make test-report-through-pa7`: PASS, 332/332 tests and 7/7 stages.

## Checkpoint Ledger

| Checkpoint | Result |
| --- | --- |
| CP1: initial PA7 namespace/type/declarator implementation | closed at 41/41 PA7 and 330/330 through PA7 |
| CP2: independent contract/spec/history/source/data-flow review | closed; six findings identified across lookup, indexing, allocation, recursion, repeated parsing, telemetry, and invalidation proof |
| CP3: whole-owner architecture refactor | closed; central flat identities/edges, reverse invalidation, narrow graph cache, iterative traversal, declarator memo, inline name paths |
| CP4: correctness and robustness validation | closed; two reference regressions, multi-TU isolation, warning-clean build, 43/43 sanitizer suite, 40k sanitizer stress |
| CP5: final performance and required gates | closed; linear counters, profile reviewed, file audit pass, 332/332 through-stage pass |

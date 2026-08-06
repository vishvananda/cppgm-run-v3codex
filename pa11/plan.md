# PA11 Final Audit Plan

## Stage Design and Spec Alignment

`--emit-types` reads each source into one immutable buffer, preprocesses into
the compact PA10 token/syntax arena once, traverses that read-only arena into a
single PA11 semantic graph, renders the required dump, and releases all
translation-unit state before advancing to the next input. Syntax and semantics
share `frontend_intern.*`; tokens, semantic names, scope indexes, type nodes,
entities, and bindings use compact IDs. Display token descriptions remain a
PA10 view while separate interned semantic payloads prevent a text round trip.

`pa11_model.*` owns canonical types, declaration identities, scopes, flat
`(scope,name)` and `(scope,using-target)` indexes, explicit using edges, lookup
work storage, and iterative rendering. `pa11_semantic.*` owns the one-pass AST
consumer, declarator construction, required constants, and phase telemetry.
`cppgm++.cpp` owns source/TU framing. This aligns the available PA11 surface
with spec §§1–3 and 8–10. Templates stop at parameter scopes as required by the
assignment; specialization, lowering, machine IR, and ELF are later-stage
surfaces and are not fabricated here.

## Performance Evidence

- Deep pointer type, 16k/32k/64k/128k layers: 0.02/0.04/0.08/0.17 s wall,
  7,140/10,452/16,656/28,980 KiB RSS, and 16,001/32,001/64,001/128,001
  rendered type-node visits. Render time was 0.47/0.96/1.95/4.13 ms with a
  fixed 384-byte inline render-stack footprint. The pre-fix 32k case
  segfaulted and the 16k case took 0.05 s.
- Indexed qualified declarations, 1k/4k/16k declarations plus uses: exactly
  2k/8k/32k lookup-scope visits and 2k/8k/32k rendered type visits; storage,
  RSS, and elapsed work followed input/output growth.
- Canonical using graph, 1k/4k/16k imported scopes: 1k/4k/16k edge visits,
  5,001/20,001/80,001 scope visits, 2,856/11,301/43,307 edge-index probes,
  and 0.02/0.07/0.22 s wall. Before edge indexing and hash finalization, the
  250/1k/4k analysis times were 0.7/4/40 ms; final probe counts are linear.
- Irrelevant statement expression, 16k/64k/128k operators: semantic analysis
  stayed at 20–34 microseconds and two rendered type visits while total stage
  time/RSS scaled with the necessarily parsed syntax.
- Repeating the representative TU 64/256/1,024 times took 0.00/0.02/0.06 s,
  produced 16,652/66,730/267,204 bytes, and used 4,396/4,432/4,872 KiB RSS,
  confirming per-TU reclamation.
- A post-fix 8k-pointer Callgrind profile attributed 986,234 of 51,897,753
  instructions (1.9%) to iterative type rendering; tokenizer lookahead was the
  largest named self-cost (19.1%), so no unexplained PA11 render hot path
  remained.

## Architecture Review

- Representation/ownership: one compact token vector and syntax arena feed one
  semantic graph; source, syntax, semantic, and render owners/lifetimes are
  explicit. No semantic spelling copy, serialized-token parse, semantic-tree
  clone, process-global cache, or external tool path remains.
- Identity/lookup: types, declarations, names, entities, and scopes have stable
  IDs. Qualified names become inline-first `NamePath` IDs at the boundary.
  Direct flat indexes and explicit using edges bound lookup to lexical parents
  and relevant imported scopes; canonical declaration IDs distinguish real
  ambiguity from the same declaration arriving by multiple paths.
- Templates/repeated work: PA11 only creates parameter entities/scopes; template
  bodies, substitution, and specialization demand are explicitly out of scope.
  The parser still parses each source region once and semantic traversal does
  not replay grammar or walk irrelevant expression subtrees.
- Lowering/backend: unavailable before their assignments. PA11 emits only its
  deterministic semantic view and introduces no textual IR or hosted fallback.
- Allocation/scaling: long-lived records are contiguous vectors/flat tables;
  common qualified names and render stacks stay inline; lookup reuses one
  worklist; render traversal is iterative; all table growth is geometric.

## Final Architecture Review

The final data-flow trace covers namespace alias + using lookup, a qualified
scoped-enum declaration/definition/enumerator use, canonical typedef ambiguity,
deep declarator rendering, and irrelevant statement syntax. Each semantic fact
is created at its owning layer and consumed by ID. Source-order dump placement
is separated from semantic scope parentage for qualified definitions. No
remaining PA11 correctness, self-containment, timeout, file-audit,
architecture, or performance blocker is known.

## Findings

1. Qualified scoped-enum definitions created a second entity and could not
   resolve their own enumerators.
2. Recursive type/scope rendering was quadratic and stack-unsafe at 32k type
   layers.
3. Syntax and semantics duplicated interned names; token descriptions and
   qualified strings were parsed as semantic keys.
4. Using lookup stopped at the first imported declaration and lacked canonical
   declaration identity, hiding ambiguity.
5. Using-edge duplicate checks and weak composite hashing caused quadratic
   construction/index behavior.
6. PA11 recursively walked expression trees even though statement expression
   semantics are out of scope.

## Changes

- Added the shared front-end interner and structured semantic token payloads.
- Added canonical binding IDs, inline name paths, indexed scope/using tables,
  complete candidate traversal, ambiguity checks, and reusable lookup storage.
- Reused the declared enum entity across qualified definitions while retaining
  the required deterministic dump placement.
- Replaced recursive renderers with bounded inline-first iterative stacks and
  added phase, work, index-probe, depth, storage, and render-node telemetry.
- Limited statement traversal to nodes that can own blocks/declarations.
- Added PA11 regressions for qualified enum-member lookup and same-type
  using-directive ambiguity.

## Validation

- `make test-pa10`: pass, 157/157.
- `make test-pa11`: pass, 68/68 local plus 2/2 course regressions.
- Final ASan/UBSan PA11 suite: pass, 68/68 plus 2/2 course regressions.
- `perl scripts/cppgm_file_audit.pl --stage pa11 --paths dev/src`: pass.
- `make test-report-through-pa11`: pass, 646/646 tests and 11/11 stages.
- `git diff --check`: pass; the cohesive audit commit and post-commit clean
  status are verified at handoff.

## Checkpoint Ledger

| Checkpoint | Result |
|---|---|
| Contract, spec, commit, source, and plan reconstruction | complete |
| End-to-end architecture and representative data-flow trace | complete |
| Correctness/identity/lookup findings and regressions | complete |
| Scaling measurements and targeted profile | complete |
| File audit and focused PA10/PA11 validation | pass |
| Sanitizer and required through-PA11 exit gate | pass |
| Cohesive audit commit and clean status | pass |

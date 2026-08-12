# PA28 Final Audit

## Checkpoint Ledger

| Area | Independent conclusion | Status |
|---|---|---|
| Requirements | PA28 requires shared virtual-base layout/access, adjusted primary and secondary dispatch, sibling pointer casts, non-primary RTTI, and the supported simple lifecycle boundary. | closed |
| Representation | PA28 facts extend the canonical semantic graph and flow directly into typed LowIR; textual LowIR is terminal output only. | pass |
| Shared layout | Complete objects own one ordered virtual-base layout; direct edges and complete-object offsets now have a canonical flat index. | fixed |
| Final overriders | Two incomparable overrides of one shared virtual slot were previously order-selected instead of rejected. | fixed |
| Boundary ABI | Hidden virtual-base contracts are cached per binding, but nested member chains repeatedly rediscovered their root binding. | fixed |
| Multi-view ABI | Physical views, aliases, address points, VTT subtrees, RTTI rows, receiver adjustments, and distinct destructor entries are explicit facts. | pass |
| Identity | Adjusted thunks were cached by rendered `symbol:offset` strings in a node-based map. | fixed |
| Performance | Virtual-base/view lookup and direct-base duplicate checks contained avoidable repeated scans. | fixed |
| Self-containment | No host/reference compiler call, fixture lookup, filename/source dispatch, cached answer, or LowIR text round trip was found. | pass |
| File ownership | Audit additions initially crossed two source limits and one function limit; stats publication and virtual-base model ownership were separated. | fixed |

## Final Findings

No open PA28 correctness, architecture, performance, self-containment,
timeout, or fatal file-audit finding remains. The new compile-fail witness
proves that a virtual diamond with two incomparable final overriders is
rejected, while focused probes prove that a most-derived override and a single
dominant branch override remain valid.

The actual staged ownership is explicit. The assignment-required PA10 token
and `SyntaxArena` owners live through semantic construction, then are released.
`SemanticGraphStorage` owns canonical identities and facts through lowering.
`TypedProgram` owns LowIR after the graph consumer returns. The renderer is an
output adapter and no structured form is reconstructed from text. PA28 has no
machine-IR/ELF responsibility; its native handoff is tested through the PA29
consumer rather than an external compiler.

## Changes

- Added a flat canonical virtual-base index keyed by derived/base `EntityId`,
  with lookup/probe telemetry and storage accounting; moved that ownership to
  `pa28_virtual_base_model.cpp`.
- Replaced quadratic direct-base duplicate validation with a reusable epoch
  table and exposed exact validation visits.
- Replaced shared-virtual-view rescans with dense entity/epoch indexing. Slot
  merge now selects a uniquely more-derived canonical binding, preserves a
  dominant override, and rejects unresolved incomparable final overriders.
- Replaced repeated boundary-expression walks with a lazy node-indexed result
  cache. It is allocated only when PA28 behavior requests it and records steps,
  hits, and one-time table growth.
- Replaced rendered adjusted-thunk keys and `unordered_map` nodes with a typed
  flat `(SymbolId, adjustment)` index and request/hit/probe counters.
- Indexed direct virtual-base initializer matching and final polymorphic-view
  classification through the same canonical layout owner.
- Added the ambiguous virtual-diamond compile-fail regression and centralized
  telemetry publication/aggregation to keep file and function limits clean.

## Performance Evidence

Measurements use five-run medians from the untouched pre-audit PA28 binary and
the audited binary. Generated source and output mode are identical; LowIR is
byte-identical at the largest compared scale in each family.

| Family | Scale | Pre-audit | Audited | Improvement / bound |
|---|---:|---:|---:|---|
| nested boundary chain | 512 | 4.527 ms lowering | 1.729 ms | 61.8%; 514 binding steps |
| nested boundary chain | 1,024 | 15.124 ms | 3.662 ms | 75.8%; 1,026 steps |
| nested boundary chain | 2,048 | 55.043 ms | 6.596 ms | 88.0%; 2,050 steps |
| wide virtual-base list | 1,024 | 43.043 ms semantic | 38.657 ms | 1,024 validation visits |
| wide virtual-base list | 2,048 | 99.173 ms | 78.277 ms | 2,048 visits |
| wide virtual-base list | 4,096 | 253.663 ms | 166.780 ms | 34.2%; 4,096 visits |

At boundary depths 512/1,024/2,048, cache hits are
1,027/2,051/4,099 and lazy table growth is 520/1,032/2,056. At virtual-base
widths 1,024/2,048/4,096, canonical layout lookups are exactly
9,216/18,432/36,864 with 4,545/8,907/18,156 probes. These slopes are linear in
semantic nodes or queried base facts. The 2,048-depth boundary output remains
162,051 bytes in both binaries.

Representative required work remains small and explained: the forwarded
template has 7 specialization requests/4 hits and 9 carried boundary facts;
the multi-level lifecycle has 11 layout-edge visits, 4 unique facts, one shared
view merge, 24 vptr stores, and 28 offset rows; the destructor-view witness has
4 typed thunk requests, 2 hits, and 2 probes. No unexplained residual hot path
appeared in the PA28 ownership surface.

## Validation

| Gate | Result |
|---|---|
| Focused PA28 suite | 43/43 passing, including the new regression |
| Required through report | 3,857/3,857 tests; 28/28 stages passing |
| `perl scripts/cppgm_file_audit.pl --stage pa28 --paths dev/src` | pass; no fatal issues (21 advisory warnings) |
| `git diff --check` | pass |
| Generated output comparison | byte-identical LowIR for largest cases in both scaling families |
| Source audit | no shell-out, fixture access, source-name shortcut, or typed/text round trip |
| Worktree handoff | cohesive final-audit commit and clean `git status --short` |

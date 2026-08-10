# PA22 Final Audit Plan

## Current Stage Design and Spec Alignment

PA22's production endpoint is typed LowIR:

```text
immutable source
  -> streaming preprocessing into one interned SyntaxToken sequence
  -> PA10 SyntaxArena plus PA12 semantic construction
  -> retained canonical SemanticGraphStorage
  -> borrowed SemanticGraphView
  -> direct GraphLowerer construction of TypedProgram
  -> terminal LowIR rendering
```

The PA10 `SyntaxArena` is the staged assignment boundary in this compiler, so
the fully integrated parser/semantic builder in `spec.md` is adapted to that
available surface. There is still one parse: template patterns retain parsed
`NodeId` regions and instantiation substitutes and checks those nodes without
replaying token grammar. The final audit made the next boundary strict. PA10
tokens, parser state, syntax, substitution tables, lookup caches, retained
template patterns, and demand worklists are destroyed before lowering starts.
Only interned names, `Program`, `DumpArena`, typed object actions, aggregate
helpers, and polymorphism facts cross in `SemanticGraphStorage`.

Canonical types, scopes, entities, bindings, template argument lists, argument
pack partitions, specializations, and selected calls/conversions have compact
identity. A specialization cache key is now the fixed tuple `(pattern,
TemplateArgumentListId, TemplateArgumentPartitionId)`. Nested pattern identity
includes its canonical owner/substitution context, and a function-template pack
partition distinguishes argument-to-parameter shapes. Completion and emission
use monotonic states and deduplicated demand sequences. Lowering consumes the
selected `BindingId`, conversions, layout, lifetime, and ABI facts directly; it
does no lookup and uses no rendered semantic key or textual LowIR transport.

Representative trace: `box<int>::apply<add_one>` is parsed once; its member
pattern is attached to the canonical class-template owner; the specialization
key interns the argument list and partition once; a later out-of-class
definition upgrades the existing specialization; selected binding and
conversion facts are recorded in the dump; demand advances not-started ->
queued -> emitted once; and `GraphLowerer` emits the binding-indexed call from
those typed facts after all analysis scratch has died.

PA23 substitution-failure behavior and the machine-IR/direct-ELF backend are
outside the PA22 endpoint and were not treated as PA22 blockers.

## Performance Evidence

An isolated detached build of pre-audit `1b35885a` and the repaired tree were
run on identical alias/class specialization-key workloads. Seven-run medians:

| N | specialization requests / hits | list requests / hits / probes | baseline / repaired semantic peak bytes | repaired semantic / lowering ms |
|---:|---:|---:|---:|---:|
| 16 | 48 / 16 | 80 / 48 / 125 | 739,473 / 718,241 | 3.551 / 0.148 |
| 32 | 96 / 32 | 160 / 96 / 281 | 1,471,137 / 1,428,529 | 6.901 / 0.238 |
| 64 | 192 / 64 | 320 / 192 / 767 | 2,905,105 / 2,819,745 | 13.478 / 0.486 |

Output and typed storage were unchanged at 1,466/2,874/5,719 bytes and
11,311/21,007/40,399 bytes. Removing vector-owned keys saved
21,232/42,608/85,360 peak bytes and reduced semantic medians by
46.5%/60.2%/47.1% on this long-key path.

A second two-pack deduction workload produced 64/128/256 partition requests,
63/127/255 hits, and exactly one probe per request: one canonical partition was
stored and reused. Semantic medians were 5.648/11.981/25.929 ms and lowering
medians 2.463/4.489/8.530 ms for 16/32/64, with linear nodes, layouts, output,
and storage. Its semantic time did not regress against the detached baseline.

After the ownership split, a final seven-run 16/32/64 sweep reported semantic
nodes 325/645/1,285, edges 276/548/1,092, specialization requests
48/96/192, typed bytes 11,311/21,007/40,399, and semantic medians
1.502/2.625/5.038 ms. Lowering medians were 0.111/0.168/0.262 ms. All measured
work remained linear; counters explained the time, so no unexplained slow path
required sampling-profiler escalation.

## Architecture Review

| Checklist area | Final PA22 disposition |
|---|---|
| Representation and ownership | One compact token sequence and the assignment-required syntax arena exist during analysis. A dedicated graph owner survives that call; all syntax and analysis scratch dies before typed lowering. Source remains immutable. No structured data is rendered and reparsed. |
| Identity and lookup | Types/entities/bindings compare by ID. Template argument lists and pack partitions are flat, open-addressed interners with full collision equality; hot specialization-table equality/hash is fixed-size. Scope, overload, ADL, hidden-friend, and template sets use indexed compact sequences rather than global scans or presentation strings. |
| Templates and repeated work | Patterns are stable deque owners; scopes are parent-linked overlays; complete keys include owner-specific pattern, canonical arguments, and pack partition. Completion, member replay, and emission have monotonic states and bounded worklists. Candidate mismatch uses `bool`/`kNoBinding`; hard selected-program errors throw. No retry-all fixed point was found. |
| Lowering | `SemanticGraphView` exposes only canonical graph fields. Calls carry selected bindings and conversions; objects carry layout/lifetime/ABI facts. `GraphLowerer` directly constructs typed LowIR and performs no semantic lookup, name parsing, or semantic-node synthesis. |
| Allocation and scaling | Long-lived records use vectors/deques and compact IDs; dominant indexes are flat/open-addressed; there is no per-node `shared_ptr`. Argument-list payload and partition offsets are stored once. Phase owners now release tokens, syntax, and semantic scratch before typed IR growth. |
| Self-containment | Production source-set scans found no host compiler, reference-binary, filename/source recognizer, expected-output cache, or test-specific dispatch. The only process-spawn code is the separate test runner. |

## Final Architecture Review

No correctness, architecture, performance, self-containment, or file-audit
blocker remains for the PA22 surface. The production flow is one-way and typed,
specialization identity is compact and complete for the implemented contexts,
and phase lifetimes are explicit. The file audit passes; its 13 messages are
pre-existing advisory header-division warnings, not fatal findings. The PA10
syntax boundary is recorded as the staged-interface adaptation, and backend/ELF
questions remain assigned to later PAs.

## Checkpoint Ledger

| Commits/checkpoint | Final audit disposition |
|---|---|
| `5d70a120`, `26eafabe` | Partial-specialization selection retains canonical owner, revision, and substitution facts; pass. |
| `da807b9f`, `07d7ba8b` | Member-template attachment and distinct template-head identity; pass. |
| `b0f34797` through `f3799486` | Alias, explicit-instantiation, friend, partial-member, nested-owner, replacement, and callable ownership paths use stable entities; pass. |
| `4d266b9d` through `6ca7ac46` | Dependent replay, nested lifecycle, packs, proxies, alias forwarding, and short-circuit demand are scoped and monotonic; pass. |
| `b17a9d97` through `1e30d5ab` | Qualified replay, incomplete heads, dependent calls/declarations, callable shapes, static initialization, conversions, and class-value materialization; pass. |
| `c230676a`, `0ab2a3f2` | Retained-call acceptance and typed aggregate-prvalue facts; pass after checkpoint audit. |
| `cc87146e`, `2223f1e0` | Canonical captureless closure and ABI ownership; pass after checkpoint audit. |
| `605d7e99`, `c71d08de` | Discarded-result provenance and ordered demand; pass after telemetry repair. |
| `3c0c79e2`, `742d8f6c` | Specialized-member scalar conversions use compact assignment-owned facts; pass. |
| `5c2f9691`, `4981a933` | Local-static ABI identity and recursive initializer ownership; pass after lifetime repair. |
| `1b35885a` | Hidden-friend class-result fixtures match the existing selected-call/typed-transfer semantics; pass. |
| Final PA-wide audit (this commit) | Replaced vector-owned specialization keys with canonical list/partition IDs, separated graph ownership from syntax and analysis scratch, added key telemetry, and reran all gates; pass. |

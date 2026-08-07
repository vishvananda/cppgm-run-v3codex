# PA16 Plan

## Stage Design and Spec Alignment

PA16 full-stage is complete. The current path is:

```text
source buffer -> streaming preprocessing / compact tokens -> PA10 SyntaxArena
  -> PA11 canonical Program + PA12 DumpArena -> borrowed SemanticGraphView
  -> PA15/PA16 TypedProgram -> one final LowIR render
```

The PA10 tree is the assignment-mandated syntax boundary; adapting `spec.md` to
this staged surface, it is consumed once and never serialized between phases.
PA11 owns interned `NameId`, `TypeId`, `ScopeId`, `EntityId`, and `BindingId`
identity plus flat name/base/using indexes. PA12 records selected declarations,
conversions, layouts, access paths, initialization/lifetime actions, and
deduplicated demand states. PA15/PA16 lowering receives a synchronous borrowed
view, constructs typed LowIR directly, and retains no semantic pointers after
the callback. Textual LowIR is only the required terminal PA16 view.

Name lookup now has stable `(scope, name, kind)` cache entries. Direct
dependencies are owned by flat `(scope, name)` buckets; lexical reuse records a
cache-fact edge. Declaration insertion invalidates only the matching name and
its reverse dependency cone, while a using-edge insertion invalidates the
affected scope because any name can change. Reverse lists keep two entries
inline and spill geometrically. There is no TU-wide revision or global retry.

Current failures: none. PA16 is 291/291, PA1-PA15 are 1,145/1,145, and all 16
tracked stages pass (1,436/1,436 total).

## Performance Evidence

Five-run medians use the release compiler with `CPPGM_FRONTEND_STATS=1`.

| Boundary | 1x / 2x evidence |
|---|---|
| Nested lookup and unrelated mutation | 2,000/4,000 scopes: 4,007/8,007 queries, 2,006/4,006 scope visits, 4,002/8,002 hits, 4,005/8,005 dependency edges, **0/0 invalidations**; 1,902,379/3,801,195 semantic peak bytes; 6.697/13.835 ms semantic and 0.494/0.935 ms lowering medians |
| Cv/member/lifetime closure | 64/128 mutable members with fixed TLS and explicit destructor demand: 561/1,073 semantic nodes, 64/128 layout-member visits, 271/527 access checks, 384/768 path visits, 471/919 instructions, 72,163/133,603 typed bytes; 0.745/1.408 ms semantic, 0.411/0.729 ms lowering, and 0.192/0.342 ms render medians |
| Template declaration demand | Two calls to one deduced specialization: 2 specialization requests, 1 cache hit, 1 demand push, 1 declaration emission, and one typed external declaration |
| Inheritance projection | 64/128 single-base edges retain linear lookup/access visits and one physical zero-offset projection; prior checkpoint probe recorded 0.701/1.317 ms semantic medians |
| Instruction profile | The 2,000/4,000 lookup probe executes 80,111,465/157,901,321 Callgrind instructions (1.97x). Token interning is the largest named self-cost at 25.76%; lookup-cache routines are below the 0.01% self-cost threshold |

The same-name shadow probe invalidates exactly one entry and emits the first
store through `@g` and the second through local `$g`. Counter growth is
proportional to semantic input, dependency edges, or emitted IR; no unexplained
slow path remains.

## Architecture Review

- Representation/ownership: source, syntax, semantic, and typed output have
  explicit call/TU/program owners. Only boundary-conversion overlap exists;
  semantic storage is released before LowIR rendering. The lowering path uses
  a null stream sink and never renders semantics for reparsing.
- Identity/lookup: hot equality and keys use compact IDs. Name, using, ADL,
  hidden-friend, base, access-grant, signature, specialization, and symbol
  relationships are indexed. Strings are confined to source interpretation,
  diagnostics, literals, and output spelling.
- Templates/demand: the PA16 contract excludes template-backed object-model
  behavior. The supported function-template declaration path uses pattern ID +
  canonical `TypeId` arguments, memoizes specialization identity, and emits a
  deferred declaration once. Constructor/destructor/function demand uses
  monotonic states and deduplicated worklists.
- Lowering: selected bindings, conversions, projections, layouts, ABI roles,
  and lifetime actions cross the borrowed graph directly. Lowering performs no
  semantic lookup, type-string parsing, LowIR reparse, or whole-program retry.
- Allocation/scaling: semantic and typed nodes use vector/arena storage; hot
  indexes are flat. Lookup reverse lists are inline-first. PA16 label,
  constructor-substitution, and literal-pooling maps use flat or direct-ID
  storage rather than node-based maps. Traversals use bounded local sequences
  or iterative worklists.
- Self-containment: the implementation has no reference-binary, host-compiler,
  filename, source-text, test, or expected-output path. Native machine IR and
  ELF checks are not applicable until the later backend assignments; PA16's
  normative terminal artifact is textual LowIR.

## Final Architecture Review

No correctness, architecture, performance, self-containment, or file-audit
blocker remains for the PA16 surface. The required file audit passes with six
non-blocking header-division advisories; no duplicate implementation,
misplaced source, or new source-set entry is involved. The required through
report passes all 1,436 tests.

## Checkpoint Ledger

| Checkpoint | Stage commit | Final result |
|---|---:|---|
| Direct-member object spine | `9651d43c` | Pass: canonical layout/member facts and typed fields |
| Resolved member-call spine | `f7be4cf7` | Pass: object-aware ranking, hidden `this`, stable ABI IDs |
| Local aggregate-action spine | `76cd7bd0` | Pass: C++11 aggregate/union rules and bounded projections |
| Special-member initialization | `790bc91a` | Pass: ordered typed actions and exception/reference facts |
| Single-base construction | `76410798` | Pass: canonical base/access edges and retained projections |
| Destruction and cleanup | `79e34477` | Pass: reverse lifetime, lexical exits, shared cleanup suffixes |
| Namespace/static lifetime | `3a19722e` | Pass: TLS/linkage identity and ordered lifecycle functions |
| Operator/ADL callable spine | `f905d52f` | Pass: indexed ordinary/hidden-friend union and typed ranking |
| Access/base-path closure | `0d60dc0e` | Pass: indexed grants/signatures and protected-object rules |
| Layout/bit-field/inherited ctor | `f77bcbf8` | Pass: physical widths, scalar state, indexed ownership |
| Typed declarator/call boundary | `7727472a` | Pass: scoped parameter facts, canonical ABI/builtin metadata |
| Aggregate/value materialization | `ac6acf41` | Pass: complete omitted-member actions and ordinary demand |
| Friend/ADL call boundary | `3f6d32d5` | Pass: indexed friends, complete conversion keys, constructor checks |
| Empty-base projection | `53c686fd` | Pass: identity-indexed zero-offset separation and one projection |
| Cv/member/lifetime full stage | `74123da2` | Pass: 291/291 PA16 tests and proportional probes |
| Final PA-wide architecture audit | this commit | Pass: precise cache invalidation, flat hot maps, full telemetry and gates |

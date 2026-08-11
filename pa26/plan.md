# PA26 Plan

## Stage Design and Spec Alignment

PA26 remains a monotonic extension of the shared PA25 pipeline. PA10 owns the
single parse, PA12 publishes canonical declarations, `TypeId`s, `EntityId`s,
selected operations, layouts, lifetimes, template/closure facts, and demand,
and PA15 borrows that graph to construct typed LowIR directly. Textual syntax,
semantic dumps, ABI names, and LowIR are output views rather than in-process
transport or semantic identity.

The audited RTTI owner now records static/dynamic query and cast facts once,
defers conditionally evaluated operand demand until its static type is known,
walks only the reachable semantic graph, deduplicates dependencies by indexed
canonical type identity, and performs O(1) lowering symbol lookup. Array,
function, pointer, member-pointer, enum, fundamental, and class RTTI share this
owner. This aligns with `spec.md` sections 2-6 and 8-10: canonical identity,
precise demand, direct typed lowering, explicit phase ownership, observable
linear work, and self-contained implementation.

## Current Failure Map

Current augmented result is **30/110**: the original checkpoint remains
**26/106** and all four audit regressions pass. The remaining 80 failures are
unchanged and partition by their first owning behavior:

| Owner | Failing | Shared behavior |
|---|---:|---|
| EH and lifetime lowering | 36 | handlers, throw conversion, unwind snapshots, branch/full-expression cleanup |
| initializer-list and aggregate semantics | 26 | overload phase, backing arrays, ranges, class-element construction |
| lambda capture semantics/lowering | 17 | value/default/nested/template capture; includes two lambda/RTTI fixtures and the timeout |
| object construction lowering | 1 | nontrivial copy construction in the cv/reference `typeid` fixture |
| RTTI semantics/lowering | 0 | 14 independently reachable fixtures plus 4 audit regressions pass |

## Performance Evidence

Release compiler, three runs per point, one reachable static query per distinct
class. Counters are emitted by the production path under
`CPPGM_FRONTEND_STATS`; timing columns are medians.

| Queries | Reachable nodes | Demands / types / lookups | Semantic | Lowering | Typed storage | LowIR output |
|---:|---:|---:|---:|---:|---:|---:|
| 128 | 905 | 128 / 128 / 128 | 5.398 ms | 1.424 ms | 357,969 B | 80,463 B |
| 256 | 1,801 | 256 / 256 / 256 | 10.972 ms | 2.594 ms | 721,233 B | 162,947 B |
| 512 | 3,593 | 512 / 512 / 512 | 21.951 ms | 5.440 ms | 1,447,761 B | 329,347 B |
| 1,024 | 7,177 | 1,024 / 1,024 / 1,024 | 45.421 ms | 11.124 ms | 2,902,305 B | 662,483 B |

From 512 to 1,024, graph visits and canonical demand/lookups double exactly;
storage, output, and median phase times grow by about 2.0-2.1x. Peak RSS across
the same points was 7.5-7.6, 8.5-8.6, 10.5-10.9, and 15.4-15.7 MiB. A nested
unevaluated regression allocates 16 semantic nodes but the demand walk visits
only 9, demands one reachable type, and emits no inactive polymorphic RTTI.

## Next Substantial Checkpoint

Implement the lambda capture ownership slice: canonical by-copy/default
capture fields, nested/template closure ownership, and closure construction and
cleanup lowering. Start from the 17 mapped failures, make the existing nested
closure timeout a scaling gate, and preserve all RTTI and PA1-PA25 results.

## Completed Checkpoints

| Checkpoint | Result | Validation |
|---|---|---|
| Canonical RTTI demand and query lowering | Canonical queries/casts and ABI RTTI; audit closed conditional demand, unreachable scans, missing type categories, and cast legality | RTTI 14/17 (3 cross-owner), audit 4/4, PA26 30/110 augmented (original 26/106), through PA25 3,607/3,607, file audit pass |

# PA7 Implementation Plan

## Stage Design and Spec Alignment

`nsdecl` will reuse the PA1-PA5 streaming preprocessing/post-token path and add
an integrated PA7 recursive-descent semantic parser. One translation-unit
owner retains interned identifiers, canonical type IDs, namespace scopes, and
declaration identities; post-tokens are released after parsing and output is a
walk of that semantic graph. This applies `spec.md` sections 1-3, 8, and 9:
single-pass parse/semantic construction, O(1)-average canonical type identity,
per-scope direct name indexes, explicit using/inline edges, stable source
ordinals for output, and phase-owned storage/counters.

## Current Failure Map

No active PA7 failures. The driver/empty graph (2), namespace/lookup (22), and
canonical type/declarator (17) groups are resolved: 41/41 PA7 tests pass, up
from the 0/41 turn-start baseline.

## Active Checkpoint

Completed full PA7 semantic boundary: the stub is replaced by a self-contained
source-to-semantic-graph path covering all `pa7.gram` declarations, canonical
fundamental/compound types, namespace reopening/aliases/inline and unnamed
namespaces, qualified and unqualified lookup, using declarations/directives,
redeclaration and array completion, parameter adjustment, and reference
collapse. `dev/src/pa7_semantic.*` owns tokens, IDs, scopes, lookup, and types;
`dev/nsdecl.cpp` owns CLI/file/output sequencing. Data flows from phase-7 token
callbacks into compact token records, through one parser into the semantic
owner, then to the required view. Expected time is O(tokens + declarations +
lookup-reachable using edges), with O(1)-average direct binding/type lookup;
space is O(tokens + identifiers + types + declarations + edges). Validation:
focused PA7 runs, both deep course cases with counters/timing, full PA7 report,
PA1-PA6 report, and file audit.

## Performance Evidence

Release-build counters on `600-deep-parenthesized-declarator.t`: 4,004
post-tokens, one canonical type, zero lookups, 4.10 ms measured front-end time,
and 66,099 peak stage bytes. On `600-deep-using-directive-chain.t`: 2,205
post-tokens, 201 namespaces, 200 declarations/using edges, 599 lookup queries,
40,000 edge visits, 2.05 ms, and 162,419 peak stage bytes. The first case is a
single linear declarator parse; the second's work matches the 200 reachable
edges for each of 200 `T0` declarations, with no unrelated namespace scan or
retry pass.

## Completed Checkpoints

| Checkpoint | Result | Validation |
| --- | --- | --- |
| Full PA7 semantic graph and `nsdecl` view | Complete | PA7 41/41; PA1-PA6 289/289; file audit pass |

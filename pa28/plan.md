# PA28 Final Audit Plan

## Current Stage Design and Spec Alignment

PA28 extends the canonical PA27 pipeline in place:

`source bytes -> token/syntax owner -> canonical semantic graph -> typed LowIR -> terminal LowIR text`

The PA10 `SyntaxArena` is the assignment-mandated syntax boundary. It and the
token/parser scratch remain local to semantic construction and die before
lowering. `SemanticGraphStorage` then owns stable `EntityId`, `TypeId`,
`BindingId`, layout, polymorphic-view, selected-slot, conversion, RTTI, and
lifecycle facts. `GraphLowerer` borrows that graph and constructs `TypedProgram`
objects directly; LowIR text is rendered only as the requested PA28 output and
is never reparsed or used as an in-process transport.

Virtual bases are indexed by the canonical `(derived EntityId, base EntityId)`
pair. Each class owns one deterministic complete-object layout and explicit
primary, physical secondary, alias, and shared-virtual views. Shared views
merge final overriders by canonical binding and base identity. Lowering carries
only demanded virtual-base ordinals across function boundaries, uses explicit
complete/base constructor and destructor identities, and interns adjusted
thunks by the typed `(target SymbolId, this adjustment)` key. PA27-only inputs
do not allocate the PA28 expression-binding cache or virtual-base index.

## Performance Evidence

Five-run medians compare the preserved pre-audit binary with the audited
binary on identical generated input. Largest outputs in both families are
byte-identical.

| Witness | Scale | Pre-audit | Audited | Audited work |
|---|---:|---:|---:|---|
| nested boundary member chain | 512 | 4.527 ms lowering | 1.729 ms | 514 binding steps, 1,027 hits |
| nested boundary member chain | 1,024 | 15.124 ms | 3.662 ms | 1,026 steps, 2,051 hits |
| nested boundary member chain | 2,048 | 55.043 ms | 6.596 ms | 2,050 steps, 4,099 hits |
| wide direct virtual bases | 1,024 | 43.043 ms semantic | 38.657 ms | 1,024 validations, 9,216 lookups |
| wide direct virtual bases | 2,048 | 99.173 ms | 78.277 ms | 2,048 validations, 18,432 lookups |
| wide direct virtual bases | 4,096 | 253.663 ms | 166.780 ms | 4,096 validations, 36,864 lookups |

Boundary lookup had been quadratic because every nested member rediscovered
the same one-child binding path. The lazy node cache makes retained steps and
table growth linear (520/1,032/2,056 slots). Wide-base validation is now one
visit per direct edge; canonical virtual-base hash probes are
4,545/8,907/18,156 for the three scales above.

Representative stage witnesses also show bounded demanded work: the forwarded
template performs 7 specialization requests with 4 cache hits, scans 29
boundary nodes, and carries 9 facts/arguments; the multi-level lifecycle visits
11 layout edges for 4 virtual-base facts, merges one of 3 virtual-view lookups,
stores 24 vptrs, and emits 28 required offset rows. The destructor-view witness
performs 4 typed thunk requests with 2 cache hits and 2 probes.

## Architecture Review

- Representation and ownership: source/token/syntax scratch, canonical graph,
  and typed LowIR have explicit owners and phase deaths. No PA28 text round
  trip, fake semantic node, compiler shell-out, or reference lookup exists.
- Identity and lookup: layouts, shared views, slots, hidden contracts, and
  thunks use compact IDs and typed integer keys. Rendered names are confined to
  symbol presentation and diagnostics.
- Templates and demand: template specializations retain canonical argument and
  environment identities from the shared template engine. PA28 boundary
  contracts are computed once per defined binding and carry only demanded
  virtual-base ordinals; the expression cache is lazy and node-indexed.
- Lowering: calls consume recorded bindings, slots, offsets, receiver
  adjustments, RTTI hints, VTT slices, and ordered lifecycle actions. Each
  vtable, VTT, RTTI object, thunk, and destructor entry has a stable typed
  emission identity.
- Allocation and scaling: virtual layouts and adjusted thunks use flat
  open-addressed indexes; shared-view lookup uses reusable epoch tables;
  boundary discovery memoizes one result per visited semantic node. No
  retry-all loop or global invalidation was found.
- Backend adaptation: PA28 terminates at LowIR by contract. Native acceptance
  is covered by the PA28 checks using the PA29 handoff; direct ELF ownership is
  therefore outside this stage rather than replaced by a host compiler.

## Final Architecture Review

The independent audit closed one correctness defect and four related
identity/scaling defects: ambiguous final overriders in a shared virtual base
were silently order-selected; virtual-base lookup and shared-view deduplication
rescanned vectors; deep boundary expressions were rediscovered repeatedly;
direct-base duplicate validation was quadratic; and adjusted thunk caching
rendered string keys into a node-based map. All now follow their canonical
semantic or lowering ownership paths. No open correctness, architecture,
performance, self-containment, timeout, or fatal file-audit issue remains.
The 21 file-audit findings are advisory inherited header-division warnings,
including the already checked PA28 CRTP lowering header; no fatal finding
remains.

## Checkpoint Ledger

| Checkpoint | Final result |
|---|---|
| Shared virtual layout and hidden addresses | one deterministic shared layout; canonical indexed queries |
| Multi-view dispatch and inverse casts | physical/logical views, adjusted receivers, and slots pass |
| Virtual-base vtable rows | demanded address points and offset rows pass |
| Construction views and VTT forwarding | recursive subtrees and indexed slices pass |
| Demand-shaped value/copy ABI | cached per-binding contracts and minimal ordinals pass |
| Multi-view destructor ABI | complete/base/deleting identities and ordered cleanup pass |
| Cast control and RTTI views | canonical runtime hint and non-primary RTTI pass |
| Complete/base lifecycle split | most-derived-only virtual-base lifetime passes |
| Independent final audit | final-overrider, typed-index, and scaling fixes closed; all gates pass |

# PA24 Final Audit Plan

## Stage Design and Spec Alignment

PA24 composes the PA19-PA23 template engine through one canonical semantic
graph and the existing typed LowIR model. Source bytes are preprocessed into
the compact PA10 token/syntax representation required by the assignment, each
region is parsed once, and semantic analysis records interned names, canonical
types and arguments, selected declarations and conversions, specialization
states, lifetime actions, ABI recipes, and explicit emission demand. The
analyzer and all parser, syntax, lookup, substitution, and demand scratch are
destroyed before a borrowed `SemanticGraphView` is lowered synchronously into
`TypedProgram`; LowIR text is rendered once as the requested stage output.

The production `spec.md` checklist is adapted at two declared PA24 boundaries.
The README explicitly requires the PA10 AST, so the compact token vector and
one syntax arena coexist with semantic construction instead of an integrated
parser/semantic cursor; there is no duplicate parsed tree and syntax does not
cross the lowering boundary. PA24 ends at LowIR, so machine-IR, ELF, and direct
object-writer checks are outside this stage. Within the available surface,
canonical keys, owner-indexed lookup, monotonic specialization states,
parent-linked substitution scopes, dependent-only replay, explicit demand
queues, direct typed lowering, phase-local ownership, and self-containment are
all present.

Representative demanded data flows as follows:

- A qualified class-scope variable-template use is retained as a typed pattern,
  found through its owner/name index, selected and substituted under canonical
  arguments, memoized by `TemplateSpecializationKey`, published as one binding
  with static-member and initializer facts, then lowered as a typed global.
- The constructor/SFINAE integration case retains dependent defaults and ABI
  recipes on the pattern, deduces canonical arguments and pack partitions,
  records candidate-local failure, selected conversions, and constructor
  action identity, queues only the selected definition, plans reference-bound
  scalar slots in declaration evaluation order, and lowers the recorded action
  directly to typed calls and object storage.

## Performance Evidence

| Workload | Measured evidence | Conclusion |
| --- | --- | --- |
| Full checked-in PA24 source sweep | 422 inputs in 8.8 s; slowest process 27.3 ms including startup. The five slowest semantic phases were 8.74-13.48 ms; lowering was at most 0.45 ms and rendering at most 0.06 ms. | No unexplained test outlier or lowering/rendering retry path. |
| Competing explicit-id ADL, width 1/2/4/8/16/32/64/128 | Associated scopes 2/3/5/9/17/33/65/129; declarations 2/3/5/9/17/33/65/129; overload visits 8/12/20/36/68/132/260/516; conversion checks 10/16/28/52/100/196/388/772; seven-run median semantic time 0.600/0.721/0.903/1.212/1.847/3.147/5.939/11.365 ms. | The shared flat candidate set grows geometrically; lookup, candidate, conversion, storage, and time follow semantic width. |
| Canonical specialization integration | Explicit-specialization widths 1-32 produced 4-97 requests and 3-65 hits; qualified variable-template NTTP widths 1-32 produced 4-128 requests, 1-32 demand pushes, and 0.50-5.37 ms semantic time. | Complete-key lookup and newly demanded specialization work remain linear. |
| Candidate failure and deferred defaults | Trailing-result failure widths 1-64 produced 9-576 deduction visits and 54-2,952 requests in 2.24-57.54 ms; default-suffix widths 1-64 produced 12-201 deduction visits while semantic nodes stayed at 26. | Candidate-local failure and selected-only default materialization avoid global replay. |
| Construction, lifecycle, and ABI lowering | Empty transfer and nested-construction series were linear through width/depth 64; trivial arrays kept 12 instructions through extent 128; ABI-recipe widths 1-64 took 0.44-7.83 ms; reference-slot widths 1-64 produced 2-128 slots and lowering time 0.147-0.323 ms. | Demand, recipe traversal, and typed emission are proportional to selected actions and output. |

The slowest checked-in case had 3,247 declarations, 2,962 lookup queries, 439
specialization requests, 1,086 partial-deduction visits, and a 13.48 ms semantic
phase. The largest partial-replay case had 1,298 deduction visits and 11.12 ms
semantic time. These counters explain the observed time; neither case shows a
whole-program retry or a broad lowering scan.

## Architecture Review

- Representation and ownership: source ownership is explicit; one PA10 syntax
  arena feeds one canonical graph; analyzer scratch dies before direct typed
  lowering; no text is rendered and parsed back.
- Identity and lookup: hot semantic relationships use compact `NameId`,
  `TypeId`, `ScopeId`, `BindingId`, entity, template-argument-list, partition,
  and emission IDs. Structured ABI recipes own semantics; mangled strings are
  output metadata. Lookup follows lexical/associated owner indexes.
- Templates and repeated work: complete specialization keys contain pattern,
  canonical argument-list, and pack-partition identity. Request states
  distinguish not-started, in-progress, success, and failure; completion,
  member replay, default, exception, and emission facts have separate owners.
  Substitution scopes are parent-linked overlays and only retained dependent
  syntax is replayed.
- Scheduling: class-member, constructor, and function demand are deduplicated
  stable-ID queues drained by monotonic cursors. No retry-until-stable scan or
  translation-unit cache flush was found.
- Lowering: selected bindings, conversion/reference facts, object identities,
  layouts, lifetime actions, ABI recipes, and demand order cross the graph
  boundary. Slot planning and emission consume typed nodes without source
  lookup, mangled-name reconstruction, LowIR reparsing, or fake semantic nodes.
- Allocation and scaling: canonical tables and the repaired request-local
  candidate set are vector-backed open-addressed structures. Temporary
  collections are phase-local and geometrically grown; release telemetry
  exposes phase bytes/times, lookup/candidate work, specialization/cache work,
  demand edges, and IR size.
- Self-containment: the production frontend source set contains no host or
  reference compiler invocation, cached-answer path, filename dispatch, or
  expected-output recognition.

## Final Architecture Review

The audit found one PA24 architecture defect: reentrant ADL canonicalization
used a node-allocating `std::unordered_set<BindingId>` even though ordinary
function-candidate deduplication already had a private flat ID set. Both paths
now use one shared `FlatBindingIdSet` with contiguous slots, open addressing,
geometric growth, and first-seen order. Focused reentrant/explicit-id ADL tests,
the full PA24 suite, and width-128 competing-candidate measurements pass.

No open PA24 correctness, architecture, performance, self-containment, or
file-audit blocker remains. The file audit reports only nonfatal existing-header
division advisories; the shared set implementation is in a `.cpp` file.

## Checkpoint Ledger

| Checkpoint | Commit | Final disposition |
| --- | --- | --- |
| Explicit specialization identity and definition publication | `62f37ef1` | Canonical ownership preserved; eight failures removed. |
| Canonical NTTP/designator/static-value and variable-template identity | `6b06e092` | Typed values and storage demand preserved; fifteen failures removed. |
| Construction-entry demand and nontrivial empty-result ABI | `6a1a66c7` | Selected constructor demand remains monotonic and linear. |
| Explicit-id ADL and candidate-local invalidity | `4c694582` | Strengthened by the final shared flat candidate set. |
| Retained current-specialization dependency | `739eab0f` | Owner facts remain typed and width-linear. |
| Default-expression candidate completion and ordering | `b555a4eb` | Unselected defaults stay unmaterialized. |
| Dependent qualified parameter/result retention | `9ab41503` | Prior-parameter dependence survives replay. |
| Destination-consistent default/value construction | `5be465c9` | Trivial arrays avoid extent-proportional no-op work. |
| Identity-only typed declaration emission | `6a107535` | Declarations lower without body/environment reconstruction. |
| Direct reference conversions and runtime materialization | `f3724c7e` | Conversion candidate count stays bounded by the actual lookup set. |
| Empty-value lifecycle demand and destination ownership | `6a238d0f` | Empty transfer/object work remains linear. |
| Nested class construction recipes | `dfe38634` | Type and recipe identity survive nested lowering. |
| Pack deduction before class defaults | `30fe0986` | Pack partitions and default suffixes remain canonical. |
| Candidate-local calls and retained receiver demand | `f18a8293` | Failure and runtime demand remain request-local. |
| Function-template ABI recipes and local-static owner identity | `5db862b8` | Structured pattern ABI and local owner identity lower once. |
| Evaluation-ordered constructor reference slots | `4f832f86` | The final PA24 failure was removed with a linear typed prepass. |
| Stage-plan completion | `d32e64a6` | Reconciled with this independent audit and final ledger. |
| Final full-stage architecture audit | current audit | Flat candidate deduplication, end-to-end traces, profiling, file audit, and through-stage validation complete. |

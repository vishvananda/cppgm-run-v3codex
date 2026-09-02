# Plan: Low-Overhead Template Witness Observer

Status: in progress

Date: 2026-09-02

## Objective

Implement the production `cppgm++ --witness FILE` diagnostic so root
`make test-strict` passes, while preserving the compiler's normal semantic,
LowIR, MIR, native, object, executable, and performance behavior when witness
output is not requested.

The implementation will observe the compiler's existing typed template
decisions.  It will not recognize fixture text, reparse source spellings to
guess semantic results, shell out to another compiler, or add witness-only
facts to LowIR.  The PA19 student-facing contract will describe the diagnostic
at the semantic level: source template uses expose the selected template and
typed parameter bindings, while closure events expose template entities that
the translation unit actually instantiates, finalizes, or requires.

## Governing rules

1. Witness output is observation, never policy.  Enabling it must not alter
   lookup, overload resolution, deduction, specialization selection, demand,
   instantiation, lowering, optimization, or native emission.
2. The normal path owns no witness session, allocates no witness event storage,
   renders no witness names or types, and asks for no witness-only source
   locations.  A nullable observer is the only permitted normal-path cost at
   irreducible semantic publication points.
3. Prefer one observation after a semantic decision is final over hooks in
   speculative candidates.  Failed overload and SFINAE candidates are recorded
   only when the public witness contract calls for a final `drop` closure fact.
4. Retain compact typed IDs and enums while analysis is active.  Convert them
   to strings once, after `CompleteTranslationUnitDemand`, while the syntax
   arena, template patterns, canonical program, and source locations coexist.
5. Source events are generic semantic properties: class-template uses,
   alias-template uses, variable-template uses, and selected function-template
   calls.  Tests must not match production source, private implementation
   names, fixture basenames, whole-program hashes, or exact compiler internals.
6. Closure events are deduplicated semantic transitions, not every internal
   request.  Their stable order follows the existing semantic publication
   order required by the checked witness artifacts.
7. Reuse the current canonical type, template-argument, binding, entity, and
   source-identity renderers.  Do not create a parallel type system or copy the
   large thread-local witness/session machinery from `~/cppgm-extended`.
8. `--witness-debug FILE` may add observer diagnostics, but it must share the
   same collection path and cannot be required for the strict contract.
9. Output-file failures use the driver's typed input/output exception path.
   Semantic failures remain semantic failures and must not leave a plausible
   successful witness artifact.
10. Use 32-way outer, inner, and object parallelism for self/inception builds.
    Before timing, verify that no stale Cachegrind, Valgrind, `perf`, benchmark,
    or inception process is consuming the host.
11. Run focused checks after every bounded increment, commit each retained
    increment, and push after no more than three retained commits and after
    each high-risk observer or driver milestone.

## Starting evidence

The planning checkpoint is `bde8535d` on clean `v3opt`, synchronized with
`origin/v3opt`.

The driver already parses `--witness FILE` and `--witness-debug FILE`, but
discards both paths.  The strict harness therefore invokes a successful
compile and then fails because `.my.witness` does not exist.  The five strict
assignment views contain 1,532 cumulative executions over 415 unique PA24
fixtures; the prior audit found no compared-content mismatch because no
production witness was ever written.

The checked witness grammar has two parts:

- `translation-unit` source events: 2,640 class uses, 837 alias uses, 33
  variable uses, and 791 function calls across the cumulative reference set;
- `template-closure-events`: 1,314 function instantiations, 1,305 required
  definitions, 951 variable instantiations, 80 class instantiations, and 15
  class finalizations.

Those events contain canonical template/entity names, selection kinds
(`primary`, `partial`, `explicit`, `instantiation`, or
`explicit_specialization`), typed argument bindings with `explicit`,
`deduced`, or `defaulted` provenance, selected-partial bindings, and final
candidate-drop facts.  Source paths are rendered relative to the assignment
working directory (`tests/...`), even though the harness passes an absolute
input path.

The implementation opportunity is substantially smaller than the historical
extended implementation:

- `semantic::Analyzer::Consume` owns the syntax arena and all class, alias,
  variable, and function template patterns through the end of
  `CompleteTranslationUnitDemand`;
- canonical arguments already use `TemplateArgument` and specialization
  bindings already retain argument lists;
- source file, line, and column are available from `SyntaxArena`;
- canonical names and types are available from `Program` and the semantic
  presentation helpers; and
- the graph handed to lowering need not retain any witness-only state.

Clang's local template-metrics plugin is useful only as a design comparison:
it visits four typed source-use kinds and sorts them by source position.  It
does not justify importing the extended compiler's roughly 4,000 lines of
witness session/renderer state, source-capture pause stacks, or thread-local
coordination into this compiler.

The no-witness performance anchor is the final explicit-32-way O3 inception at
`bde8535d`: 223 self and 223 inception objects were byte-identical, the linked
binaries had SHA-256 `571653d5c3576c9098fe1903a85a0a275c8f87f98ea52897fd5c593dc86fe8c3`,
and the run took 45.66 seconds wall / 1,286.98 seconds aggregate CPU.  Because
host noise makes one run insufficient, retention will use repeated
same-source no-witness AB/BA workloads in addition to this anchor.

## Provenance-first correction

The PA22 convergence work is paused after comparing the implementation with
`~/clang.diff`.  Clang's patch is 2,227 insertions across eight files.  Its
source-use collector is mostly one finished-AST traversal: `TypeLoc`,
`TemplateArgumentLoc`, specialization declarations, expression locations, and
declaration contexts already keep source syntax attached to the final semantic
decision.  Only overload drops and lifecycle transitions require distributed
Sema hooks.

The cppgm implementation since `bde8535d` is 4,026 insertions across 33 files,
including a provisional 993-insertion PA22 increment across 14 files.  File
count is partly explained by cppgm's smaller semantic modules, but the content
shows a real design problem: the renderer searches token spelling, assigns
unused matching tokens to events, reconstructs explicit argument spellings,
propagates provenance between later events, and asks the retained-template
validator to infer source uses.  This violates the governing rule that the
observer records semantic decisions instead of reconstructing them.

Before further strict convergence, build the missing analogue of Clang's
source-location layer:

1. Every parsed qualified-name component and template-id argument uses its
   existing `SyntaxNode::token_first/token_last` fields as an authoritative
   half-open source range.  Do not enlarge `SyntaxNode` or add a parallel range
   vector.
2. A resolved template use is anchored on the exact name-component node, not a
   containing declaration, replay root, or whole qualified name.  The direct
   children of that component's template-argument list are the authoritative
   argument source nodes.
3. Semantic publication carries one compact source-use value: exact source
   node, typed selected pattern/binding, canonical arguments, argument source
   nodes, provenance, selection, and selected-partial bindings.  The value is
   allocated only by the optional observer.
4. Retained replay reuses the original component node as its provenance.  It
   must not recover a location by name search or by maintaining a global set of
   already-used tokens.
5. Constructor and overloaded-operator observations are published at the
   source-aware caller after inspecting the selected binding already stored in
   the resulting semantic dump node.  Do not thread a witness-only source
   parameter through initialization and expression APIs.
6. Default/deduced/explicit origin is captured when the argument list is
   completed.  It is not inferred by comparing later events with the same
   canonical specialization.
7. The finished renderer is a pure stable sort and formatter over complete
   records.  Token scans, source-event retargeting, deferred source guesses,
   and semantic classification in `FinishTranslationUnit` are deletion
   targets.
8. Retained-template validation publishes typed declaration-context facts for
   exact source nodes: dependent class/alias template-ids and calls originating
   in a retained definition.  Variable-template uses are deliberately distinct
   because replay can turn one into the public deduced source event.  The facts
   cover the complete retained target and template parameters/defaults,
   including syntax that the ordinary validator deliberately skips.  This
   observer-only provenance replaces renderer tests for whether a token happens
   to occur inside a template.

The source-range portion is general syntax provenance and must be validated
before witness behavior changes.  It uses fields already present in every
syntax node, so it has no per-node storage cost.  Parser output, ordinary
LowIR, object output, and no-witness timings must remain unchanged.  Observer
side tables and vectors remain absent when `--witness` is absent.

## Contract and ownership

PA19 is the earliest assignment that owns template instantiation and therefore
the earliest suitable home for the high-level witness requirement.  Its README
will describe the optional diagnostic without prescribing class names, files,
containers, event structs, or an exact implementation strategy.  PA20, PA22,
PA23, and PA24 extend the same diagnostic through their existing template
features; they do not each define a new transport.

Strict `.ref.witness` files remain the byte-exact external artifact oracle, but
new focused tests will validate relationships rather than compiler source:

- source locations follow actual template-use syntax and remain ordered;
- changing a type argument changes its typed binding without changing event
  shape;
- explicit, defaulted, and deduced arguments are distinguished;
- primary, partial, and explicit selections follow semantic behavior;
- only selected function-template calls are published;
- repeated internal requests do not duplicate closure transitions; and
- compiling without `--witness` neither creates an artifact nor changes LowIR
  or native output.

## Validation ladder

### Fast checks

- rebuild only directly changed objects and `dev/cppgm++`;
- compile one PA19 fixture for each source-use kind and compare its witness;
- compile one PA20 specialization, one PA22 SFINAE/drop, one PA23 variadic, and
  one PA24 hosted-template fixture;
- run an observer contract script covering absent/present output, relative
  paths, binding provenance, ordering, and typed output-file failure;
- run `make test-pa19` and `make test-report-through-pa19` after the first
  observer increment.

### Cumulative checks

- run strict one assignment at a time (`pa19`, `pa20`, `pa22`, `pa23`, then
  `pa24`) and record both execution and exact-match counts;
- run the normal test and through-report at every affected assignment boundary;
- run `make test-report-through-pa38`, PA37/PA38 debug and round-trip tests, and
  all architecture, source-set, exception, LowIR-contract, and file audits;
- compare no-witness O0/O1/O3 LowIR/object outputs before and after by hash;
- run repeated no-witness source-diverse AB/BA timings with the current compiler
  as control; and
- finish with root `make inception` using explicit 32-way outer, inner, and
  object settings.

Any normal-path CPU regression above 0.2% in a repeated matched workload is a
design failure to investigate, not acceptable witness overhead.  A result
inside 0.2% must also have exact ordinary outputs and no new normal-path
allocation.  The broader historical 1% performance gate is not a license to
spend 1% on an optional diagnostic.

## Phase W0: Freeze the boundary

1. Record the current strict failure counts and prove missing artifacts are the
   only failure class.
2. Inventory every witness grammar production and value category from checked
   references without inferring semantics from fixture names.
3. Record current executable sections and a source-diverse no-witness timing
   sample, and verify the host has no stale profiler or inception process.
4. Select a small cross-PA exact fixture set and a separate relationship-based
   observer contract test for rapid iteration.

Commit boundary: this plan and its baseline ledger only.  Push before source
changes.

## Phase W1: Define the typed observer boundary

1. Add a semantic observer interface owned by the template diagnostic layer.
   It accepts compact event values: semantic kind, syntax node, pattern ID,
   specialization/binding ID, argument span, provenance span, selection kind,
   and optional selected-partial or dropped-candidate details.
2. Add one nullable observer pointer to `Analyzer` and the semantic/lowering
   entry points.  Keep existing call sites source-compatible with a default
   null pointer.
3. Allocate the concrete witness collector only in the driver when a witness
   path was supplied.  The driver passes a borrowed pointer down one translation
   unit at a time.
4. Render and flush before the analyzer and syntax arena die.  The observer
   never escapes that lifetime.
5. Add a no-witness allocation/call census to the focused contract check or a
   temporary diagnostic build; remove temporary instrumentation after proving
   the null path.

Commit and push the driver/interface skeleton after absent/present/error tests,
ordinary output equality, and through-PA19 pass.

## Phase W2: Source-use events

Add observations only at the final semantic owners:

1. class and alias template-id resolution, after canonical arguments and
   primary/partial/explicit selection are known;
2. variable-template value resolution, after selected specialization and
   canonical binding are known; and
3. resolved call construction, after overload resolution has selected a
   function-template specialization.

For every event, retain the original source node separately from any replayed
template-definition node.  Suppress generated, header-only, replay-internal,
and speculative uses according to semantic ownership, not path substrings.
Store canonical binding provenance when arguments are assembled so the
renderer does not reverse-engineer `explicit`, `deduced`, and `defaulted` from
values later.

After collection, stable-sort source events by primary-file token position and
event insertion ordinal.  Normalize only the path representation; never use
the path or fixture name to choose event content.

Commit source-use support after PA19 strict and its ordinary cumulative gates
pass.  Push before closure-event work.

## Phase W3: Closure events

Observe final state transitions at their existing centralized owners:

1. successful class-specialization materialization and later finalization;
2. successful variable-template specialization materialization;
3. successful function-template specialization materialization;
4. first transition that requires a function definition; and
5. final selected-partial binding and final rejected-candidate/drop facts that
   are part of the checked public artifact.

Deduplicate by typed semantic identity and transition kind.  Preserve semantic
transition order rather than sorting closure events by names or addresses.
Do not log cache hits, recursive in-progress requests, failed speculative
materializations, or repeated demand reasons unless the witness grammar
explicitly represents the final failure.

Commit closure support after PA20 and PA22 strict suites and cumulative normal
gates pass.

## Phase W4: Canonical rendering

1. Implement one deterministic renderer for the checked indentation and line
   grammar.
2. Render types and arguments through canonical semantic presentation helpers,
   with source identity where the witness contract intentionally preserves
   typedef or template-template spelling.
3. Render selected template and entity names from semantic owners, including
   namespace and member qualification, without storing preformatted strings in
   hot analysis paths.
4. Map primary-file absolute paths to the harness's relative `tests/...` form
   using lexical path normalization, independent of process-specific prefixes.
5. Write to a temporary in-memory stream and create/truncate the requested file
   only after successful translation-unit analysis.  Report open/write failure
   through `ThrowInputOutput`.

Commit and push exact renderer convergence separately from event discovery.

## Phase W5: Strict convergence and generality audit

1. Advance exact strict coverage in order: PA19, PA20, PA22, PA23, PA24.
2. Classify each mismatch as missing event, surplus event, ordering, semantic
   identity, argument rendering, provenance, selection, lifecycle, or path.
3. Fix semantic categories at their central owner.  Do not add per-fixture
   branches or lists of library/template names.
4. After every assignment turns green, run its ordinary suite and cumulative
   report so witness hooks cannot conceal a semantic regression.
5. Search the implementation and tests for fixture basenames, test paths,
   hashes, and exact production-source matching.  The audit must be clean.
6. When convergence reveals a semantic distinction observable only through a
   candidate-drop reason, add a relationship-based witness check as well as
   retaining the exact artifact oracle.  In particular, direct initialization
   must keep a copy/move candidate that needs a valid user conversion viable
   for ranking, while a genuinely invalid conversion and the restricted
   copy-initialization step remain non-viable.  Describe viability versus rank
   in the student-facing witness contract without prescribing implementation
   names or data structures.

PA24 strict is the unique-fixture closure gate; root `make test-strict` is the
cumulative final gate.

## Phase W5P: Source-provenance foundation

This corrective phase precedes any further PA22 convergence.

1. Add authoritative half-open ranges to structured name components,
   template-argument lists, type-id arguments, and declarator/name wrappers by
   filling existing `SyntaxNode` fields at parse time.
2. Add small syntax queries for range validity, exact name-component
   selection, and direct template-argument source nodes.  Queries inspect the
   syntax graph; they never scan token spelling across the translation unit.
3. Exercise repeated identical names on one line, multi-component template-ids,
   member template-ids, and nested template arguments in W5R's relationship-
   based witness tests.  Range metadata deliberately has no new PA10 dump
   surface; the first public consumer, not exact internal fields, owns the
   student-facing test.
4. Run parser/PA10 gates, PA19/20 ordinary gates, output hashes, file audits,
   and a repeated no-witness AB/BA timing before retaining the foundation.
5. Commit and push the provenance foundation independently.  No PA22
   witness-parity workaround belongs in this commit.

## Phase W5D: Declaration-context provenance

1. Reuse the retained validator's template-parameter environment to classify
   exact name-component nodes whose argument syntax is still dependent at the
   declaration point, separately from call nodes owned by retained syntax.
2. Publish this typed provenance for the full retained definition and every
   template-parameter specifier, declarator, nested parameter, and default.
   Do not rely on the validator's selective behavioral traversal: function
   casts, unevaluated syntax, and defaults may correctly sit outside that
   traversal while still producing semantic events later.
3. Allocate and traverse only when the nullable witness observer exists.
   Ordinary analysis receives no side table, node growth, or extra syntax
   walk.
4. Make each event kind consume only the fact that applies to its contract.
   Dependent class/alias uses and retained calls are suppressed; a replayed
   variable-template use remains publishable with deduced arguments.  The
   final renderer may sort and format complete records, but may not rediscover
   declaration context by parent walks, token searches, or spelling rules.
5. Prove the boundary with dependent function-body, dependent default,
   concrete-in-template, and replay-resolution fixtures; then repeat the
   no-witness AB/BA gate before retaining it.

Commit and push this provenance increment independently before the renderer
reduction.

## Phase W5R: Rebase the observer on complete source-use records

1. Change class, alias, variable, function, constructor, and operator
   publication sites to pass the exact component node and explicit argument
   source nodes available at the final semantic decision.
2. Capture provenance and selected-partial bindings in that publication call.
   Cache reuse publishes a new source use but does not create a new lifecycle
   transition.
3. Replace constructor/operator source-parameter tunnelling with publication
   at source-aware callers using the selected binding in the returned dump
   node.
4. Remove global token searches, `used_tokens`, event retargeting, pairwise
   default-provenance propagation, and renderer-side declaration-context
   inference.  Keep only W5D's exact semantic provenance publisher.  A
   remaining recovery heuristic blocks this phase from being committed.
5. Re-run PA19 and PA20 strict/ordinary/cumulative gates before resuming PA22.
   Then classify PA22 differences against complete records rather than adding
   renderer inference.

Commit and push the observer reduction before continuing with PA23/PA24.

## Phase W5M: Retained semantic-decision provenance

Split this phase at a hard foundation boundary.  W5M-F may only retain syntax
and semantic provenance that is useful independently of witness formatting:
half-open ranges on existing expression nodes, exact overload-use anchoring,
nested-declarator identity, and the exact owner component already represented
by a retained class-member definition.  It must not add or suppress a witness
event.  Verify unchanged retained-record sizes, ordinary output, file audits,
and repeated no-witness A/B timing, then commit and push W5M-F by itself.

W5M-O may consume those facts from the nullable observer.  Its retained-call
classification must run at the validator's semantic replay walk, where the
compiler already knows which expressions are deferred, rather than in a
second whole-target syntax scan.  This keeps concrete declaration-time calls
public while suppressing specialization-specific replay events.  Constructor
and owner publication likewise happen only after their existing semantic
selection is final.  W5M-O receives its own PA19/20/22 strict and no-witness
gate before it can be committed.

### Phase W5M-S: Declaration-owned semantic source facts

Pause W5M-O before adding further PA22 witness cases and establish the missing
semantic/source join as an independently measured foundation.  A retained
template declaration currently keeps either syntax or semantic identity at
different stages, but not a compact record that deliberately keeps both.  As
a result, late replay can identify a concrete class specialization but cannot
reliably distinguish a class-id resolved when the declaration was registered,
a dependent current-partial class-id, and an arbitrary dependent primary
class-id that became concrete only through replay.

1. Define a small typed source-fact record whose minimum payload is the exact
   canonical name-component node, the semantic pattern kind and stable pattern
   index, and a source-resolution class.  The initial resolution classes are
   declaration-complete, current-partial, and replay-required; do not encode
   witness grammar, rendered names, token spelling, or fixture identity.
2. Populate facts only at existing semantic registration/selection boundaries
   that already possess the necessary information.  Class partial
   registration owns its canonical partial arguments and parameter
   environment; out-of-class member registration owns its selected primary or
   partial owner and exact declarator components.  Do not call `BuildType`,
   instantiate a template, or perform a new lookup merely to create a fact.
3. Store facts on the retained declaration/pattern that owns their parameter
   environment.  Deduplicate only within that owner by typed node identity.
   A translation-unit-wide claimed-node set is invalid because the same syntax
   may first be visited under an incomplete outer environment and later under
   the complete retained-declaration environment.
4. Keep the ordinary compiler path inert.  Prefer existing tail padding and
   already-retained vectors; if a new side vector is required, allocate it only
   when the nullable observer is present.  This foundation must not add,
   suppress, reorder, or render any witness event.
5. Validate record sizes, byte-exact no-witness O0/O1/O3 output, PA19/20
   ordinary and strict suites, file audits, and four position-balanced frozen
   no-witness A/B blocks.  Reject or redesign any result above the plan's
   0.2% matched CPU threshold.  Commit and push W5M-S separately.
6. Only after W5M-S passes may W5M-O consume the typed facts to publish class
   uses.  Add one semantic category at a time—current partials, retained member
   owner/type occurrences, then static-member definitions—and require a lower
   PA22 mismatch count without changing PA19/20 after each increment.

Two prototypes establish the negative boundary.  Building complete class
arguments during the observer's retained-source prepass changed witness-mode
semantic behavior and produced unknown dependent type names.  Moving that
work into the retained walk still conflated declaration-time resolution with
later replay, while a translation-unit-wide claimed-node set suppressed facts
when a nested declaration was revisited under its authoritative environment.
Both designs are rejected.  Semantic source facts must be produced by the
declaration owner, not reconstructed by the observer.

The retained foundation records a 16-byte `(owner node, source node, semantic
index, kind, resolution)` fact only in the optional observer.  The existing
retained template-argument validation walk already knows the class-template
pattern, so capture adds no lookup, type construction, instantiation, or
second traversal.  A temporary debug census over the PA22 forward/partial
switch fixture found 42 facts spanning all three resolution classes and kept
the same source node distinct when it belonged to different retained owners.
The debug census was removed before retention.  PA19 remains 295/295 ordinary,
279/279 strict, and 10/10 course; PA20 remains 164/164 ordinary, 158/158
strict, and 11/11 course.  Frozen O0/O1/O3 objects are byte-identical to the
pre-foundation compiler.  Four O0 ABBA blocks measure -0.06% paired user,
-0.48% wall, and +0.07% RSS; the report is
`/tmp/v3codex-w5ms-ab.json`.

The first W5M-O consumer confirms the abstraction.  Retained validation now
records every template-id component in a qualified type, not only its terminal
component, and class/member registration publishes only facts whose normalized
argument syntax is equivalent to the selected partial owner's argument shape.
This distinction is required: a `holder<T const>` mention inside a
`holder<T const *>` partial is not the current specialization merely because
both share the same primary pattern.  The contained current-partial consumer
makes the four focused `MapBase<Pair<Key, Value>, enabled_tag>`, `T<V<A>>`, and
`Vec<bool, A>` owner/return fixtures exact, keeps the top-cv and variadic
negative controls exact, preserves all PA19/20 strict comparisons, and lowers
full PA22 mismatches from 68 to 65.  Four no-witness ABBA blocks against the
committed W5M-S foundation produce byte-identical objects and measure -0.06%
paired user, -0.54% wall, and -0.04% RSS; the report is
`/tmp/v3codex-w5mo-current-partial-ab.json`.

Before adding the next consumer, retain a second output-inert join for
out-of-class members.  A retained definition can first be applied while class
work is speculative: its observer events are correctly rolled back, but the
semantic declaration survives for later committed use.  Recording at replay
therefore cannot be authoritative.  Keep an observer-only retained-member
source fact keyed by the semantic class-pattern index, selected partial or
concrete owner, and member `NameId`, with the exact owner component and owning
declaration node.  Populate it when `AnalyzeClassTemplateMember` has completed
owner selection.  This must not enlarge `BindingRecord` or
`ClassTemplateMemberPattern`, add an ordinary-path allocation, or render an
event.  The later static-member consumer must select these typed facts from the
committed binding and enclosing specialization; it may not rescan retained
declarators or search template-name spelling.

The retained-member index is a 24-byte observer-only record and leaves both
production records at their existing sizes.  PA19 remains 295/295 ordinary,
279/279 strict, and 10/10 course; PA20 remains 164/164 ordinary, 158/158
strict, and 11/11 course; full PA22 remains at the pre-foundation 65 witness
mismatches.  Frozen O0/O1/O3 objects are byte-identical.  Four O0 ABBA blocks
against `4e6befac` measure -0.00% paired user, -0.53% wall, and +0.09% RSS;
the report is `/tmp/v3codex-w5m-member-source-ab.json`.

The first retained-member consumer replaces the old recursive declarator scan
and template-name-component search.  A committed static-member binding now
walks its typed enclosing-specialization chain and selects the indexed source
fact by pattern, member `NameId`, selected partial, and concrete owner.  It then
publishes the exact owner component plus same-pattern semantic source facts.
Only the two targeted PA22 witness files change: the nested static-member
fixture becomes exact, while the rooted definition gains its two missing
explicit uses and is left only with a distinct deduced type occurrence.  PA22
mismatches fall 65 -> 64 with PA19/20 exact.  Frozen O0/O1/O3 objects remain
byte-identical; four ABBA blocks measure -0.00% paired user, +0.48% wall, and
-0.27% RSS.  The report is
`/tmp/v3codex-w5m-retained-member-consumer-ab.json`.

Treat the remaining rooted-definition occurrence as a separate semantic source
role, not a renderer offset.  Clang's typed AST exposes both the enclosing
variable object-type location and its nested template-specialization location;
the former has zero explicit bindings and the latter owns the written template
arguments.  Extend the declaration-owned fact kind with `class-object-type`
and retain the exact structured-type root while the validator already walks
the declaration and resolves its class pattern.  Add and measure this role as
an output-inert foundation first.  Only afterward may the retained-member
consumer publish it from the committed specialization, preserving the typed
distinction between a deduced object-type occurrence and an explicit
template-id component.

The object-type role is retained output-inert.  A temporary consumer probe
located the expected rooted source node and was removed before retention.
PA19 remains 295/295 ordinary, 279/279 strict, and 10/10 course; PA20 remains
164/164 ordinary, 158/158 strict, and 11/11 course; PA22 remains at 64
mismatches.  Frozen O0/O1/O3 objects are byte-identical to `424c9a91`.
Four O0 ABBA blocks measure -0.12% paired user, -0.52% wall, and +0.24% RSS;
the report is `/tmp/v3codex-w5m-object-type-source-ab.json`.

The contained consumer routes `class-object-type` facts through the existing
deduced-class publication path while explicit component facts retain their
written argument count.  Exactly one PA22 witness file changes, the rooted
static-member fixture becomes exact, and PA22 mismatches fall 64 -> 63 with
PA19/20 exact.  Frozen O0/O1/O3 objects remain byte-identical to the committed
foundation.  Four O0 ABBA blocks measure -0.66% paired user, -0.43% wall, and
-0.00% RSS; the report is
`/tmp/v3codex-w5m-object-type-consumer-ab.json`.

Before changing member-alias witness rendering, retain the semantic qualifier
that `LookupStructuredName` already has at the successful alias-template
decision.  For a qualified source use this is the concrete entity represented
by the carrier scope immediately before the alias component.  An unqualified
member alias deliberately has no source qualifier; its typed declaration owner
remains authoritative.  Together these are the cppgm analogue of the source
and declaration context attached to Clang's resolved `TypeAliasTemplateDecl`.
Today cppgm discards the written qualifier identity and the renderer
tries to recover it by searching for an earlier class-use event with the same
syntax node and token order, then falls back to the primary class-template
name.  That event-order join is neither authoritative nor complete.

Add the written qualifier entity to the observer's typed source-use record at
the existing lookup publication point.  An unqualified use records no
qualifier instead of the lookup result's later naming class.  The field must be
observer-only, occupy existing record padding if possible, perform no lookup
or rendering, and initially make no witness-output change.  Preserve the
declaration-time qualifier when a dependent retained use is later joined with
concrete replay; do not replace it with whichever specialization replayed
first.  Validate record size, PA19/20
strict and ordinary output, frozen no-witness O0/O1/O3 objects, audits, and four
position-balanced A/B blocks, then commit and push this qualifier-provenance
foundation independently.

Only after that gate may the alias renderer consume the retained qualifier
entity.  A declaration-complete written qualifier becomes a direct
`qualified owner + alias name` operation.  Unqualified and retained-dependent
uses keep the typed alias declaration owner until their own source-owner fact
is available; they must not adopt a concrete replay owner.  Delete the scan of
neighboring class-use events, while retaining the semantic mapping from an
internal class-pattern owner to its public primary-template name.  The consumer
must improve the member-alias identity fixtures without changing unrelated
PA22 witnesses or PA19/20 exactness, and receives its own object-equality and
timing gate before retention.

The qualifier-provenance foundation stores one `EntityId` in existing
`SourceEvent` padding, so the record remains 232 bytes and ordinary compiler
records are unchanged.  PA19 remains 295/295 ordinary, 279/279 strict, and
10/10 course; PA20 remains 164/164 ordinary, 158/158 strict, and 11/11 course;
PA22 remains at 63 mismatches.  The source-set, semantic-owner, and compiler-
layout audits pass, and frozen O0/O1/O3 objects are byte-identical to
`38db37b3`.  The initial four-block sample landed on the timer's 0.01-second
quantization boundary at +0.247% paired user; an extended six-block sample
under lower load measures -0.299% paired user, -0.267% wall, and -0.038% RSS.
The retained report is
`/tmp/v3codex-w5m-alias-owner-source-ab-extended.json`.

The contained consumer removes the neighboring-event scan and uses the
qualifier only for declaration-complete source uses.  Exactly four PA22
witness files change and become exact: member-alias default scope, partial-
specialization rebind, allocator-traits owner rebind, and pack-expanded member
alias source scope.  The retained-dependent and unqualified controls remain
unchanged, PA22 mismatches fall 63 -> 59, and PA19/20 remain exact.  Frozen
O0/O1/O3 objects and all three audits remain exact.  Four ABBA blocks against
`80c93fe2` measure +0.178% paired user, +0.263% wall, and +0.438% RSS; the CPU
gate passes.  The report is
`/tmp/v3codex-w5m-alias-owner-consumer-ab.json`.

The remaining dependent member-alias mismatch requires a second, distinct
foundation.  A later replay knows a concrete class, but an unqualified source
use such as `f<T>` names an alias whose declaration belongs to the current
partial specialization.  Using the replay class would make output depend on
which specialization replayed first.  While the retained validator already
classifies the current partial's exact owner template-id, also retain its
relationship to unresolved template-id components in that declaration.  A
later real alias lookup confirms whether the component's alias declaration is
owned by the same class pattern, so unrelated namespace aliases are not
captured.  The compact observer-only fact is keyed by exact alias component and
carries the owner class-pattern index, selected partial-pattern ordinal, and
source-resolution class.  Populate it in the existing structured-name walk;
do not resolve the alias, build arguments, or instantiate anything for the
observer.

Resolve the fact's partial ordinal immediately after class-partial registration
selects or appends that partial, then join it into the optional alias source-use
record when the later real alias lookup supplies alias-pattern identity.  This
follows the compiler's authoritative validation -> registration -> replay
lifecycle and avoids a second event backpatching mechanism.  The join remains
output-inert initially.  Measure its record sizes, PA19/20/22 output, frozen
objects, audits, and no-witness timing as another independent W5M-S boundary.
Only afterward may a consumer render a current-partial owner from the retained
class partial's canonical semantic arguments.  Declaration-complete qualifiers
continue to use their concrete entity; replay-required owners remain deferred.

The retained-alias foundation uses a 20-byte observer fact.  Its ordered join
keeps the alias declaration's primary class-pattern identity in the source
event, raising that optional record from 232 to 240 bytes; ordinary semantic
records and allocations are unchanged.  A temporary debug probe showed the
dependent `f<T>` event joined to class pattern 8, partial 0, while the unrelated
global aliases in the same declaration had no matching owner; the probe was
removed.  PA19 remains 295/295 ordinary, 279/279 strict, and 10/10 course;
PA20 remains 164/164 ordinary, 158/158 strict, and 11/11 course; PA22 remains
at 59 mismatches.  Frozen O0/O1/O3 objects and all three audits are exact.  An
initial larger-record run was rejected after a noisy external outlier.  The
compact ordered join then measures -0.705% paired user, -0.835% wall, and
-0.106% RSS across four clean ABBA blocks.  The retained report is
`/tmp/v3codex-w5m-retained-alias-source-ab-compact.json`.

Before consuming that fact, add a canonical dependent-identity presentation
foundation.  The semantic model already gives synthesized template-parameter
entities an ordinal in `EntityRecord`, and template-template proxy
specializations retain both that ordinal and their canonical arguments.  Two
creation paths currently omit the ordinal: the retained-validator type shapes
and the proxy's marker entity.  Fill the existing field at those creation
points; do not add a field to `EntityRecord`, `TemplateParameter`, LowIR, or an
ordinary analysis event.

Extend the shared source-identity renderer with an explicit opt-in policy that
uses those typed entity ordinals to spell type, value, and template-template
parameters by depth and ordinal, and that can preserve semantic pack-expansion
markers while recursively rendering a proxy specialization's existing
canonical arguments.  The default renderer entry points must remain exactly
unchanged.  This is the cppgm analogue of asking Clang's typed AST for a
`TemplateTypeParmType` or `TemplateTemplateParmDecl`, rather than rewriting
rendered names afterward.  It is also reusable by the remaining PA24
dependent-owner case.

Retain this W5M-S identity layer as an independent, output-inert commit only
after ordinary PA19/20 behavior, the current PA22 witness set, frozen O0/O1/O3
objects, source/layout audits, and repeated no-witness AB/BA timing remain
unchanged.  In the following W5M-O commit, the alias renderer may combine the
already-retained owner pattern/partial ordinal with that opt-in renderer.  It
must not add fixture-specific replacements, scan source spelling, or infer a
parameter kind from text.  Test the public result through semantic
relationships: rename template parameters and vary their pack roles while the
canonical depth/ordinal identity remains stable.

The retained identity layer fills only the existing entity ordinal at every
retained/ordinary/partial-materialization type-shape creation point and at the
template-template proxy marker creation point.  The shared renderer's old
entry points retain their
old behavior; a new explicit policy recursively renders typed parameter
identities and pack expansions.  A temporary consumer probe rendered the
dependent partial owner exactly as
`replace_if_impl<template-parameter-0-0<type-parameter-0-1...>,
template-parameter-0-2, type-parameter-0-3>`; the probe was removed before the
foundation commit.  PA19 and PA20 remain exact and PA22 remains at 59
mismatches.  Frozen O0/O1/O3 objects and all three file/ownership/layout audits
are exact.  The first four-block timing was inconclusive at +0.724% paired
user; a six-block repeat under lower host load measured +0.118% paired user,
-0.219% wall, and -0.363% RSS, inside the 0.2% CPU gate.  The retained report
is `/tmp/v3codex-w5m-canonical-identity-ab-extended.json`.  An audit then found
and completed the otherwise order-dependent partial-materialization creator.
Its final eight-block sample against the same pre-foundation compiler measured
-0.119% paired user, +0.109% wall, and +0.271% RSS; the report is
`/tmp/v3codex-w5m-canonical-identity-complete-ab-extended.json`.

Before adding more PA22 rendering rules, replace the remaining class-template
argument recovery with a typed presentation sidecar.  The semantic
instantiation boundary already knows the canonical specialization binding, its
complete canonical argument vector, and the number of arguments supplied on
each path.  Retain the minimum observed presentation arity for that exact
typed specialization, and let an observer-only recursive argument renderer
consult it by entity identity.  This must work for nested specializations and
for qualified and anonymous-namespace entities without manufacturing global
string-replacement aliases.  Keep the ordinary presentation API and
`TemplateArgument` layout unchanged.

Treat declared defaults as a second, narrower provenance source.  A supplied
count alone cannot represent the checked witness convention: some fixtures
write trailing arguments that are semantically equal to their declarations'
defaults and the reference still presents them as `defaulted`.  Do not infer
that equality from identifier text.  First factor class-template default
materialization so the existing missing-argument path and an observer-gated
canonical-equivalence probe use the same typed operation and binding scope.
The probe may lower the presentation arity only for a contiguous trailing
suffix whose materialized defaults equal the specialization's canonical
arguments.  A substitution failure leaves the arguments explicit and must not
change compilation success, closure publication, or semantic caches visible to
ordinary compilation.

Land the typed recursive renderer as an output-inert foundation and measure it
before consumption.  Then consume naturally omitted provenance; validate
PA19/20 exactness and compare PA22/23/24 manifests.  Only after that boundary
is stable may the observer-gated default-equivalence producer be added.  Reject
the earlier experiment that replaced every explicit specialization argument:
although it fixed the `char` closure spelling, it rewrote dependent zero to a
character literal, erased required casts, and rendered `-1` as an unsigned
maximum in PA23/24.

The typed renderer foundation is output-inert.  It adds an opt-in
`TemplateArgumentElision` sidecar keyed by `EntityId`; recursive entity/type
rendering applies a bounded argument count without changing any existing
presentation entry point or semantic record.  PA19 remains 295/295 ordinary,
279/279 strict, and 10/10 course; PA20 remains 164/164 ordinary, 158/158
strict, and 11/11 course; PA22 remains at 56 mismatches.  Frozen O0/O1/O3
LowIR and all three audits are exact.  Four ABBA blocks measure -0.241% paired
user, -0.538% wall, and +0.100% RSS; the report is
`/tmp/v3codex-w5m-typed-elision-foundation-ab.json`.

The first consumer derives typed limits only when an observed specialization
fact exactly matches the canonical arguments stored on its entity.  It ignores
trailing-pack templates and applies the sidecar to source bindings, selected-
partial bindings, member owners, and closure entity presentation.  PA22 moves
from 56 to 55 mismatches: the inherited alias operator fixture becomes exact,
and every nested binding/specialization in the template-template use-scope
fixture is corrected (its remaining difference is unrelated anonymous-
namespace qualification).  PA19/20 remain exact; PA23/24 witness manifests do
not change; frozen O0/O1/O3 LowIR and all audits remain exact.  Four ABBA
blocks against the pre-foundation compiler measure -0.847% paired user,
-0.544% wall, and -1.110% RSS; the report is
`/tmp/v3codex-w5m-typed-elision-consumer-ab.json`.

The declared-default producer now shares one typed materialization operation
with ordinary class-template completion.  It runs only with a witness observer,
under candidate-substitution and observer-suppression guards, and compares
canonical `TemplateArgument` values.  Crucially, it refuses to reinterpret a
default whose syntax refers to a template parameter: a dependent default that
happens to become equal after substitution is explicit provenance, not a
source default.  The final source-resolution call publishes the complete
canonical argument vector together with this presentation arity, fixing the
prior split between incomplete source arguments and completed specialization
facts.

PA22 moves from 55 to 50 mismatches.  Five fixtures become exact: defaulted
template-template partial ordering, non-type partial ordering, explicit values
equal to nondependent defaults, nested-member partial application, and chained
member closures.  The dependent-default reference-reset fixture is an exact
negative control.  PA19/20 remain exact and PA23/24 manifests do not change.
Six focused before/after witness compilations produce byte-identical LowIR;
the frozen O0/O1/O3 no-witness LowIR is also exact.  Four ABBA blocks against
the prior consumer measure -0.717% paired user, -0.536% wall, and -0.237% RSS;
the report is `/tmp/v3codex-w5m-default-provenance-ab.json`.

Re-examine provenance before extending that producer to dependent defaults or
conversion-function names.  Two narrowly scoped probes exposed a missing
abstraction.  The first allowed a concrete supplied argument to compare equal
to a dependent declared default.  It made the `V<char, traits<char>>` entity
presentation closer to the reference and made one PA24 file exact, but it also
reclassified the explicitly written `int` in
`deduce<proxy<result>, int>` as defaulted.  The second rendered a conversion
target from its typed primary class-template pattern.  Although that spelling
is correct for the focused PA22 cases, its first full-manifest run changed
eight PA23 and four PA24 files, including source-call publication as well as
closure spelling.  Reject both consumers in that form.

The underlying problem is that three distinct facts are currently compressed
into one specialization-wide minimum argument count:

1. which arguments were written at a particular source use;
2. whether a written argument has the same source-semantic shape as a declared
   default; and
3. how a canonical entity should be abbreviated when it is nested in another
   type or printed in the closure.

`PrepareSourceEvents` currently applies the minimum count from a typed
specialization fact back to every source event with the same binding and
canonical arguments.  That loses occurrence provenance.  The entity-wide
`TemplateArgumentElision` sidecar is useful for canonical closure spelling,
but it must not decide the `source=explicit/defaulted` label of an unrelated
source occurrence.

Add the missing machinery in output-inert stages before either consumer:

1. Add an observer-owned class-template source fact recorded only at a
   successful semantic lookup.  It carries the exact name-component `NodeId`,
   primary pattern ID, canonical specialization `BindingId`, selected partial
   ordinal, replay/source role, and the exact explicit argument `NodeId`s.
   Keep this separate from `ClassSpecializationFact`, whose typed canonical
   vector remains suitable for entity presentation.  No production semantic
   record or `TemplateArgument` grows.
2. Give each `SourceEvent` its own typed argument-origin decision.  Join a
   source fact only by `(source NodeId, pattern ID, canonical binding)`, never
   by spelling or merely by canonical argument equality.  Remove the global
   post-pass that lowers every matching event's provenance only after focused
   positive and negative fixtures prove the replacement is equivalent for
   already passing PA19/20 behavior.
3. Represent declared-default equivalence as a small structural source
   identity, not a rendered string and not post-substitution value equality
   alone.  The identity must distinguish a dependent template-id default such
   as `traits<CharT>` supplied as `traits<char>` from a dependent qualified
   result such as `typename traits<V>::scalar_type` supplied as `int`.  Resolve
   referenced templates/entities and template-parameter ordinals while the
   declaration and source syntax are alive; use `NodeId` only as provenance,
   not as the semantic key.  A failed or ambiguous structural comparison keeps
   the argument explicit.
4. Retain two consumers after those foundations pass independently.  The
   occurrence consumer labels only the joined source event.  The canonical
   entity consumer may lower a nested/closure presentation arity from a proven
   omitted or structurally-default-equivalent suffix.  Neither consumer may
   rewrite source spelling globally.
5. Before changing conversion-operator presentation, retain its typed target
   role explicitly: target `TypeId`, target class-template pattern/entity when
   present, and source occurrence remain separate from the display name.
   Deduplicate and join events by typed IDs before rendering.  Re-run the
   PA22/23/24 manifest experiment in isolation; a display-only change must not
   add, remove, or retarget any source event.

Gate every foundation and consumer separately with PA19/20 exactness, full
PA22/23/24 before/after witness manifests, focused witness-mode LowIR equality,
frozen no-witness O0/O1/O3 equality, the file/source/owner audits, and repeated
position-balanced no-witness timing.  Use 32-way builds.  Foundations must be
output-inert and each consumer must have no later-assignment regression.

The first revised foundation retains a 48-byte observer-only
`ClassTemplateSourceFact` at the successful class-template lookup.  Its key and
payload are the exact component `NodeId`, primary pattern ID, canonical
specialization binding, selected partial ordinal, explicit argument nodes, and
replay role.  It is deliberately separate from the canonical specialization
fact and has no renderer consumer yet.  All 731 PA19/20/22 witness manifests
and witness-mode LowIR files are byte-identical to the prior compiler; PA19 is
295/295 ordinary, 279/279 strict, and 10/10 course, PA20 is 164/164 ordinary,
158/158 strict, and 11/11 course, and PA22 ordinary/course is 309/309 plus
2/2.  Frozen no-witness O0/O1/O3 objects and all three audits are exact.  Four
ABBA blocks measure -0.971% paired user, -1.033% wall, and +0.354% RSS; the
report is `/tmp/v3codex-w5m-class-source-fact-ab.json`.

The second revised foundation adds stable token-kind and interned-spelling
queries over existing arena storage, then computes an observer-only structural
default identity.  It substitutes prior template-parameter tokens with the
exact argument nodes from the same source fact; the result is considered only
after the ordinary typed default materialization equals the canonical
`TemplateArgument`.  This is generic syntax structure, not fixture text or a
rendered-name key.  Focused debug facts give presentation arities 1 for
`V<char, traits<char>>`, 2 for the negative
`deduce<proxy<result>, int>`, 4 for the graph's two written default-equivalent
arguments, and 2 for `named_parameter<const int, outer::max, const int &>`.
The value remains unconsumed.  All 7,660 generated PA19--PA24 status, output,
witness, and witness-LowIR artifacts are byte-identical to the prior compiler;
frozen O0/O1/O3 objects and all audits are exact.  Four ABBA blocks measure
+0.061% paired user, -0.544% wall, and -0.284% RSS.  The report is
`/tmp/v3codex-w5m-structural-default-foundation-ab.json`.

The occurrence consumer joins that identity only to a class source event with
the same exact source node, primary pattern, and canonical binding.  It changes
four origin labels and no other witness text: `traits<char>` in the PA22 view,
the two written graph defaults in PA23, and `const int &` in the PA24 named
parameter become `source=defaulted`.  The dependent qualified-result negative
control remains explicit.  PA19/20 stay exact; no PA22/23/24 file regresses,
and every before/after witness LowIR is exact.  Frozen O0/O1/O3 objects and all
audits are exact.  Four ABBA blocks measure +0.063% paired user, +0.384% wall,
and -0.007% RSS; the report is
`/tmp/v3codex-w5m-source-origin-consumer-ab.json`.

The canonical entity consumer folds the same structurally proven arity into
the typed `TemplateArgumentElision` sidecar, separately from occurrence
provenance.  Exactly the same three PA22--PA24 files change.  The PA22 view's
class/constructor closure entities become `V<char>`, the PA23 nested graph
owner omits its two default-equivalent arguments, and the PA24 namespace/base
fixture becomes fully exact.  PA19/20 remain exact, no later file regresses,
all witness LowIR and frozen O0/O1/O3 objects are exact, and all audits pass.
Four ABBA blocks measure -1.566% paired user, -1.562% wall, and +0.275% RSS;
the report is `/tmp/v3codex-w5m-canonical-default-consumer-ab.json`.

After those separations, the conversion-target consumer is display-only in an
isolated manifest run.  `FunctionInfo::conversion_target` already retains the
typed target `TypeId`; when its unqualified type entity maps to a class-template
pattern, closure presentation uses that pattern's terminal `NameId` rather
than the specialization or qualified source spelling.  Nine files change, all
only on conversion-operator closure lines.  One PA22, two PA23, and one PA24
file become exact; five others improve locally; no source event moves and no
file regresses.  PA19/20 and all witness LowIR remain exact.  Frozen O0/O1/O3
objects and all audits are exact.  Four ABBA blocks measure -1.340% paired
user, -0.929% wall, and +0.041% RSS; the report is
`/tmp/v3codex-w5m-conversion-target-consumer-ab.json`.

The next repeated drop-reason class exposed an ordinary overload-resolution
overreach rather than missing witness machinery.  N3485 13.3.3.1/4 excludes a
second user-defined conversion when considering the copy/move constructor used
for the temporary's second copy-initialization step; the implementation had
applied that exclusion to every direct constructor selection.  Gate the
existing typed `ChainsUserConversion` fact by the already-carried
`copy_initialization` context.  This changes 34 witness files and no other
generated artifact: PA22 gains 6 exact files (245 -> 251), PA23 gains 7
(231 -> 238), and PA24 gains 8 (270 -> 278); the remaining 13 all move locally
toward the oracle and no previously exact file regresses.  PA19/20 remain
exact, the ordinary report through PA24 is 3,565/3,565, frozen no-witness
O0/O1/O3 objects and all audits are exact, and the clean-baseline four-block
timing is +0.001% paired user, +0.057% wall, and -0.182% RSS.  The report is
`/tmp/v3codex-w5m-copy-init-clean-ab.json`.  Retain this as a semantic
correctness fix, not as witness-only classification policy, and add the
relationship check required above before closing the plan.

Function lifecycle rendering now also consults the binding's final typed
explicit-instantiation suppression state.  Collection may observe a demand
before a later `extern template` declaration is processed, but a suppressed
binding did not ultimately instantiate or require a definition and must not be
published as though it did.  The isolated PA19--PA24 manifest changes exactly
the two PA22 extern-function witnesses, both become exact (251 -> 253), and no
later witness changes.  The locally used inline/defaulted member of an
extern-instantiated class remains present, proving the filter is per binding
rather than a blanket syntactic suppression.  PA22 ordinary is 311/311,
frozen no-witness O0/O1/O3 objects and all audits are exact.  A noisy initial
four-block sample measured +0.495% paired user; the extended eight-block run
measures +0.000% user, -0.055% wall, and -0.099% RSS.  The retained report is
`/tmp/v3codex-w5m-extern-final-state-ab8.json`.

Explicit class instantiation now records class finalization after the common
successful `EnsureClassDefinition` boundary, before the declaration/definition
branches diverge.  An `extern template class` declaration still completes the
class semantic surface before it suppresses member emission.  Three witnesses
change: one PA22 and one PA24 file become exact, while the PA22 inline/defaulted
member control gains only its missing finalization.  PA19/20/23 remain exact to
their baselines, PA22 ordinary is 311/311, frozen no-witness O0/O1/O3 objects
and all audits are exact.  The initial four-block timing was noisy at +0.428%
paired user; the eight-block repeat measures -0.488% user, -0.543% wall, and
-0.169% RSS.  The retained report is
`/tmp/v3codex-w5m-explicit-class-finalization-ab8.json`.

The first consumer uses the retained owner pattern/partial ordinal only after
their bounds and completed canonical-argument state are validated.  It renders
the primary name and typed partial arguments through the opt-in identity
policy, then appends the alias member name.  Across the full PA22 manifest,
exactly `400-member-alias-template-template-dependent-replay` changes: its
dependent owner line becomes exact, with no other event affected.  That file
still differs only in the independent canonical cv placement of one explicit
outer alias argument, so PA22 remains at 59 non-exact files.  PA19/20 remain
exact, frozen O0/O1/O3 objects and the three audits are exact, and four ABBA
blocks measure +0.178% paired user, +0.489% wall, and +0.889% RSS.  The CPU
gate passes; the report is
`/tmp/v3codex-w5m-partial-owner-consumer-ab.json`.

The remaining cv difference is presentation, not provenance.  A rejected
experiment rendered every explicit alias argument from its canonical semantic
value; PA22 worsened 59 -> 72 because that erases intentional source identities
such as typedefs, `decltype`, elaborated type specifiers, dependent
expressions, packs, and the `template` disambiguator.  Retain source spelling.
Choose canonical spelling only when tokenization proves the two identities
differ solely by `const`, `volatile`, or `_Atomic` placement and the canonical
form has fewer postfix cv qualifiers.  This is independent of identifiers and
fixture content.  It makes the dependent partial-owner fixture exact and moves
PA22 59 -> 58 while PA19/20 remain exact; PA23/24 manifests do not change.
Frozen O0/O1/O3 objects and all three audits remain exact.  The first noisy
four-block timing was rejected at +1.034% paired user; six lower-load blocks
reversed it.  After consolidating token classification into one pass, the
final four-block estimate was a single tick outside the gate at +0.245%; its
six-block repeat measures -0.122% paired user, -0.000% wall, and +0.154% RSS.
The retained report is
`/tmp/v3codex-w5m-cv-presentation-compact-ab-extended.json`.

Apply the existing canonical primitive-type spelling normalization to the
member-owner portion of selected call names as well as their bindings.  This
single final-render boundary makes two PA22 files exact (`unsigned long int`
and `long int` disappear), moving PA22 58 -> 56 while PA19/20 remain exact.
It also improves five later manifests and makes one PA23 plus two PA24 files
exact, with no PA23/24 regression.  Frozen O0/O1/O3 objects and all three
audits are exact; four ABBA blocks measure +0.000% paired user, -0.054% wall,
and -0.094% RSS.  The report is
`/tmp/v3codex-w5m-callee-normalization-ab.json`.

The first W5M-F expression-range implementation called `Make` and then
`SetTokenRange` for every parenthesized call and subscript node.  Although its
output was exact, four ABBA blocks measured +0.77% paired user time, so that
shape was rejected.  `MakeRanged` now initializes the already-present range
fields while constructing the node.  Four replacement blocks are exact and
measure +0.12% paired user time, +0.21% wall, and -0.18% RSS; the CPU gate is
met.  The report is `/tmp/v3codex-w5mf-packed-ab.json`.

A follow-up W5M-F boundary makes terminal source identity canonical across
direct, qualified, parenthesized, and member calls.  The arena query follows
only the callee/name-bearing edge and stops at an exact name component or
identifier; it never descends into call or template arguments, where an
unrelated identifier could otherwise become the event anchor.  Both retained
declaration provenance and later semantic replay therefore join on one node
without a token/name search.  An isolated foundation-plus-query build passes
PA19 295 ordinary + 279 witness + 10 course and PA20 164 ordinary + 158
witness + 11 course.  Four no-witness ABBA blocks have exact objects and
measure +0.00% paired user, +0.11% wall, and -0.35% RSS versus `fc6514a2`;
the report is `/tmp/v3codex-w5mf-anchor-only-ab.json`.

PA22 exposes one additional provenance boundary: an out-of-class member
definition retains canonical owner arguments but historically discards the
exact owner template-id node.  Preserve that component when the definition is
classified, alongside its canonical owner shape.  Store nested owner
components in place of their argument-list children so routing can carry the
same provenance forward without adding a parallel allocation; derive the
argument list structurally when it is needed.

Publish the written owner use only after replay has selected that retained
partial-owner definition.  The publication receives the exact component,
typed pattern, and canonical written arguments directly.  It must not search
the retained declaration, match a name spelling, or infer the source from a
later concrete event.  Keep the node ID in existing tail padding of
`ClassTemplateMemberPattern`, verify its size is unchanged, and benchmark the
no-witness path before retention.

Use the same two-stage provenance join for dependent alias template-ids.  The
retained validator marks the exact written component; a later successful
lookup supplies the typed alias-pattern identity.  The observer records that
identity against the original component while keeping its direct argument
syntax, rather than substituting the values from whichever concrete replay
happened first.  This join is observer-owned and performs no additional
lookup, instantiation, or ordinary-analysis work.

Complete the existing syntax-range foundation for parenthesized, call, and
subscript expressions.  These nodes are themselves source-aware semantic
boundaries: nested call operators and overloaded subscripts legitimately use
the expression node as their witness source.  Populate the existing half-open
range from the already-ranged operand through the closing delimiter; do not
recover an inner callee token in the renderer.

Add one syntax query for the exact identifier owned by a declarator, including
nested declarators, and use it in place of validator-local recursion.  Add one
observer-gated semantic query that extracts a selected template constructor
from an already-built constructor action (through only transparent
single-child materialization/transfer wrappers).  Variable declarations and
class functional casts can then publish at their source-aware boundary after
analysis returns; initialization APIs remain free of witness-only parameters.
Constructor selection must publish the same typed final candidate/drop record
as ordinary calls and overloaded operators.  Keep that record source-free;
the later action/source join owns location.  Allocate and classify drops only
when the observer exists, and do not publish quiet speculative selections.

Use PA22 strict mismatches to find any remaining decision sites that discard
source identity.  Extend the retained semantic records first, using existing
storage where possible, and only then add the observer publication.  Do not
restore constructor parameter tunnelling, deferred alias guesses, or renderer
recovery removed by W5R.

The retained W5P representation packs each parsed component's name, optional
argument-list node, and half-open range into one temporary record.  A first
implementation added two parallel range vectors and reproducibly regressed the
frozen no-witness compile by 1.35% wall / 1.51% user; it was rejected.  Four
ABBA blocks for the packed representation measured +0.22% wall / +0.31% user
with byte-identical output and +0.21% RSS, all inside the 0.5% noise allowance.
The report is `/tmp/v3codex-provenance-packed-ab.json`.

Literal nodes originally reused `token_first` for their scalar-fact index, so
W5R exposed the remaining provenance hole.  The retained representation stores
the source token in `token_first` and the literal-fact index in `token_last`,
whose interpretation is already guarded by the literal flag.  Parser
publication combines both values in the existing `SetLiteralFact` call; the
rejected intermediate form made a second `SetTokenRange` call for every
literal.  The final four-block ABBA comparison against the committed packed
foundation measured -0.41% wall and -0.41% combined user+system CPU with
byte-identical objects.  The report is
`/tmp/v3codex-w5-foundation-combined-ab.json`.

### Phase W5M-O: Terminal object-type source role

Treat the terminal component of a written structured object type as a distinct
source role.  A terminal template-id is already an explicit class use and must
not also publish the resolved object type as deduced.  A terminal alias,
typedef, or nested member name has no direct class-template event of its own;
if its resolved object type is a class specialization, publish that result at
the structured type's source range with deduced provenance.

Do not apply this rule prematurely inside a retained dependent definition.
The W5D validator already marks exact dependent source nodes.  If a
non-terminal qualifier component has that typed provenance, leave publication
to replay at the final semantic boundary.  This distinguishes
`outer<int>::type` in ordinary source from `deduce<A>::type` in a retained
function body without inspecting names, source text, fixture paths, or
canonical result spelling.  Reuse the terminal component for source-name
identity instead of walking the same components a second time.

Validate the consumer against every PA19--PA24 witness and LowIR artifact.  A
terminal-only prototype is rejected if it publishes a concrete result from a
retained dependent qualifier.  Retain only the exact-node join with W5D's
dependent-source facts, and require the usual frozen O0/O1/O3, audit, ordinary
test, and repeated no-witness timing gates before committing.

### Phase W5M-S: Recursive entity-presentation policy

Make source-identity presentation policies recursive rather than applying them
only to the outer entity.  In particular, `show_anonymous_namespace` must flow
through typed template arguments, declarator types, member-pointer owners, and
value-binding scopes.  The public default remains false; witness closure paths
that already request anonymous namespaces retain that request through the
complete typed identity.  This is presentation machinery, not a textual
replacement pass, and it does not change canonical entities or source events.

The full PA19--PA24 manifest must prove that any changed nested identity moves
toward the same policy already applied to its owner.  Preserve byte-identical
witness LowIR and normal frozen output, and benchmark because the shared source
identity renderer also serves non-witness diagnostics.

Replacement identities built from source events must carry the same policy.
Otherwise a fully qualified canonical entity is immediately replaced by an
otherwise identical argument rendered with the default hidden-anonymous
policy.  Add explicit policy overloads to the shared typed-argument renderer
and use them only while constructing closure/entity replacements; ordinary
source-event bindings retain their existing spelling policy.

### Phase W5M-O: Call-operator deduction source

Publish failed call-operator template candidates through the exact call source
already carried by `TryAnalyzeOverloadedOperator`.  Member call-operator
deduction uses the shared function-template deduction routine, which already
computes typed arity and deduction-failure reasons, but previously omitted its
optional `witness_syntax` argument.  Pass the existing source node without
adding syntax recovery, changing candidate formation, or allocating argument
syntax on the normal path.

Keep the handoff at this final operator candidate boundary.  Do not infer a
call source from the selected binding in the renderer and do not publish from
the lower-level deduction routine unless its caller has supplied authoritative
syntax.

### Phase W5M-F/O: ADL deduction source

Carry the same optional source identity through ADL and hidden-friend candidate
formation.  `CompleteArgumentDependentCallCandidates` begins at a real call
site and already receives the callee's argument syntax; retain its exact callee
component separately through namespace and associated-entity lookup, then give
it to the typed deduction routine.  Synthesized range-for ADL and other callers
without source syntax continue to pass `kNoNode` and cannot publish witness
drops.

This is a compact provenance handoff only: candidate sets, lookup order,
deduction, and overload selection remain unchanged.  The observer still owns
allocation, and a failed candidate is visible only when its source-aware caller
requests witness output.

## Phase W5N: Normalize provenance before adding more witness cases

Pause PA22 event-by-event convergence again.  The retained-call and typed-drop
experiments show that the remaining fanout is caused by two semantic identities
that the compiler currently compresses away and later tries to recover from
syntax or presentation.  Add these identities as small, output-inert machinery
before another witness event is added:

1. Define a typed source-occurrence role for a resolved use: evaluated source
   use, unevaluated/declaration-only use, deferred template definition, and
   specialization replay.  Publish it only where the existing semantic walk
   already decides that role.  Store it in the optional observer's node-indexed
   facts; do not add a field to every syntax, expression, binding, or LowIR
   record.  This role replaces the retained-call boolean and must distinguish
   an explicit template-id that is genuinely evaluated from identical syntax
   visited during retained validation or replay.
2. Define a typed function-candidate declaration origin.  A function-template
   specialization maps to its retained `FunctionTemplatePattern`; an ordinary
   function maps to its canonical declaration binding.  Candidate instances
   created during substitution therefore share one origin, while distinct
   copy/move/constructor declarations remain distinct even when their public
   names render identically.  Compute the origin from existing typed fields and
   retain it only in observer drop facts; do not grow `BindingRecord` or
   `FunctionInfo`.
3. Keep occurrence identity separate from declaration origin.  Drops from
   different source calls cannot consume each other, while repeated internal
   candidates for one call and one declaration collapse before rendering.
   Rendering remains a pure formatter and never compares names to infer either
   property.
4. Expose the already-final lifecycle policy through narrow typed queries
   before repairing remaining variable/function closure cases.  A query may
   inspect explicit-instantiation state, owning template state, and final
   demand state; the observer must not reimplement those policies or infer them
   from entity spelling.
5. Land each foundation without changing witness output.  Require focused
   semantic controls, exact PA19/20 strict and ordinary output, exact frozen
   O0/O1/O3 output, architecture audits, and repeated no-witness A/B timing
   within the 0.2% paired-CPU threshold.  Only a following W5N-O commit may
   consume one foundation, and its manifest must be confined to the semantic
   category that foundation owns.

The first public test for this machinery is the PA19 observer property test,
not a dump of private fields.  It checks that source positions follow the
written template-id, argument-origin labels follow typed completion, only the
selected evaluated call is published, and repeated demand produces one closure
transition.  PA22 adds the candidate-origin control: two distinct declarations
with the same displayed constructor name remain distinguishable, while two
instantiated candidates from one declaration do not become duplicate drops.

## Phase W6: Performance and repository closure

1. Build matched before/after GCC-O3, Clang-O3, self-O1, and self-O3 compilers
   from identical source configurations.
2. Compare executable `.text`, `.rodata`, exception/unwind sections, and normal
   no-witness allocations.  Optional witness code may increase static size,
   but unexplained hot-section growth must be inspected.
3. Run repeated position-balanced source-diverse no-witness builds.  Require
   exact object manifests and final binaries; reject or redesign any observer
   placement that exceeds the 0.2% matched CPU bound.
4. Run all cumulative tests, strict tests, debug/round-trip checks, and audits.
5. Run an explicit-32-way PA39 inception and require byte-exact self/inception
   objects and final binaries.
6. Update this plan with every retained/rejected design decision, counts,
   hashes, timing evidence, and the final status.  Remove benchmark roots and
   generated `.my.*` test artifacts, commit, and push.

## Decision ledger

| Phase | Change or experiment | Evidence | Decision |
| --- | --- | --- | --- |
| Planning | Audited driver, strict harness, witness corpus, Analyzer lifetime, extended implementation, and Clang metrics visitor | driver discards both witness paths; 415 unique refs; all semantic pattern tables and syntax locations coexist through demand completion; extended session machinery is far larger than the four-source-use visitor shape | use one optional in-analyzer observer and end-of-analysis renderer; no LowIR state or copied session framework |
| W1 | Added the driver-owned nullable observer, successful-analysis flush, typed output errors, and compact typed event storage | `3c00abac`; absent/present/error checks and through-PA19 ordinary output remained clean | retain the observer boundary; the no-witness path owns no collector |
| W2 | Published canonical source uses and binding provenance at class, alias, variable, and selected-call owners | `3eea8156`; PA19 strict exact matches rose from 0 to 261 of 279 without changing ordinary output | retain typed IDs through analysis and render only at translation-unit completion |
| W2/W3 | Added final overload-drop facts, semantic completeness-demand events, retained-template dependency marks, and source-backed anonymous/local type presentation | PA19 strict passes all 279 witness comparisons, all 295 ordinary fixtures, and all 10 course tests; `make test-report-through-pa19` passes 2,092/2,092; layout, semantic-owner, source-set, and exception audits pass | retain the centralized final-decision hooks; suppress replayed dependent uses from the retained lexical type model rather than source-text matching |
| W5 PA20 | Added pack-aware class/function/variable provenance, variable-template selection, user-defined-literal calls, source-aware non-type argument presentation, constexpr-function closure, and actual-use tracking for retained static-member definitions | PA19 remains exact at 279/279 witness, 295/295 ordinary, and 10/10 course; PA20 passes 158/158 witness, 164/164 ordinary, and 11/11 course; `make test-report-through-pa20` passes 2,267/2,267 | retain typed final-decision events; a demanded definition alone does not create a source use, while an observed nested static member maps through its enclosing template owner |
| W5 PA22 audit | Compared `~/clang.diff` with the full cppgm witness delta | Clang: +2,227/-11 in 8 files, with finished-AST source-use traversal; cppgm: +4,026/-135 in 33 files, including provisional +993/-112 in 14 dirty files; cppgm's renderer searches token spelling and reconstructs lost provenance | pause parity patches; do not commit the provisional PA22 increment; add authoritative source provenance first, then delete recovery logic |
| W5P | Filled existing syntax ranges for exact name components, template-argument lists, type-id arguments, declarator names, and dependent `typename` uses; consolidated the name parser's parallel vectors into one component record | PA10 165/165, PA19 469/469, PA20 11/11, through-PA20 2,267/2,267; frozen output exact; packed-record A/B +0.22% wall / +0.31% user versus the pre-foundation binary | retain packed representation; reject the parallel-vector prototype; test provenance through W5R's public witness behavior |
| W5D | Added observer-gated declaration-context provenance over retained definitions and all template-parameter syntax, with distinct facts for dependent class/alias uses and retained calls | dependent function-body casts, dependent defaults, and dependent non-type parameter declarators are suppressed; replayed variable-template uses remain public; PA19 279/279 and PA20 158/158 strict remain exact | retain typed context facts; reject a single generic `inside template` suppression bit |
| W5R | Anchored explicit uses on terminal name components, kept deduced alias-backed class uses on the written type-id start, added literal provenance, and reduced final rendering to preparation/selection/binding/specialization/drop/closure routines | PA19 295/295 ordinary + 279/279 strict + 10/10 course; PA20 164/164 ordinary + 158/158 strict + 11/11 course; witness module file audit passes; final frozen A/B -0.41% wall / -0.41% combined CPU with exact objects | retain direct provenance and decomposed renderer; delete global token searches, event retargeting, pairwise provenance repair, constructor source tunnelling, and provisional deferred-alias inference |
| W5M-F | Retained exact owner components, initialized existing call/subscript/parenthesized ranges at node creation, and centralized overload/declarator source anchors | retained-member record remains 120 bytes; PA10 165/165, PA19 295 ordinary + 279 witness + 10 course, and PA20 164 ordinary + 158 witness + 11 course; exact frozen objects; four-block paired user +0.12%, wall +0.21%, RSS -0.18% | retain and commit independently; rejected the two-call range initializer at +0.77% paired user |
| W5M-F source identity | Canonicalized terminal name-component selection across call/member wrappers without descending into argument syntax | isolated PA19 and PA20 strict/ordinary/course suites exact; frozen objects exact; four-block paired user +0.00%, wall +0.11%, RSS -0.35% | retain as provenance-only machinery before the observer consumes it |
| W5M-S | Added declaration-owned typed semantic source facts before expanding PA22 publication | 16-byte observer-only facts reuse the existing validation lookup; PA19/20 strict and ordinary suites remain exact; O0/O1/O3 objects exact; four-block paired user -0.06%, wall -0.48%, RSS +0.07%. Earlier observer-side type construction failed semantically, its scoped variant worsened PA22 from 68 to 74, and a TU-wide claimed-node set still worsened it to 70 | retain the output-inert foundation; reject observer reconstruction and global node ownership |
| W5M-S retained members | Indexed exact out-of-class member owner components by semantic pattern, selected owner, and member identity without publishing events | 24-byte observer-only facts; production record sizes unchanged; PA19/20 exact; PA22 unchanged at 65 mismatches; O0/O1/O3 objects exact; four-block paired user -0.00%, wall -0.53%, RSS +0.09% | retain before replacing the static-member declaration scan; speculative replay is not an authoritative publication boundary |
| W5M-O current partials | Consumed owner-scoped facts only when the written argument shape is equivalent to the selected partial owner | four focused current-owner/return fixtures and two negative controls exact; PA19 279/279 and PA20 158/158 strict remain exact; PA22 mismatches 68 -> 65; exact frozen objects; four-block paired user -0.06%, wall -0.54%, RSS -0.04% | retain; same primary-pattern identity alone is too broad |
| W5M-O retained members | Selected retained definition provenance from the committed static-member binding and removed recursive declarator/template-name scanning | only two targeted witness files change; nested fixture exact; PA22 65 -> 64 mismatches; PA19/20 exact; O0/O1/O3 objects exact; four-block paired user -0.00%, wall +0.48%, RSS -0.27% | retain; publish same-pattern typed facts only, leaving deduced source identity as a separate category |
| W5M-S object-type role | Retained the exact structured-type root separately from its explicit template-id component | output remains unchanged; temporary consumer probe finds the intended rooted location; PA19/20 exact; PA22 remains 64; O0/O1/O3 objects exact; four-block paired user -0.12%, wall -0.52%, RSS +0.24% | retain as output-inert source-role machinery before consumption |
| W5M-O object-type role | Published retained object-type facts through deduced provenance while preserving explicit counts on nested components | exactly one witness file changes and becomes exact; PA22 64 -> 63; PA19/20 exact; O0/O1/O3 objects exact; four-block paired user -0.66%, wall -0.43%, RSS -0.00% | retain; source role, not renderer inference, determines binding provenance |
| W5M-S alias qualifier | Retained the concrete structured-name carrier for a written alias qualifier at the final lookup decision, leaving unqualified uses explicitly unqualified | `SourceEvent` remains 232 bytes; PA19/20 exact; PA22 remains 63; audits and frozen O0/O1/O3 objects exact; extended six-block paired user -0.299%, wall -0.267%, RSS -0.038% | retain before deleting alias owner reconstruction in the renderer |
| W5M-O alias qualifier | Rendered declaration-complete written alias qualifiers directly and removed the neighboring class-use event scan | exactly four PA22 witnesses change and become exact; PA22 63 -> 59; retained-dependent controls and PA19/20 unchanged; objects/audits exact; four-block paired user +0.178% | retain; unresolved retained qualifiers require a declaration-owned fact, not a replay-derived owner |
| W5M-S retained alias owner | Related unresolved retained template-id components to the current partial owner, then joined only when final alias lookup confirmed the same declaration-owner pattern | 20-byte observer fact; optional source event 240 bytes; probe distinguishes member `f<T>` from unrelated global aliases; PA19/20 exact, PA22 unchanged at 59; objects/audits exact; compact four-block paired user -0.705% | retain before rendering the partial's canonical owner arguments |
| W5M-S dependent identity | Completed existing template-parameter entity ordinals at every shared-shape/proxy creation path and added an opt-in typed source-identity rendering policy | temporary probe exactly renders the dependent partial owner including template/type parameter kinds and pack role; old renderer output unchanged; PA19/20 exact, PA22 stays 59; objects/audits exact; final eight-block paired user -0.119%, wall +0.109%, RSS +0.271% | retain as general presentation machinery; reject name/string replacement and keep the policy off the ordinary renderer path |
| W5M-O dependent alias owner | Rendered a retained partial owner from its typed primary-pattern and partial-argument identities | exactly one PA22 witness changes, only its owner line; PA19/20 exact; frozen objects/audits exact; four-block paired user +0.178% | retain the bounded consumer; resolve the remaining cv presentation difference independently |
| W5M-O alias cv presentation | Canonicalized cv placement only when token structure otherwise preserves the explicit source identity | broad canonical rendering worsened PA22 59 -> 72 and was rejected; narrow policy makes the owner fixture exact and PA22 59 -> 58, with PA19/20 exact and no PA23/24 manifest changes; objects/audits exact; final extended six-block paired user -0.122%, wall -0.000%, RSS +0.154% | retain the structural normalization; source-preserved identities remain authoritative |
| W5M-O call-owner normalization | Applied the existing primitive spelling normalization at selected member-call owner presentation | PA22 58 -> 56; one PA23 and two PA24 files also become exact with no later-manifest regressions; PA19/20 and objects/audits exact; four-block paired user +0.000% | retain the shared final-render normalization |
| W5M-S typed argument elision | Added an opt-in recursive source-identity policy keyed by semantic entity and bounded presentation arity | existing APIs and output unchanged; PA19/20 exact; PA22 stays 56; O0/O1/O3 LowIR and audits exact; four-block paired user -0.241%, wall -0.538%, RSS +0.100% | retain as the foundation for nested default provenance; do not manufacture qualified/unqualified string aliases |
| W5M-O omitted argument provenance | Derived typed entity limits from exact canonical specialization facts and consumed them recursively | PA22 56 -> 55; inherited alias operator exact; nested use-scope arguments corrected; PA19/20 exact; PA23/24 unchanged; objects/audits exact; four-block paired user -0.847% | retain; keep semantic default equivalence separate from genuinely omitted source provenance |
| W5M-S/O declared default provenance | Factored typed class-default materialization and published complete canonical source facts with an observer-only arity; excluded defaults that refer to template parameters | PA22 55 -> 50 with five newly exact fixtures; dependent-default negative control exact; PA19/20 exact; PA23/24 unchanged; focused witness LowIR and frozen O0/O1/O3 LowIR exact; four-block paired user -0.717% | retain; reject post-substitution equality for dependent defaults |
| W5M provenance re-audit | Probed dependent-default equality and typed conversion-target presentation against PA22/23/24 | dependent-default probe made one PA24 file exact but regressed explicit `deduce<proxy<result>, int>` provenance; conversion-target probe changed 8 PA23 and 4 PA24 files and was not display-contained | reject both consumers; first separate per-occurrence source provenance, structural default identity, and canonical entity presentation in output-inert foundations |
| W5M-S class source provenance | Retained successful class-template source identity separately from canonical specialization presentation | 48-byte observer-only fact; 731 PA19/20/22 witness and LowIR manifests exact; PA19/20 strict and ordinary clean; frozen O0/O1/O3 and audits exact; four-block paired user -0.971% | retain as output-inert foundation; consumers must join exact source node, typed pattern, and canonical binding |
| W5M-S structural default identity | Added stable arena token identities and computed dependent-default structure by substituting parameter tokens from the same source occurrence, gated by typed equality | four focused positive/negative arities correct; 7,660 PA19--PA24 artifacts exact; frozen O0/O1/O3 and audits exact; four-block paired user +0.061% | retain output-inert identity; consume it separately for source provenance and canonical entity abbreviation |
| W5M-O source default origin | Joined structural default provenance to only the exact source node, typed pattern, and canonical binding | exactly four intended origin labels change across PA22--PA24; dependent-result control stays explicit; no manifest regression; LowIR/frozen/audits exact; four-block paired user +0.063% | retain the occurrence-local consumer; canonical entity abbreviation remains separate |
| W5M-O canonical default identity | Added structurally proven source arities to the typed entity-elision sidecar | only the same three PA22--PA24 files change; one PA24 file becomes exact and PA22/PA23 closure identities improve; no regression; LowIR/frozen/audits exact; four-block paired user -1.566% | retain; conversion-target primary identity remains a separate typed role |
| W5M-O conversion target identity | Rendered a conversion closure's typed class-template target from its primary pattern's terminal name | 9 files change only in closure lines; PA22 +1, PA23 +2, PA24 +1 exact; no source-event movement or regression; LowIR/frozen/audits exact; four-block paired user -1.340% | retain; existing typed `conversion_target` is sufficient after provenance/presentation separation |
| W5M verification repair | Finished the semantic initialization unit's earlier typed-exception migration by routing its two direct typed throws through the lightweight semantic helpers | exception-taxonomy fanout returns from 33 to the audited ceiling of 32; PA22 311/311; frozen O0/O1/O3 objects exact; four-block paired user -0.001%, wall -0.163%, RSS -0.684% | retain as an independent audit repair before measuring further witness convergence; do not raise the architecture ceiling |
| W5M semantic candidate viability | Restricted chained-user-conversion rejection to the N3485 class copy-initialization context already represented by `copy_initialization` | 34 witness-only deltas; PA22 +6, PA23 +7, PA24 +8 exact, 13 additional local improvements, no regression; through-PA24 3,565/3,565; frozen outputs/audits exact; clean four-block paired user +0.001% | retain as ordinary semantic correctness; back it with a relationship-based viability/rank check before plan exit |
| W5M rejected source-range ordering | Ordered same-start source events by widest syntax range before semantic insertion order | made 4 witnesses exact but regressed 19 previously exact files; PA22 -4, PA23 -4, PA24 -7 exact | reject; source containment is not semantic evaluation/dependency order, so retain the authoritative ranges but add no geometric tie-breaker |
| W5M-O final function lifecycle | Filtered collected function instantiation/requirement facts by the binding's final explicit-instantiation suppression state | exactly 2 PA22 extern-function witnesses change and become exact, later manifests unchanged; inline/defaulted extern-class member control preserved; PA22 311/311; frozen outputs/audits exact; extended paired user +0.000% | retain; final typed state, not collection timing or source spelling, owns lifecycle publication |
| W5M-O explicit class finalization | Published class finalization at the common completed-class boundary for explicit instantiation declarations and definitions | 3 witness deltas; PA22 +1 and PA24 +1 exact, no regression; PA22 311/311; frozen outputs/audits exact; extended paired user -0.488% | retain; class completion precedes the typed declaration/definition emission policy split |
| W5M-O terminal object-type source | Used the authoritative terminal name component to distinguish explicit template-ids from alias/typedef/member results, while suppressing a retained dependent qualifier through W5D's exact-node provenance | terminal-only prototype improved 7 files but regressed one exact PA23 retained-body witness and was rejected; exact-node join changes 7 files, with PA22 +1 and PA24 +3 exact, no PA19/20/23 regression, all 1,532 LowIR artifacts exact; PA22 ordinary 311/311; frozen O0/O1/O3 and audits exact; extended paired user +0.182%, wall +0.000%, RSS +0.020% | retain the source-role consumer; do not infer retained context from result type or source spelling |
| W5M rejected retained-call shortcut | Allowed an explicit template-id call to bypass the retained-source suppression bit | one intended PA22 call became exact, but 6 PA19, 7 PA20, 6 PA22, 48 PA23, and 33 PA24 previously exact witnesses regressed; LowIR stayed exact | reject; explicit syntax does not prove that a call is a public evaluated use, so add typed evaluation/replay provenance before revisiting |
| W5M-S recursive entity presentation | Propagated the existing anonymous-namespace policy through typed template arguments, declarator types, member-pointer owners, value bindings, and closure replacement identities | 2 witnesses change: PA22 gains 1 exact file, and every affected function/require/variable closure identity in one PA24 file gains its missing anonymous scopes; no regression, all 1,532 LowIR artifacts exact; PA22 ordinary 311/311; frozen O0/O1/O3 and audits exact; cumulative paired user -0.547%, wall -0.650%, RSS +0.704% | retain as shared typed presentation machinery; keep ordinary source-event rendering's default policy unchanged |
| W5M-O call-operator deduction source | Passed `TryAnalyzeOverloadedOperator`'s exact call node to the shared typed function-template deduction result | exactly 1 PA22 witness changes and becomes exact (missing `too_few_arguments`); every other PA19--PA24 witness and all 1,532 LowIR artifacts are identical; PA22 ordinary 311/311; frozen O0/O1/O3 and audits exact; paired user -0.301%, wall -0.212%, RSS -0.202% | retain; source-aware callers own drop publication and the deduction engine remains source-optional |
| W5M-F/O ADL deduction source | Carried the exact callee component through ADL namespace and hidden-friend candidate formation into typed function-template deduction | exactly 1 PA22 witness changes and becomes exact (missing hidden-friend `non_deduced_mismatch`); every other PA19--PA24 witness and all 1,532 LowIR artifacts are identical; PA22 ordinary 311/311; frozen O0/O1/O3 and audits exact; paired user +0.062%, wall -0.218%, RSS -0.140% | retain the nullable provenance parameter; synthesized source-free ADL remains non-publishing |
| W5M rejected typed-drop display dedup | Deduplicated rendered drops by raw, then canonical, candidate binding/pattern/reason instead of displayed declaration and reason | made the intended duplicate-constructor-drop file exact, but changed 17 PA22, 42 PA23, and 25 PA24 files and regressed 15/20/14 previously exact files; canonical binding did not reduce the fanout; LowIR remained exact | reject; instantiated binding identity is still finer than public candidate-declaration identity, so retain display dedup until declaration-origin and source-occurrence provenance are available |
| W5N public provenance contract | Documented the optional PA19 semantic diagnostic and added a strict relationship test for computed source anchors/order, typed explicit/defaulted/deduced bindings, selected-template calls, closure deduplication, witness-off LowIR identity, and output failure | property test passes without exact witness matching; PA19 strict 279/279 plus 10/10 course, ordinary 305/305, and through-PA19 2,092/2,092 | retain as the public test boundary for the machinery-first tranche; later provenance foundations must satisfy this test without exposing private representation |
| W5N-F candidate declaration origin | Retained a rejected specialization's originating function-template pattern in the existing observer drop record; ordinary candidates continue to identify their canonical declaration binding and no production record grows | all 3,064 PA19--PA24 witness/LowIR artifact pairs byte-identical to the pre-foundation compiler; PA19/20 strict and ordinary clean; frozen O0/O1/O3 exact; audits exact; four-block paired user +0.000%, wall -0.383%, RSS -0.404% | retain as output-inert typed machinery; a following consumer may deduplicate per source occurrence and declaration origin, never by rendered name or specialization binding |
| W5M-O | Publish retained owners, dependent aliases, operators, and constructors only from final semantic decisions using W5M-F/W5M-S provenance | PA22 convergence in progress from a stable 68 mismatches; require PA19/20 exactness, improved PA22 exact count, exact no-witness objects, and repeated A/B timing | prefer declaration-owned semantic source facts over any observer-side syntax recovery |

## Exit criteria

This plan is complete only when:

- root `make test-strict` passes all configured PA19/20/22/23/24 executions;
- the PA19 README describes the high-level semantic diagnostic and focused
  tests validate properties a student can implement from that description;
- the implementation contains no fixture/program-content matching and does not
  invoke another compiler for witness output;
- compiling without witness performs no witness allocation or rendering and
  remains within the 0.2% matched no-witness CPU bound with exact outputs;
- `make test-report-through-pa38`, relevant debug/round-trip tests, and all
  audits pass; and
- an explicit-32-way root inception is byte-exact, the plan ledger is complete,
  all retained increments are committed, and `v3opt` is pushed.

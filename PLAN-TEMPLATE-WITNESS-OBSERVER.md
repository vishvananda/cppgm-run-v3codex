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

# Early Semantic Core Unification Plan

Status: planned; no implementation work has started.

Date: 2026-08-20

## 1. Objective

Restore the original PA7/PA8 extension boundary without making either early
tool depend on the whole PA11+ compiler.  The intended dependency direction is:

```text
                         +-- PA7 direct parser -> nsdecl view
shared semantic core ----+-- PA8 direct parser -> linkage/init/image extension
                         +-- PA10 AST adapter -> PA11+ semantic extensions
```

The shared core owns only the language facts common to these assignments:

- interned identifiers and canonical structural types
- namespace scopes, bindings, and declaration identity
- namespace aliases, using declarations, and using-directive graphs
- qualified and unqualified lookup
- source ordinals needed for deterministic assignment output

PA7 continues to analyze each translation unit independently.  PA8 adds the
program-level linkage, initialization, relocation, and mock-image layers.
PA10 and later assignments adapt their syntax tree into the same core and add
class, template, expression, ABI, demand, and lowering facts outside it.

This is a maintainer plan.  It does not change any student-facing assignment
contract.

## 2. Why not link PA7 and PA8 directly to the current PA11 model?

The logical PA11 model contains the right concepts, but its present physical
boundary is too broad for the early tools:

- `pa11_model.cpp` directly includes PA22 lambda-presentation support.
- the main semantic entry installs builtins and completes definition demand,
  even for a source-facing type view
- `BindingRecord` and `EntityRecord` contain later ABI, template, layout,
  lambda, lifecycle, and demand facts
- hundreds of later semantic and lowering sites index those records directly
- the `cppgm++` source set has 178 objects, while `nsdecl` and `nsinit` have
  deliberately small assignment-local source sets

Making `nsdecl` link that graph would reverse the intended assignment layering
and make early-tool time, memory, and binary size depend on late compiler
features.  The first job is therefore to extract a genuinely small common
core, not to route the early drivers through `ConsumeSemanticTranslationUnit`.

## 3. Current baseline

The current implementations are independent:

- `dev/src/pa7_semantic.cpp` is a 2,495-line namespace/type analyzer.
- PA8 has its own model, parser, and semantic layer in `pa8_model.cpp`,
  `pa8_parser.cpp`, and `pa8_semantic.cpp`.
- PA11+ uses `pa11::Program`, `NameTable`, and `TypeTable` as the production
  graph.

A 100-round measurement over the 41 PA7 translation units took approximately
1.19 seconds wall / 0.68 seconds user / 7.5 MiB peak RSS through `nsdecl`,
versus 5.04 seconds wall / 4.33 seconds user / 10.2 MiB peak RSS through the
mainline semantic route.  PA7+PA8 currently report 110/110 in about 0.46
seconds.  These are comparison baselines, not stable performance promises;
the host may be intermittently loaded.

## 4. Target ownership

### 4.1 Shared core

Use dense integer IDs and append-only vectors for names, types, scopes,
declarations, bindings, and using edges.  Hash tables map compact structural
keys to IDs; they must not use rendered type or qualified-name strings as
identity.  Keep source order in separate ordinal vectors instead of ordering
semantic identity maps for presentation.

The core API should expose operations, not mutable record layouts:

- create or reopen a namespace
- intern a canonical type constructor
- declare or redeclare a supported entity
- add an alias, using declaration, or using directive
- perform filtered qualified or unqualified lookup
- enumerate source declarations and namespaces in source order

Lookup caching, if retained, must use typed keys and explicit generation
counters for every mutation that can affect a result.  Prefer an uncached
linear/vector implementation until measurement shows the cache wins on real
inputs; the PA7/PA8 graphs are small, and correctness is more valuable than a
cache whose invalidation surface is difficult to prove.

### 4.2 PA7 adapter and view

Retain the PA7 phase-7 parser and translate its semantic actions directly into
the shared core.  The `nsdecl` renderer owns only PA7 filtering and formatting.
Each input translation unit receives an isolated core graph, as required by
the PA7 no-linking boundary.

### 4.3 PA8 extension

Retain the PA8 parser but replace its duplicate namespace/type/lookup model
with the shared core.  Store PA8-only facts in side tables keyed by core IDs:

- storage and linkage
- definition and ODR state
- initializer expressions and constant values
- cross-translation-unit canonical entities
- image placement, relocation, and emitted bytes

Per-translation-unit lexical scopes remain isolated.  A separate program-link
layer associates linkable declarations across translation units; it must not
merge namespace lookup graphs from different translation units.

### 4.4 PA11+ adapter and extensions

The PA10 AST analyzer becomes another client of the shared operations.  Late
facts remain in PA11+ side tables or extension records.  Presentation helpers,
especially lambda and ABI emission names, depend on core identity; the core
must never depend on them.

Do not compact the existing PA11 records at the same time as the first
boundary extraction.  There are roughly 706 direct binding-index sites and
620 direct entity-index sites, so an all-at-once record rewrite would obscure
semantic regressions.  After all three clients use the core, measure record
population and move cold late facts to side tables only where the result is a
clear time or memory win.

## 5. Migration phases

### Phase 0: lock contracts and measurements

1. Record exact PA7 and PA8 output, exit status, binary-image bytes, binary
   size, peak RSS, and repeated timing baselines.
2. Classify every active PA7/PA8 fixture that production compilers reject.
   Rewrite standard-invalid positive cases before using the suite as a parity
   oracle; retain genuinely diagnostic cases as expected failures.
3. Add focused lookup tests for namespace reopening, canonical duplicate
   declarations, aliases, using graphs, and mutation after lookup.

### Phase 1: extract typed names and canonical types

Introduce a dependency-neutral core library containing name interning,
fundamental and structural type nodes, and their typed keys.  Adapt PA7 first.
Require exact PA7 output and a clean through-PA7 report before proceeding.

Then adapt PA8's type layer without changing linkage or image construction.
Require exact mock-image output and a clean through-PA8 report.  Add the core
objects to the `nsdecl`, `nsinit`, and `cppgm++` lists in
`dev/frontend_source_sets.mk`; do not include implementation files as source.

### Phase 2: extract scopes, declarations, and lookup

Move namespace ownership, aliases, using edges, declaration identity, and
lookup into the core.  Migrate PA7 first, then PA8.  Use generation-stamped
caches only after an uncached implementation is exact and profiling shows a
need.

The acceptance gate for each substep is byte-exact PA output, the owning PA
test, the full report through that PA, and screened performance.  A regression
that first manifests in PA11+ gets an earliest-owned PA7 or PA8 reducer.

### Phase 3: isolate PA8 program linkage

Replace PA8's duplicated source-scope graph while preserving its program-level
entity association and image order.  Test multiple translation units whose
same spellings have internal, external, and no linkage, plus namespace aliases
and using directives that differ between translation units.

Linkage keys must be structured data: linkage kind, owning namespace identity,
unqualified name ID, and function/type signature IDs as required.  Do not use
mangled or rendered strings as primary identity.

### Phase 4: adapt the PA10/mainline analyzer

Insert a narrow adapter between PA10 syntax and the shared declaration API.
Move only the PA7/PA8-common behavior first.  Keep class scopes, overload
resolution, templates, implicit members, constants beyond the early subset,
layout, ABI, demand, and lowering in later extensions.

Break the current upward presentation dependency by moving generic emission
name construction out of `pa11_model.cpp`; lambda-specific rendering remains
in PA22 and consumes typed identity from below.

### Phase 5: remove duplication and measure compaction

Delete the superseded name/type/scope/lookup code only after all output paths
are exact.  Measure:

- hot record sizes and capacities
- hash probes and lookup-cache hit rates
- allocated bytes and peak RSS
- `nsdecl`, `nsinit`, PA11 type-view, and frozen-compile time
- executable text and total binary size for all three tools

Only then decide whether late PA11 facts should move from `BindingRecord` and
`EntityRecord` into side tables.  Land each independently measurable
compaction as a separate changeset.

## 6. Test and commit discipline

Every phase is split into reviewable commits.  Before each commit:

1. run the affected `make test-paN`
2. run `make test-report-through-paN` to obtain the full failure set
3. run root `make test-report` after any shared-core change
4. run the PA39 file audit and resolve every fatal finding by restoring clean
   component/source-set boundaries
5. compare screened timings against the recorded baseline; do not attribute a
   single noisy host sample to the change

Run inception only at major boundaries: after the PA10/mainline adapter lands
and at final completion.  The final gate is a from-scratch PA39 self build and
inception comparison.  Do not use PGO for this work.

New tests belong to the earliest owning assignment under
`cppgm.tests/course/paN/`.  If the pinned reference disagrees with a
standard-valid case that the course adopts, update the public contract and
authoritative reference before activating the course fixture.  Do not change
the implementation merely to reproduce a non-normative reference defect.

## 7. Success criteria

1. PA7, PA8, and PA11+ use one typed implementation of common name, type,
   namespace, declaration, and lookup behavior.
2. PA7 and PA8 do not link PA10 syntax, PA22 presentation, ABI, demand,
   lowering, or other late compiler layers.
3. PA8 lexical lookup stays translation-unit-local while program linkage is
   explicit and independently testable.
4. No hot semantic identity is stored or recovered as rendered text.
5. PA7/PA8 output remains byte exact for standard-valid fixtures; later
   outputs and the frozen object remain unchanged unless a separately reviewed
   standard correction requires a fixture update.
6. Full `make test-report`, the PA39 audit, the final self build, and inception
   are clean.
7. Repeated screened measurements show no material compile-time regression;
   the preferred result is a visible reduction in PA11+ semantic time and
   allocated bytes from deleting the duplicate paths.

## 8. Non-goals

- Do not replace the PA7/PA8 direct parsers with the PA10 AST parser.
- Do not make early tools instantiate the full production analyzer.
- Do not combine this migration with new language features or backend work.
- Do not preserve a lookup cache solely because an old implementation had
  one; preserve observable semantics and measured efficiency.
- Do not put this architecture or migration history into student-facing
  READMEs.

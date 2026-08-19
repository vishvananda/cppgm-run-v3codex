# Typed Compiler Boundary Plan: Remove Production Text Round-Trips

Status: in progress

Date: 2026-08-19

Audit anchor: `c349d7f5`

## 1. Objective

Remove the remaining cases where the integrated compiler renders a typed fact
as text, reparses text to recover a typed fact, or uses presentation text as
semantic identity.  The target pipeline is:

```
source bytes -> classified tokens -> syntax facts -> semantic IDs
             -> typed LowIR -> typed MIR -> native object
```

Text remains correct and necessary at true boundaries: source input,
diagnostics, requested dumps, standalone textual tools, external symbol
spellings, assembly payloads, and object-file string tables.  It must not be a
transport representation between compiler phases.

This work follows `PLAN-LOWIR-COMPACT-IDENTITY.md` and
`PLAN-LOWIR-MIR-PRESENTATION.md`.  Those plans removed text identity from the
production LowIR and MIR core.  This plan does not reopen or replace that
work.  It covers the broader audit of preprocessing, syntax, semantics, ABI
mangling, presentation ordering, operators, and literals.

The implementation must follow `spec.md`: compact IDs, enums, direct-indexed
tables, bounded per-translation-unit ownership, and function-at-a-time work
where applicable.  A string set or string map is acceptable at a cold input or
output boundary; it is not the default representation for a hot compiler
fact.

## 2. Success criteria

The project is successful only if all of the following are true:

1. The production source-to-object path no longer reparses qualified names,
   ABI fact text, fixed operator spellings, or literal spellings when the
   corresponding typed fact is already available.
2. Each retained changeset removes measured calls, bytes, allocations, or
   comparisons rather than adding a second index beside the old text path.
3. The cumulative work produces at least a 5% median frozen `-O0` user-time
   improvement against the immutable audit anchor in a screened interleaved
   comparison.  A 5-10% improvement is the working target.
4. Peak RSS does not regress by more than 3% without a separately measured and
   justified tradeoff.
5. Serialized syntax, semantic, LowIR, and MIR contracts remain exact unless a
   deliberate behavior correction has its own earliest-owned regression and
   tracker entry.
6. Root `make test-report` passes, the PA39 file audit has zero fatal findings,
   and a clean self-host/inception comparison passes on the final committed
   tree.

An individual representation cleanup may be retained without a standalone
timing win only when it removes structural work, is neutral within the
predeclared noise screen, and enables a later measured phase.  Such a result
must be recorded as enabling work rather than claimed as a performance win.

## 3. Boundary rule

### 3.1 Text that is allowed

The following are input, output, or language-visible text boundaries and are
not targets merely because they contain strings:

- source lexing and the first decoding of token and literal spellings;
- include paths, directives, macro paste, and macro stringization;
- diagnostics and language-visible strings such as `__PRETTY_FUNCTION__`;
- explicitly requested AST, semantic, LowIR, and MIR serialization;
- the standalone PA14 ABI-fact parser;
- the standalone textual LowIR parser and resolver;
- GNU assembly payloads and final assembly labels;
- external symbol names, section names, ELF string tables, and debug strings;
- the PA30 private serialized-object join boundary; and
- error-message text, which is not a test oracle.

These boundaries should parse once into a typed model or render once from a
typed model.  Their adapters may use transient text indexes when the input is
intrinsically textual, but those indexes must not become the production core.

### 3.2 Text that is not allowed as core identity

The following are implementation smells in an integrated compile:

- splitting a joined qualified name on `::` after the parser already saw its
  components;
- constructing textual ABI fact records from `TypeId` and `BindingId`, then
  parsing and interning them in the mangler;
- materializing qualified display or emission names before an output consumer
  requests them;
- comparing operator or token spellings after token classification;
- formatting a numeric value and parsing it again in lowering;
- decoding the same string literal into code units in more than one phase;
- reclassifying an emitted preprocessor spelling on every occurrence; and
- sorting by repeatedly rendering or virtually comparing presentation text
  when an exact compact collation key can be computed once.

The replacement must be the fact itself: an enum, strong integer ID, typed
tuple, scalar bits, or arena slice.  It must not be an auxiliary string-to-ID
map while the original string-bearing hot representation remains in place.

## 4. Audit evidence and limitations

The frozen source is:

`cppgm-extended-pa39-source-layout/benchmarks/self_compile/stable/semantic_overload.cpp`

The audit used an immutable `-O0` compiler and the extended checkout's
`dev/src` include root.  The profiling run produced a 4,415,448-byte object
with SHA-256
`d52599359535b175519d1ce1249f2a7eafa443fa1765d1c39d7d38f93716c37f`.
Phase 0 must rebuild and archive an immutable compiler at the audit anchor and
confirm these output facts before using the timing as an implementation
baseline.

The compiler reported:

| Metric | Audit value |
| --- | ---: |
| Source bytes | 361,883 |
| Preprocessed bytes | 24,806,674 |
| Input files | 1,079 |
| Output post-tokens | 490,861 |
| Syntax nodes | 583,346 |
| Semantic nodes | 189,763 |
| Semantic edges | 175,657 |
| Declarations | 465,363 |
| Types | 106,717 |
| Scopes | 262,049 |
| Semantic program bytes | 151,941,808 |
| Semantic dump bytes | 46,431,448 |
| Semantic side bytes | 159,384,181 |
| Shared string bytes | 12,480,984 |
| Accounted semantic peak | 427,284,244 |
| Typed bytes | 61,499,568 |
| Preprocess time | 1.2935 s |
| Parse time | 0.2775 s |
| Semantic time | 2.5814 s |
| Typed-to-LowIR time | 0.6354 s |
| Frontend total | 4.2015 s |

The frontend interner made 2,336,592 calls: 2,223,029 hits, 113,563 misses,
31,218,769 hashed bytes, 2,389,454 probes, and 2,225,961 comparisons.  Source
location construction and token-spelling interning each occurred 490,862
times.

A full Callgrind run observed 18,182,405,968 instruction references.  The
percentages below are inclusive unless identified otherwise and therefore
overlap.  They rank work and supply call counts; they are not wall-time
claims.  Wall/user/system time and RSS acceptance must come from uninstrumented
interleaved runs.

## 5. Audit summary

| Area | Evidence on the frozen compile | Diagnosis | Priority |
| --- | --- | --- | --- |
| Semantic name paths | 892,548 `ParseNamePath` calls; about 2.31% inclusive; 894,041 range interns | Joined syntax payload is repeatedly split to recover components or only the terminal name | First, low risk |
| Production ABI bridge | 9,714 function mangles; about 8.03% inclusive; 179,831 ABI string interns; 50,564 path interns | Typed program facts are expanded into a fresh string-rich ABI fact graph for each requested symbol | First major project |
| Qualified presentation | 93,496 `EmissionName`, 86,496 `DisplayName`, 199,629 emission-path builds, 340,270 scope-prefix queries | Qualified display strings are created eagerly and retained as program facts | After typed ABI |
| Block presentation order | 266,511 `PresentationLess` calls; about 0.71% inclusive | Object-only EH order is derived with repeated lexical presentation comparison | Independent PA15 slice |
| Fixed operators/tokens | 527,293 simple-token classifications; widespread spelling comparisons; 15,013 lowering prefix strips | An enum exists at tokenization but is not carried through all consumers | Medium |
| Literal transport | Repeated semantic/static-lowering decode sites; small frozen numeric counts | Decoded literal facts are incomplete, so later stages parse spelling again | Medium, architecture/correctness |
| Preprocessor handoff | 1,290,244 macro spelling interns; 490,862 frontend token-spelling interns | Integrated handoff loses spelling identity and classification, then reinterns each emitted occurrence | Later, interface-wide |
| Specialized parsers | Ambiguous relational declaration, PA32 ABI identity tags, attributes/asm | Small or uncommon production text recovery remains | Last |

The audit found no general production semantic table keyed by
`std::string`.  Canonical types, scope lookup, declarations, signatures,
template specialization keys, and most generated identities already use
typed IDs.  They should be preserved.  This plan targets specific downgrade
and recovery boundaries, not a wholesale rewrite of every string in the
compiler.

## 6. Phase 0: durable counters and immutable baseline

Before behavior changes:

1. Build immutable baseline and candidate compiler paths; never overwrite an
   executable participating in a comparison.
2. Confirm exit status, object size, allocatable section sizes, exported
   symbols, and SHA-256 for the frozen compile.
3. Add `--stats`-only counters for the work each phase intends to remove.  The
   counters must be numeric fields, disabled by default, and must not introduce
   a string-keyed hot-path registry.
4. Record record sizes and accounted arena/vector bytes for every proposed
   replacement.  A compact identity change may not silently enlarge all
   syntax or semantic nodes.
5. Establish at least one screened A/A sequence to estimate current host
   noise.  Record load and CPU pressure with each run.

Required counters include:

- joined-name renders, structured-name fast paths, name-path parses, and
  terminal-only parses;
- ABI fact records, strings, paths, bytes, dependency walks, and per-binding
  remangle counts;
- display/emission renders and actual output consumers;
- block-order comparisons and characters examined;
- operator spelling comparisons and lowering prefix strips;
- literal decode/render/redecode counts and literal side-arena bytes; and
- emitted spelling occurrences, distinct emitted spellings, classification
  repeats, and frontend remap misses.

Measurement-only changes are committed separately from representation or
behavior changes.

## 7. Phase 1: carry syntax name identity into semantics

### 7.1 Current path

`Parser::ParseName` in `dev/src/pa10_syntax.cpp` already sees every identifier
as a `TextId` and can retain a structured name for qualified, templated, and
operator forms.  It nevertheless always calls `JoinSpellings`.  Simple names
often retain only that joined payload.

`SemanticAnalyzer::ParseNamePath` in `dev/src/pa12_semantic.cpp` then scans
`::`, template depth, conversion-operator spacing, and interns each range.  Its
largest callers are:

| Caller | Calls |
| --- | ---: |
| `DeclaratorName` | 441,439 |
| `LookupSpelling` | 245,933 |
| retained-template identifier validation | about 74,500 |
| `DeclaratorNamePath` | 47,791 |
| function-template lookup | 37,191 |
| class analysis clones | about 17,600 |

`DeclaratorName` is especially wasteful because it reconstructs a complete
`NamePath` and keeps only `Last()`.

### 7.2 Target representation

- Every name-bearing syntax node carries the terminal `TextId` already known
  by `ParseName` in its semantic payload or an equally compact typed field.
- Qualified, templated, conversion, destructor, and operator names continue to
  use the existing structured-name child where components are required.
- A `SyntaxNamePath` helper returns the structured components when present and
  otherwise constructs a one-component typed path from the terminal ID.
- Simple identifiers do **not** gain a structured child node.  Creating one
  for every simple name would enlarge the 583,346-node syntax arena and could
  cost more than it saves.
- Joined payload text remains available for exact syntax serialization and
  diagnostics.  It is not the semantic lookup source.
- Generated compiler names use typed paths or preinterned IDs rather than
  routing through `ParseNamePath` on a string literal.

### 7.3 Changesets

1. Give `DeclaratorName` a terminal-ID fast path.  Require exact output and
   measure this slice independently.
2. Add the one-component/structured `SyntaxNamePath` helper and convert
   `DeclaratorNamePath` consumers.
3. Replace string-only lookup entry points with overloads accepting `NameId`,
   `NamePath`, or the owning syntax node.
4. Convert template and class consumers by call-count order.
5. Replace fixed generated paths such as `std::bad_alloc` with preinterned
   typed components.
6. Retain `ParseNamePath` only for genuine text adapters and language-visible
   generated text; add an assertion or counter preventing accidental use on
   the ordinary syntax path.

### 7.4 Ownership and gates

PA10 owns the syntax fact.  PA11/PA12 own typed semantic lookup.  Template
consumers are exercised at their earliest PA19-and-later contract.  Add each
behavioral reducer at the earliest PA that understands its language feature,
not at the later PA where a failure happens to surface.

No serialized fixture change is intended.  If a syntax or semantic fixture
changes, stop and explain whether the public contract exposed the wrong field
or the implementation changed behavior.  Do not regenerate fixtures merely
to accept an internal representation change.

## 8. Phase 2: replace the production ABI fact-file bridge

### 8.1 Current path

`dev/src/pa15_lowering_abi.cpp` starts from `Program`, `TypeId`, `BindingId`,
template argument IDs, and typed declaration facts.  For each requested symbol
it constructs an `abi_mangle::AbiFactFile` containing strings, record objects,
definition/reference IDs, and split qualified paths.  `dev/src/abi_mangle.cpp`
then interns and traverses that graph to emit an Itanium name.

The standalone parser in `dev/src/abi_mangle_parse.cpp` is the PA14 textual
input contract.  It must remain supported, but it need not define the
production representation.

The frozen profile recorded:

- 9,714 `MangleFunction` calls;
- 9,231 `mangle_fact_file` calls;
- 50,564 `PathPool::intern` calls;
- 179,831 ABI `StringPool::intern` calls;
- substantial `AbiFactBuilder` construction and `AbiType` move/destruction;
  and
- repeated recursive function-template-parameter dependency walks.

### 8.2 Target representation

Introduce one translation-unit-scoped typed ABI context.  It may be a compact
numeric `AbiGraph` or a direct encoder over the semantic program, but it must
have these properties:

- dense fact/path IDs or direct `TypeId`, `BindingId`, `EntityId`, and
  `TemplateArgumentListId` references;
- component `NameId`s only where spelling is semantically required;
- no per-symbol cloning of type and declaration record trees;
- per-mangle Itanium substitution state keyed by typed identity;
- cached immutable dependency facts stored in the translation-unit context,
  never in a process-global cache; and
- text only at the PA14 parser adapter and the final mangled-name output.

The PA14 parser should parse its text once into the same numeric graph used by
the encoder.  It is not acceptable to keep a complete string graph for
production and add a typed lookup layer beside it.

### 8.3 Changesets

1. Add fact/byte/dependency counters and a translation-unit ABI context without
   changing output.
2. Define numeric ABI records and adapt the PA14 text parser to them.  Keep all
   existing PA14 exact fixtures.
3. Convert builtin, qualified-name, function, template-argument, and type
   records one family at a time.
4. Make the PA15 production path populate or directly traverse typed records;
   remove production `AbiFactFile` construction after parity.
5. Store function-template-parameter dependency on the owning type/recipe fact
   so each mangle does not recursively rediscover it.
6. Add dense final-result caching only after counters prove the same binding,
   variant, and substitution-independent result is requested repeatedly.
   Caching must not precede representation cleanup or retain unbounded text.

### 8.4 Ownership and gates

PA14 owns standalone fact parsing and exact mangling fixtures.  PA15 owns the
production semantic-to-ABI integration.  Template ABI behavior is owned by the
earliest applicable PA23/PA32 test.

For every record family, run the PA14 exact suite, the PA15 LowIR/object owners,
selected PA23/PA32 reports, and the full cumulative report before accepting
the batch.  Mangled bytes, binding, COMDAT/weak ownership, aliases, exported
symbols, and runtime behavior must remain exact.

### 8.5 Residual Phase 2 inventory after T3r

The accepted T3 slices remove all production text-to-path parsing, all
textual definition/reference records, all synthetic substitution strings,
and ordinary function, object, entity, and type name copies.  The remaining
assignments in `pa15_lowering_abi.cpp` fall into four distinct groups and
must not be handled with one generic string table:

1. **Fixed ABI vocabulary:** builtin and standard-substitution spellings,
   operator terminals, constructor/destructor variants, dependent-expression
   operators, and the block-pointer spelling.  Carry the existing semantic
   enum or introduce a small ABI enum, then render the Itanium code in the
   encoder.  These are not source names.
2. **Numeric local presentation:** lambda ordinals, local-name ordinals, and
   discriminators currently formatted with `std::to_string`.  Carry the
   ordinal plus its presence/placement flags and format it once in the
   encoder.  Preserve the ABI's one-based/zero-based and omitted-first-value
   rules exactly.
3. **Language names already represented by IDs:** dependent member names,
   template-id names, literal-operator suffixes, member external-entity names,
   and ABI tags.  Carry graph string IDs resolved directly from `NameId`; do
   not copy the spelling into each transient fact.  The graph still owns the
   final bytes because mangling must emit them.
4. **True output or adapter text:** explicit assembly/C-linkage names,
   qualified-name overrides supplied to the mangling API, the raw `main`
   context fragment, builtin runtime symbol names, and PA14 textual fact keys.
   These remain strings unless a counter proves that an internal typed fact
   was unnecessarily rendered to create them.

Convert one family at a time in that order.  Reuse kind-disjoint numeric slots
or compact side arenas and prove with `sizeof`/owned-byte counters that a
common fact record does not grow.  Keep PA14's textual adapter exact.  Phase 2
ends only after a fresh assignment audit accounts for every remaining
production string as a final spelling or external override; merely reducing
the frozen count to zero is insufficient.

T3s completes the function-terminal portion of item 1: lifecycle variants,
operator terminals, literal-terminal classification, lambda call terminals,
and member-NTTP operator terminals now use one byte-sized enum.  The remaining
fixed-vocabulary work is builtin/standard type substitution, the block-pointer
type marker, and dependent-expression operation codes.

## 9. Phase 3: make semantic presentation lazy

### 9.1 Current path

`dev/src/pa12_semantic_scope.cpp` builds and interns qualified strings in
`ScopePrefixId`, `DisplayName`, and `EmissionName`.  `DeclareFunction` and
related paths retain both display and emission forms.  Class-template
specialization display/storage names also render canonical template arguments
even though specialization lookup already uses typed keys.

The frozen run observed 93,496 emission-name calls, 86,496 display-name calls,
199,629 emission-path builds, and 340,270 scope-prefix queries.  Class-template
specialization presentation was smaller but still repeated.

### 9.2 Target representation

- Store an emission path as a typed path ID or compact tuple such as owner
  `ScopeId` plus terminal `NameId`.
- Store specialization identity as pattern, argument-list, context, and
  partition IDs, not a rendered specialization spelling.
- Render qualified names only for semantic dump output, diagnostics,
  `__PRETTY_FUNCTION__`, or the final ABI encoder.
- Do not retain both display and emission strings on ordinary object paths.
- Reuse the existing typed emission-identity work in PA15 instead of creating
  another qualified-name table.

Implement this phase after the typed ABI context so a downstream string-only
consumer does not force the eager presentation back into the semantic model.

PA12 owns declaration/scope identity, PA19/PA20 own specialization identity,
and PA22 owns lambda identity.  Exact semantic dumps remain the contract and
must render from the typed facts without fixture churn.

## 10. Phase 4: replace object-only block text comparison

`FinalizeBlockPresentation` in `dev/src/pa15_local_presentation.cpp` sorts
generated block presentations for functions requiring EH ordering.
`PresentationLess` performs a virtual character-by-character lexical compare,
including repeated decimal digit access.  The frozen profile observed 266,511
comparisons, about 0.71% inclusive, concentrated in roughly 885 EH functions.

This is not a LowIR operand text round-trip.  It is an ordering dependency on
presentation.  Native LSDA construction consumes `block_presentation_order`,
so changing to insertion order or raw `BlockId` order is not valid.

Replace it with an exact compact collation key computed once per block, or a
linear/radix rank construction over the presentation prefix, generated flag,
and ordinal.  The new order must compare identically to the current lexical
spelling for every label, including numeric boundaries such as 9/10/99/100
and collisions between explicit and generated labels.

This is an independent PA15/PA26/PA29 changeset.  Add focused ordering and EH
reducers before the implementation, preserve exact LowIR/MIR/object/LSDA
fixtures, and record comparison-count and character-count removal.

## 11. Phase 5: carry token and operator enums end to end

### 11.1 Current path

`SimpleTokenKind` exists after post-tokenization, but syntax nodes primarily
retain textual token descriptions and spelling payloads.  Semantic operator
code repeatedly compares those spellings.  Lowering still strips textual
operation prefixes in some paths.

### 11.2 Target representation

- Carry `SimpleTokenKind` or a smaller `SemanticOperatorKind` through syntax,
  `ExpressionInfo`, semantic dump facts, and LowIR lowering.
- Convert operator-function declarations to the existing `OperatorKind` once.
- Compare GNU attributes, builtin traits, and other fixed identifier
  vocabularies using preinterned `NameId`s or compact registry enums.
- Keep the original `TextId` for source serialization and diagnostics only.

`SyntaxNode` is a high-multiplicity record.  Before adding a field, measure its
current size and inspect unused flag bits or a compact sidecar.  Prefer packing
an 8-bit token/operator code with a `sizeof` assertion over enlarging every
node.  Do not scatter isolated `strcmp` micro-optimizations across semantic
files; make one pipeline-wide representation change.

PA2 owns token classification, PA10 owns the syntax handoff, PA12 owns semantic
operators, and PA15 owns lowering.  Tests belong at the earliest operator or
fixed-vocabulary feature.  Existing textual fixtures should remain exact.

## 12. Phase 6: carry decoded literals end to end

### 12.1 Current path

The post-tokenizer decodes numeric, character, and string literal values.
`SyntaxLiteralFact` retains only a subset of those facts.  Semantic analysis
and PA16 static lowering consequently decode string or floating spelling
again.  Constant evaluation sometimes formats a typed scalar through
`InternScalar`, after which lowering recovers its value from text.  Pragma pack
alignment has a similar format/parse handoff.

The frozen benchmark has few floating literal decodes, so this phase is
primarily an architectural and correctness improvement.  It must not be sold
as the main frozen-speed win without measurements.

### 12.2 Target representation

Add a unified `LiteralFactId` referring to a compact tagged fact:

- literal kind and target type;
- normalized scalar bits, including wide integer and floating forms;
- a compact code-unit or byte-arena slice for character/string sequences;
- suffix `NameId` where language semantics require it; and
- optional original `TextId` for exact presentation.

Keep common scalar facts inline when size measurements support it.  Put wide
values and sequences in side arenas.  Semantic `ExpressionInfo`, dump facts,
constant evaluation, static lowering, and MIR consume the same bits or slice.
No hot string map is needed.

PA2 owns first decoding, PA10 owns syntax retention, PA11/PA12 own semantic
literal facts, PA15/PA16 own lowering, and PA21 owns constant evaluation.  Add
earliest-owned reducers for wide integers, floating encodings, escape/code-unit
sequences, user-defined suffixes, and pragma alignment.  Exact source-facing
and IR serialization must render from the typed fact.

## 13. Phase 7: preserve spelling identity across preprocessing

### 13.1 Current path

The macro processor already has a private compact `SpellingTable` and
`SpellingId`.  Its public integrated handoff returns `std::string` through
`IPostTokenStream`.  `PostTokenAnalyzer` classifies the spelling, and the
syntax sink interns every emitted spelling into the frontend table.

The audit recorded 527,293 `FindSimple` calls, 1,290,244 macro spelling-table
interns, and 490,862 frontend token-spelling interns.  By contrast,
`TranslationCursor::Next` scanning 24.8 million preprocessed bytes is
legitimate source lexing and must not be conflated with identity loss.

### 13.2 Target representation

- Preserve the PA2 and PA4 text interfaces as adapters for standalone tools
  and fixtures.
- Give the integrated path a spelling handle carrying the owning
  `SpellingId`, broad token kind, and cached phase-7 classification.
- In the syntax sink, use a dense lazy `SpellingId -> frontend TextId` remap so
  each distinct emitted spelling is interned once rather than once per token.
- Retain only emitted spellings.  Do not share the entire macro spelling table
  with the semantic program, because that would retain discarded macro-only
  text and increase RSS.

This is an interface-wide PA4/PA10 change and therefore follows the narrower
semantic and ABI phases.  Preserve exact PA2, PA4, and PA10 fixtures and add
counters for distinct remaps, discarded spellings, retained bytes, and
classification reuse.

## 14. Phase 8: secondary cleanup

Address these only after the preceding measured work:

1. Replace PA32 function-template ABI result tag `NameId` comparisons with
   syntax-tag/operator enums, or publish the ABI type fact directly.
2. Replace PA19 ambiguous-relational-declaration substring and stream parsing
   with a retained parser alternative or typed token range.  The frozen count
   is small, so correctness and clarity are the reasons.
3. Classify GNU assembly and attribute literal forms once, while retaining the
   language-visible assembly payload and final labels as text.
4. Audit RTTI, lambda, generated-name, and pretty-function paths so structured
   identity remains internal and rendering occurs only at the ABI or
   language-visible boundary.

Do not turn these into a general ban on strings.  Each change needs evidence
that text is being used to recover an already-known compiler fact.

## 15. Test and fixture policy

### 15.1 Earliest-owned regressions

For every failure:

1. preserve the original failing command and input;
2. reduce it while keeping the same failure or observable mismatch;
3. identify the earliest PA whose public contract owns the fact;
4. add the reducer under `cppgm.tests/course/paN/` when it belongs in the
   course-wide suite; and
5. run the owner report and the late-stage report where the issue was found.

A PA36, PA37, PA38, or inception discovery is not automatically a late-PA
test.  Name lookup belongs to its semantic owner, ABI encoding to its earliest
ABI owner, and native EH ordering to its backend owner.

### 15.2 Fixture policy

- Internal identity changes should leave serialized fixtures exact.
- If a retained `-O0` change alters existing MIR, record every affected
  fixture and the reason in the results ledger.
- If an experiment broadly changes LowIR, defer it from this plan rather than
  mixing a public IR redesign into text-identity cleanup.
- New exact fixtures may be checked in only when the assignment reference
  agrees and the fixture expresses the public contract.
- If the reference disagrees but the standard/compiler behavior supports the
  experiment, place the candidate under `proposed/paN/` with a maintainer note;
  do not make it gate the assignment.
- A behavior-only or relaxed-MIR lane is appropriate when the source behavior
  matters but an exact layout does not.  A test whose only value is enforcing
  a more efficient MIR remains proposed until that layout is part of the
  contract.

### 15.3 Student-facing documentation

This plan is the maintainer history and rationale.  Assignment `README.md`
files should change only if students must implement a newly exposed public
fact to pass the tests.  They must not describe old implementations, internal
experiments, benchmark history, or repository maintenance.  Optional advice
about an efficient implementation belongs only in the bottom `Design Notes`
section and must remain implementation-neutral.

## 16. Per-changeset validation loop

While editing, run the narrowest owner case and then collect the whole relevant
failure set with PA-selected report mode, for example:

```sh
make test-report ACTIVE_TEST_REPORT_PAS='pa10 pa12 pa15 pa32 pa37 pa38'
```

Use the actual affected PA list; do not use `make test` as the failure-set
signal because it stops at the first failure.

Before accepting each changeset:

1. run `make test-paN` for the earliest owner;
2. run `make test-report-through-paN`;
3. run selected downstream reports for every touched interface;
4. for shared frontend/semantic/ABI representation changes, run the full root
   `make test-report` before the commit;
5. run
   `perl scripts/cppgm_file_audit.pl --stage pa39 --paths dev/src` and require
   zero fatal findings;
6. compare structural counters and record sizes;
7. run the frozen performance proxy with immutable compilers; and
8. commit the independently validated changeset and update the ledger.

If the file audit requires a split, create self-contained implementation
modules under `dev/src` and add every new `.cpp` file to the correct tool list
in `dev/frontend_source_sets.mk`.  Do not solve a fatal limit with
include-as-code-split patterns.

## 17. Performance protocol

The host is intermittently loaded.  A single wall-time run is evidence of
nothing.  Use this protocol:

- compare immutable A and B executables in alternating ABBA or ABC/CBA order;
- run under the same optimization level, include roots, output filesystem, and
  environment;
- record wall, user, system, maximum RSS, load, CPU pressure, object size,
  section sizes, exported symbol count, and SHA-256;
- require matching exit status and deterministic candidate output before
  considering time;
- use at least three usable runs per candidate and compare medians; use five
  for a final performance claim;
- reject or repeat windows with obvious concurrent build/load contamination;
- require a structural counter reduction even when timing is favorable; and
- compare the final candidate against both its immediate predecessor and the
  original audit anchor so a prior regression cannot disappear inside a later
  win.

`test-report` and selected-PA reports are the fast iteration signal.  Inception
is expensive and should run only after a meaningful high-risk batch and on the
final clean committed tree.  Do not run another build concurrently with a
timed self-build or inception lane.

No PGO is part of this plan.  It slows iteration and can be evaluated only
after the unprofiled representation work is complete.

## 18. Reject and stop rules

Reject or revert an experiment when any of these apply:

- it adds a side index but retains the same string construction, ownership,
  hashing, or parse work;
- it enlarges a high-multiplicity syntax, semantic, LowIR, or MIR record
  without an offsetting measured reduction;
- it introduces a hot `std::string`, `unordered_map<string, ...>`, or
  `unordered_set<string>` path where a dense ID table is available;
- it depends on unordered iteration for observable output;
- it changes exact fixtures without a public-contract or correctness reason;
- it regresses frozen median user time or RSS outside the screened noise band;
- it makes diagnostics or required semantic validation demand-dependent; or
- it moves work to `-O1`/`-O2` merely to make `-O0` look cheaper when the work
  is required for correct lowering.

Pause at a clean committed boundary between phases.  Do not begin unrelated
O0 instruction-placement or O1/O2 optimization work while a typed-boundary
phase has unresolved fixtures, audit failures, or unexplained timing.

## 19. Completion gate

The plan is complete only when one clean commit satisfies all of the following:

1. Phase counters demonstrate removal of the production text round-trips
   described in sections 7-14, or the ledger explains why a measured item was
   rejected or intentionally left at a true boundary.
2. The frozen `-O0` compile shows at least a 5% median user-time improvement
   against the immutable audit anchor under the protocol in section 17.
3. Output is deterministic and every intentional byte or fixture change is
   explained and tested at the earliest owner.
4. Peak RSS is within the accepted budget and record/arena sizes are recorded.
5. Root `make test-report` is a full pass.
6. The PA39 file audit has zero fatal findings.
7. A timed from-scratch PA39 self-build completes from the same commit.
8. Clean timed 8-way and 32-way inception comparisons are recorded separately,
   with peak RSS, and every compared object plus the final binary matches.
9. The ledger contains commit hashes, counters, timings, fixture effects, test
   totals, and final disposition for every experiment.

## 20. Results ledger

Update this table after every experiment, including rejected and reverted
ones.  Do not replace a result with a narrative that loses the measured data.

| ID | Hypothesis / representation change | Structural result | Frozen median user / wall / RSS | Output and fixture effect | Tests and audit | Commit / disposition |
| --- | --- | --- | --- | --- | --- | --- |
| T0 | Immutable audit baseline and durable semantic-name counters | 892,548 path parses, 894,041 components, 891,063 single-component parses, 314,951 structured paths, 245,943 spelling lookups, 481,558 declarator-name requests, and 537,588 declarator-path walks | Two A/A ABBA blocks: 4.750 s median user, 5.250-5.255 s wall, and 364,248-364,716 KiB RSS; stats run 4.84 s user / 5.36 s wall | Exact 4,415,448-byte object and SHA-256 `d52599359535b175519d1ce1249f2a7eafa443fa1765d1c39d7d38f93716c37f` | Full report 5,210/5,210; zero-fatal audit with 26 warnings | `bfca2d7d`; accepted measurement-only anchor |
| T1 | Terminal `NameId` avoids full declarator path parsing | Name-path parses fall 892,548 -> 450,868 (-49.5%); declarator-path walks fall 537,588 -> 55,745 while 481,558 declarator-name requests remain | Clean two-block A/B/B/A: baseline/candidate medians 4.770/4.735 s user, 5.255/5.210 s wall, and 365,314/364,590 KiB RSS; paired user -0.58%, wall -0.90%, RSS -0.10% | Frozen object remains exact at 4,415,448 bytes with baseline SHA; no fixture changes | PA10 164/164; through PA10 583/583; selected downstream 830/830; full report 5,210/5,210; zero-fatal audit with 26 warnings | `2cd2a307`; accepted structural win with small corroborating timing gain |
| T2a | One-component declarator paths use the retained terminal ID | Name-path parses fall 450,868 -> 403,300; all 47,568 removed parses were single-component; the PA17 conversion-operator reducer exposed and anchored the parser's established terminal-component identity | Clean two-block A/B/B/A against T1: 4.850/4.765 s user, 5.345/5.255 s wall, and 365,296/364,406 KiB RSS; paired user -0.62%, wall -0.75%, RSS -0.15% | Frozen object remains exact at 4,415,448 bytes with baseline SHA; no fixture changes | PA12 180/180; through PA12 833/833; PA17 reducer; full report 5,210/5,210; zero-fatal audit with 26 warnings | `bd683c1c`; accepted |
| T2 | Structured/one-component syntax paths replace ordinary `ParseNamePath` | Name-path parses fall 403,300 -> 34,823 (-91.4%) from T2a; spelling lookups fall 245,943 -> 7,326 (-97.0%); 396,098 typed syntax-path requests include 359,513 direct one-component paths and only 9,343 compatibility fallbacks | Two-block A/B/B/A against T2a: 4.760/4.650 s median user, 5.235/5.150 s wall, and 364,486/364,484 KiB RSS; paired user -2.36%, wall -2.00%, RSS -0.01% | Frozen object remains exact at 4,415,448 bytes with baseline SHA; no fixture changes | Selected semantic/template/downstream report 3,069/3,069; full report 5,210/5,210; zero-fatal audit with 26 warnings | `6431f436`; accepted |
| T3a | Add production-only ABI bridge counters before changing representation | Frozen compile observes 8,122 instrumented production mangles, 76,547 fact records, 200,542,813 cumulative fact-owned bytes, 60,672 canonical type nodes, 36,857 argument nodes, 19 expression nodes, 94,108 split path components, and 17,903 definition-cache hits | Measurement-only; timing intentionally deferred because the extra owned-byte walk runs only under `--stats` | Frozen object remains exact at 4,415,448 bytes with baseline SHA; no fixture changes | Selected PA14/15/23/32/37/38 report 913/913; full report 5,210/5,210; zero-fatal audit with 26 warnings | `ce42921b`; accepted measurement anchor |
| T3b | Reuse one numeric ABI encoder graph across production mangles in a translation unit | Even with lifecycle aliases now included (9,159 vs 8,122 measured mangles), newly canonicalized type nodes fall 60,672 -> 4,265, arguments 36,857 -> 3,501, and expressions 19 -> 3; transient fact construction and 105,284 repeated path-component visits remain for the next slice | Two-block A/B/B/A against T2: 4.720/4.720 s median user, 5.225/5.210 s wall, and 364,438/363,952 KiB RSS; paired user -0.47%, wall -0.81%, RSS -0.24%, treated as neutral enabling work | Frozen object remains exact at 4,415,448 bytes with baseline SHA; no fixture changes | Selected ABI/lowering/object report 1,103/1,103; full report 5,210/5,210; zero-fatal audit with 26 warnings | `1768e757`; accepted enabling context |
| T3c | Cache compact resolved ABI type handles by semantic type/function/recipe identity | 22,430 of 60,427 typed-handle requests hit; repeated ABI path-component visits fall 105,284 -> 82,469 (-21.7%) without enlarging `AbiType`; canonical node counts and exact output remain stable | Two-block A/B/B/A against T3b: 4.600/4.540 s median user, 5.080/5.000 s wall, and 364,678/364,124 KiB RSS; paired user -1.94%, wall -2.30%, RSS -0.07% | Frozen object remains exact at 4,415,448 bytes with baseline SHA; no fixture changes | Selected ABI/template/lowering/object report 2,257/2,257; full report 5,210/5,210; zero-fatal audit with 26 warnings | `cb310013`; accepted |
| T3d | Store integrated ABI definitions, function facts, and target in separate typed families while retaining PA14's ordered fact-file adapter | Cumulative fact-owned storage falls 221,826,942 -> 160,694,542 bytes (-27.6%) across the same 9,159 production mangles and 85,133 records; type-cache and graph counters remain unchanged | Explicit-`-O0` two-block A/B/B/A against T3c: 4.655/4.635 s median user, 5.150/5.105 s wall, and 364,680/363,964 KiB RSS; candidate -0.43% user, -0.87% wall, and -0.20% RSS | All eight frozen objects and the stats object are exact at 4,415,448 bytes with the baseline SHA; no fixture changes | Selected ABI/template/lowering/object report 2,257/2,257; full report 5,210/5,210; zero-fatal audit with 29 warnings after splitting the oversized driver function | `3582826a`; accepted. Audit boundary split: `6c63641b` |
| T3e | Count the remaining production definition families before selecting a numeric reference representation | 42,187 of 42,894 definitions (98.4%) are template arguments; the remainder is 242 types, 19 expressions, 446 contexts, and no entity definitions | Measurement-only; timing deferred because the definition walk is restricted to `--stats` | Frozen object remains exact at 4,415,448 bytes with the baseline SHA; no fixture changes | Selected PA14/15/23/32/37/38 report 913/913; full report 5,210/5,210; zero-fatal audit with 29 warnings | `1bf657d0`; accepted measurement anchor for direct argument handles |
| T3f | Carry production argument, expression, and local-context references as numeric graph handles; retain names only in PA14's textual adapter | Production records fall 85,133 -> 42,239 (-50.4%) and cumulative fact-owned storage falls 160,694,542 -> 54,238,801 bytes (-66.2%) from T3d. All 42,187 argument, 19 expression, and 446 context definitions disappear; compact case-local context bindings preserve late ABI substitution identity without a string map. | Three explicit-`-O0` A/B/B/A blocks against T3d: 4.540/4.390 s median user, 4.990/4.860 s wall, and 364,218/364,852 KiB RSS; paired candidate -2.97% user, -2.73% wall, and +0.40% RSS | All 12 timed objects and the stats object are exact at 4,415,448 bytes with the baseline SHA; no fixture changes. Existing PA23 and PA26 reducers caught case-bound entity caching and a resolved-type substitution overlay during development. | Selected PA14/15/23/32 report 791/791; full report 5,210/5,210; zero-fatal audit with 26 warnings after extracting local-owner fact construction | `d230ddf7`; accepted |
| T3g | Store entity template arguments by direct graph handle instead of a case-local textual definition | The last production definition family is removed. The frozen source has no entity definitions, so this is an architecture cleanup rather than a frozen-counter or timing claim; PA14 retains its textual entity-definition contract. | Not timed independently because the frozen benchmark has zero affected records | Frozen object remains exact at 4,415,448 bytes with the baseline SHA; no fixture changes | Selected PA14/15/23/26/32 report 902/902; full report 5,210/5,210; zero-fatal audit with 26 warnings | `72897065`; accepted |
| T3h | Resolve every self-contained production `AbiType` to a graph ID immediately, including case-bound local types, while caching only case-independent IDs | Function/type facts now carry resolved graph IDs whenever production has no textual definition dependency. Cumulative fact-owned storage falls a further 19,583 bytes to 54,219,218; two explicit substitution-overlay nodes are added, with graph behavior otherwise stable. | Three explicit-`-O0` A/B/B/A blocks against T3f, inclusive of frozen-neutral T3g: 4.500/4.495 s median user, 4.975/4.985 s wall, and 365,834/365,594 KiB RSS; paired candidate -0.22% user, -0.20% wall, and +0.10% RSS. Retained as neutral enabling work, not a timing win. | All 12 timed objects and the stats object are exact at 4,415,448 bytes with the baseline SHA; no fixture changes | Selected PA14/15/23/26/32 report 902/902; full report 5,210/5,210; zero-fatal audit with 26 warnings | `5f49cb87`; accepted enabling work |
| T3i | Feed structured semantic emission paths directly into the ABI graph for named and global template-specialization types | Textual ABI path-component parses fall 29,454 -> 20,206 (-31.4%). The path handle reuses `AbiType::index` by kind, so the common record does not grow; cumulative fact-owned storage falls another 5,719 bytes to 54,213,499. | Three explicit-`-O0` A/B/B/A blocks against T3h: 4.475/4.450 s median user, 4.950/4.925 s wall, and 365,504/365,570 KiB RSS; paired candidate -0.89% user, -0.10% wall, and +0.01% RSS. Retained primarily for removed parsing. | All 12 timed objects and the stats object are exact at 4,415,448 bytes with the baseline SHA; no fixture changes. Existing PA33 dependent-member-alias coverage prevented applying a full-path handle to a terminal-name record. | Selected PA14/15/23/26/32 report 902/902 plus PA33 96/96; full report 5,210/5,210; zero-fatal audit with 26 warnings | `4528fbd2`; accepted |
| T3j | Carry ordinary production function paths as graph IDs built from semantic scope/name components | Textual ABI path-component parses fall 20,206 -> 10,831, or -63.2% from T3h. Despite adding one path handle to the target record, removing retained qualified strings lowers cumulative fact-owned storage by 91,102 bytes to 54,122,397. | Three explicit-`-O0` A/B/B/A blocks against T3i: 4.460/4.445 s median user, 4.955/4.930 s wall, and 365,900/364,622 KiB RSS; paired candidate -0.11% user, 0.00% wall, and -0.12% RSS. Retained as neutral structural work. | All 12 timed objects and the stats object are exact at 4,415,448 bytes with the baseline SHA; no fixture changes | Selected PA14/15/23/26/32/33 report 998/998; full report 5,210/5,210; zero-fatal audit with 26 warnings | `c894478f`; accepted |
| T3k | Carry ordinary variable, TLS-wrapper, and variable-entity paths as graph IDs while retaining explicit external-name overrides | Textual ABI path-component parses fall 10,831 -> 10,585 (-2.3%), and cumulative fact-owned storage falls another 3,667 bytes to 54,118,730. The existing `AbiFunctionTarget` path field is reused, so no common record grows. | Three explicit-`-O0` A/B/B/A blocks against T3j: 4.530/4.500 s median user, 5.030/4.990 s wall, and 364,348/364,616 KiB RSS; paired candidate -1.42% user, -1.69% wall, and +0.17% RSS. | All 12 timed objects and the stats object are exact at 4,415,448 bytes with the baseline SHA; no fixture changes | Selected PA14/15/23/26/30/32/33 report 1,096/1,096; full report 5,210/5,210; zero-fatal audit with 26 warnings | `fa4c842b`; accepted |
| T3l | Classify every residual text-to-path parse by semantic use before selecting the next representation | Of 10,585 remaining components, 737 are function-path fallbacks and 9,848 are explicit presentation/substitution keys. Type, object, and entity path parses are all zero, and the category sum accounts for every path parse. | Measurement-only; timing intentionally deferred because the counters run only under `--stats` | Stats object remains exact at 4,415,448 bytes with the baseline SHA; no fixture changes | Selected PA14/15/23/26/30/32/33 report 1,096/1,096; full report 5,210/5,210; zero-fatal audit with 26 warnings | `d6e42057`; accepted measurement anchor |
| T3m | Carry conversion-function, local-context, and function-entity paths by graph ID, removing every production function-path fallback | Function text-path components fall 737 -> 0, leaving only 9,848 explicitly classified presentation/substitution-key components. Cumulative fact-owned storage falls another 288 bytes to 54,118,442, and the production `::operator` prefix scan is removed. | A clean repeat of three explicit-`-O0` A/B/B/A blocks against T3k: 4.510/4.495 s median user, 5.035/5.000 s wall, and 365,576/364,636 KiB RSS; paired candidate +0.34% user, +0.20% wall, and -0.24% RSS. Retained as timing-neutral structural work. An earlier window was discarded after a contaminated final pair. | All 12 retained timed objects and the stats object are exact at 4,415,448 bytes with the baseline SHA; no fixture changes | Selected PA14/15/17/23/26/30/32/33 report 1,337/1,337; full report 5,210/5,210; zero-fatal audit with 26 warnings | `698ecd3a`; accepted enabling work |
| T3n | Carry structured class-template name components and substitution prefixes as incrementally extended graph paths | All ABI text-to-path parsing falls to zero, and cumulative fact-owned storage falls 54,118,442 -> 53,960,140 (-158,302 bytes). The two kind-disjoint numeric slots already present in `AbiFunctionRecord` carry prefix-path and terminal-string IDs, so the record does not grow. | Three low-load explicit-`-O0` A/B/B/A blocks against T3m: 4.425/4.405 s median user, 4.900/4.875 s wall, and 364,090/364,054 KiB RSS; paired candidate -0.34% user, -0.41% wall, and -0.01% RSS. A first prototype that rebuilt every prefix from its first component measured +0.68% paired user and was rejected; extending one path per component removed that repeated work. | All 12 retained timed objects and the stats object are exact at 4,415,448 bytes with the baseline SHA; no fixture changes | Selected PA14/15/17/23/26/30/32/33 report 1,337/1,337; full report 5,210/5,210; zero-fatal audit with 26 warnings | `78e06958`; accepted |
| T3o | Carry function terminals, member names, ABI tags, and namespace-lambda paths as graph IDs | Cumulative fact-owned storage falls 53,960,140 -> 53,924,738 (-35,402 bytes) without widening `AbiFunctionRecord` or `AbiFunctionTarget`; namespace-lambda qualifier vectors are replaced by one resolved path. | Three explicit-`-O0` A/B/B/A blocks against T3n: 4.425/4.420 s median user, 4.915/4.900 s wall, and 365,272/364,676 KiB RSS; paired candidate 0.00% user, +0.31% wall, and -0.29% RSS. Retained as timing-neutral structural work. | All 12 timed objects and the stats object are exact at 4,415,448 bytes with the baseline SHA; no fixture changes. During development the existing PA22 per-specialization local-static reducer caught a terminal-only name record being mistaken for an absent record; using source-name presence rather than path presence restored distinct symbols. | Selected PA14/15/17/22/23/26/30/32/33 report 1,647/1,647; full report 5,210/5,210; zero-fatal audit with 26 warnings | `1eb62650`; accepted |
| T3p | Replace rendered class, template-name-argument, and member-template substitution identities with compact typed keys | The three `__cppgm_abi_*` production families become one packed `(domain, semantic ID)` integer. Cumulative fact-owned storage falls 53,924,738 -> 53,827,481 (-97,257 bytes), while canonical type and argument counts remain exactly 4,260 and 3,503. Canonical nodes reuse their substitution slot plus a boolean discriminator; no string map or parallel node family is added. | Three explicit-`-O0` A/B/B/A blocks against T3o: 4.390/4.340 s median user, 4.870/4.810 s wall, and 365,162/364,010 KiB RSS; paired candidate -1.03% user, -1.23% wall, and -0.23% RSS. | All 12 timed objects and the stats object are exact at 4,415,448 bytes with the baseline SHA; no fixture changes | Selected PA14/15/17/22/23/26/30/32/33 report 1,647/1,647; full report 5,210/5,210; zero-fatal audit with 26 warnings | `38bffeb8`; accepted |
| T3q | Replace encoder-rendered tagged-path and member-template-prefix identities with exact typed composite keys | The remaining `__cppgm_abi_*` strings disappear. A composite pool is allocated only for a mangle that actually has a tagged key; it compares the complete base key and tag-ID vector, so equality is exact rather than hash-only. Untagged mangles retain only one null pointer. Fact and canonical-node counts are unchanged. | Three low-load explicit-`-O0` A/B/B/A blocks against T3p: 4.370/4.370 s median user, 4.840/4.835 s wall, and 364,770/364,376 KiB RSS; paired candidate -0.23% user, -0.41% wall, and +0.11% RSS. Retained as neutral structural work. | All 12 timed objects and the stats object are exact at 4,415,448 bytes with the baseline SHA; no fixture changes | Selected PA14/15/17/22/23/26/30/32/33 report 1,647/1,647; full report 5,210/5,210; zero-fatal audit with 26 warnings | `c19f8cd3`; accepted |
| T3r | Carry namespace-lambda type paths and ordinary member/local type terminals by graph ID | Namespace-lambda qualifier vectors and member/local type string copies disappear. `AbiType::index` is reused by kind and resolves into existing `TypeNode::path` or `TypeNode::symbol`, so neither common record grows. Frozen fact-owned storage and canonical counts remain 53,827,481 bytes, 4,260 types, and 3,503 arguments because the spellings were already graph-interned. | Three screened explicit-`-O0` A/B/B/A blocks against T3q: 4.400/4.390 s median user, 4.900/4.875 s wall, and 364,358/363,868 KiB RSS; paired candidate -0.00% user, -0.31% wall, and -0.05% RSS. An early contaminated window was discarded; consolidating type-kind classification into one switch made the accepted form neutral. | All 12 timed objects, the stats object, and the final post-split check are exact at 4,415,448 bytes with the baseline SHA; no fixture changes | Selected PA14/15/17/18/22/23/26/30/32/33 report 1,683/1,683; full report 5,210/5,210; zero-fatal audit with 26 warnings after extracting typed ABI identity helpers from the oversized mangler | `c5a9f79d`; accepted neutral enabling work |
| T3s | Carry fixed function, lifecycle, lambda-call, and member-NTTP terminals as a byte-sized enum | Production no longer creates terminal words or interns member-operator terminals. PA14 classifies its retained input word once, while the encoder uses an indexed Itanium-code table. Cumulative fact-owned storage falls 53,827,481 -> 53,779,377 (-48,104 bytes), with identical graph counts. Compile-time assertions hold `AbiTemplateArgument` at 1,976 bytes, `AbiFunctionTarget` at 1,104, and `AbiFunctionRecord` at 840. | Three screened explicit-`-O0` A/B/B/A blocks against T3r: 4.470/4.480 s median user, 4.950/4.955 s wall, and 364,734/364,856 KiB RSS; paired candidate +0.11% user, -0.10% wall, and +0.10% RSS. Retained as neutral structural work. | All 12 timed objects, the stats object, and the post-split final object are exact at 4,415,448 bytes with the baseline SHA; no fixture changes. The responsibility split produced a compiler byte-identical to the timed candidate. | PA14 117/117; through PA14 1,050/1,050; selected PA14/15/17/18/19/22/23/26/30/32/33/37/38 report 2,106/2,106; full report 5,210/5,210; zero-fatal audit with 26 warnings | `e139f2fa`; accepted neutral structural work |
| T3 | Typed translation-unit ABI context replaces production fact files | In progress: production definition/reference families, semantic paths, ordinary source names, ABI tags, namespace-lambda paths, member/local type terminals, fixed function terminals, and all substitution-key families use typed graph or per-mangle keys; ABI text-to-path parsing and synthetic substitution strings are zero. Builtin/standard type vocabulary, the block-pointer marker, generated local ordinals/discriminators, dependent-expression names/operators, type ABI tags, and explicit external-name overrides remain to classify or replace. | Accepted T3 slices are cumulatively faster and reduce fact-owned storage by more than two thirds from T3d | Exact mangles, symbols, binding, and object behavior | PA14/15/17/18/19/22/23/26/30/32/33/37/38 plus full report | Continue the remaining fixed-vocabulary subfamilies in section 8.5, then execute numeric presentation and language-name handles |
| T4 | Lazy semantic emission/display/specialization identity | Planned | Planned | Exact semantic serialization expected | PA12/19/20/22 plus downstream reports | Planned |
| T5 | Compact exact block collation removes repeated lexical comparison | Planned | Planned | Exact MIR/object/LSDA expected | PA15/26/29 plus full report | Planned |
| T6 | Token/operator enums replace fixed-vocabulary spelling recovery | Planned | Planned | Exact textual fixtures expected | PA2/10/12/15 plus full report | Planned |
| T7 | Unified literal facts remove render/reparse and repeated decode | Planned | Planned | Exact serialization; typed behavior reducers | PA2/10/12/15/16/21 | Planned |
| T8 | Integrated spelling handles and dense emitted-spelling remap | Planned | Planned | Exact PA2/4/10 fixtures expected | PA2/4/10 plus full report | Planned |
| T9 | Secondary specialized text parsers use retained typed facts | Planned | Planned | Per-item decision | Earliest owner per item | Planned |

## 21. Code map

The initial implementation surfaces are:

- syntax names: `dev/src/pa10_syntax.cpp`;
- semantic name recovery and declarations: `dev/src/pa12_semantic.cpp` and
  semantic declaration/operator modules;
- semantic scope presentation: `dev/src/pa12_semantic_scope.cpp`;
- specialization presentation: PA19/PA20 semantic modules;
- ABI production bridge: `dev/src/pa15_lowering_abi.cpp`;
- ABI model/encoder/parser: `dev/src/abi_mangle.h`,
  `dev/src/abi_mangle.cpp`, and `dev/src/abi_mangle_parse.cpp`;
- block presentation ordering: `dev/src/pa15_local_presentation.cpp`;
- post-token classification: `dev/src/post_tokenizer.cpp` and PA4 token-stream
  interfaces;
- semantic literals: `dev/src/pa12_semantic_literals.cpp`;
- constant rendering: `dev/src/pa21_constant_evaluator.cpp`;
- static/object literal lowering: PA15/PA16 lowering modules; and
- PA32 template ABI identity and PA19 ambiguity modules for the final cleanup.

Re-run the code audit at the beginning of each phase.  File names may be split
to satisfy the fatal file audit, but the representation and ownership rules in
this plan remain the decision criteria.

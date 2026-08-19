# Typed Compiler Boundary Plan: Remove Production Text Round-Trips

Status: in progress; Phase 2, the production T2x closeout, standalone PA11
T2y parity, T4a measurement, T4b1 lazy function display, and T4b2 lazy
binding emission presentation, and T4b3 typed entity/scope presentation are
complete; T4c specialization and lambda identity is next

Date: 2026-08-19

Audit anchor: `c349d7f5`

Current execution checkpoint: `285ef075` (T4b3 typed entity/scope emission
presentation)

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
| Semantic name paths | Audit anchor: 892,548 PA12 `ParseNamePath` calls, about 2.31% inclusive, plus a separate PA11 `--emit-types` implementation | Joined syntax payload was repeatedly split to recover components or only the terminal name | PA12 production closeout complete; PA11 parity remains |
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

### 5.1 Full source-audit classification

A source-wide search for parsing, slicing, fixed-spelling comparison, string
containers, and presentation helpers produces many false positives.  The
following classification is the durable decision record for those sites.  A
future implementation must update the classification when it finds a site
whose actual data flow differs from this table; it must not infer that every
`substr`, `std::string`, or string comparison is a hot-path defect.

| Site family | Current role | Disposition | Earliest owner |
| --- | --- | --- | --- |
| `abi_mangle_parse.cpp` | Parses and serializes PA14's public textual fact language | Keep as a text adapter; parse once into the numeric graph | PA14 |
| `lowir_parse.cpp`, `lowir_serialize.cpp`, and standalone CY86 input | Public textual LowIR and requested serialization | Keep as adapters; the integrated LowIR/MIR path is already typed | PA15/PA28 |
| `pa11_semantic.cpp` | The standalone `--emit-types` assignment implementation reparses syntax-owned qualified names and literal/operator spelling; it is not invoked by the PA39 source-to-object path | Give PA11 the same structured/simple syntax-name access pattern as PA12, then handle its operators and literals in T6/T7. Measure on PA11 inputs and do not claim a frozen compile win. | PA10/PA11 |
| `pa12_semantic_names.cpp` and semantic string overloads | T2x removes all frozen production path parses. Retained calls are compatibility fallbacks, literal recovery, specialized PA19 ambiguity, or explicit spelling adapters. | Keep separately named counters, remove syntax-owned fallbacks when exercised, and assign operator/literal/ambiguity recovery to T6/T7/T9 rather than hiding it in a generic lookup helper. | PA12 and the feature owner |
| `ScopePrefixId`, `DisplayName`, `EmissionName`, and specialization rendering | Eagerly constructs and interns qualified presentation although lookup identity is typed | Replace retained presentation with owner/name/specialization IDs and render only for an actual consumer | PA12/PA19/PA20/PA22 |
| Semantic and lowering operation comparisons, plus `StripOperationPrefix` | Fixed operator vocabulary is repeatedly recovered from semantic text | Carry a packed semantic operator enum through syntax, semantic facts, and lowering | PA10/PA12/PA15 |
| `pa12_semantic_literals.cpp`, constant rendering, and static-lowering decoders | Source literal text is decoded in more than one phase, or typed scalar data is rendered and recovered | Publish one compact literal/scalar fact and consume its bits or arena slice | PA2/PA10/PA12/PA16/PA21 |
| `pa19_semantic_ambiguity.cpp` | Recovers a parser alternative by removing whitespace and slicing a retained declaration spelling | Replace with a retained typed token range or parser alternative; do not generalize this specialized parser | PA19 |
| `pa15_lowering_abi.cpp` and ABI graph construction | Production carries semantic/graph IDs; only emitted name bytes, final external spellings, diagnostics, and PA14 adapter text remain | Phase 2 complete; preserve the T3x classification and zero text-recovery counters | PA14/PA15/PA33 |
| `pa30_object.cpp` | Reads/writes the PA30 private serialized-object contract | Keep textual/binary decoding at that serialization boundary | PA30 |
| native ELF symbol, section, assembly, and runtime-name handling | Produces object-file-visible bytes and consumes externally named symbols | Keep final spelling; remove only an upstream typed-to-text-to-typed recovery if separately demonstrated | PA29/PA30/PA34 |
| recognizer grammar, macro processor, tokenizer, diagnostics, and test runner | Consumes source/configuration text or emits human-visible text | Keep as boundary text; only the integrated spelling handoff in Phase 7 is a representation target | PA2/PA4/PA6/PA10 |

The remaining `ParseNamePath` count after T2 is therefore an inventory to
split, not proof that all remaining calls have the same replacement.  In
particular, ordinary syntax-owned calls, fixed compiler-generated names, and
the PA19 ambiguity recovery require three different treatments.

### 5.2 Replacement hierarchy

For every site classified as internal recovery, choose the first applicable
representation below:

1. reuse the owning `NameId`, `TypeId`, `BindingId`, `EntityId`, syntax fact,
   graph ID, or compact path already present;
2. carry a byte-sized enum for a fixed vocabulary;
3. carry a typed tuple or an arena `(begin, count)` slice for a composite;
4. carry integer or scalar bits with explicit presence and interpretation;
5. render once at the final output, diagnostic, or public adapter.

Do not add a string-keyed memo table while leaving the original rendering,
hashing, comparison, or parse in place.  Any unavoidable text index must live
inside a cold adapter and must not be retained by the production semantic or
lowering graph.

### 5.3 Post-T2x semantic and source-wide closeout inventory

T2x drives every PA12 name-path family to zero on the frozen source, but that
does not make every remaining string operation a boundary or every dormant
fallback correct.  The closeout source audit assigns the residual sites as
follows:

| Residual family | Source-audit finding | Durable disposition |
| --- | --- | --- |
| PA11 `TypeAnalyzer` name lookup | At T2y0, `ParseNamePath`, `LookupSpelling`, declaration paths, namespace targets, id expressions, and `decltype` recovered syntax-owned names in the standalone `--emit-types` pipeline | T2y complete: PA11 reuses PA10 structured/simple name facts, qualified enums retain the structure already parsed by PA10, and all 52 successful PA11 inputs have zero path reparses and spelling lookups.  This path is not part of PA39 source-to-object compilation. |
| PA12 compatibility name fallbacks | Qualified-enum and member-pointer owner fallback, `SyntaxNamePath` fallback, and no-syntax template overloads remain in source but have zero frozen calls | Keep separately counted. Convert an exercised syntax-owned site at its PA12/PA19/PA27 owner; retain a spelling overload only when its caller actually accepts arbitrary text |
| PA12 call spelling adapters | The common frozen users were 1,922 overloaded operators, 186 allocation/deallocation requests, 162 destructor names, and 2 compiler aliases; T2x now passes their existing IDs/paths. Dormant functional-cast, conversion-target, and explicit spelling overloads remain | Operator/conversion vocabulary belongs to T6. A genuine API spelling adapter may parse once, but it must have an adapter-specific counter rather than appearing as an ordinary syntax lookup |
| Template spelling overloads | `FindFunctionTemplates`, `FindFunctionTemplateOwners`, `FindClassTemplate`, and retained-template callers still expose string overloads, although the frozen template counter is zero after the retained using-directive conversion | Audit each caller during T4/T6. Pass `SyntaxNamePath` or `NameId` when syntax owns the name; delete an overload after its last adapter caller is gone |
| Literal and constant spelling recovery | PA11/PA12 integer and floating parsing, PA12/PA16 string and floating decode, PA20 scalar payload parsing, and PA21 render/redecode sites repeat facts first decoded by post-tokenization | T7 unified literal facts. Keep source spelling only for exact presentation and user-defined suffix bytes |
| Fixed operator/token spelling | PA11/PA12/PA16/PA21/PA27/PA32 compare operation strings extensively, and lowering still calls `StripOperationPrefix` | T6 packed operator/token vocabulary; this is the largest remaining semantic text-identity family after name paths |
| Qualified presentation slicing | Scope/display/emission helpers and a small number of owner-name `rfind("::")` sites recover identity from retained presentation | T4 typed owner/name/specialization identity and lazy renderers |
| PA19 ambiguity parser | The retained relational-declaration fallback uses `substr` and `istringstream` to reconstruct names and a literal | T9 specialized typed parser alternative; keep separate from ordinary name and literal counters |
| ABI and LowIR text parsers | PA14 ABI facts, textual LowIR/CY86 input, and their validators legitimately use string maps and numeric parsing | Keep as standalone input adapters. T3 already proves the production ABI bridge is typed; the completed LowIR plans prove the integrated LowIR/MIR path does not use these maps |
| Native object labels and PA30 object join | Native symbol/fixup maps hold final ELF-visible names; PA30 rename maps consume the private object serialization contract | Keep as output/serialization boundary text. Reopen only if a measured upstream typed-to-text-to-typed conversion is demonstrated |
| Preprocessor, recognizer, diagnostics, and test runner | Grammar, directives, include paths, macro stringization/paste, diagnostics, and harness files are intrinsically textual | Keep. T8 targets only the integrated emitted-token spelling/classification handoff |

The source audit also confirms that the production semantic model has no
general `unordered_map<string, ...>` or `unordered_set<string>` identity
table.  Existing string-keyed containers are confined to standalone parsers,
grammar/preprocessor input, native output labels, and serialized-object
adapters.  Do not replace those merely to reduce the number of source-level
`std::string` occurrences.

### 5.4 Layer-by-layer residual registry

The following registry is the actionable result of the source-wide audit.  It
classifies data flow, not syntax: a file is not a target merely because it uses
`std::string`, `substr`, or `std::to_string`.  Each row names the fact that is
being recovered or presented, the correct representation, and the slice that
owns its disposition.

| Layer and representative sites | Current data flow | Classification and required action | Owner / slice |
| --- | --- | --- | --- |
| Macro and post-token input: `macro_processor.cpp`, `post_tokenizer.cpp` | Source spelling is interned in the macro processor; numeric and literal tokens are decoded at the language-input boundary.  The integrated handoff then returns strings, reclassifies them, and interns every emitted occurrence again. | Keep first decoding and macro paste/stringization as text-boundary work.  Preserve an emitted `SpellingId` and cached token class into PA10, with a dense lazy remap for only emitted spellings. | PA2/PA4/PA10, T8 |
| PA10 token sink: `SyntaxTokenSink`, `EmitScalarLiteral`, `EmitLiteralArray`, user-defined literal callbacks, and pragma-pack callbacks | Scalar facts are retained for some literals, while arrays, user-defined literals, and pragma alignment retain only text; pragma alignment is formatted to text immediately. | Extend the literal side arena instead of enlarging every token.  Carry decoded scalar/sequence/suffix/alignment facts and retain `TextId` only for exact syntax output. | PA2/PA10, T7 |
| PA10 name parser: `ParseName`, `ParseDeclarator`, `FinishSimpleOrFunction`, and `AggregateSyntax::AppendDeclaratorNames` | `ParseName` already knows component and terminal `TextId`s, but some declarator/name-fact paths return joined `std::string`, place names in `vector<string>`, and call the string overload of `SetNameFact`, which interns them again.  Joined payload text is also used for simple qualification/operator tests. | Keep joined payload construction required by the PA10 serialization contract.  In parallel, return/carry terminal IDs and compact flags for parser decisions and name facts; use `vector<TextId>` for structured-binding/declarator fact publication.  Do not add a second spelling field to every syntax node. | PA10, T8b |
| PA10 semantic-only name children | Qualified/template names publish component IDs in `structured-type-name`; simple names usually publish a semantic payload ID. | This is the correct syntax-to-semantic boundary.  T2x consumes it in production and T2y consumes it in PA11.  Any later syntax-owned fallback must add or reuse the fact here rather than add a semantic string cache. | PA10/PA11/PA12, T2y and closeout counters |
| Standalone PA11 `TypeAnalyzer` | T2y removed joined-name parsing from namespace, declaration, declarator, type, id-expression, and `decltype` paths.  Literals and operators still use their spellings. | Preserve typed-name parity and leave operator and literal representation to the shared T6/T7 designs so PA11 does not create a private competing enum or scalar model. | PA10/PA11, T2y complete; T6/T7 remain |
| PA12 name lookup compatibility APIs | `ParseNamePath`, `LookupSpelling`, no-syntax template overloads, qualified-enum/member-pointer fallbacks, and explicit spelling adapters remain in source after the frozen production count reached zero. | Retain only callers whose input is genuinely arbitrary text, give them adapter-specific counters, and delete an overload after its last such caller is gone.  Any exercised syntax-owned fallback is a correctness/architecture bug at its earliest semantic owner. | PA12/feature owner, T4/T6/T9 closeout |
| Ordinary semantic presentation: `ScopePrefixId`, `DisplayName`, `EmissionName`, declaration `qualified_name`/`display_name`, and scope prefix vectors | Owner/name paths are rendered, interned, and retained even though scope, binding, entity, and terminal IDs already exist. | Store owner plus terminal or a compact path identity.  Render on demand for exact semantic dumps, diagnostics, pretty-function text, or final symbol emission.  Do not retain both old text IDs and new typed identities on common records. | PA12/PA22, T4a/T4c |
| Template and lambda presentation: `CanonicalTemplateArgumentPresentation`, `ExplicitArgumentPresentation`, class-specialization names, `LambdaContextIdentity`, and `LambdaIdentityComponent` | Canonical template arguments, pattern IDs, owners, token ranges, and ordinals are rendered and sanitized into semantic/storage names.  Some of those names then become retained identity. | Use pattern/argument-list/partition/owner IDs for specialization identity and an owner/token-range/ordinal tuple for lambda identity.  Keep rendering only where an exact dump, source-identity builtin, ABI spelling, or object symbol consumes it. | PA19/PA20/PA22, T4b/T4c |
| Generated semantic names: default-constructor emission, anonymous/local types, range-for temporaries, initializer backing objects, and template-shape sentinels | Several paths concatenate or slice qualified names (`rfind("::")`) or format ordinals into interned synthetic names. | First distinguish observable serialized names from private identity.  Replace private identity with kind plus owner/ordinal/source identity; render checked-in semantic names lazily.  The default-constructor owner/leaf recovery is a definite T4 typed-path conversion. | PA12/PA19/PA25/PA26, T4d/T9 |
| Fixed syntax and semantic vocabulary: operators, cv/ref markers, `default`/`delete`, linkage, traits, builtins, attributes, and predefined identifiers | PA11-PA34 repeatedly compare payload/name strings.  Operation strings flow through overload resolution, constant evaluation, and lowering; backend code calls `StripOperationPrefix`. | Carry a packed `SemanticOperatorKind` and small feature-specific enums.  Use preinterned `NameId`s for extensible identifier registries.  Keep source `TextId` for dumps/diagnostics, but make semantic control flow enum/ID based. | PA2/PA10/PA12/PA15 and feature owners, T6 |
| Literal semantics and constant evaluation: PA11/PA12 literal parsing, `InternNumber`, `InternScalar`, PA20/PA21 constants, and PA15/PA16 lowering | Values decoded by post-tokenization are incompletely retained; semantics redecodes spelling, evaluated scalars are formatted into a `NameId`, and lowering later consumes text. | Publish one compact tagged literal/scalar fact.  Carry integer/floating bits, type, suffix ID, and a byte/code-unit arena slice.  Render only for exact semantic/IR output. | PA2/PA10/PA11/PA12/PA15/PA16/PA20/PA21, T7 |
| GNU asm and attributes | String-literal payloads are decoded in semantics, templates/constraints are classified, and external asm labels/payload bytes remain textual. | Reuse T7 decoded string facts and classify supported operations once into the existing enum.  Preserve assembly payloads, constraints, section names, and external labels as language/object-boundary text. | PA32/PA34, T7/T9 |
| PA19 relational-declaration ambiguity recovery | A retained declaration spelling has whitespace removed, is sliced with `find`/`substr`, and a literal is parsed with `istringstream` to reconstruct an alternative parse. | Retain the competing parser alternative or a compact token-range recipe and resolve it after lookup.  This is a specialized correctness path, not a reason to add a generic semantic text parser. | PA10/PA19, T9a |
| PA34 source-identity rendering and local-static symbol construction | Typed function/type/template facts are deliberately rendered to language-visible builtin strings or final stable object-symbol components. | Keep as final presentation.  Add demand/render counters during T4, but do not replace it unless a consumer reparses the result into semantic facts. | PA21/PA34, boundary; T4 audit only |
| Production ABI graph and encoder | Production uses numeric graph IDs after T3; strings are exact Itanium source-name bytes, final assembly/C symbols, diagnostics, or PA14 adapter values.  PA14 text definitions still use string indexes and adapter-only numeric parsing. | Keep the completed T3 boundary.  Do not reopen its string table merely because final emitted bytes need deduplication.  Production text-recovery counters must remain zero. | PA14/PA15/PA33, T3 closeout invariant |
| LowIR and MIR core | The integrated path carries operation enums, symbol/block/slot/value IDs, typed operands, and scalar bits.  Text serializers render those facts.  `resolve_lowir_function_operands` uses string maps only after textual LowIR input. | Keep the completed typed LowIR/MIR representation.  Text parsing, name maps, and floating conversion are legal in standalone adapters; they must remain unreachable from the normal source-to-object path. | PA15/PA28, prior LowIR plans and final audit |
| CY86, native labels, ELF, and PA30 object join | CY86 emits textual assembly by assignment contract.  Native code/object modules map final symbol or assembler-label spellings, and PA30 consumes its private serialized-object name contract. | Keep as output or serialized-input boundaries.  Numeric internal-label IDs may be evaluated as a separate backend optimization, but are not text-recovery work unless a typed control-flow fact is first rendered and then parsed. | PA28-PA30, outside this plan unless new evidence appears |
| Grammar, diagnostics, harness, and configuration | Recognizer rule maps, include paths, environment numbers, errors, and test fixtures are intrinsically textual. | Keep.  Cold diagnostic formatting and test-runner parsing are not compiler identity. | PA1-PA10/test infrastructure, no action |

Two conclusions constrain implementation order.  First, the largest remaining
semantic family is not a string-keyed table: it is fixed operator/token text
threaded through many APIs.  It requires one packed vocabulary change, not a
collection of local string caches.  Second, presentation and literal work
must remove the old owning/rendering path when the typed path lands; retaining
both would increase memory and violate the success criteria.

### 5.5 Reproducible audit and closeout method

At the beginning and end of each remaining phase, repeat a source audit using
the following query families (or equivalent syntax-aware tooling):

```sh
rg -n --glob 'dev/src/*.{cpp,h}' \
  'std::(strto|sto|istringstream)|strto[a-z]*\(' dev/src
rg -n --glob 'dev/src/*.{cpp,h}' \
  'find\("::"|rfind\("::"|StripOperationPrefix|std::to_string\(' dev/src
rg -n --glob 'dev/src/*.{cpp,h}' \
  '(unordered_map|unordered_set|map|set)<[^;]*(std::)?string' dev/src
rg -n --glob 'dev/src/pa*.{cpp,h}' \
  '(PayloadSource|Payload\(|SemanticPayload|names.Get)[^;]*(==|!=|compare|find)' dev/src
```

The current audit finds string-to-number conversion only in first-input
decoders, standalone ABI/LowIR/CY86 adapters, PA11/PA12 literal recovery, and
the PA19 ambiguity parser.  It finds no general string-keyed semantic lookup
container.  Static search is only a candidate generator: every hit must be
traced to its producer and consumer before classification.

Each phase closes with a checked-in registry update that records:

1. which candidate sites were converted, retained as true boundaries, or
   deferred with a named owner;
2. before/after dynamic counts for the affected production and assignment
   paths, including zero-occurrence architecture cleanups;
3. common record sizes and side-arena bytes;
4. exact fixture/object effects and the earliest-owned reducer for any
   behavior correction; and
5. the commit and validation totals in the results ledger.

The source closeout condition is not zero textual operations.  It is that
every remaining textual operation has a named input, output, diagnostic, or
standalone-adapter boundary and that no integrated consumer reconstructs a
fact already represented by an ID, enum, scalar, or arena slice.

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

T3t completes dependent-expression operations from item 1 and the dependent
member/template-id names from item 3.  Production recipes now carry an
operation enum and graph string ID; PA14 retains arbitrary operation text only
as an adapter fallback.  The remaining fixed vocabulary is builtin/standard
type substitution and the block-pointer marker.

T3u completes item 1.  Fundamental, complex, and `_BitInt` types, standard
substitution codes, and the block-pointer vendor qualifier now use byte-sized
enums plus a numeric `_BitInt` width.  PA14 retains arbitrary text only as an
adapter fallback; production creates none of these fixed words.  The common
fact and canonical graph records did not grow.

T3v completes item 2.  Production array/vector extents, lambda ordinals, and
local-name ordinals now remain integers through canonicalization and are
formatted only by the final Itanium presentation module.  Dependent bounds
remain expression handles, and PA14 retains its arbitrary textual adapter
forms.  Kind-disjoint slots carry the integers, so the common fact, function,
and canonical type records did not grow.

### 8.6 Phase 2 execution record

T3u and T3v are complete.  T3w1 through T3w5 are also complete: retained-recipe
type terminals, local-owner names, member-NTTP source names, and
literal-operator suffixes and type ABI tags now cross the production boundary
by graph ID.  Residual entity, variable, TLS, and local-context paths now use
semantic IDs, while the special `main` local context uses a typed enum.  The
independently validated historical sequence was:

1. **T3u: fixed type vocabulary (complete).**  Represent fundamental/builtin types,
   standard-library substitution codes, and the block-pointer vendor qualifier
   with byte-sized enums.  Carry `_BitInt` signedness as an enum and its width
   as an integer.  The production PA15 path sets these facts directly; it must
   not construct words such as `uint`, `bitint37`, `Sa`, or `block_pointer` for
   the encoder to classify.  PA14 classifies canonical input once and retains
   arbitrary textual fallback only where its public fact grammar permits it.
   Render fixed Itanium codes by indexed tables in the encoder.  Use existing
   alignment or kind-disjoint slots and hold `AbiType`, `TypeNode`, and function
   record sizes with compile-time assertions.
2. **T3v: numeric local presentation (complete).**  Inventory every lambda ordinal,
   local-name ordinal, discriminator, and array-bound `std::to_string` site.
   Separate true arbitrary constant-expression text from integer facts already
   owned by semantics.  Carry the latter as integers plus explicit presence and
   placement flags, and format them once at final ABI output.  Write down and
   test the exact omitted-zero, one-based, and zero-based rules before changing
   the representation.
3. **T3w1: retained type and local-owner source names (complete).**  Existing
   kind-disjoint numeric fields carry graph string IDs.  Production text
   occurrences on the frozen source fall to zero and fact-owned bytes fall by
   3,143 without changing a common record size.
4. **T3w2: member-NTTP names and literal suffixes (complete).**  Existing
   argument/function fields carry graph string IDs.  The frozen source has no
   occurrences, so this is architecture cleanup rather than a timing claim.
5. **T3w3: ABI type presentation names (complete).**  Replace `AbiType`'s separate
   `namespace_qualifiers` and `abi_tags` string vectors with one compact
   discriminated list.  The list stores either PA14 adapter strings or graph
   string IDs and carries a 32-bit namespace-prefix count so namespace-lambda
   qualifiers and type tags can coexist without ambiguity.  The target layout
   is one vector union plus the mode and prefix count (no more than 32 bytes),
   replacing 48 bytes of vectors in every `AbiType`.  Production pushes tag
   IDs directly from the semantic `NameId` range; PA14 continues to accept and
   serialize arbitrary tag/qualifier text exactly.  Add `sizeof` assertions
   for `AbiType` and every high-multiplicity enclosing record, and reject a
   representation that grows any of them.  The accepted 32-byte list shrinks
   `AbiType` by 16 bytes and every enclosing record measured below.
6. **T3w4: member-template specialization identity (complete).**  Remove the remaining
   full `::`-joined name built by `MakeFunctionTemplateAbiType`.  Carry a
   resolved path plus an explicit terminal/member interpretation, preserving
   the distinction caught by the PA33 dependent-member-alias fixture.  Do not
   reuse a path field by kind until tests prove whether substitution identity
   is the whole path or only the terminal at that record position.  The
   encoder needs the member terminal: its owner child and terminal ID already
   form the exact member-template-prefix substitution key.  The accepted
   implementation therefore carries the terminal graph ID without retaining
   a redundant path.
7. **T3w5: residual fallbacks and true boundaries (complete).**  Per-category
   instrumentation first separated function, variable, TLS, local-context,
   explicit-variable-override, assembly/C-linkage, builtin-runtime, global-TLS,
   and `main` cases.  The frozen source exercised none of the seven internal
   fallback families; its only text was 50 assembly names, 595 C functions,
   21 builtin runtime symbols, and 13 C variables, all returned directly as
   final external spellings.  The PA35 static-data-member fixture supplied the
   missing live override case: semantic owner/name facts remove its one text
   override and two parsed path components without changing the object.  All
   entity, variable, TLS, and ordinary local-context production paths now
   require a semantic terminal and pass a graph path.  `main` uses a typed
   context enum whose final renderer emits the fixed Itanium fragment.  The
   arbitrary PA14 raw-context adapter, final external spellings, and diagnostic
   formatting remain text boundaries; no production ABI path fallback remains.
8. **T3x: Phase 2 closeout audit (complete).**  The source audit covers every
   ABI fact constructor and every production `std::string` use, not only the
   frozen source.  Production assigns no textual fact identity fields.  Name
   spellings enter the graph only through a semantic `NameId`-keyed interning
   call because those exact bytes must be emitted.  The remaining direct
   strings are final assembly/C/builtin/TLS symbols, a completed mangled alias,
   read-only recognition of semantic source names into enums/effect metadata,
   or cold diagnostics.  Arbitrary fact text and raw contexts are PA14 adapter
   values.  All fixed-vocabulary, presentation, and source-name text counters,
   all five text-path counters, and synthetic-identity searches are zero.

For T3u-T3w, record typed-versus-text occurrence counters under `--stats`,
owned fact bytes, canonical graph counts, and relevant `sizeof` values.  A
frozen-source count of zero makes a slice architectural cleanup, not a timing
claim.  Each retained slice still requires exact PA14 mangles, exact frozen
object bytes, selected owner/downstream reports, full `make test-report`, and
a zero-fatal PA39 audit before its implementation and ledger commits.

T3x, the production T2x name-path closeout, and standalone PA11 T2y parity are
complete.  Proceed to lazy semantic presentation (T4).  The
dependency remains intentional: the ABI bridge now accepts semantic paths, so
removing eager `DisplayName`/`EmissionName` storage cannot recreate the same
text through a different ABI helper.

### 8.7 T3w3 results

T3w3 is owned jointly by PA14's textual type-fact adapter and PA33's ABI-tag
semantics.  Its minimum focused gate is PA14 exact mangling plus PA33 tag
coverage, followed by selected PA15/23/32/37/38 reports.  Because `AbiType` is
nested in several large records, the changeset also requires:

- before/after `sizeof` values for `AbiType`, `AbiTemplateArgument`,
  `AbiDependentExpression`, `AbiFunctionTarget`, and `AbiFunctionRecord`;
- before/after frozen fact-owned bytes even if the frozen source has no tags;
- exact frozen object size and SHA-256;
- a screened immutable ABBA comparison if the common-record shrink changes
  accounted storage; and
- full `make test-report` plus a zero-fatal PA39 audit before the
  implementation commit.

The accepted implementation is `5e733931`.  It produced these record sizes:

| Record | Before | After |
| --- | ---: | ---: |
| `AbiTypePresentationNames` | two 24-byte vectors | 32 |
| `AbiType` | 416 | 400 |
| `AbiTemplateArgument` | 1,976 | 1,912 |
| `AbiDependentExpression` | 1,056 | 1,024 |
| `AbiFunctionTarget` | 1,104 | 1,072 |
| `AbiFunctionRecord` | 840 | 824 |

On the PA33 multi-tag source, 12 type tags move from text to graph IDs, fact
storage falls 77,212 -> 75,196 bytes, and the object is byte-identical.  The
PA14 adapter still reports 0 typed and 2 textual tags for its tagged-special
types fixture and exactly matches the checked-in reference.  On the frozen
source, which has zero tag occurrences, common-record shrinkage lowers
cumulative fact-owned storage 53,771,416 -> 52,530,664 (-1,240,752 bytes),
with canonical graph counts unchanged at 4,260/3,503/3 and the exact baseline
object unchanged.

Three screened explicit-`-O0` A/B/B/A blocks measured baseline/candidate
medians of 4.390/4.400 seconds user, 4.870/4.870 seconds wall, and
366,088/364,396 KiB RSS.  Paired candidate ratios are -0.23% user, -0.62%
wall, and -0.16% RSS.  This is accepted as a structural and storage win, not a
standalone compile-time claim.  PA14 passed 117/117, through PA14 passed
1,050/1,050, selected PA14/15/23/32/33/37/38 passed 1,009/1,009, the full
report passed 5,210/5,210, and the PA39 audit had zero fatal findings and 26
warnings.

### 8.8 T3x production-string closeout

The closeout audit found one production ABI constructor,
`pa15_lowering_abi.cpp`.  Its retained text sites have the following complete
classification:

| Site | Retained text role | Why it is a boundary rather than identity transport |
| --- | --- | --- |
| `resolve_external_name(NameId, spelling)` | Emitted C++ source-name bytes | The semantic ID is the cache key; the graph stores the spelling once because the final Itanium name must contain it. No path is rendered or parsed. |
| `StandardSubstitutionFor` and builtin/native metadata comparisons | Read-only source/external-name recognition | The comparison immediately selects a byte-sized ABI enum or symbol-effect flag. No string is retained in a fact. |
| Assembly names, C-linkage functions/variables, builtin runtime symbols, and global TLS names | Final external symbol spelling | These return directly from the mangling API and never enter the ABI fact graph. |
| Lifecycle object aliases | Completed mangled output | `MangleFunction` has already performed final rendering; the alias is an object-file-visible result. |
| Thread-local invariant diagnostics | Human-visible error text | Numeric IDs are formatted only on the cold failure path. |
| `abi_mangle_parse.cpp` text fields and raw contexts | PA14 public adapter | The standalone adapter retains arbitrary checked-in fact grammar; integrated production never assigns these fields. |
| `abi_mangle_presentation.cpp` and encoder number formatting | Final Itanium output | Integers and enum facts remain typed until the output buffer is written. |

On the frozen compile, all text expression-operation, builtin-type,
standard-substitution, vendor-qualifier, array-bound, local-presentation,
type/argument/local source-name, ABI-tag, and literal-suffix counters are zero.
All type, function, object, entity, and substitution text-path component
counters are also zero.  The source tree contains no `__cppgm_abi` synthetic
identity family, and the only production-bridge `std::to_string` calls are in
the TLS invariant diagnostic.  The same run retains 1,049 typed builtin types,
700 typed standard substitutions, 4 typed array bounds, 446 typed local
presentations, 671 typed type source names, and 122 typed local source names.

The final closeout object remains 4,415,448 bytes with SHA-256
`d52599359535b175519d1ce1249f2a7eafa443fa1765d1c39d7d38f93716c37f`.
Fact-owned bytes remain 52,530,664 and graph counts remain 4,260/3,503/3.
T3x adds no implementation or fixture change and makes no independent timing
claim; it closes on the already validated T3w5 compiler, whose selected report
passed 2,038/2,038, full report passed 5,212/5,212, and PA39 audit had zero
fatal findings with 26 warnings.

## 9. Phase 3: finish semantic identity and make presentation lazy

The production T2x name-path inventory is complete.  The T2x0 measurement
anchor classified all 34,823 frozen `ParseNamePath` requests:

| Caller family | Frozen requests | Initial disposition |
| --- | ---: | --- |
| Declaration analysis | 17,866 | Split syntax-owned names from anonymous/generated and true spelling adapters before conversion |
| Syntax-path fallback | 9,343 | Identify missing PA10 path payloads by syntax tag and backfill the earliest-owned facts |
| Semantic-ID recovery | 5,183 | Convert first: the terminal is already a `NameId`, so parsing its spelling is redundant |
| Call analysis | 2,272 | Reuse the path already built by the caller and eliminate duplicate lookup parsing |
| Friend analysis | 153 | Use the structured/syntax path already present at the PA22 boundary |
| Fixed generated library names | 4 | Build from preinterned component IDs, not a parsed qualified spelling |
| Template analysis | 2 | Convert with its owning template slice |
| Literal and PA19 ambiguity recovery | 0 | Retain separately named counters; PA19 ambiguity remains assigned to Phase 8 |

The family sum equals the total exactly.  T2x drives every one of these frozen
families to zero.  These measurements split the work
into:

- syntax-owned names that can use `SyntaxNamePath` or an existing semantic
  payload ID;
- fixed compiler-generated names that should use preinterned components;
- arbitrary spelling adapters that must remain text; and
- the PA19 ambiguity recovery, which remains assigned to Phase 8.

The accepted order was direct semantic-ID recovery, declaration facts,
syntax fallbacks, friend lookup, then the smaller call/template/generated
families.  The T2x exit counter is zero for every frozen family; true spelling
adapters and PA19 recovery remain separately named.  The PA11 `TypeAnalyzer`
is a separate standalone assignment pipeline and is therefore tracked as
T2y rather than being hidden in the production result.

### 9.0 T2y: PA11 standalone typed-name parity

`dev/src/pa11_semantic.cpp` implements the student-facing PA11
`--emit-types` mode independently of the PA12 production analyzer.  At the
T2y0 anchor it parsed joined syntax payloads for namespace targets, class/enum
declarations, declarators, type lookup, id expressions, and `decltype`.  That
work did not affect the frozen PA39 compile, but it was the same architectural
downgrade and therefore remained part of the full semantic audit.

T2y was implemented as a bounded non-production slice:

1. add PA11-only counters for path parses, components, syntax direct paths,
   fallbacks, and spelling lookups;
2. add a PA11 helper over the existing PA10 structured-name child and terminal
   semantic payload, without adding a field to every syntax node;
3. convert namespace alias/using targets, class/enum declarations,
   declarators, type-id lookup, id expressions, and `decltype` in measured
   order;
4. keep PA11 literal and operator spelling work assigned to T7 and T6 so this
   slice does not duplicate those representations; and
5. require exact PA11 fixtures, `make test-pa11`,
   `make test-report-through-pa11`, selected downstream reports, the full
   report, and a zero-fatal audit.

T2y makes no frozen-speed claim.  Its acceptance evidence is zero PA11
syntax-owned path reparses on representative PA11 course/assignment inputs,
exact output, and no high-multiplicity record growth.

The T2y0 measurement anchor aggregates all 52 successful PA11 assignment and
course translation units.  It records 323 path parses, 375 parsed components,
276 single-component parses, and 88 spelling lookups.  Parse ownership is 227
declarator, 24 class, 24 enum, 14 using, 7 type lookup, and 27 expression
requests.  Type and expression lookup contribute 44 spelling requests each.
Structured, syntax-path, direct-terminal, and fallback counters were all zero,
confirming that the baseline PA11 implementation ignored the available PA10
name facts rather than mixing typed and text paths.

T2y1 moves all 323 parses, 375 components, and 88 spelling lookups to zero
across those same 52 successful translation units.  It records 372 typed
syntax-path requests, 325 direct terminal paths, and zero fallbacks.  The only
initial fallbacks were the two qualified scoped-enum fixtures; PA10 already
parsed their components, so retaining the same semantic-only structured-name
child removes the final reparses without changing syntax serialization or any
common record layout.

The concatenated 52-translation-unit output is byte-identical with SHA-256
`a1fc494ad9e41ea2f93c07245a13608fa8ef2c1f6b803b5b0f59753a0d827b2d`.
Four interleaved 500-iteration suite batches measured baseline/candidate
medians of 2.710/2.675 seconds user, 4.190/4.175 seconds wall, and 6,180/6,192
KiB RSS.  This corroborates the structural removal on PA11 inputs and is not a
frozen source-to-object performance claim.

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

### 9.3 Changeset sequence

1. **T2x0 (complete):** retain compact per-family residual name-path counters
   without changing behavior.  Add finer counters only inside a heterogeneous
   family whose representation cannot be selected from the source audit.
2. **T2x1 (complete):** remove semantic-ID-to-spelling-to-path recovery.  Pass
   the existing `NameId` or one-component `NamePath` directly to lookup.
3. **T2x2 (complete):** split declaration recovery into syntax-owned,
   anonymous/fixed generated, and adapter paths; convert the first two without
   enlarging every syntax node.
4. **T2x3 (complete):** classify syntax fallbacks by syntax tag and add the
   missing path fact at the earliest PA10 owner where the existing node already
   has the necessary components.  All 9,343 frozen fallbacks were fundamental-
   type `id-expression` nodes; publishing their already-known terminal ID moves
   syntax fallback parsing to zero without changing serialized syntax.
5. **T2x4 (complete):** convert call, friend, template, and generated-library
   paths by reusing their structured paths or preinterned component IDs;
   retain explicitly classified adapters.  Every frozen family is zero.
6. **T2y (complete):** bring the standalone PA11 `TypeAnalyzer` to typed-name
   parity, with PA11-only counters and no frozen performance claim.
7. **T4a (complete):** add per-consumer counters for scope-prefix, display-name,
   emission-name, specialization-name, lambda-name, and generated-name
   rendering.  Record reads as well as writes so a retained field is not
   removed before its output consumer has a replacement.
8. **T4b (complete):** replace ordinary scope-prefix/display/emission retention with
   owner scope plus terminal `NameId` or a compact path ID.  Convert the
   default-constructor owner/leaf `rfind("::")` path in this slice.
9. **T4c:** replace class/function-template specialization presentation
   identity with pattern, argument-list, owner, and partition IDs.  Replace
   lambda identity with owner/context, token range, and ordinal facts rather
   than sanitized rendered type/function names.
10. **T4d:** classify generated semantic names by observable presentation
    versus private identity.  Carry kind plus owner/ordinal/source facts for
    private local types, range-for objects, backing objects, and shape
    sentinels; keep exact checked-in names behind the lazy renderer.
11. **T4e:** add lazy renderers for semantic dumps, diagnostics,
    `__PRETTY_FUNCTION__`, source-identity builtins, and final external names,
    then remove obsolete eager fields and counters.  Re-audit PA19/PA20/PA22
    helpers to prove that no canonical lookup or specialization table is keyed
    by the rendered result.

Each slice records common semantic record sizes and accounted arena bytes.
No slice may retain both the old qualified `NameId` and a new path identity on
ordinary records merely to make migration easier.

### 9.4 T4a measurement anchor

T4a adds stats-only counters at the render sites and at the consumers of the
retained presentation fields.  It also counts the final retained values and
spelling bytes, so a low read count cannot be mistaken for proof that the
field is cheap.  Counters are numeric, use no string-keyed registry, and are
aggregated per translation unit.  Common record sizes are reported beside the
population counts.

On the frozen `semantic_overload.cpp` compile, T4a records:

| Family | Renders or requests | Components | Rendered bytes |
| --- | ---: | ---: | ---: |
| Scope-prefix requests | 340,270 (340,212 cache hits) | - | - |
| Materialized scope prefixes | 89 | 90 | 4,094 |
| Function display names | 86,496 | 86,496 | 4,914,684 |
| Binding emission names | 93,496 | 272,234 | 5,224,902 |
| Class-specialization presentation | 15,987 | 42,595 | 1,368,114 |
| Class-specialization storage identity | 15,778 | 47,334 | 648,605 |
| Class-specialization scope slots | 8,715 | 17,430 | 247,865 |
| Lambda identity | 31 | 124 | 2,251 |
| Other generated identity | 770 | 784 | 21,168 |

The retained-field census and observed consumers are:

| Retained field | Values | Retained spelling bytes | Observed reads |
| --- | ---: | ---: | ---: |
| `FunctionInfo::display_name` | 78,840 | 4,554,625 | 44,790 |
| `BindingRecord::qualified_name` | 81,612 | 4,653,620 | 153 |
| `EntityRecord::presentation_name` | 8,715 | 749,951 | 8,715 |
| scope emission name | 17,656 | 424,506 | 15,869 |

The binding read count covers presentation consumers, not typed ABI paths;
the completed T3 ABI encoder already consumes owner/name paths directly and
reports its own path-component counts.  The entity count shows a different
shape: each specialization presentation is consumed by the lowering
replacement map, so T4c must replace that consumer before deleting the field.

The host record sizes at this checkpoint are 136-byte `BindingRecord`,
208-byte `EntityRecord`, 208-byte `FunctionInfo`, and 152-byte `DumpNode`.
T4b-T4e must not enlarge these common records or add a parallel retained path
to them.

The measurement build produces the exact 4,415,448-byte frozen object with
SHA-256
`d52599359535b175519d1ce1249f2a7eafa443fa1765d1c39d7d38f93716c37f`.
The selected PA12/19/20/22/23/25/34/37/38 report passes 2,009/2,009, the full
report passes 5,212/5,212, and the PA39 file audit has zero fatal findings and
27 advisory warnings.  T4a is measurement-only and makes no timing claim.

### 9.5 T4b1 lazy function display

T4b1 replaces `FunctionInfo::display_name`, which retained a fully qualified
spelling for every canonical function, with a terminal-name override used
only by compiler-provided functions whose semantic presentation differs from
their binding name.  Ordinary functions derive the terminal directly from
their binding.  Constructor/destructor lifecycle entries inherit the source
terminal through the typed override rather than retaining the old qualified
name.  `DisplayName(owner, terminal)` now runs only when a semantic dump,
callee/action node, or language-visible function-name builtin requests it.

On the frozen compile:

- qualified function-display fields retained: 78,840 -> 0;
- retained display spelling bytes: 4,554,625 -> 0;
- display renders: 86,496 -> 52,971;
- display-rendered bytes: 4,914,684 -> 2,696,370;
- interner calls: 1,459,215 -> 1,425,690;
- interner misses: 113,559 -> 111,490;
- interner hashed bytes: 23,741,119 -> 21,522,805; and
- shared string storage: 12,480,922 -> 12,352,616 bytes.

The 33,525 removed renders equal the removed interner calls.  The remaining
52,971 renders consist of 44,790 observed function-display consumers plus
class/enum prefix presentation that T4b3/T4c still owns.  `FunctionInfo`
remains 208 bytes: its existing four-byte name slot now carries only the rare
typed terminal override, so no common record or parallel path grows.

Three sequential A/B/B/A blocks against the immutable T4a compiler measure
baseline/candidate median user time of 4.375/4.375 seconds, wall time of
4.88/4.86 seconds, and RSS of 364,106/364,464 KiB.  This is accepted as a
timing-neutral structural removal, not a speed claim.  The frozen object is
exact at 4,415,448 bytes and the baseline SHA.  PA12 passes 166 assignment and
14 course tests, the selected PA12/19/20/22/23/27/34/37/38 report passes
1,965/1,965, the full report passes 5,212/5,212, and the file audit has zero
fatal findings with 27 warnings.  No fixture changes are required.

### 9.6 T4b2 lazy binding emission presentation

T4b2 replaces the fully rendered `BindingRecord::qualified_name` retained by
ordinary functions and variables with a terminal presentation override.  The
override is populated only when a language rule gives a binding a terminal
presentation different from its ordinary semantic name.  Presentation
consumers in PA15, PA19, PA20, and PA22 now share one renderer that accepts the
binding's typed owner and terminal; ordinary ABI identity continues to use the
typed path established in T3 and does not pass through this renderer.

This is a replacement, not a parallel cache.  The common `BindingRecord`
remains 136 bytes, and no qualified name is retained beside the owner/name
identity.  On the frozen compile:

- qualified binding fields retained: 81,612 -> 0;
- retained binding spelling bytes: 4,653,620 -> 0;
- emission-name renders: 93,496 -> 12,562;
- interner calls: 1,425,690 -> 1,344,603;
- interner misses: 111,490 -> 74,537;
- interner hashed bytes: 21,522,805 -> 16,901,448; and
- shared string storage: 12,352,616 -> 10,116,611 bytes.

The 153 observed binding-presentation reads now render only when their exact
semantic or source presentation is consumed.  The remaining 12,562 emission
renders belong to demanded output and specialized presentation families; T4b3
through T4e must classify those consumers before removing more storage.
`EntityRecord`, `FunctionInfo`, and `DumpNode` remain 208, 208, and 152 bytes.

Three sequential A/B/B/A blocks against the immutable T4b1 compiler measured
baseline/candidate medians of 4.370/4.320 seconds user, 4.870/4.805 seconds
wall, and 364,506/360,234 KiB RSS.  The screened medians are favorable by
1.14% user, 1.33% wall, and 1.17% RSS; the cumulative final gate still decides
the plan-wide performance claim.  The frozen object remains exact at
4,415,448 bytes with SHA-256
`d52599359535b175519d1ce1249f2a7eafa443fa1765d1c39d7d38f93716c37f`.
PA12 passes 166 assignment and 14 course tests, the selected
PA12/19/20/22/23/30/32/34/37/38 report passes 2,116/2,116, the full report
passes 5,212/5,212, and the file audit has zero fatal findings with 27
warnings.  No fixture changes are required.

### 9.7 T4b3 typed entity and scope emission presentation

T4b3 makes the previously overloaded `EntityRecord::name` contract explicit.
An entity now retains an emission terminal plus a byte-sized form identifying
whether output uses the terminal directly, qualifies it through the typed owner
scope, or temporarily retains an already rendered exceptional identity.  The
ordinary integrated class and template-marker paths use owner plus terminal;
PA11 and enum terminal presentation remains unchanged.  The rendered form is
currently confined to lambda identity and is assigned to T4c rather than
hidden behind a string parse.

`Program::RenderEntityEmissionName` is the single final renderer.  Type dumps,
diagnostics, aggregate-helper names, polymorphism helpers, template argument
presentation, and source-identity output now request it only when they consume
the spelling.  Class and scoped-enum member scopes similarly retain a typed
presentation owner and segment in the existing side vectors.  Their prefix is
materialized only on demand.  The default-constructor path now renders from
the entity owner and terminal and no longer finds its leaf with
`rfind("::")`.

The lowering presentation replacement map now indexes the entity's terminal
directly.  It no longer rebuilds an emission path for every specialized entity
to rediscover the terminal component.  On the frozen compile, relative to
T4b2:

- emission-name renders: 12,562 -> 185;
- display-name renders: 52,971 -> 44,790;
- scope-prefix requests: 306,745 -> 298,564;
- scope-prefix materializations: 89 -> 3,071, because 2,982 class/enum
  prefixes are actually demanded instead of all prefixes being constructed
  eagerly;
- scope-emission presentation reads: 15,869 -> 8,677;
- interner calls: 1,344,603 -> 1,318,845;
- interner misses: 74,537 -> 57,092;
- interner hashed bytes: 16,901,448 -> 15,650,661; and
- shared string storage: 10,116,611 -> 9,259,150 bytes.

`EntityRecord` remains 208 bytes, so the form uses existing padding and no
parallel identity table or string-keyed index is introduced.  The frozen
object remains exact at 4,415,448 bytes with SHA-256
`d52599359535b175519d1ce1249f2a7eafa443fa1765d1c39d7d38f93716c37f`.

Three sequential A/B/B/A blocks against the immutable T4b2 compiler measured
baseline/candidate medians of 4.355/4.350 seconds user, 4.845/4.820 seconds
wall, and 359,730/360,440 KiB RSS.  This is accepted as timing-neutral
structural enabling work, not a standalone speed claim.  PA11 passes 68
assignment and 2 course tests, PA12 passes 166 assignment and 14 course tests,
the through-PA12 report passes 833/833, the selected
PA11/12/16/17/18/19/20/22/23/28/30/34/37/38 report passes 2,652/2,652, the
full report passes 5,212/5,212, and the file audit has zero fatal findings with
27 warnings.  No fixture changes are required.

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

### 11.3 Changeset sequence

1. **T6a:** count operation-spelling comparisons by semantic caller,
   `StripOperationPrefix` calls by lowering caller, and fixed keyword/builtin
   comparisons separately.  Measure `SyntaxNode`, `DumpNode`, and
   `ExpressionInfo` before selecting storage.
2. **T6b:** define one compact `SemanticOperatorKind` mapping at the PA2/PA10
   boundary.  Pack it into existing flags or a dense syntax sidecar and retain
   the source `TextId` for exact serialization.
3. **T6c:** change PA11/PA12 overload resolution, builtin conversions,
   member-pointer logic, and constant evaluation to accept the enum.  Convert
   one operator family at a time so reducers identify the earliest semantic
   owner.
4. **T6d:** carry the enum through `DumpNode` or a compact parallel arena and
   change PA15/PA16/PA21/PA27/PA34 lowering to switch on it.  Delete
   `StripOperationPrefix` after its last integrated caller; arbitrary textual
   adapter operations remain separately classified.
5. **T6e:** convert small fixed vocabularies (`default`/`delete`, linkage,
   traits, attributes, builtins, predefined identifiers) to existing syntax
   kinds, byte enums, or preinterned `NameId`s.  Do not force extensible or
   language-visible strings into a global enum.

The exit condition is zero integrated operator spelling comparisons and zero
lowering prefix strips, plus a reviewed boundary list for every residual fixed
spelling comparison.  This phase must not enlarge every syntax or dump node.

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

### 12.3 Changeset sequence

1. **T7a:** count post-token decodes, retained fact hits, semantic fallback
   decodes, string/code-unit decodes, scalar renders, scalar reparses, and
   pragma format/parse events by literal family.  Record token/fact/dump record
   sizes and arena bytes.
2. **T7b:** complete integral and character facts, including signedness,
   width, suffix, character kind, and normalized bits.  Reuse the current
   retained scalar path rather than introduce a second literal table.
3. **T7c:** add floating bit facts and string/character byte or code-unit arena
   slices.  User-defined suffixes use `NameId`; exact source spelling remains a
   presentation handle.  GNU asm may reuse decoded narrow-string slices while
   its final label/template text remains a boundary.
4. **T7d:** give evaluated constants a compact `ScalarFactId` or equivalent
   kind-disjoint slot so `InternNumber`/`InternScalar` no longer formats values
   for later lowering.  PA15/PA16/PA20/PA21 consume bits directly and exact
   semantic/IR dumps render from the fact.
5. **T7e:** carry pragma-pack alignment as an integer fact through PA10 and
   semantics.  Remove every integrated fallback decoder whose corresponding
   retained fact is mandatory; leave standalone textual LowIR/CY86 conversion
   unchanged.

Reject a design that embeds wide integers, `long double`, or owning strings in
every token or dump node.  Common scalar facts should fit existing storage or
a compact fixed record; wide values and sequences belong in side arenas.

## 13. Phase 7: preserve spelling and parser identity across the front end

### 13.1 Current path

The macro processor already has a private compact `SpellingTable` and
`SpellingId`.  Its public integrated handoff returns `std::string` through
`IPostTokenStream`.  `PostTokenAnalyzer` classifies the spelling, and the
syntax sink interns every emitted spelling into the frontend table.

Once tokens reach PA10, most parser name facts use `TextId`, but several
declarator paths still return a joined `std::string`, temporarily collect
declaration names in `vector<string>`, and call the string overload of
`SetNameFact`.  `ParseName` already computes terminal and component IDs, so
this is a second, narrower identity loss after the preprocessing handoff.

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
- Extend PA10 name/declarator helpers to return the already-known terminal
  `TextId` beside any joined spelling required for exact syntax serialization.
  Publish name facts by ID and use compact ID vectors for multi-declarator and
  structured-binding publication.
- Replace parser decisions such as qualified/decorated/operator presence with
  flags accumulated while consuming components where doing so removes a scan;
  do not stop constructing a checked-in syntax payload that the public PA10
  format requires.

Implement this as two independently measurable slices.  T8a changes only the
integrated PA4-to-PA10 handoff and its dense remap.  T8b changes PA10 parser
name-fact publication without changing syntax output.  This interface-wide
work follows the narrower semantic and ABI phases.  Preserve exact PA2, PA4,
and PA10 fixtures and add counters for emitted occurrences, distinct remaps,
discarded spellings, retained bytes, classification reuse, string-overload
`SetNameFact` calls, and joined-name rescans.  Reject T8b if it requires an
extra field on every syntax node; the IDs already exist in tokens, parser
locals, or semantic-only name children.

## 14. Phase 8: secondary cleanup

Address these only after the preceding measured work:

1. **T9a:** replace PA19 ambiguous-relational-declaration substring and stream
   parsing with a retained parser alternative or typed token-range recipe.
   The frozen count is small, so correctness and clarity are the reasons.
2. **T9b:** replace any PA32 function-template ABI result tag `NameId`
   comparisons left after T6 with syntax-tag/operator enums, or publish the ABI
   type fact directly.
3. **T9c:** close GNU assembly and attribute handling after T7: decode literal
   sequences once, classify supported templates/constraints once, and retain
   language-visible assembly payloads, section names, and final labels as
   text.
4. **T9d:** close RTTI, generated-name, pretty-function, and hosted-builtin
   paths after T4/T6.  Structured identity must remain internal; rendering is
   allowed only at the ABI, object, diagnostic, or language-visible boundary.
5. **T9e:** repeat the complete layer registry in section 5.4.  Any newly found
   integrated text recovery gets an independent owner reducer and changeset;
   true boundary sites are recorded rather than mechanically rewritten.

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
| T3t | Carry production dependent-expression operations by enum and member/template-id names by graph ID | The production recipe path no longer assigns `AbiDependentExpression::op` or `text`. Four fixed operations use a byte enum; member and template-id names reuse the kind-disjoint index slot as graph string ID + 1. PA14 retains arbitrary operation text for exact serialization. `AbiDependentExpression` remains exactly 1,056 bytes. | The frozen source has three canonical ABI expressions but zero affected operations or source names, so this slice has no direct frozen structural timing opportunity. A screened three-block A/B/B/A check measured 4.445/4.395 s user, 4.910/4.885 s wall, and 364,300/364,282 KiB RSS; paired -1.35% user, -1.11% wall, and -0.01% RSS, recorded as neutral rather than claimed as a win. | All 12 timed objects and the stats object are exact at 4,415,448 bytes with the baseline SHA; no fixture changes. The final responsibility split produced a compiler byte-identical to the timed candidate. | PA14 117/117; selected PA14/15/19/20/23/32/33/37/38 report 1,483/1,483; full report 5,210/5,210; zero-fatal audit with 26 warnings | `9832b1b5`; accepted architecture cleanup with zero frozen occurrences |
| T3u | Carry builtin/standard/vendor ABI type vocabulary by byte-sized enums | The frozen production path resolves 1,049 builtin types and 700 standard substitutions through enums with zero text fallbacks; it has zero block-pointer occurrences. Fact-owned storage falls 53,779,377 -> 53,774,748 (-4,629 bytes), while canonical graph counts remain 4,260 types, 3,503 arguments, and 3 expressions. Compile-time assertions hold `AbiType` at 416 bytes, canonical `TypeNode` at 184, and `AbiFunctionRecord` at 840. | Three screened explicit-`-O0` A/B/B/A blocks against T3t: 4.425/4.420 s median user, 4.900/4.895 s wall, and 364,344/364,614 KiB RSS; paired candidate -0.23% user, 0.00% wall, and +0.10% RSS. Retained as neutral structural work. | All 12 timed objects and the stats object are exact at 4,415,448 bytes with the baseline SHA; no fixture changes. The frozen source has no vendor qualifier, so block-pointer coverage comes from the cumulative suite rather than a timing claim. | PA14 117/117; through PA14 1,050/1,050; selected PA14/15/17/18/19/20/22/23/26/30/32/33/37/38 report 2,279/2,279; full report 5,210/5,210; zero-fatal audit with 26 warnings | `80d0535c`; accepted neutral structural work |
| T3v0 | Count numeric ABI presentation before changing representation | The frozen production path carries 4 array/vector extents and 446 local-name/lambda presentations as text, with zero typed occurrences. The inventory also classifies dependent array bounds as expression handles and diagnostics/final number formatting as true boundaries. | Measurement-only; timing intentionally deferred because the counters run only under `--stats` | Stats output remains exact at 4,415,448 bytes with the baseline SHA; no fixture changes | Full report 5,210/5,210; zero-fatal audit with 26 warnings | `3f89649f`; accepted measurement anchor for T3v |
| T3v | Carry array extents and local/lambda ordinals as integers through the ABI graph | Frozen array bounds move 0/4 -> 4/0 typed/text, and local presentations move 0/446 -> 446/0. Fact-owned storage falls 53,774,748 -> 53,774,559 (-189 bytes), while graph counts remain 4,260 types, 3,503 arguments, and 3 expressions. Kind-disjoint slots hold the values; assertions retain `AbiType` at 416 bytes, `AbiFunctionTarget` at 1,104, `AbiFunctionRecord` at 840, and `TypeNode` at 184. | Three screened explicit-`-O0` A/B/B/A blocks against T3u: 4.445/4.435 s median user, 4.950/4.910 s wall, and 363,344/364,656 KiB RSS; paired candidate -0.79% user, -0.81% wall, and +0.01% RSS. | All 12 timed objects and the stats object are exact at 4,415,448 bytes with the baseline SHA; no fixture changes. Final formatting and substitution ownership were split into compiled responsibility modules, and the post-split compiler is byte-identical to the timed candidate. | PA14 117/117; through PA14 1,050/1,050; selected PA14/15/17/18/19/20/22/23/25/26/30/32/33/37/38 report 2,420/2,420; full report 5,210/5,210; zero-fatal audit with 26 warnings | `ba277ced`; accepted structural work with a corroborating timing gain |
| T3w0 | Classify the remaining emitted language-name copies before changing their storage | The frozen path has 397 typed and 274 textual type source names, plus 0 typed and 122 textual local-owner names. It has no type-tag, member-external source-name, or literal-suffix occurrences. The source audit separately identifies explicit qualified-name/API overrides as true text boundaries and one `"-"` control marker as removable non-language text. | Measurement-only; timing intentionally deferred because the counters run only under `--stats` | Stats output remains exact at 4,415,448 bytes with the baseline SHA; fact bytes remain 53,774,559 and graph counts remain 4,260/3,503/3; no fixture changes | Full report 5,210/5,210; zero-fatal audit with 26 warnings | `7597cf41`; accepted measurement anchor for T3w |
| T3w1 | Carry retained-recipe type terminals and local-owner names by graph string ID | Type source names move 397/274 -> 671/0 typed/text, and local-owner names move 0/122 -> 122/0. Existing kind-disjoint `AbiType` slots carry both IDs, and the redundant production `"-"` complete-substitution marker is removed. Fact-owned storage falls 53,774,559 -> 53,771,416 (-3,143 bytes); graph counts and common record sizes are unchanged. | The first screened three-block window was mixed (+0.46% paired user), while two clean repeats favored the candidate in every block. The deciding repeat measured 4.425/4.385 s median user, 4.905/4.875 s wall, and 363,946/364,048 KiB RSS; paired candidate -1.24% user, -1.69% wall, and +0.10% RSS. Treated as a structural improvement with timing corroboration, not a 1% standalone claim. | All 36 timed objects across the three windows and the stats object are exact at 4,415,448 bytes with the baseline SHA; no fixture changes | PA14 117/117; through PA14 1,050/1,050; selected PA14/15/17/19/20/22/23/25/26/30/32/33/37/38 report 2,384/2,384; full report 5,210/5,210; zero-fatal audit with 26 warnings | `6638de4b`; accepted |
| T3w2 | Carry member-NTTP source names and literal-operator suffixes by graph string ID | `AbiTemplateArgument::index` and `resolved_entity`, plus the function record's existing resolved-source slot, carry the emitted IDs by kind. The frozen benchmark has zero occurrences, so counters and fact bytes remain unchanged. Canonical argument records and hashing were also extracted into a compiled graph-argument module, leaving the core mangler at 2,910 lines with no representation or size change. | Not timed independently because every affected frozen counter is zero; no compile-time claim | Frozen and stats objects remain exact at 4,415,448 bytes with the baseline SHA; no fixture changes | Selected PA14/15/23/32/34/37/38 report 1,283/1,283; full report 5,210/5,210 after the ownership split; zero-fatal audit with 26 warnings | `228e489e`; accepted architecture cleanup |
| T3w3 | Compact mixed adapter-name/graph-ID storage for namespace qualifiers and type ABI tags | One 32-byte discriminated list replaces two 24-byte vectors. `AbiType` shrinks 416 -> 400 bytes; enclosing argument/expression/target/function records shrink 64/32/32/16 bytes. Frozen fact storage falls 53,771,416 -> 52,530,664 bytes despite zero frozen tags. PA33 moves 12/0 text/typed tags to 0/12 and its fact storage falls 77,212 -> 75,196. | Three screened explicit-`-O0` A/B/B/A blocks: baseline/candidate medians 4.390/4.400 s user, 4.870/4.870 s wall, and 366,088/364,396 KiB RSS; paired candidate -0.23% user, -0.62% wall, and -0.16% RSS. Accepted as structural/storage work, not a timing claim. | All 12 frozen objects are exact at 4,415,448 bytes with the baseline SHA. PA33 object is byte-identical; PA14 tagged adapter output remains exact with 2 textual tags; no fixture changes. | PA14 117/117; through PA14 1,050/1,050; selected PA14/15/23/32/33/37/38 1,009/1,009; full report 5,210/5,210; zero-fatal audit with 26 warnings | `5e733931`; accepted |
| T3w4 | Carry dependent member-template specialization terminals without joining `::` text | The PA32 reducer moves the affected source-name count from 1 typed/1 text to 2 typed/0 text. The owner child plus terminal ID is the complete member-template-prefix substitution identity, so no full path or new field is retained. Frozen counters and 52,530,664 fact bytes are unchanged because it has zero occurrences. | Not timed independently because the frozen benchmark has zero affected records | The reducer corrects an invalid `13N::value_type` source-name component to `10value_type`; the pinned reference and Clang both encode the terminal. Frozen output remains exact at 4,415,448 bytes with the baseline SHA. One earliest-owned PA32 inspect regression was added through the documented reference target; no existing fixture changed. | PA32 149/149; through PA32 4,391/4,391; selected PA14/15/23/32/33/37/38 1,010/1,010; full report 5,211/5,211; zero-fatal audit with 26 warnings | `eefb458a`; accepted correctness and architecture cleanup |
| T3w5 | Replace residual ABI fallback names with semantic paths and type the `main` context | The measurement anchor found all seven internal fallback categories at zero on the frozen source and classified the 679 live text occurrences as final external spellings: 50 assembly, 595 C-function, 21 builtin-runtime, and 13 C-variable names. The PA35 addressability fixture exposed one explicit variable override and two text object-path components; both fall to zero when its existing semantic owner/name path is used. A main-local-type reducer records one typed main context and zero text-path components. Fact bytes and graph counts remain 52,530,664 and 4,260/3,503/3. | Not timed independently: every changed frozen category has zero occurrences, so this is architecture cleanup rather than a compile-time claim | The frozen object remains exact at 4,415,448 bytes and the baseline SHA. The PA35 object and the new PA30 main-local-type object are byte-identical before/after; the pinned reference agrees on `_Z8use_typeIZ4mainE5LocalEiv`. One earliest object-emission regression was added. | Selected ABI/semantic/lowering/object report 2,038/2,038; full report 5,212/5,212; zero-fatal audit with 26 warnings | Counter anchor `bd4d29a3`; implementation `5732e547`; accepted |
| T3x | Close the production ABI-string audit | Every production fact constructor is accounted for. Production assigns no text identity field; exact emitted names are interned under semantic IDs, while direct strings are final external spellings, completed aliases, read-only enum/effect recognition, diagnostics, final formatting, or PA14 adapter data. All text fact/path counters and synthetic identity searches are zero. | Audit-only closeout; no independent timing claim | Frozen object, PA35 override object, and PA30 main-context object remain exact; no fixture changes beyond T3w5 | Reuses T3w5 selected 2,038/2,038 and full 5,212/5,212 reports; zero-fatal audit with 26 warnings | Accepted closeout; section 8.8 |
| T3 | Typed translation-unit ABI context replaces production fact files | Complete: production definitions/references, semantic paths, ordinary source names, type ABI tags, fixed ABI vocabulary, dependent-expression facts, numeric presentation, substitution keys, and residual paths use typed graph/per-mangle identities. Text remains only for exact emitted bytes, final external symbols, diagnostics, and PA14 input/output. | Accepted T3 slices are cumulatively faster and reduce fact-owned storage by more than two thirds from T3d | Exact mangles, symbols, binding, and object behavior | PA14/15/17/18/19/20/22/23/26/30/32/33/34/37/38 plus full report | Complete; proceed to T2x |
| T2x0 | Count residual semantic name-path recovery by caller family | All 34,823 requests are classified: 17,866 declaration, 9,343 syntax fallback, 5,183 semantic-ID recovery, 2,272 call, 153 friend, 4 generated-library, 2 template, and 0 literal/ambiguity. The family sum equals the total. | Measurement-only; timing intentionally deferred because counters are consumed under `--stats` | Frozen stats object remains exact at 4,415,448 bytes with the baseline SHA; no fixture changes | Selected semantic report 1,953/1,953; full report 5,212/5,212; zero-fatal audit with 29 warnings, identical to an audit of the clean parent archive | `870e329a`; accepted measurement anchor |
| T2x1 | Carry one-component semantic lookup names by `NameId` | Semantic-ID recovery falls 5,183 -> 0 and total path parses fall 34,823 -> 29,640. A direct qualified-name lookup entry point also removes temporary one-component `NamePath` construction from typed path traversal without changing lookup-query semantics. | Three clean A/B/B/A blocks against T2x0: baseline/candidate medians 4.425/4.415 s user, 4.910/4.920 s wall, and 364,970/364,702 KiB RSS; candidate -0.23% user, +0.20% wall, and -0.07% RSS, accepted as timing-neutral structural work | All 12 frozen objects remain exact at 4,415,448 bytes with the baseline SHA. The existing PA34 dependent `__ext_vector_type__(N)` case moves 6 semantic-ID reparses to 0; no fixture changes. The PA22/PA23 alias-only call sites have zero observed suite/frozen occurrences and are architectural cleanup. | PA11 68/68; through PA11 653/653; selected PA11/12/19/22/23/34/37/38 report 1,765/1,765; full report 5,212/5,212; zero-fatal `dev/src` audit with 26 warnings | `3c751b70`; accepted |
| T2x2 | Carry declaration names as syntax paths or semantic terminal IDs | A finer anchor splits 17,866 declaration parses into 17,601 class, 257 enum, 8 namespace-alias/using, and 0 parameter/member-pointer requests. The implementation moves every frozen subfamily to zero and total path parses fall 29,640 -> 11,774. Class/specialization naming is isolated in a responsibility-named compiled module; ordinary class/enum names reuse syntax IDs, generated names are interned directly, and namespace targets reuse structured paths. Qualified enum compatibility remains explicitly counted but has zero frozen occurrences. | Three clean A/B/B/A blocks against the finer counter anchor: baseline/candidate medians 4.445/4.430 s user, 4.935/4.915 s wall, and 364,304/364,088 KiB RSS; candidate -0.34% user, -0.41% wall, and -0.06% RSS | All 12 frozen objects remain exact at 4,415,448 bytes with the baseline SHA; no fixture changes and no LowIR/MIR changes | PA12 166/166; through PA12 833/833; selected PA12/19/20/22/23/34/37/38 report 1,868/1,868; full report 5,212/5,212; zero-fatal `dev/src` audit with 26 warnings | Counter anchor `11ede2c8`; implementation `45990d88`; accepted |
| T2x3 | Publish the terminal ID on fundamental-type expression syntax | The finer anchor attributes all 9,343 syntax fallbacks to `id-expression`. The PA10 fundamental-type expression branch now publishes the terminal ID it already parsed. Syntax fallback reparses fall 9,343 -> 0, total path parses fall 11,774 -> 2,431, interner calls fall 1,470,696 -> 1,461,353, and hashed bytes fall 23,800,969 -> 23,763,803; interner misses remain 113,561. | The first series was rejected as noisy. Three quiet A/B/B/A blocks against the immutable counter anchor measured baseline/candidate medians 4.400/4.405 s user, 4.915/4.890 s wall, and 364,774/364,600 KiB RSS; candidate +0.11% user, -0.51% wall, and -0.05% RSS, accepted as timing-neutral structural work | All frozen objects remain exact at 4,415,448 bytes with the baseline SHA; serialized syntax, LowIR, and MIR are unchanged, so no fixture regeneration or new behavior-only fixture is warranted | PA10 157/157 plus course 7/7; through PA10 583/583; selected PA10/12/19/22/23/34/37/38 report 1,859/1,859; full report 5,212/5,212; zero-fatal `dev/src` audit with 26 warnings | Counter anchor `b276dcbd`; implementation `09d3f082`; accepted |
| T2x4a | Reuse typed paths in friend and functional-cast lookup | Friend declarations now use their structured components or already-interned simple payload ID for both lookup and declaration placement. Friend parses fall 153 -> 0, total path parses fall 2,431 -> 2,278, interner calls fall 1,461,353 -> 1,461,195, and hashed bytes fall 23,763,803 -> 23,760,406; misses fall by 2. The functional-cast spelling adapter now reuses its already-parsed path for lookup; that branch has zero frozen occurrences and is recorded as architectural cleanup, not a performance claim. | Three screened A/B/B/A blocks against the immutable T2x3 compiler: baseline/candidate medians 4.380/4.390 s user, 4.880/4.885 s wall, and 364,908/364,580 KiB RSS; candidate +0.23% user, +0.10% wall, and -0.09% RSS, accepted as timing-neutral structural work | All frozen objects remain exact at 4,415,448 bytes with the baseline SHA; no serialized syntax, semantic, LowIR, or MIR fixture changes | PA22 308/308 plus course 2/2; through PA22 2,667/2,667; full report 5,212/5,212; zero-fatal `dev/src` audit with 26 warnings | `b8c94562`; accepted |
| T2x4b | Eliminate residual call, template, and generated-library path parsing | All 2,278 remaining frozen requests move to typed facts: 1,922 overloaded operators reuse their `NameId`; 186 allocation/deallocation requests reuse the already-interned operator name; 162 destructors form a typed terminal path; 2 compiler aliases intern their guaranteed single terminal; 2 retained using-directives reuse syntax paths; and 4 fixed standard-library requests use a byte-sized vocabulary plus typed components. Total path parses fall 2,278 -> 0, interner calls fall 1,461,195 -> 1,459,215, hashed bytes fall 23,760,406 -> 23,741,119, and misses remain 113,559. | Three screened A/B/B/A blocks against the immutable T2x4a compiler: baseline/candidate medians 4.375/4.390 s user, 4.895/4.880 s wall, and 365,310/365,336 KiB RSS; candidate +0.34% user, -0.31% wall, and +0.01% RSS, accepted as timing-neutral structural work | Every frozen object remains exact at 4,415,448 bytes with the baseline SHA. Existing owner fixtures exercise the converted behavior; no serialized fixture moves and no new layout-only fixture is warranted. | Selected PA12/16/19/21/23/26/34/37/38 report 1,938/1,938; full report 5,212/5,212; zero-fatal `dev/src` audit with 26 warnings | `51119165`; accepted |
| T2x | Close production semantic name-path recovery by caller family | Complete. The frozen total falls 34,823 -> 0: semantic-ID 5,183 -> 0, declaration 17,866 -> 0, syntax fallback 9,343 -> 0, call 2,272 -> 0, friend 153 -> 0, generated library 4 -> 0, template 2 -> 0, with literal/ambiguity remaining 0. The source closeout in section 5.3 keeps true adapters and later operator/literal/ambiguity work explicitly assigned. | Accepted slices are individually neutral or favorable within the screened host-noise band; the final plan gate will measure the cumulative result against the immutable audit anchor | Exact syntax, semantic, LowIR, MIR, and frozen object output; no fixture churn | Owner/through gates recorded per slice; final full report 5,212/5,212; zero-fatal audit with 26 warnings | Complete at `51119165`; T2y next |
| T2y0 | Count standalone PA11 semantic name recovery | Across all 52 successful PA11 assignment/course translation units: 323 path parses, 375 components, 276 single-component parses, and 88 spelling lookups. Families are 227 declarator, 24 class, 24 enum, 14 using, 7 type lookup, and 27 expression parses; type/expression lookup contribute 44 spelling requests each. Structured/direct/fallback path counters are all zero. | Measurement-only; no frozen claim because `--emit-types` is not in the PA39 object path | Exact PA11 output; no fixture changes | PA11 68/68 plus course 2/2; through PA11 653/653; full report 5,212/5,212; zero-fatal audit with 26 warnings | `5fcc10cb`; accepted measurement anchor |
| T2y | Bring standalone PA11 name identity to parity | Across the same 52 successful inputs, 323 path parses, 375 components, and 88 spelling lookups fall to zero.  All six parse families are zero; 372 typed syntax paths comprise 325 direct terminals and structured paths with zero fallbacks.  PA10 retains a semantic-only structured child for the two qualified enum cases; no common record grows. | Four interleaved 500-iteration PA11-suite batches: baseline/candidate medians 2.710/2.675 s user, 4.190/4.175 s wall, and 6,180/6,192 KiB RSS.  No frozen claim because `--emit-types` is outside source-to-object compilation. | Concatenated 52-TU output is byte-identical with SHA-256 `a1fc494ad9e41ea2f93c07245a13608fa8ef2c1f6b803b5b0f59753a0d827b2d`; no fixture changes | PA10 157/157 plus course 7/7; through PA10 583/583; PA11 68/68 plus course 2/2; through PA11 653/653; selected PA10/11/12/19/20/22/34/37/38 1,690/1,690; full report 5,212/5,212; zero-fatal audit with 26 warnings | `975edc1a`; accepted; T4 next |
| T4a | Measure semantic presentation production, retention, and demand | Frozen requests reproduce the 340,270 scope-prefix, 86,496 display-name, and 93,496 emission-name profile totals.  Specialized renders are split into presentation/storage/scope-slot/lambda/generated families.  Final retained values and bytes are counted separately from consumers; common record sizes are recorded. | Stats-only anchor; no timing claim | Exact 4,415,448-byte frozen object and baseline SHA; no fixture changes | Selected PA12/19/20/22/23/25/34/37/38 report 2,009/2,009; full report 5,212/5,212; zero-fatal audit with 27 warnings | `b9e05991`; accepted measurement anchor |
| T4b1 | Render function display names from owner and terminal on demand | Qualified display retention falls 78,840 -> 0 values and 4,554,625 -> 0 bytes.  Display renders and interner calls each fall by 33,525; hashed spelling falls by 2,218,314 bytes and shared string storage by 128,306 bytes. `FunctionInfo` remains 208 bytes with only a rare terminal override in the former slot. | Three A/B/B/A blocks: baseline/candidate median user 4.375/4.375 s, wall 4.88/4.86 s, RSS 364,106/364,464 KiB; accepted as timing-neutral structural removal | Exact frozen object and baseline SHA; no fixture changes | PA12 166/166 plus course 14/14; selected semantic/downstream report 1,965/1,965; full report 5,212/5,212; zero-fatal audit with 27 warnings | `e02423aa`; accepted |
| T4b2 | Render binding emission presentation from owner and terminal on demand | Qualified binding retention falls 81,612 -> 0 values and 4,653,620 -> 0 bytes. Emission renders fall 93,496 -> 12,562, interner calls fall by 81,087, hashed spelling falls by 4,621,357 bytes, and shared string storage falls by 2,236,005 bytes. `BindingRecord` remains 136 bytes with only a rare terminal override in the former slot. | Three A/B/B/A blocks: baseline/candidate median user 4.370/4.320 s, wall 4.870/4.805 s, RSS 364,506/360,234 KiB; favorable by 1.14%/1.33%/1.17% | Exact frozen object and baseline SHA; no fixture changes | PA12 166/166 plus course 14/14; selected semantic/downstream report 2,116/2,116; full report 5,212/5,212; zero-fatal audit with 27 warnings | `2e793e19`; accepted |
| T4b3 | Store ordinary entity and named-scope emission presentation as typed owner plus terminal | Emission renders fall 12,562 -> 185 and display renders fall 52,971 -> 44,790. Interner calls fall by 25,758, misses by 17,445, hashed spelling by 1,250,787 bytes, and shared string storage by 857,461 bytes. `EntityRecord` remains 208 bytes; the constructor path has no qualified-name parse. | Three A/B/B/A blocks: baseline/candidate median user 4.355/4.350 s, wall 4.845/4.820 s, RSS 359,730/360,440 KiB; accepted as timing-neutral structural work | Exact frozen object and baseline SHA; no fixture changes | PA11 and PA12 owner tests; through PA12 833/833; selected report 2,652/2,652; full report 5,212/5,212; zero-fatal audit with 27 warnings | `285ef075`; accepted |
| T4c-e | Typed specialization, lambda, and generated identity with lazy boundary rendering | Planned from the T4a-T4b3 census | Planned | Exact semantic serialization expected; no dual retained identity | PA19/20/22 plus downstream reports | T4c next |
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

## 22. Ordered execution plan from the current checkpoint

This is the authoritative order after `285ef075`.  Later rows may be
re-prioritized only by updating this document with the new dependency or
measurement; do not silently skip an unresolved closeout gate.

| Order | Slice | Required evidence before acceptance | Clean boundary |
| ---: | --- | --- | --- |
| 1 | T3w3 compact ABI type presentation names (complete) | PA14/PA33 exact behavior, record sizes, fact bytes, frozen exactness, selected reports, full report, audit, and ABBA recorded in section 8.7 | `5e733931`; ledger recorded |
| 2 | T3w4 member-template specialization identity (complete) | PA32 reducer, pinned-reference/Clang terminal parity, zero joined production path, full report, and audit recorded in the ledger | `eefb458a`; ledger recorded |
| 3 | T3w5 residual ABI fallbacks (complete) | Per-category counter anchor, final-boundary classification, PA30/PA35 reducers, exact frozen/object outputs, full report, and audit recorded in section 8.7 | `bd4d29a3` counter anchor; `5732e547` implementation; ledger recorded |
| 4 | T3x Phase 2 closeout (complete) | Source audit, complete retained-text classification, zero production fixed-word/path/numeric/synthetic-identity recovery, and exact frozen object recorded in section 8.8 | Closeout ledger recorded |
| 5 | T2x residual production semantic name paths (complete) | All 34,823 frozen requests are classified and removed; source-retained adapters and later operator/literal/ambiguity work are recorded in section 5.3 | `870e329a` anchor through `51119165` closeout; ledger recorded |
| 6 | T2y standalone PA11 typed-name parity (complete) | All 52 successful inputs have zero path reparses, spelling lookups, and fallbacks; exact aggregate output, no common-record growth, owner/through/selected/full reports, audit, and PA11 timing are recorded in section 9.0 and the ledger | `5fcc10cb` measurement anchor; `975edc1a` implementation; no frozen claim |
| 7 | T4a semantic presentation measurement (complete) | Frozen per-family renders, retained values/bytes, consumers, record sizes, exact object, reports, and audit are recorded in section 9.4 | `b9e05991`; accepted counter anchor |
| 8 | T4b1 lazy function display (complete) | Zero retained qualified function display values, exact object/dumps, reports, audit, and screened timing are recorded in section 9.5 | `e02423aa`; accepted structural removal |
| 9 | T4b2 lazy binding emission presentation (complete) | Zero retained qualified binding values, exact object/dumps, unchanged common records, reports, audit, and screened timing are recorded in section 9.6 | `2e793e19`; accepted structural and measured removal |
| 10 | T4b3 typed entity/scope emission presentation (complete) | Owner-plus-terminal entity and scope presentation, no constructor text parse, unchanged common records, exact object/dumps, reports, audit, and screened timing are recorded in section 9.7 | `285ef075`; accepted structural removal |
| 11 | T4c-e remaining lazy semantic presentation | Typed specialization, lambda, generated, and final output identities; exact dumps; no common-record growth or dual retained identity | One commit per specialization/lambda, generated, and output family |
| 12 | T5 block collation | Exact ordering reducers including decimal boundaries; exact MIR/object/LSDA | Independent PA15/26/29 commit |
| 13 | T6 operator and fixed-vocabulary enums | Packed representation proof, zero integrated operator spelling comparisons and lowering prefix strips, reviewed residual vocabulary list, exact fixtures | Counter anchor; syntax/semantic/lowering/fixed-registry commits by family |
| 14 | T7 literal and scalar facts | Decode/redecode/render/reparse counters, scalar/arena sizes, direct lowering consumption, earliest literal reducers | Counter anchor; integral, floating/sequence, evaluated-scalar, and pragma commits |
| 15 | T8 spelling and parser identity handoff | Distinct-remap/retained-byte/classification counters; zero avoidable parser string-overload name-fact calls; exact PA2/4/10 interfaces | T8a integrated spelling handoff, then T8b PA10 terminal-ID/name-fact publication |
| 16 | T9 specialized recovery and final source audit | Separate disposition and owner test for ambiguity, ABI tags, asm/attributes, and generated/presentation sites; section 5.4 registry fully closed | Independent commits only, followed by a registry closeout commit |
| 17 | Final gate | Five-run anchor comparison, full report, zero-fatal audit, timed clean self-build, clean timed 8-way and 32-way inception with peak RSS and exact compares | Final ledger commit |

At every row, the fastest iteration signal is the PA-selected
`make test-report ACTIVE_TEST_REPORT_PAS='...'` form.  Root `make test-report`
is mandatory before an accepted shared-representation commit.  Inception is
reserved for the final gate or a separately justified high-risk epoch.

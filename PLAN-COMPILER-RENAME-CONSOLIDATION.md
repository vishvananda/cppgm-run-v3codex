# Plan: Compiler Naming and Ownership Consolidation

Status: ready

Date: 2026-08-28

## Objective

Replace assignment-number terminology in production compiler paths, internal
namespaces, and implementation identifiers with names that describe stable
compiler responsibilities.  Reorganize `dev/src/` into a directory hierarchy
that makes the preprocessing, syntax, semantic, lowering, LowIR, and native
boundaries visible.  After names are stable, move functions and classes to
their actual owners and undo file splits that reflect historical PA size
pressure rather than coherent translation-unit responsibilities.

This is an output-inert architecture program.  It does not authorize a new
language feature, a LowIR or MIR contract change, an optimization change, or a
student-facing assignment change.  Generated tool output, diagnostic record
schemas, LowIR, MIR, objects, linked programs, and behavior must remain exact.
The compiler executable itself may differ where source paths, mangled internal
names, or deliberate translation-unit boundaries necessarily affect symbols
and link layout; every such difference must be classified and performance must
remain at the established level.

The program must:

- remove `paN` prefixes from production filenames under `dev/src/`;
- remove PA-numbered production namespaces and internal function/class names;
- retain PA terminology wherever it is part of a handout, test target,
  assignment-owned diagnostic record, or compatibility contract;
- preserve the independent PA6, PA7, and PA8 implementation models rather than
  falsely merging them with the final PA10-PA12 frontend;
- give every source file one domain owner and every cross-file class one clear
  home;
- preserve all through-PA38 behavior, the LowIR contract audit, the file audit,
  and exact same-revision inception;
- keep the final same-source self/GCC and self/Clang performance ratios at or
  below the current level, with the binding self/GCC result no higher than
  1.50x; and
- use 32-way outer, inner, and object parallelism for all large builds and
  inception runs.

## Scope boundary

The following PA-named surfaces are deliberately **not** renamed:

- `pa1/` through `pa39/`, `cppgm.tests/course/paN/`, assignment READMEs,
  reference fixtures, and root targets such as `make test-pa30`;
- executable checkpoint and harness names whose assignment identity is part of
  the staged course structure;
- documented diagnostic keys including `pa10_stats`, `pa11_stats`,
  `pa12_stats`, `pa15_stats`, `pa30_compile_stats`, `pa30_driver_stats`,
  `pa31_object_stats`, `pa34_preproc_stats`, `pa37_opt_stats`, and
  `pa37_prepare_stats`, plus the existing `pa15_retained_fallback` and
  `pa15_unreachable_internal` diagnostic records; and
- comments or developer records that intentionally identify the assignment
  which owns a public requirement.

Those are contract names.  By contrast, a production path, include guard,
namespace, private type, private helper, or generic implementation comment is
not a contract merely because it currently contains a PA number.

No course test will inspect production filenames or source spelling.  The new
layout rule is a developer repository audit, not a student-facing language
feature.  Course tests continue to express structural or behavioral properties
that a student can implement from the owning PA README.

## Starting checkpoint

The planning checkpoint is `e98f385218a4` on `v3opt`, clean and synchronized
with `origin/v3opt`.

The current `dev/src/` census is:

| Item | Count |
| --- | ---: |
| C++ source and header files | 440 |
| `.cpp` translation units | 224 |
| headers | 216 |
| filenames beginning with `paN` | 215 |
| files declaring a PA-numbered namespace | 207 |
| direct `#include "paN..."` lines | 417 |
| distinct PA-like internal identifier spellings | 153 |

The dominant namespace references are
`pa10_syntax_detail` (1,257), `pa11` (423),
`pa12_semantic_detail` (301), `pa15_lowir_detail` (255),
`pa15_lowering_support` (83), and the PA16-PA34 lowering-detail families.
This is therefore a dependency-spine migration, not a collection of isolated
file renames.

The build already accepts source IDs with subdirectories in
`dev/frontend_source_sets.mk`, and `dev/Makefile` creates matching object and
dependency directories.  PA39 also derives shared object IDs from relative
source paths.  The probe and inception paths still need an explicit nested-file
test before the first bulk move; support inferred from Make expressions is not
sufficient evidence.

The inherited final gates are:

- through-PA38: 5,471/5,471;
- LowIR contract audit: 124 ledger rows, 99 retained;
- file audit: zero fatal findings and 33 established warnings;
- tight file scan: 134 duplicate and 31 division advisories, all classified;
- same-source O1: 1.499x self/GCC and 1.427x self/Clang;
- optimized-host frozen guard: GCC -0.27%, Clang -0.52%; and
- explicit-O1 inception: 215/215 current objects and byte-identical compiler,
  SHA-256 `ef5c434d...`, 8,626,303 text bytes.

The object count is a baseline, not a permanent requirement.  A justified
translation-unit merge or split changes the expected source-set census, but
self and inception must still contain and match every expected object at the
same revision.

Several current owners sit immediately below audit size limits, while their
names describe an assignment or a broad phase rather than the behavior inside:

| Current file | Lines | Ownership concern |
| --- | ---: | --- |
| `pa10_syntax.cpp` | 2,994 | parser orchestration and many grammar method families |
| `pa12_semantic_initialization.cpp` | 2,947 | scalar, class, array, allocation, and lifetime policy |
| `pa12_semantic.cpp` | 2,923 | analyzer orchestration mixed with expression/statement families |
| `pa12_semantic_declarations.cpp` | 2,902 | ordinary, class, constructor, and function declaration work |
| `pa15_lowering_abi.cpp` | 2,863 | calling convention, identity, lifecycle, and ABI layout |
| `pa21_constant_evaluator.cpp` | 2,858 | evaluator orchestration and operation families |
| `pa15_lowering.cpp` | 2,740 | program lifecycle plus core expression/control lowering |
| `pa12_semantic_detail.h` | 2,396 | analyzer declaration and unrelated retained facts |
| `lowir_native.cpp` | 3,000 | top-level native pipeline plus lowering mechanics |
| `lowir_native_elf.cpp` | 2,982 | emission orchestration mixed with ELF policy |

This plan does not aim to make every file small.  It uses these files as the
first ownership audits because their current boundaries are both broad and
structurally constrained.

## Naming and layout rules

1. Paths name a compiler responsibility, not the assignment which introduced
   it.  Directory context supplies the broad domain, so leaf names do not
   repeat it: use `semantic/templates/deduction.cpp`, not
   `semantic/semantic_template_deduction.cpp`.
2. Public module entry headers use the module name.  Private headers use the
   responsibility they contain; `internal.h`, `detail.h`, `misc.h`, `util.h`,
   `part2.cpp`, and line-count-based suffixes are not final owners.
3. PA-numbered namespaces collapse into stable domain namespaces.  A `detail`
   namespace is permitted only for genuinely private implementation machinery,
   never as a replacement for a PA number.
4. Scope supplies context for type names.  Generic private names such as
   `Parser`, `Reader`, `Writer`, and `Pool` are made specific when they remain
   in a broad namespace; a clear narrow scope may instead carry the context.
5. One source file owns one cohesive method family.  File size is a guard, not
   an ownership rule.  A large coherent owner may be split by named behavior;
   unrelated small fragments are merged into their domain owner.
6. Template and CRTP implementation stays in headers when required by C++11
   visibility.  This program does not move hot template code out of line merely
   to make the tree look symmetrical.
7. Anonymous-namespace helpers and local statics move with the functions whose
   policy they implement.  They are not promoted to external linkage to make a
   move easier.
8. Source-set composition preserves canonical link order unless a measured,
   deliberate translation-unit change requires an audited update.
9. No permanent forwarding headers, namespace aliases, duplicate definitions,
   or `using namespace paN...` bridges remain.  A namespace family is migrated
   atomically after collision analysis.
10. Mechanical organization commits contain no expression rewrites, data
    structure changes, optimizer changes, or opportunistic cleanup.

## Final production tree

Entrypoint wrappers such as `dev/cppgm++.cpp`, `dev/recog.cpp`, and
`dev/lowir2native.cpp` remain at `dev/` because they define build targets.  The
implementation tree converges on:

```text
dev/src/
  support/
    containers/           compact generic containers
    interning/            frontend string and spelling identity
    numeric/              decimal spelling and neutral numeric helpers
    testing/              the in-process test runner

  preprocess/
    tokens/               preprocessing and post-tokenization
    expressions/          controlling-expression evaluation
    macros/               macro operators and expansion engine
    hosted/               hosted builtin registry and probes
    preprocessor.*        complete preprocessing pipeline

  recognition/            independent translation-unit recognizer
  namespace_semantics/    independent namespace-declaration tool
  namespace_initialization/ independent initialization/linkage model
  cy86/                    CY86 model, frontend, and backend

  abi/
    itanium/               mangling model, graph, substitution, rendering

  syntax/
    model/                 arena, nodes, tags, consumers, statistics
    parser/                parser, cursor, name facts, token classification
    extensions/            lambdas, range-for, regions, attributes, GNU forms
    syntax.*               module entry API and driver

  semantic/
    model/                 names, types, scopes, entities, graph storage
    analysis/              analyzer orchestration, scope, storage, tables
    declarations/          names, declarators, functions, classes, enums
    expressions/           calls, conversions, operators, literals, conditions
    initialization/        scalar, aggregate, list, constructor initialization
    lifetime/              demand, elision, destruction, static lifecycle
    templates/             identity, deduction, substitution, instantiation
    object_model/          layout, inheritance, virtual bases, RTTI, members
    constants/             scalar, object, and address evaluation and storage
    extensions/            hosted types, builtins, attributes, GNU forms
    semantic.*             module entry API, driver, render/view entrypoints

  lowering/
    ir/                    typed source-to-LowIR model, identity, and rendering
    core/                  program lowerer, source types, driver, reachability
    control/               branches, switch, regions, exception flow
    expressions/           scalar, assignment, conditional, bit-field lowering
    calls/                 arguments, constructors, special members, ABI calls
    objects/               storage, lifetime, virtual bases, RTTI, statics
    abi/                   calling convention, symbols, lifecycle, mangling use
    extensions/            initializer lists, range-for, GNU asm, complex data
    lowering.*             module entry API and statistics

  compiler_object/
    format.*               private compiler-object serialization
    elf_reader.*           relocatable host-object import
    link.*                 compiler-object linking policy

  lowir/
    model/                 public typed LowIR vocabulary and identity
    io/                    parse, prepare, serialize, frontend adapter, debug
    analysis/              reachability, function, EH, phi, inline analysis
    optimize/              established O1/O2/O3 pass implementations
    cy86/                  LowIR-to-CY86 conversion
    driver/                LowIR tool reporting and policy

  native/
    mir/                   MIR model, construction, and control-flow facts
    analysis/              source LowIR and MIR analysis
    lowering/              operation-family selection and lowering
    allocation/            locations, registers, spills, homes, forwarding
    frame/                 stack and frame layout
    encoding/              instruction and global encoding
    eh/                    EH, host EH, references, and LSDA
    object/                code buffers, fixups, ELF output, string tables
    driver/                session, program, statistics, and top-level pipeline
```

This hierarchy does not imply new runtime layers.  Direct calls remain direct,
and header-only lowering mixins remain compile-time composition.  Directories
record ownership; they do not authorize a generic framework.

### Family routing

| Current family | Final owner |
| --- | --- |
| `pa6_*` | `recognition/` |
| `pa7_semantic.*` | `namespace_semantics/` |
| `pa8_*` | `namespace_initialization/` |
| `pa10_*` syntax and parser files | `syntax/model`, `syntax/parser`, or `syntax/` |
| PA25/PA30/PA32/PA34 `*_syntax*` files | `syntax/extensions/` |
| `pa11_*` model files | `semantic/model/` or `semantic/object_model/` |
| `pa12_*` and later `*_semantic*` files | the semantic subdirectory named by behavior |
| `pa15_lowir_*` | `lowering/ir/` |
| PA15-PA34 `*_lowering*` files | the lowering subdirectory named by behavior |
| `pa30_object*` | `compiler_object/format` and `compiler_object/link` |
| `pa30_elf_object*` | `compiler_object/elf_reader` |
| `pa30_lowir_adapter*` | `lowir/io/frontend_adapter` |
| `pa30_region_*` | split among syntax, semantic, and lowering extension owners |
| `abi_mangle_*` | `abi/itanium/` |
| token, macro, hosted preprocessor files | `preprocess/` |
| `lowir_*` | `lowir/` by model, I/O, analysis, optimization, or driver role |
| `mir_model*`, `lowir_native_*`, x86 models | `native/` by MIR, lowering, allocation, frame, encoding, EH, object, or driver role |

The PA number never decides the destination.  For example,
`pa15_switch_semantic.cpp` goes to `semantic/analysis/switch.cpp`, while
`pa15_control_flow_lowering.h` goes to `lowering/control/control_flow.h`.

Representative leaf destinations make the intended naming concrete:

| Current path | Destination |
| --- | --- |
| `pa6_grammar_data.h` | `recognition/grammar_definition.h` |
| `pa6_recognizer.{h,cpp}` | `recognition/recognizer.{h,cpp}` |
| `pa7_semantic.{h,cpp}` | `namespace_semantics/analysis.{h,cpp}` |
| `pa8_program.h`, `pa8_model.cpp`, `pa8_parser.cpp` | `namespace_initialization/program.h`, `model.cpp`, `parser.cpp` |
| `pa10_syntax_model.{h,cpp}` | `syntax/model/arena.{h,cpp}` |
| `pa10_syntax_tags.{h,cpp}` | `syntax/model/tags.{h,cpp}` |
| `pa10_syntax.cpp` | `syntax/parser/parser.cpp` |
| `pa10_parser_cursor.h` | `syntax/parser/cursor.h` |
| `pa11_model.{h,cpp}` | `semantic/model/program.{h,cpp}` |
| `pa11_type_layout.cpp` | `semantic/object_model/type_layout.cpp` |
| `pa12_semantic_detail.h` | `semantic/analysis/analyzer.h` plus responsibility-owned fact headers |
| `pa12_semantic_tables.{h,cpp}` | `semantic/analysis/index_tables.{h,cpp}` |
| `pa19_function_template_deduction.cpp` | `semantic/templates/function_deduction.cpp` |
| `pa21_constant_evaluator.cpp` | `semantic/constants/scalar_evaluator.cpp` |
| `pa15_lowir_model.{h,cpp}` | `lowering/ir/program.{h,cpp}` |
| `pa15_lowering.cpp` | `lowering/core/program_lowerer.cpp` |
| `pa15_lowering_abi.{h,cpp}` | `lowering/abi/abi_lowering.{h,cpp}` before symbol-family redistribution |
| `pa18_polymorphism_lowering.{h,cpp}` | `lowering/objects/polymorphism.{h,cpp}` |
| `pa30_object.{h,cpp}` | `compiler_object/format.{h,cpp}` plus `link.cpp` |
| `pa30_lowir_adapter.{h,cpp}` | `lowir/io/frontend_adapter.{h,cpp}` |
| `lowir_inline_o1.{h,cpp}` | `lowir/optimize/inline_o1.{h,cpp}` |
| `lowir_native_frame_layout.{h,cpp}` | `native/frame/layout.{h,cpp}` |
| `lowir_native_elf.cpp` | `native/object/elf_writer.cpp` after ownership audit |

## Namespace and identifier destination

The primary namespace migration is:

| Current name | Final name |
| --- | --- |
| PA6 recognizer internals in broad `cppgm` scope | `cppgm::recognition` |
| PA7 namespace-analysis internals in broad `cppgm` scope | `cppgm::namespace_semantics` |
| `cppgm::pa8` | `cppgm::namespace_initialization` |
| `cppgm::pa10_syntax_detail` and PA25/32/34 syntax-detail namespaces | `cppgm::syntax` |
| `cppgm::pa11`, `cppgm::pa12_semantic_detail`, and later semantic-detail namespaces | `cppgm::semantic` |
| `cppgm::pa19_template_presentation`, `pa22_lambda_presentation`, and `pa34_source_identity` | `cppgm::semantic::presentation` |
| `cppgm::pa15_lowir_detail` | `cppgm::lowering::ir` |
| PA15-PA34 lowering-detail namespaces | `cppgm::lowering` |
| `pa15_lowering_support` | responsibility owners under `cppgm::lowering` and `cppgm::lowering::support` |
| `pa15_lowering_abi` | `cppgm::lowering::abi` |
| `pa15_function_reachability` | `cppgm::lowering::reachability` |
| `pa15_force_inline` | `cppgm::lowering::inline_policy` |
| `pa15_local_presentation` | `cppgm::lowering::presentation` |
| `pa16_cleanup_continuation` | `cppgm::lowering::cleanup` |
| `pa16_zero_initialization` | `cppgm::lowering::zero_initialization` |
| `pa21_constant_template_lowering` | `cppgm::lowering::constant_pool` |
| `cppgm::pa30` | `cppgm::compiler_object` |

Well-named non-PA namespaces such as `lowir_model`, `lowir_opt`,
`lowir_native`, `mir_model`, and `abi_mangle` are not renamed merely because
their files move into directories.  A separate namespace normalization would
create broad mangling and layout churn without advancing the PA-removal goal.

Merging `pa11` and `pa12_semantic_detail` into `cppgm::semantic`, and merging
all lowering-detail generations into `cppgm::lowering`, require a declaration
collision ledger first.  Existing `using namespace pa11` and cross-generation
lowering composition show that these types are conceptually one domain, but
they are not proof that all unqualified declarations are collision-free.  Any
collision is resolved by giving the less-specific internal type a
responsibility name before the namespaces merge; nested `paN` substitutes are
not an acceptable escape hatch.

Representative identifier renames establish the convention:

| Current identifier | Final identifier |
| --- | --- |
| `kPA6Grammar` | `recognition::kCppGrammarDefinition` |
| `PA6Grammar()` | `recognition::CppGrammar()` |
| PA6 private `Parser` | `recognition::Recognizer` |
| `PA3Mock_IsDefinedIdentifier` | `MockIsDefinedIdentifier` |
| `RecognitionStats` / `RecognizeTranslationUnit` | `recognition::Stats` / `recognition::RecognizeTranslationUnit` |
| PA7 `SemanticAnalysisStats` | `namespace_semantics::Stats` |
| PA7 `SemanticTranslationUnit` | `namespace_semantics::TranslationUnit` |
| PA7 private `SemanticModel` / `SemanticParser` | `namespace_semantics::Program` / `Parser` |
| `InitializationStats` / `InitializationProgram` | `namespace_initialization::Stats` / `namespace_initialization::Program` |
| `pa8::ProgramModel` | `namespace_initialization::Model` |
| PA8 private `Parser` | `namespace_initialization::TranslationUnitParser` |
| PA10 private `Parser` | `syntax::SyntaxParser` |
| `SyntaxStats` / `SyntaxInterningStats` | `syntax::Stats` / `syntax::InterningStats` |
| `WriteSyntaxTranslationUnit` | `syntax::WriteTranslationUnit` |
| `ConsumeSyntaxTranslationUnit` | `syntax::ConsumeTranslationUnit` |
| `TypeAnalysisStats` / `WriteTypeTranslationUnit` | `semantic::TypeViewStats` / `semantic::WriteTypeView` |
| `SemanticAnalysisStats` | `semantic::Stats` |
| `SemanticAnalyzer` | `semantic::Analyzer` |
| `pa15_lowir_detail::TypedProgram` | `lowering::ir::Program` |
| `LowIRSource` / `LowIRLoweringStats` | `lowering::Source` / `lowering::Stats` |
| `GraphLowerer` | `lowering::ProgramLowerer` |
| lowering-driver `GraphConsumer` | `lowering::SemanticGraphConsumer` |
| constant-template `Pool` | `lowering::constant_pool::Pool` |
| reachability `Summary` | `lowering::reachability::Summary` |
| `pa30::CompilerObject` | `compiler_object::Object` |
| private `Writer` / `Reader` / `ElfReader` | `BinaryWriter` / `BinaryReader` / `ElfObjectReader` |
| `append_pa12_template_stats` | `append_template_analysis_stats` |

This table is a floor, not a license for aesthetic churn.  R0 records every
remaining PA-bearing identifier and every vague cross-file owner; well-named
domain types such as `SyntaxArena`, `Program`, `TypeTable`, `SemanticGraphView`,
and `SourceTypeLowering` remain unless the namespace merge produces a real
ambiguity.

## Function and class ownership moves

Path and namespace work happens before cross-translation-unit ownership work.
The final ownership pass then applies these concrete rules:

### Syntax

- `SyntaxArena`, nodes, tags, tag catalog, tree consumers, and their statistics
  live under `syntax/model/`.
- `SyntaxParser`, `ParserCursor`, `ParserNameFacts`, token classification, and
  brace matching live under `syntax/parser/`.
- Lambda capture, range-for, region, object-attribute, aggregate, template,
  control-flow, GNU-asm, and hosted grammar mixins live under
  `syntax/extensions/`; they remain template definitions where required.
- The current near-limit `pa10_syntax.cpp` becomes the parser orchestrator.
  Method clusters move only after an internal parser declaration has one clear
  owner; the change must not turn private parser machinery into a public API.

### Semantic analysis

- The canonical PA11 `Program`, name/type/scope tables, identity, mainline
  views, and neutral inheritance graph become `semantic/model/` owners.
- Declaration names, declarator types, ordinary declarations, function
  declarations, enum declarations, anonymous unions, and class-scope rules are
  grouped under `semantic/declarations/` rather than split by introduction PA.
- Calls, conversions, operators, literals, conditionals, casts, member
  pointers, and RTTI expression rules are owned by `semantic/expressions/`.
- Initialization, list initialization, constructor action construction, elision,
  demand, special members, and destruction are divided between
  `semantic/initialization/` and `semantic/lifetime/` by lifecycle ownership.
- The current `pa12_semantic_inheritance.cpp` is not retained as a mixed owner:
  base/access facts move to `object_model`, conversions and casts to
  `expressions`, and call-argument policy to the call owner.
- Template syntax identity, presentation, validation, deduction,
  instantiation, partial storage, nondeduced contexts, result identity,
  placeholders, and integration are grouped by those behaviors under
  `semantic/templates/`, independent of PA19-PA24 numbering.
- Scalar, object, and address evaluators plus static constant storage live
  together under `semantic/constants/`; they remain distinct evaluators unless
  the deduplication ledger separately proves equal policy.
- Hosted builtins, traits, numeric extensions, complex values, ABI tags,
  source identity, attributes, and GNU forms live under
  `semantic/extensions/`.

### Typed lowering

- The typed source-to-LowIR vocabulary, program, identity tables, and renderer
  are one `lowering/ir/` module.  They remain distinct from public
  `lowir_model`; this plan does not reopen the LowIR contract audit.
- `ProgramLowerer` owns orchestration and per-program/per-function lifecycle in
  `lowering/core/`.  CRTP mixins are grouped under control, expression, call,
  object, ABI, and extension directories according to the operation they add.
- The 2,863-line ABI lowering owner is divided by calling convention, symbol
  identity, lifecycle entry, and ABI layout only when each cluster has an
  explicit symbol manifest and exact generated-output comparison.
- `pa15_lowering_support` is dissolved by ownership: literal decoding goes to
  source/type lowering support, presentation maps to `lowering/presentation`,
  ID maps to a narrow lowering support header, and counting output machinery to
  the driver/statistics owner.  No generic dumping or map framework is added.
- Polymorphism, virtual bases, member pointers, RTTI, constructors,
  destruction, local statics, and static lifecycle group under
  `lowering/objects` or `lowering/calls` according to whether they own storage
  or invocation.

### Compiler object, LowIR, and native backend

- Private object serialization, deserialization, format detection, and linking
  leave the generic `pa30` namespace and become `compiler_object` owners.
  Region syntax/semantics/lowering do not move with object serialization.
- The frontend adapter lives beside LowIR I/O because its single job is to
  convert `lowering::ir::Program` into `lowir_model::LowirProgram`.
- Existing LowIR optimization pass boundaries remain intact.  Files move into
  `lowir/optimize/`, but this plan does not recombine or reopen the completed
  optimizer-duplication decisions.
- Native files are grouped by MIR, analysis, lowering, allocation, frame,
  encoding, EH, object, and driver ownership.  The already coherent native
  namespaces remain.  Large `lowir_native.cpp`, `lowir_native_elf.cpp`, and
  `lowir_native_elf` helper clusters may be redistributed only by symbol-level
  ownership with direct performance guards.

Before any cross-TU move, record the moved definitions, private dependencies,
local statics, template instantiations, old/new object symbols, and expected
link-order effect.  A move that requires broadening linkage, adding a virtual
call, allocating policy state, or changing an inline boundary is redesigned or
rejected.

## Output and binary invariants

The expected comparison depends on the kind of increment:

| Increment | Required invariant |
| --- | --- |
| source-set factoring only | compiler and all tool binaries byte-identical |
| path-only `git mv` | generated outputs and allocated code/data sections exact; compiler differences limited to audited file names, symbol tables, dependency paths, and derived build-id data |
| namespace or internal symbol rename | generated outputs exact; compiler symbol/mangling changes listed; no unexplained data, control-flow, or timing change |
| move within one translation unit | generated outputs exact and compiler allocated sections exact unless definition order deliberately changes |
| move across translation units | generated outputs exact; compiler code/layout delta classified; direct A/B and host guards neutral |

The generated-output corpus covers every staged view: preprocessing tokens,
post-tokens, controlling expressions, macro/preprocessor output, recognition,
namespace declarations, namespace initialization, syntax, type view, semantic
view, textual LowIR, optimized LowIR at each level, native objects, linked
executables, runtime behavior, debug facts, and ordered statistics records.
Baseline and candidate binaries run the same checked-in fixtures; no fixture is
rewritten to accommodate the refactor.

Add a developer-only `scripts/check_compiler_refactor_outputs.pl` oracle which
accepts explicit baseline and candidate tool roots and compares the frozen
representative outputs and exit statuses.  It may compare complete artifacts
because it is a pre/post compiler regression oracle, not a student-facing
feature test.  It must not inspect production source text or special-case an
input program.  The normal course tests remain the authoritative contract
gate.

Exact diagnostic error prose is not a reference contract, but this plan still
preserves it.  A rename is not a reason to rewrite errors, statistics field
order, or student-facing render text.

## Verification cadence

### Fast verification for every increment

1. confirm no stale Cachegrind, Valgrind, `perf`, detached compiler benchmark,
   or inception build is running;
2. run `git diff --check` and the compiler-layout audit;
3. build all affected dev tools with `-j32`;
4. run the earliest affected `make test-paN` plus the directly affected later
   assignment tests;
5. run the pre/post refactor-output oracle for the affected staged views; and
6. inspect old/new include and namespace census for the migrated family.

### Cumulative verification

After no more than three retained commits, and after every namespace-family or
cross-TU boundary:

```sh
make -j32 test-report-through-pa38 \
  TEST_REPORT_ASSIGNMENT_JOBS=1 TEST_REPORT_SUBTEST_JOBS=32
make audit-lowir-contract
perl scripts/cppgm_file_audit.pl --stage pa38
git diff --check
```

Assignments are serialized only to avoid the known cross-assignment harness
flake; each assignment receives 32 subtest workers.  This is not an 8-way
fallback.

Run fresh explicit-O1 inception after each completed pipeline tier (early
tools, syntax, semantics, lowering/object, LowIR/native) and after every change
to source IDs or translation-unit count.  All inception job settings are 32:

```sh
make -j32 inception \
  INCEPTION_BUILD_JOBS=32 INCEPTION_OBJECT_BUILD_JOBS=32
```

The final comparison must match every object in the current source-set census
and the final compiler exactly.  Scratch roots go under `/dev/shm` when space
allows and are removed after results are recorded.

### Performance retention

Path, namespace, and cross-TU changes can perturb compiler layout even when
source semantics are unchanged.  For every hot syntax, semantic, lowering,
LowIR, or native batch:

1. compare the prior and candidate self compilers on the identical candidate
   source with interleaved order, recording wall, user, system, aggregate CPU,
   maximum RSS, compiler hash/text, and object census;
2. run the corrected 32-way O1 same-revision oracle with self-, GCC-, and
   Clang-built `cppgm++` binaries compiling that same revision;
3. use at least three lanes and six self/GCC lanes whenever the result is
   within 0.5% of a retention boundary; and
4. retain the fully optimized GCC-O3 and Clang-O3 compiler guard compiling the
   immutable frozen workload at compiler `-O0`.

A batch is rejected or reworked if it causes a repeatable regression greater
than 0.5% in wall or aggregate CPU, if either optimized-host frozen lane
regresses beyond the measurement floor, or if the final self/GCC ratio exceeds
1.50x.  A favorable denominator movement does not hide an absolute self
regression, and a small self increase is evaluated against the same-revision
GCC/Clang change rather than raw host compiler time.

No unrelated optimization may be added to pay for a naming regression.  If
symbol length or link order causes a real loss, first restore canonical object
order or choose equally descriptive shorter names; otherwise reject the
specific move.

## Execution phases

### R0. Freeze the manifest and developer oracles

1. Record every production path, include, namespace, include guard, class, and
   function containing a PA number.
2. Classify each occurrence as internal-to-rename or contract-visible-to-keep.
3. Build a source-set and include dependency graph for all 14 dev targets.
4. Build declaration-collision ledgers for the syntax, semantic, lowering, and
   compiler-object namespace merges.
5. Add `make audit-compiler-layout`, backed by a developer script that rejects
   PA-numbered production paths/namespaces/identifiers outside an explicit
   contract-string allowlist.
6. Add the pre/post output oracle and freeze the current hashes and diagnostic
   schemas.
7. Refresh correctness, file/LowIR audits, O1 performance, optimized-host
   frozen performance, disk headroom, and process state.

Exit: every old name has exactly one destination or justified allowlist entry,
and the layout/output oracles fail when deliberately seeded with a bad name or
changed output.

### R1. Make source-set organization explicit

1. Factor `frontend_source_sets.mk` into ordered responsibility variables such
   as preprocessing, syntax, semantic model, semantic analysis, lowering IR,
   lowering, LowIR, and native sources.
2. Compose each tool from those variables without changing the expanded source
   list or link order.
3. Test a nested scratch source ID through dev build, PA39 object probe, clean
   rebuild, depfile reload, self-host build, and inception object comparison.
4. Remove the scratch source and prove all binaries byte-identical.

Exit: directory moves are supported by measured build paths, and source-set
factoring is binary exact.

### R2. Move shared non-PA foundations

Move `support`, preprocessing, CY86, and Itanium ABI files into their final
directories using `git mv`.  Update includes and ordered source IDs only; keep
well-named namespaces and identifiers unchanged in this phase.  Commit one
domain at a time.

Exit: all associated staged tools pass, path-only binary deltas are classified,
and the first cumulative 32-way inception tier is exact.

### R3. Rename the independent PA6-PA8 stages

For recognition, namespace semantics, and namespace initialization, use one
path commit followed by one namespace/identifier commit per stage.  Rename the
PA6 grammar symbols and the broad/generic parser-model names listed above.
Keep all three staged representations independent from final syntax and
semantic models.

Exit: `recog`, `nsdecl`, and `nsinit` outputs and exit statuses are exact; no
PA-numbered production name remains in these modules; through-PA8 and the early
inception tier pass.

### R4. Consolidate syntax names and ownership

1. Move PA10 core files and later syntax extensions into `syntax/`.
2. Atomically migrate syntax-detail namespaces after collision review.
3. Rename the syntax entry API, stats, parser, include guards, and remaining
   PA-bearing syntax identifiers.
4. Reconcile model, parser, and extension ownership without yet moving methods
   across translation units.
5. In a separately measured increment, move only parser method clusters whose
   present file owner is demonstrably wrong.

Exit: no PA terminology remains in syntax production names, syntax and all
downstream semantic/lowering views are exact, through-PA38 passes, and O1 plus
inception retention is demonstrated.

### R5. Consolidate the semantic model and analyzer

1. Move PA11 model files and all PA12/later semantic files by final behavior.
2. Rename `pa11`, `pa12_semantic_detail`, and semantic helper namespaces into
   `cppgm::semantic` in one collision-resolved family increment, without a
   compatibility alias.
3. Rename the entry APIs, stats, analyzer, presentation helpers, guards, and
   remaining PA-bearing semantic identifiers.
4. Keep the class/function bodies in their existing translation units until
   all names compile and all output is exact.
5. Run a complete semantic symbol-owner manifest before R6.

Exit: all PA11-PA34 semantic behavior and presentations are exact, the
semantic namespace has no shadow PA generation, and full corrected performance
and inception gates pass.

### R6. Repair semantic method ownership

Move one method family per commit in this order:

1. declarations and declarator/name ownership;
2. expressions, calls, conversions, and operators;
3. initialization, constructor action, lifetime, and demand;
4. templates and presentation/identity;
5. object model, inheritance, virtual bases, and RTTI; and
6. constants and hosted extensions.

Merge tiny historical fragments into the named owner and split mixed large
files only by these behaviors.  Preserve private dependency clusters and
definition order where possible.  Each cross-TU increment receives direct A/B,
optimized-host, exact-output, audit, and inception verification.

Exit: every `semantic::Analyzer` method has the owner prescribed by the final
tree, no file is divided solely by historical PA boundary or size, and no
performance or output regression is retained.

### R7. Consolidate typed IR and lowering names

1. Move typed source-to-LowIR files to `lowering/ir` and migrate their namespace.
2. Move lowering core and mixins by control, expression, call, object, ABI, and
   extension behavior.
3. Atomically consolidate lowering-detail namespaces and rename
   `ProgramLowerer`, entry APIs, stats, helpers, and guards.
4. Dissolve assignment-number support namespaces into their responsibility
   owners without changing implementation or inline policy.
5. Keep established CRTP composition and direct call structure intact.

Exit: typed IR remains distinct from public LowIR, generated LowIR is exact,
all PA15-PA34 lowering tests pass, and corrected O1/inception gates are neutral.

### R8. Repair lowering method and class ownership

Move one lowering behavior family per commit: control, scalar/expression,
calls/constructors, lifetime/storage, object model, ABI, then extensions.
Treat any cross-TU move out of the hot `ProgramLowerer` or ABI owners as a code
layout change even when the C++ body is textually identical.

Exit: no arbitrary PA-generation split remains in lowering, no hot inline
boundary changed without evidence, and exact LowIR/native output plus all
performance guards pass.

### R9. Consolidate compiler-object and region ownership

Move serialization, ELF import, linking, and frontend adaptation to their final
owners; migrate `pa30` names and specific reader/writer classes.  Split region
syntax, semantics, and lowering into their respective pipeline domains.
Preserve private object magic, serialization bytes, symbol order, relocation
facts, presentation policy, and diagnostic records exactly.

Exit: PA30-PA32 objects and links are byte-exact, compiler-object namespaces are
responsibility-named, and no region code remains under object serialization.

### R10. Move LowIR and native files into domain directories

Move already descriptively named LowIR, MIR, and native files into the final
directory hierarchy, shortening redundant leaf prefixes only after path moves
are exact.  Do not rename the established non-PA namespaces or reopen optimizer
policy.  Redistribute functions/classes between native translation units only
where the symbol-owner manifest identifies a mixed owner.

Exit: PA13, PA29-PA31, PA37, and PA38 outputs are exact; LowIR audit remains
124/99 or has only explicitly justified documentation-only path updates; file
audit does not worsen; full performance and inception pass.

### R11. Remove migration residue

1. Require zero PA-numbered production filenames, namespaces, include guards,
   function names, and class names outside the R0 allowlist.
2. Remove all temporary aliases, forwarding headers, duplicate source IDs, and
   stale dep/object paths.
3. Rewrite generic implementation comments that use an assignment number where
   the new architectural name is clearer; retain ownership and contract notes.
4. Verify every source-set variable expands without duplicates and every file
   has at least one intended tool owner.
5. Refresh the file-audit and tight-scan classifications using the new paths.

Exit: the layout audit is clean, `rg` finds only contract-approved PA strings,
and a clean build does not depend on any removed path.

### R12. Final closure

Run the complete 32-way through-PA38 report, LowIR contract audit, file and
layout audits, pre/post output corpus, corrected same-revision O1 oracle,
optimized GCC/Clang frozen guards, and fresh 32-way explicit-O1 inception.
Record final source/object counts, compiler hashes and text, path/symbol delta
classification, timings, warning census, and the complete old-to-new manifest.
Remove scratch worktrees, object roots, and timing artifacts after recording
results; confirm no stale profiler or compiler process remains.

Exit: every completion criterion below is evidenced and the final ledger is
committed and pushed.

## Commit and push protocol

1. Commit path moves separately from namespace/identifier renames.
2. Commit namespace/identifier renames separately from function/class ownership
   moves.
3. One module family or one cross-TU method family per implementation commit.
4. Use `git mv` so history remains traceable.
5. Fast verification precedes every commit.
6. Cumulative through-PA38 verification and a push occur after no more than
   three retained commits.  A namespace-family merge, source-count change, or
   high-risk hot-TU move is pushed individually after its full gate.
7. A failed output, audit, inception, or performance increment is reverted or
   repaired before the next family begins; failures are recorded in the plan
   ledger rather than hidden in a later aggregate.
8. Do not mix fixture regeneration, feature work, optimizer work, or unrelated
   cleanup into these commits.

## Completion criteria

The plan is complete only when:

1. all 215 starting PA-prefixed production files have responsibility names or
   have been deliberately merged into a named owner;
2. no production namespace, function, class, include guard, or non-contract
   identifier contains a PA number;
3. every retained PA string is present in the explicit contract allowlist;
4. the final directory and namespace structure matches this plan and has no
   forwarding compatibility layer;
5. semantic and lowering symbol-owner manifests show no arbitrary historical
   PA or line-count split;
6. staged PA6-PA8 models remain independent and all 14 dev tools retain their
   intended source-set boundaries;
7. root 32-way `test-report-through-pa38` is clean at the then-current test
   count;
8. the LowIR contract audit passes without an unjustified public or private
   contract addition;
9. the file audit has zero fatal findings and no warning regression; the tight
   scan is fully reclassified against new paths;
10. all staged text, diagnostics, LowIR, MIR, object, link, and behavior outputs
    are exact against the frozen pre-refactor tools;
11. every compiler-binary difference is explained by path, internal name, or
    accepted translation-unit layout evidence;
12. corrected same-revision O1 ratios do not regress and self/GCC is at most
    1.50x; optimized GCC and Clang frozen lanes remain neutral;
13. fresh explicit-O1 inception at 32/32/32 matches every current object and the
    final compiler exactly; and
14. all retained commits and the completed execution ledger are pushed.

## Execution ledger

Append one entry per retained or rejected increment.  Each entry records the
old/new path and symbol manifest, mutation class, affected tools and earliest
assignment, fast and cumulative tests, exact-output hashes, compiler-binary
delta classification, file/layout audits, performance measurements, inception
checkpoint where applicable, commit, and push state.

- `6cf37205` — R1 source-set factoring.  Mutation: build plumbing only; all 14
  expanded source lists and their order remained byte-for-byte exact.  All 14
  dev binaries were byte-identical.  A temporary nested source ID passed dev
  depfile reload, PA39 object probing, and 32/32/32 inception before removal.
  Pushed.
- `8c6a1955` — R0 developer oracles.  Mutation: audit/test infrastructure only;
  no compiler source or source-set change.  Added the 454-file legacy manifest,
  the contract-string allowlist, and 18-surface output oracle.  Both layout and
  output oracles rejected deliberate negative seeds; the file audit remained
  zero-fatal/33-warning and the LowIR audit passed 124/99.  Pushed.
- `cf23f305` — R2 support and preprocessing paths.  Moved generic support into
  `support/` and token, expression, macro, hosted, and pipeline sources into
  `preprocess/`; identifiers and translation-unit order were unchanged.
  Earliest assignment PA1; all 18 output surfaces were exact, PA1-PA8 passed
  401/401, and the cumulative report passed 5471/5471.  Seven binaries remained
  byte-identical; the other seven differed only because `__FILE__` basenames
  changed from `hosted_builtin_registry.cpp`/`hosted_preprocessor_probes.cpp`
  to their responsibility names.  Layout counts, zero-fatal/33-warning file
  audit, and LowIR 124/99 audit were unchanged; 32/32/32 inception matched.
  Performance: not applicable to this non-hot path-only batch.  Pushed.
- `c1166c55` — R2 CY86 paths.  Moved the model, frontend, backend, and internal
  headers into `cy86/`; identifiers and link order were unchanged.  Earliest
  assignment PA9; PA9 passed 20/20, all 18 output surfaces were exact, and all
  affected binaries were byte-identical.  The layout audit was unchanged and
  32/32/32 inception matched.  Performance: not applicable to this path-only
  batch.  Pushed.
- `b30a4397` — R2 Itanium ABI paths.  Moved the mangling model, graph,
  substitution, vocabulary, and rendering sources into `abi/itanium/`;
  identifiers and link order were unchanged.  Earliest assignment PA14; PA14
  passed 117/117, all 18 output surfaces and affected binaries were exact, and
  32/32/32 inception matched.  The closing R2 cumulative report passed
  5471/5471; layout and LowIR audits passed and the file audit remained
  zero-fatal/33-warning.  Performance: not applicable to this path-only batch.
  Push: with this ledger checkpoint.

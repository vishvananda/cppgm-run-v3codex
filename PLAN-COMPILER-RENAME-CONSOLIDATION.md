# Plan: Compiler Naming and Ownership Consolidation

Status: complete

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
| `pa30_object*` | `compiler_object/model`, `serialization`, and `linker` |
| `pa30_elf_object*` | `compiler_object/elf_import` |
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
| `pa30_object.{h,cpp}` | `compiler_object/model.h`, `serialization.{h,cpp}`, and `linker.{h,cpp}` |
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
- `e6e1877a` — R3 recognition paths.  Moved the embedded grammar and recognizer
  to `recognition/grammar_definition.h` and `recognizer.{h,cpp}` without symbol
  changes.  PA6 passed 48/48 and all 18 output surfaces were exact.  The only
  binary-content change was the embedded `__FILE__` basename
  `pa6_recognizer.cpp` becoming `recognizer.cpp`; 32/32/32 inception matched.
  Pushed.
- `6f1c3782` — R3 recognition grammar identifiers.  Renamed the embedded
  grammar definition and accessor and removed the PA-bearing raw-string
  delimiter.  PA6 and all 18 output surfaces were exact.  The binary delta was
  confined to local symbol names and the resulting build ID; after stripping
  symbols and build ID it was byte-identical.  Pushed.
- `2b807e0a` — R3 recognition namespace/API completion.  Moved the public API
  and private recognizer into `cppgm::recognition`, and renamed `Stats` and the
  private `Recognizer`.  PA6 and all output surfaces were exact; allocated
  changes were limited to the expected private token-sink RTTI spelling.  The
  cumulative report passed 5471/5471 and all audits retained their baselines.
  Pushed.
- `fd9c6db4` — R3 namespace-semantics paths.  Moved PA7 into
  `namespace_semantics/analysis.{h,cpp}` with unchanged symbols and link order.
  PA7 passed 43/43, all output surfaces were exact, and the binary change was
  the `pa7_semantic.cpp` to `analysis.cpp` basename only; 32/32/32 inception
  matched.  Pushed.
- `7736b80f` — R3 namespace-semantics names.  Moved the stage into
  `cppgm::namespace_semantics` and renamed its `Stats`, `TranslationUnit`,
  private `Program`, and private `Parser`.  PA7 and all output surfaces were
  exact; binary deltas were the expected internal manglings and token-sink
  RTTI.  The cumulative report passed 5471/5471 and all audits retained their
  baselines.  Pushed.
- `3fcb5662` — R3 namespace-initialization paths.  Moved the driver, types,
  internal model, program interface, model implementation, and parser into
  `namespace_initialization/`.  PA8 passed 67/67 and all output surfaces were
  exact; binary changes were exactly the three renamed source basenames.
  32/32/32 inception matched.  Pushed.
- `f6ced201` — R3 namespace-initialization names.  Replaced `cppgm::pa8` with
  `cppgm::namespace_initialization` and renamed public `Stats`/`Program`,
  internal `Model`, and `TranslationUnitParser`.  PA8 and all output surfaces
  were exact; binary deltas were internal/API manglings and token-sink RTTI.
  The closing R3 report passed 5471/5471, LowIR remained 124/99, the file audit
  remained zero-fatal/33-warning, and fresh 32/32/32 inception matched.  Layout
  counts fell from 215/428/426/72 to 203/415/421/68.  Performance: not
  applicable to these independent early-tool path/name batches.  Push: with
  this ledger checkpoint.
- `caf2c41a` — R4 syntax paths.  Moved the syntax arena, tags, parser
  machinery, driver, statistics, and extension mixins under `syntax/model/`,
  `syntax/parser/`, and `syntax/extensions/` without changing source-set or
  link order.  PA10, PA25, PA30, PA32, and PA34 passed 933/933; all 18 output
  surfaces and explicit-O1 32/32/32 inception were exact.  The GCC-O3 compiler
  retained 7,061,845 text bytes, and every section except symbol/string tables
  and the path-derived build ID was byte-identical.  Pushed with the R4
  checkpoint.
- `d6b203ab` — R4 syntax namespace/API completion.  Collapsed the five
  PA-numbered syntax namespaces into `cppgm::syntax`; renamed the public
  statistics and translation-unit entry points and the private parser; and
  updated semantic and driver consumers atomically after a collision audit.
  The closing report passed 5471/5471, the LowIR audit remained 124/99, the
  file audit remained zero-fatal/33-warning, the layout census fell to
  179/365/374/68, and all 18 output surfaces and 32/32/32 inception were exact.

  A three-block identical-current-source parser A/B measured prior/current
  wall medians of 3.015/2.990 seconds and user medians of 2.900/2.880 seconds.
  The full-corpus prior/current guard was tied in aggregate CPU (920.97/921.70
  seconds, +0.08%); its wall median moved 32.33/32.62 seconds with pairs in
  both directions.  Fully optimized frozen `-O0` guards improved GCC-O3
  4.610/4.580 seconds (-0.97% paired) and Clang-O3 4.785/4.775 seconds
  (-0.11% paired), with exact objects.

  The first corrected matrix accidentally selected the PA39 default-O3 self
  producer and is discarded.  With source-matched O1 producers, six
  self/GCC lanes and three Clang lanes measured 31.805/21.170/21.650-second
  wall medians, or 1.502x self/GCC and 1.469x self/Clang; median aggregate CPU
  was 903.80/589.58/604.87 seconds.  The self/GCC result is 0.24% above the
  inherited 1.499x checkpoint, inside the 0.5% retention floor but not yet the
  final `<= 1.50x` exit gate, so that gate remains explicitly open for R12.
  Pushed with this ledger checkpoint.
- `990ac6a8` and `27cf5542` — R5 semantic paths.  Moved the PA11 model and
  every PA12/later semantic implementation into the responsibility-specific
  `semantic/` hierarchy while preserving the expanded source list and link
  order.  PA11/PA12 and explicit-O1 32/32/32 inception passed after the first
  group; the completed path set passed 5471/5471, all 18 output surfaces,
  LowIR 124/99, and the zero-fatal/33-warning file audit.  The GCC-O3
  compiler retained 7,061,813 text bytes and every meaningful allocated
  section was exact against the R4 checkpoint.  Pushed with the namespace
  checkpoint.
- `6b176c40` — R5 semantic namespace and API completion.  Atomically
  collapsed `pa11`, `pa12_semantic_detail`, `pa25_semantic_detail`, and the
  three presentation namespace families into `cppgm::semantic` and
  `cppgm::semantic::presentation`, without aliases.  Renamed the analyzer,
  graph storage, statistics, type-view, and semantic entry APIs.  PA11,
  PA12, PA15, and PA34 passed 752/752; the cumulative report passed
  5471/5471; all 18 outputs and fresh explicit-O1 32/32/32 inception were
  exact; LowIR remained 124/99; and the file audit remained
  zero-fatal/33-warning.  The layout namespace census fell from 374 to 154.

  Six corrected current-revision lanes measured self/GCC wall medians of
  31.95/20.99 seconds and three Clang lanes measured 21.54 seconds, or
  1.522x/1.483x.  Median aggregate CPU was 906.79/590.46/606.15 seconds.
  The preserved pre-namespace source and matching producers measured
  32.56/21.02 seconds wall and 913.79/591.87 seconds aggregate CPU, or
  1.549x/1.544x: the rename therefore improves the matched-revision wall and
  CPU ratios by 1.7% and 0.5%, respectively, and reduces absolute self CPU by
  0.77%.  The global `<= 1.50x` wall gate remains open rather than being
  hidden by this machine window.

  A reverse identical-current-source confirmation was +0.25% aggregate CPU;
  the combined direct set was +0.57% only because two renamed lanes carried
  visible system-load spikes.  Fully optimized frozen objects were exact in
  every lane.  Six-lane GCC-O3 medians were 4.615/4.635 seconds baseline/new
  wall and 4.095/4.115 user (+0.43%/+0.49%); three-lane Clang-O3 medians were
  4.92/4.90 seconds.  GCC/Clang compiler text fell 55/40 bytes, with code-size
  stability and deltas explained by semantic manglings, RTTI/unwind
  references, string tables, and build ID.  Pushed.
- `a7537498` — R5 overload-resolution path correction.  The ownership census
  found the remaining 34-method semantic implementation at
  `pa16_operator_resolution.cpp`; moved it to
  `semantic/expressions/overload_resolution.cpp` and gave it an explicit
  semantic source-set owner at the same link position.  PA16 passed 300/300,
  all 18 output surfaces were exact, and every meaningful allocated compiler
  section was byte-identical.  Push: with this ledger checkpoint.
- `0aebe1eb` — R5 semantic symbol-owner manifest.  Added an audited inventory
  of all 850 out-of-line `semantic::Analyzer` definitions, keyed by path,
  method, and overload ordinal.  The starting audit records 694 methods with
  coherent owners and queues the 156 definitions in seven mixed owners for
  explicit R6 routing; unrecorded or stale cross-file moves now fail
  `make audit-semantic-owners`.  Compiler mutation: none.  Push: with this
  ledger checkpoint.
- `6baafb5f` — R6 semantic name-resolution ownership.  Renamed the remaining
  generic `semantic/model/names.cpp` owner to
  `semantic/analysis/name_resolution.cpp` at the same source-set and link
  position, and closed all nine methods in the ownership manifest.  PA12
  passed 184/184, all 18 output surfaces were exact, and every meaningful
  allocated GCC-O3 compiler section was byte-identical.  The semantic audit
  now reports 147 methods still queued for explicit R6 disposition.
- **Rejected R6 declarator body transfer.**  A textually exact 650-line family
  (`BuildSpecifiers`, `BuildTypeId`, declarator-name access, parameter
  construction, and `BuildDeclarator`) was moved from `declarations.cpp` into
  the existing `declarator_types.cpp` owner without a source-count change.
  PA12, all 18 output surfaces, LowIR 124/99, the layout/file audits, and fresh
  explicit-O1 32/32/32 inception all passed; every inception object and the
  final compiler matched.  Fully optimized frozen `-O0` objects were exact,
  with GCC-O3 medians 4.60/4.57 seconds and Clang-O3 4.87/4.83 seconds.

  The required extended identical-current-source A/B nevertheless rejected
  the layout.  Across six lanes per producer, old/new wall medians were
  31.445/31.995 seconds (+1.75%); five of six adjacent comparisons favored
  the old layout.  Aggregate-CPU medians were nearly flat at 903.32/904.51
  seconds (+0.13%), confirming a layout/scaling penalty rather than extra
  semantic work.  The same-revision candidate oracle improved relative to the
  R5 checkpoint (self/GCC 1.516x wall and 1.529x aggregate CPU; self/Clang
  1.452x/1.482x), but a favorable denominator does not excuse the repeatable
  absolute wall regression.  The move and its manifest changes were reverted
  exactly; R6 must preserve this declaration cluster or use a layout-preserving
  organization instead.
- `b89dfc02` — R6 semantic presentation owner.  Moved the intact
  `semantic/render.cpp` translation unit to `semantic/presentation/render.cpp`
  at the same source-set position and closed its two rendering methods in the
  owner manifest.  PA12 passed 184/184, all 18 output surfaces were exact, and
  the old/new render objects were byte-identical.
- `32745c32` — R6 declared-entity ownership.  Moved the intact late-linked
  `semantic/model/entity_ownership.cpp` translation unit to
  `semantic/declarations/entity_ownership.cpp`, retaining its link position
  and all five mutually coupled identity/injected-storage methods.  PA12
  passed 184/184, all 18 output surfaces were exact, and the old/new objects
  were byte-identical.
- `8b197d98` — R6 declaration-analysis owner.  Renamed the intact mixed owner
  from redundant `semantic/declarations/declarations.cpp` to
  `semantic/declarations/analysis.cpp` and recorded all 33 declaration,
  declarator, class, function, using, and demand methods as one deliberately
  retained private dependency cluster.  PA12 passed 184/184, all 18 output
  surfaces were exact, and stripped old/new objects were byte-identical; the
  full object delta is only the source basename in its symbol table.

  A second layout-preserving attempt extracted the seven declarator methods
  into a responsibility-named implementation fragment included at their
  original token position.  It produced a byte-identical object, but the file
  audit correctly rejected the inline include-after-code pattern as a fatal
  artificial split.  The fragment and proposed audit changes were reverted
  exactly rather than weakening the guard.  After the earlier cross-TU timing
  rejection and this structural rejection, the honest final owner is the
  intact declaration-analysis translation unit.  The semantic audit is now
  850 definitions with 107 queued for the remaining R6 families.  The
  three-increment cumulative gate passed 5471/5471; LowIR remained 124/99,
  and the compiler-layout and zero-fatal/33-warning file audits retained their
  baselines.  Push: with this ledger checkpoint.
- `e191d577` — R6 initialization-analysis owner.  Renamed the intact
  `semantic/initialization/initialization.cpp` translation unit to
  `semantic/initialization/analysis.cpp` at the same link position and closed
  its 39 construction, initialization, return, allocation, temporary, and
  cleanup methods as one scratch-state pipeline.  PA12 and PA16 passed
  484/484, all 18 output surfaces were exact, and stripped old/new objects
  were byte-identical.  The file/layout audits retained their baselines.
- `5f2833dc` — **Rejected R6 template translation-unit split; combined owner
  retained.**
  Split `semantic/templates/aliases_and_lambdas.cpp` into adjacent
  `lambda_runtime.cpp` and `alias_analysis.cpp` owners in original public
  definition order.  The source count rose from 216 to 217.  PA12, PA18,
  PA19, and PA25 passed 669/669, all 18 output surfaces were exact, both audits
  passed, and fresh explicit-O1 32/32/32 inception matched every one of 217
  objects and the final compiler.

  The split was not performance-inert.  GCC-O3 compiler text grew 1,568 bytes.
  Six identical-current-source lanes per self producer measured old/new wall
  medians of 32.21/32.405 seconds (+0.61%) and aggregate CPU of
  919.03/918.45 seconds (-0.06%), with mixed pair directions.  The corrected
  same-revision wall ratio improved only from 1.522x to 1.519x and remained
  above the gate, while aggregate-CPU ratio worsened from 1.536x to 1.552x
  because the duplicated analyzer header added source work disproportionately
  to self-host.  Exact optimized-host frozen medians were GCC-O3 4.54/4.56
  seconds (+0.44%) and Clang-O3 4.79/4.77 seconds.  The files, source set, and
  manifest routes were reverted exactly; all 27 methods are deliberately kept
  in the accurately named combined owner rather than paying this repeated
  parse/code-layout cost.  The semantic audit is now 850 definitions with 41
  core-analysis methods still queued.
- `1d002c38` — R6 core analyzer ownership.  The final 41 queued definitions
  are the deliberately cohesive `semantic/analysis/analyzer.cpp` core: syntax
  access, type/conversion primitives, central expression/declaration/statement
  dispatch, rendering traversal, and the translation-unit `Consume` entry.
  The owner already matches the class and responsibility; no source mutation
  or artificial split was made after the measured declaration and template
  failures.  The audited manifest is closed at 850 definitions / 0 queued.

  The R6 closing gate passed 5471/5471, all 18 output surfaces, LowIR 124/99,
  the unchanged compiler-layout audit, and the zero-fatal/33-warning file
  audit.  Fresh explicit-O1 inception from a 32/32/32 root matched all 216
  current objects and the final compiler in 49.80 seconds wall, 1,402.93
  seconds aggregate CPU, and 232,224 KiB peak RSS.  Every retained R6 source
  mutation is path-only with either a byte-identical object or a basename-only
  symbol-table delta; the only cross-TU/source-count candidates were rejected.
  Therefore the R5 corrected performance checkpoint remains the retained code
  baseline and the global `<= 1.50x` wall gate stays open for R12.  R6 is
  complete.  Push: with this ledger checkpoint.
- `dc96c3e4` — R7 typed lowering-IR paths.  Moved the internal typed `types`,
  `model`, and `render` files from five `pa15_lowir_*` paths into
  `lowering/ir/` without changing symbols, source count, or link order.
  PA15 passed 121/121, all 18 output surfaces were exact, GCC-O3 compiler
  text/data were unchanged, and stripped old/new model and render objects were
  byte-identical.  The layout census fell from 70/172 to 65/123 paths/includes;
  file audit remained zero-fatal/33-warning.
- `7aba34e7` — R7 typed lowering-IR namespace.  Atomically migrated
  `cppgm::pa15_lowir_detail` to `cppgm::lowering::ir` across the representation
  and every lowering/object consumer, with no compatibility alias.  PA15 and
  PA18 passed 158/158, all 18 outputs were exact, GCC-O3 text/data remained
  unchanged, and the namespace census fell from 154 to 98.

  The first explicit-O1 inception exposed one C++11 issue hidden by GCC's
  extension acceptance: a forward declaration had become the C++17 spelling
  `namespace lowering::ir`.  It was corrected to explicit nested namespace
  blocks; no test or source contract was changed.  The fresh 32/32/32 rerun
  then matched all 216 objects and the final compiler in 50.13 seconds wall.
  Six identical-current-source lanes per producer measured old/new wall
  medians 31.925/31.895 seconds (-0.09%) and aggregate CPU 912.03/910.14
  seconds (-0.21%).  Exact optimized-host frozen medians were GCC-O3
  4.65/4.61 seconds and Clang-O3 4.87/4.88 seconds.

  The corrected same-revision candidate matrix measured self/GCC/Clang wall
  medians of 31.895/21.57/21.57 seconds, or 1.479x in both comparisons;
  aggregate CPU medians were 910.14/592.90/609.09 seconds, or 1.535x/1.494x.
  Thus the wall target is below 1.50x in this retained window, while R12 still
  must reconfirm the final tree.  The cumulative report passed 5471/5471,
  LowIR remained 124/99, semantic ownership remained 850/0, and layout/file
  audits retained their baselines.  Pushed with this ledger checkpoint.
- `16552a00` — R7 lowering foundation paths.  Moved the public API, central
  program lowerer, driver, graph entry, source-type conversion, and shared
  utilities into `lowering/`, `lowering/types/`, and `lowering/support/` while
  preserving source count and link order.  PA15 passed 121/121, all 18 outputs
  were exact, GCC-O3 text/data were unchanged, and all four moved
  implementation objects were allocated-byte exact.
- `8bd0087e` — R7 source-lowering ABI path.  Moved the intact Itanium lowering
  ABI owner to `lowering/abi/itanium.{h,cpp}` at its original link position.
  PA15 passed 121/121, all 18 outputs were exact, and the moved implementation
  object was allocated-byte exact.  One discarded verification attempt
  accidentally ran two writers against the shared `obj/dev` tree and produced
  only `.Td`/generated-config temp-file races; the required serialized rebuild
  and every subsequent check were clean.
- `dc183daf` — R7 typed analysis/transform/presentation paths.  Moved force
  inlining, function reachability, and local-name presentation into
  `lowering/transforms/`, `lowering/analysis/`, and
  `lowering/presentation/`, preserving their separated late link positions.
  PA15 passed 121/121, all 18 outputs were exact, and all three implementation
  objects were allocated-byte exact.

  Across the three path-only commits, legacy PA paths/includes fell from
  65/123 to 49/51 with namespace/identifier counts unchanged.  The cumulative
  report passed 5471/5471, LowIR remained 124/99, semantic ownership remained
  850/0, and file audit stayed zero-fatal/33-warning.  Performance: not
  applicable because all moved implementation code and aggregate compiler
  text/data are exact.  Push: with this ledger checkpoint.
- `93be5a4e` — R7 control and expression paths.  Moved the control-flow,
  conditional, control-expression, scalar-unary, assignment, member-address,
  and bit-field CRTP mixins into `lowering/control/` and
  `lowering/expressions/`.  Their include order, namespaces, bodies, and inline
  boundaries were unchanged.  PA15 passed 121/121, all 18 output surfaces
  were exact, and the compiler executable was byte-identical.
- `9791ae4b` — R7 call-boundary paths.  Moved argument marshaling,
  constructor-call handling, and value-boundary handling into
  `lowering/calls/` without changing the central lowerer's composition.  PA17
  passed 247/247, all 18 output surfaces were exact, and the compiler
  executable was byte-identical.
- `75dad567` — R7 lifetime, initialization, and storage paths.  Grouped the
  aggregate, array, destructor, general lifetime, special-member, temporary,
  local-static, and static-storage CRTP mixins under `lowering/lifetime/`, with
  initialization actions and slot planning in their explicit domains.  PA33
  passed 97/97, all 18 output surfaces were exact, and the compiler executable
  was byte-identical.  Removing the last PA17 and PA33 paths made four legacy
  layout allowlist rows stale; only those obsolete path/include rows were
  removed.

  The scheduled cumulative report passed 5471/5471; LowIR remained 124/99,
  semantic ownership remained 850/0, and the file audit remained
  zero-fatal/33-warning.  Legacy path/include counts fell from 49/51 to 29/31,
  while namespace/identifier counts remained 98/68.  Performance testing is
  not applicable to these path-only increments because every rebuilt compiler
  was byte-identical to its predecessor.  Push: with this ledger checkpoint.
- `c46ef022` — R7 lifetime-support implementation paths.  Moved cleanup
  continuations, static-storage initialization, and zero filling into their
  lifetime/initialization owners while preserving source count and link order.
  PA16 passed 300/300, all 18 output surfaces were exact, and every allocated
  section in all three moved implementation objects was byte-identical.
- `6a936f5c` — R7 object-model lowering paths.  Grouped static members,
  polymorphism, RTTI, member pointers, and virtual bases under
  `lowering/objects/`; named the retained smaller polymorphism translation unit
  `vtable_thunks.cpp` for its actual owner.  PA28 passed 45/45, all 18 output
  surfaces were exact, and the polymorphism and thunk objects' allocated
  sections were byte-identical.
- `4da083eb` — R7 constant and extension paths.  Moved constant values and
  templates into `lowering/constants/`, and range-for, exceptions, initializer
  lists, regions, GNU asm, and complex lowering into `lowering/extensions/`.
  PA34 passed 375/375, all 18 output surfaces were exact, and the moved
  constant-template object's allocated sections were byte-identical.

  The scheduled cumulative report passed 5471/5471; LowIR remained 124/99,
  semantic ownership remained 850/0, and file audit remained
  zero-fatal/33-warning.  Legacy path/include counts fell from 29/31 to 6/6;
  the only remaining paths are the three PA30 compiler-object modules reserved
  for R9.  Namespace/identifier counts remained 98/68.  Fresh explicit-O1
  inception with a new 32/32/32 object root matched all 216 current objects and
  the final compiler.  Performance is unchanged because every moved
  implementation object's allocated sections are exact.  Push: with this
  ledger checkpoint.
- `de45c72a` — R7 lowering foundation namespaces.  Migrated support, Itanium
  ABI lowering, local presentation, reachability, and typed force-inline policy
  to their final `cppgm::lowering::*` namespaces with explicit C++11 nesting
  and no aliases.  Section sizes stayed exact; expected mangling-driven text
  and rodata layout changed.  PA15, all 18 output surfaces, the 5471/5471
  cumulative report, and fresh 216-object explicit-O1 inception passed.

  Six identical-current-source lanes per self producer measured old/new hot-TU
  wall medians 7.575/7.625 seconds; paired wall and user changes were +0.20%
  and +0.28%.  Frozen optimized-host paired changes were GCC-O3 -0.33% wall
  and tied user, and Clang-O3 +0.31% wall / -0.12% user.  All results are within
  the measurement floor, so the namespace batch is retained.
- `73907b00` — R7 standalone service namespaces.  Migrated cleanup
  continuations, zero initialization, and constant-template pooling to
  `lowering::cleanup`, `lowering::zero_initialization`, and
  `lowering::constant_pool`.  PA21 passed 149/149 and all 18 output surfaces
  were exact.
- `511c11a0` — R7 consolidated lowering implementation namespace.  Merged all
  twelve PA15-PA34 CRTP detail namespaces into `cppgm::lowering`, renamed the
  central `GraphLowerer` to `ProgramLowerer`, and named the lowering driver
  consumer `lowering::SemanticGraphConsumer`.  The only declaration collision
  was the intended consumer/base leaf name; explicit namespace qualification
  resolved it without an alias or weaker name.  PA34 passed 375/375 and all 18
  output surfaces were exact.

  The scheduled cumulative report passed 5471/5471; LowIR remained 124/99,
  semantic ownership remained 850/0, file audit remained zero-fatal/33-warning,
  and fresh explicit-O1 32/32/32 inception matched all 216 objects and the
  final compiler.  Legacy namespace declarations fell from 59 after the
  foundation batch to 4, all belonging to the PA30 compiler-object namespace
  reserved for R9.

  Six identical-current-source lanes per self producer measured the service
  plus CRTP merge at old/new wall medians 7.520/7.535 seconds; paired wall and
  user changes were +0.07% and +0.14%.  Current hot-TU self/GCC/Clang medians
  were 7.585/4.760/4.605 seconds.  Those single-TU ratios are recorded as local
  diagnostics, not substituted for the plan's 216-source parallel-build exit
  oracle.  Frozen optimized-host paired changes were GCC-O3 -0.64% wall /
  -0.24% user and Clang-O3 -0.31% wall / -0.69% user.  The namespace merge is
  therefore performance-neutral.  Push: with this ledger checkpoint.
- `70b034ee` — R7 lowering public vocabulary.  Replaced the last historical
  typed-lowering API names with `lowering::Source`, `lowering::Stats`,
  `lowering::ir::Program`, `lowering::BuildProgram`, and
  `lowering::WriteLowIR`; named the graph and rendering boundaries
  `lowering::LowerGraph` and `lowering::ir::RenderLowIR`.  The IR-program name
  exposed ambiguous imported `Program` leaves, so every affected source-model
  boundary now explicitly says `semantic::Program` rather than relying on a
  using-directive lookup accident.  No alias or forwarding API remains.
- `687610f4` — R7 lowering include guards.  Replaced all 25 PA-numbered
  lowering guards with path-derived `CPPGM_LOWERING_*` guards.  Rebuilding the
  hot lowering translation unit left the compiler SHA-256 exactly
  `5c60a215cafd4533c50caeceb49f7cd10e0732ed3e5f25602368741256512211`;
  the layout identifier census fell from 68 to 18, with the remaining
  production identifiers owned by the PA30 compiler-object phase.

  PA15 passed 121/121, PA34 passed 375/375, and all 18 frozen output surfaces
  were exact.  The scheduled cumulative report passed 5471/5471; LowIR
  remained 124/99, semantic ownership remained 850/0, and file audit remained
  zero-fatal/33-warning.  Fresh explicit-O1 32/32/32 inception matched all 216
  objects and the final compiler.  Old/new self compilers had identical
  allocated section sizes; six identical-current-source lanes measured hot-TU
  medians of 7.680/7.620 seconds, with paired wall/user changes of -0.20% and
  -0.55%.

  The corrected three-lane 216-source matrix measured self/GCC/Clang wall
  medians of 31.64/21.48/21.93 seconds, or 1.473x/1.443x.  Aggregate-CPU
  medians were 907.95/590.66/607.91 seconds, or 1.537x/1.494x, effectively
  unchanged from the earlier accepted matrix.  Across the extended reversed-
  order GCC-O3 frozen samples, old/new wall and aggregate-CPU medians changed
  by +0.22%/+0.22%; Clang-O3 changed by +0.41%/+0.41%.  Both are within the
  0.5% measurement floor, so the vocabulary batch is retained.  R7 exits with
  generated LowIR exact, all lowering behavior gates clean, and only the PA30
  namespaces and paths reserved for R9 still present.  Push: with this ledger
  checkpoint.
- `96ee005b`, `6bfeaad1`, and this commit — R8 lowering ownership paths,
  first batch.  Grouped the program driver, graph bridge, source-type support,
  and reachability analysis under `lowering/core/`; moved exception and region
  lowering from the extension bucket into `lowering/control/`; and placed
  conditional and short-circuit logical expression lowering under
  `lowering/expressions/`.  Source-set and link order did not change.  The four
  moved implementation objects retained exact allocated non-`NOBITS` section
  contents, and both header-only regroupings rebuilt the compiler to the exact
  pre-move SHA-256
  `a1338c6eefc576cc7f26cc3be4ffe8a8564ed3fc3e114c170c2dcc02405a355d`.

  Focused PA15, PA17, PA26, PA34, and PA37 gates passed, and all 18 frozen
  output surfaces remained exact after every increment.  The three-commit
  checkpoint passed 5471/5471; LowIR remained 124/99, semantic ownership
  remained 850/0, the compiler-layout census remained 6/6/4/18, and the file
  audit remained zero-fatal/33-warning.  Performance measurement is not
  applicable to this batch because executable bytes or every meaningful
  allocated object section were exact.  Push: with this ledger checkpoint.
- `cf7c0acd`, `99ac8e66`, and this commit — R8 call, storage, and lifetime
  ownership paths.  Placed special-member and destructor invocation lowering
  with `lowering/calls/`; grouped slot planning, initialization, array and
  aggregate lifetime, temporary cleanup, local/static lifetime, and cleanup
  continuations under `lowering/objects/`.  The obsolete `lowering/lifetime/`,
  `lowering/initialization/`, and `lowering/storage/` divisions are empty and
  no production include refers to them.  Source-set and link order did not
  change.

  Header-only moves left the compiler exact within each compiled-object batch.
  The static-initialization, zero-initialization, and cleanup-continuation
  objects retained 17/17, 4/4, and 11/11 exact allocated non-`NOBITS` sections.
  Focused PA16, PA18, PA21, PA32, and PA34 gates passed, all 18 frozen output
  surfaces remained exact, and the three-commit checkpoint passed 5471/5471.
  LowIR remained 124/99, semantic ownership remained 850/0, the layout census
  remained 6/6/4/18, and the file audit improved from 33 to 32 warnings after
  the vague `aggregate_helpers` leaf acquired a responsibility name.
  Performance measurement is not applicable because the executable or every
  meaningful allocated moved-object section was exact.  Push: with this
  ledger checkpoint.
- `b2a17a1d`, `e1e44dc2`, and this commit — R8 lowering support dissolution.
  Split the catch-all header into narrow inline sequences and identity maps;
  moved emission-name presentation to `lowering/presentation`, byte counting
  into the lowering driver, scalar and literal interpretation beside source
  types, storage facts beside object lowering, and symbol normalization beside
  ABI lowering.  Renamed `PresentationNameMap` to the responsibility-specific
  `presentation::EmissionNameMap` and `SanitizeSymbol` to
  `abi::NormalizeSymbolName`.  Removed the unused internal
  `DecodeStringLiteral` entry instead of carrying a compatibility API.  No
  `lowering/support/utilities` file, include, or symbol remains.

  Focused PA15, PA16, PA18, PA21, and PA34 gates passed and all 18 frozen
  output surfaces were exact.  The three-commit checkpoint passed 5471/5471;
  LowIR remained 124/99, semantic ownership remained 850/0, the layout census
  remained 6/6/4/18, and the file audit remained zero-fatal/32-warning.  The
  optimized-host compiler text shrank by 920 bytes.  Across 12 samples per
  compiler in both label orders, old/candidate frozen wall medians tied at
  4.645 seconds and user medians were 4.150/4.155 seconds (+0.12%).  Fresh
  explicit-O1 32/32/32 inception matched all 217 current objects (216 shared
  plus the compiler runner) and the final compiler exactly.  Push: with this
  ledger checkpoint.
- `5c491c7e`, `394b206f`, and this commit — R8 lowering method-owner audit and
  first repairs.  Moved complete conditional-expression lowering into the
  existing conditional CRTP owner and complete call assembly into the new
  `calls/FunctionCallLowering` owner, preserving inline composition and direct
  calls.  Added `make audit-lowering-owners` and a checked-in manifest covering
  all 96 lowering class/struct definitions and all 92 methods still owned by
  the central `ProgramLowerer`; it queues 47 explicit storage, expression,
  control, call, and constant routes for the rest of R8.

  The conditional move rebuilt the compiler byte-for-byte.  The call move
  changed its private method mangling and added 64 bytes of text/alignment;
  PA15, PA17, PA18, and PA34 gates and all 18 frozen output surfaces remained
  exact.  Six optimized-host samples per side measured old/candidate wall
  medians of 4.635/4.655 seconds and tied user medians at 4.130 seconds; the
  interleaved paired changes were 0.00% wall and -0.24% user, so the layout is
  neutral.  The three-commit checkpoint passed 5471/5471, LowIR 124/99,
  semantic ownership 850/0, lowering ownership 188/47, layout 6/6/4/18, and
  zero-fatal/32-warning file audit.  Source IDs did not change; fresh inception
  remains scheduled for the completed lowering tier.  Push: with this ledger
  checkpoint.
- `bd5f619b`, `41e8bf06`, and this commit — R8 small method-owner repairs.
  Moved the direct-call instruction factory into `FunctionCallLowering`, and
  moved constant-array template copying plus floating-literal operand creation
  into `ConstantLowering`.  The first two moves rebuilt the compiler exactly.
  The floating helper retained its code address and all allocated sizes; only
  its private symbol-table name changed from the central class to its CRTP
  owner.  The lowering manifest queue fell from 47 to 44 methods.

  Focused PA15, PA18, PA21, and PA34 gates passed and all 18 frozen output
  surfaces remained exact.  The already-benchmarked call-owner layout remains
  the only allocated compiler delta, so no new timing delta is possible from
  this batch.  The three-commit checkpoint passed 5471/5471, LowIR 124/99,
  semantic ownership 850/0, lowering ownership 185/44, layout 6/6/4/18, and
  zero-fatal/32-warning file audit.  Push: with this ledger checkpoint.
- `2b9c6e47`, `a12366b7`, and this commit — R8 value and statement ownership.
  Moved storage access and scalar-expression routing out of the central
  `ProgramLowerer` into named CRTP owners.  Consolidated destructor handling
  with special-member calls and statement scheduling with exception statements,
  removing two arbitrary header divisions while keeping the file-audit census
  at zero fatal findings and 32 warnings.  The lowering ownership manifest now
  covers 100 class/struct definitions and the 45 deliberately retained central
  methods, with no R8 method queued for repair.

  The expression/lifecycle increment was allocated-byte exact after local
  symbols and derived build-id data were removed.  The statement increment
  reduced optimized-host compiler text by 180 bytes.  Across both A/B label
  orders (12 samples per compiler), old/candidate frozen medians were
  4.580/4.595 seconds wall (+0.33%), 4.090/4.070 seconds user (-0.49%), and
  369818/369390 KiB RSS (-0.12%), so the layout change is neutral.  Focused
  PA15, PA16, PA17, PA18, PA21, PA26, and PA34 gates passed, all 18 output
  surfaces remained exact, and the cumulative checkpoint passed 5471/5471.
  LowIR remained 124/99, semantic ownership remained 850/0, and the layout
  census remained 6/6/4/18.  Fresh inception remains scheduled for the
  completed lowering tier.  Push: with this ledger checkpoint.
- `16b21bd7` — R8 ABI ownership and lowering-tier closure.  Renamed the
  retained Itanium fact-builder core to `lowering/abi/mangling` and extracted
  independent emission-policy and runtime-symbol-metadata translation units
  with narrow public headers.  The shared 2,500-line mangling core remains
  intact because its type, function, variable, and local-name routes all depend
  on one private `AbiFactBuilder`; splitting it by size would create an
  artificial cross-file interface.  Extended the lowering owner audit to all
  13 public ABI functions.  It now passes with 100 class/struct definitions,
  13 ABI functions, 45 deliberate `ProgramLowerer` methods, and no queued R8
  repair.

  The first two link orders exposed a repeatable layout penalty and were not
  retained.  Keeping symbol metadata and emission policy adjacent to the
  lowering driver, followed by the mangling core, reduced optimized-host text
  from 7,060,722 to 7,059,254 bytes.  Across both final A/B label orders
  (12 samples per compiler), old/candidate frozen medians were 4.645/4.640
  seconds wall (-0.11%), 4.145/4.145 seconds user (tie), and
  368866/369446 KiB RSS (+0.16%).  Focused PA14, PA15, PA16, PA18, PA31, and
  PA34 gates passed and all 18 output surfaces remained exact.

  The tier-closing report passed 5471/5471; LowIR remained 124/99, semantic
  ownership remained 850/0, file audit remained zero-fatal/32-warning, and
  layout remained 6/6/4/18.  Fresh explicit-O1 32/32/32 inception matched all
  219 current objects and the final compiler exactly at SHA-256
  `75e9321418e67dc21671c39b0e7c16b652181150858680ece851bed759bb0d43`.
  R8 exits with no arbitrary lowering-generation split and no retained output
  or performance regression.  Push: with this amended closure checkpoint.
- `900fde85`, `2da200c0`, and this commit — R9 compiler-object paths and
  vocabulary.  Grouped the typed-LowIR backend adapter, private object format,
  and ELF import under `compiler_object/`; migrated the historical `pa30`
  namespace to `cppgm::compiler_object`; and replaced assignment-numbered
  object, serialization, linking, ELF-import, and backend-adaptation names with
  responsibility names.  No compatibility alias or forwarding API remains.
  The already-separated region syntax, semantics, and lowering owners were
  audited and require no R9 move.

  Every allocated non-`NOBITS` section of the stripped compiler remained exact
  through all three increments, including unchanged 7,059,254-byte text.
  Focused PA30, PA31, and PA32 gates passed and all 18 frozen output surfaces
  were exact.  The scheduled cumulative report passed 5471/5471; LowIR
  remained 124/99, semantic ownership remained 850/0, lowering ownership
  remained 100/13/45/0, and the file audit remained zero-fatal/32-warning.
  The layout census improved from 6/6/4/18 to 0 legacy paths, 0 legacy
  includes, 0 legacy namespaces, and 18 legacy identifiers.  Performance
  measurement is not applicable because all meaningful allocated executable
  contents were exact.  Push: with this amended ledger checkpoint.
- `59909bd5` and `ae0665ad` — R9 ownership split and tier closure.  Replaced
  the mixed object-format owner with `compiler_object/model.h`, dedicated
  serialization/deserialization and linker translation units, and narrow
  public headers.  Renamed the private binary and ELF readers to
  `BinaryWriter`, `BinaryReader`, and `ElfObjectReader`.  Moved the in-memory
  typed-LowIR adapter out of object ownership to
  `lowir/io/frontend_adapter` and `cppgm::lowir_io`; it does not render,
  serialize, or import objects.  Region syntax, semantic analysis, and
  lowering were confirmed already resident in their three pipeline owners.

  The split adds one translation unit and 26,256 bytes of optimized-host text
  (7,059,254 to 7,085,510), caused by heavy LowIR-model template emission in
  both owners; rodata and writable data sizes did not change.  Across both A/B
  label orders (12 samples per compiler), old/candidate frozen medians were
  4.655/4.640 seconds wall (-0.32%), 4.165/4.135 seconds user (-0.72%), and
  368692/369792 KiB RSS (+0.30%).  The later private-class and adapter
  namespace renames retained the same allocated compiler sizes, so the
  cross-TU layout is performance-neutral.

  Old/new two-source private objects were byte-exact at SHA-256
  `5e6aa5b4e29ad96e71f51d0f52dde78ea83703474852063369701ece201ded01`
  and `1b99d0a9e3c7d6dbb02f0105a3cae79ab79f935a00f745a3004aecc7b35ba3a8`;
  their linked executable was byte-exact at
  `87a1901891ea285b412c41152e5bfb26d1cfaf9deb99483729d53b3234e02a76`.
  Focused PA30-PA32, all 18 output surfaces, and the cumulative 5471/5471
  report passed.  LowIR remained 124/99, semantic ownership remained 850/0,
  lowering ownership remained 100/13/45/0, layout remained 0/0/0/18, and the
  file audit remained zero-fatal/32-warning.

  Fresh explicit-O1 32/32/32 inception matched all 218 shared source objects,
  the entry and runner objects (220 total), and the final compiler exactly at
  SHA-256
  `4c7d7fedb8c28feb1f0f3ae9f68ac6c8beb7cafe69c0242355f116db9df60015`.
  R9 exits with private serialization bytes, symbol/link order, foreign ELF
  import behavior, presentation policy, diagnostics, and performance intact.
  Push: with this amended tier-closure checkpoint.
- `94acca09`, `3e85a667`, and `e5bc895d` — R10 LowIR and native path
  checkpoint.  Grouped the LowIR model, I/O, analyses, optimizers, CY86
  conversion, and driver under their final domain directories, then removed
  redundant `lowir_` prefixes from the LowIR leaf names.  Grouped the native
  backend under `mir`, `analysis`, `lowering`, `allocation`, `frame`,
  `encoding`, `eh`, `object`, and `driver` while deliberately retaining its
  leaf names for the next, separately audited vocabulary increment.  PA29 and
  PA38 student documentation now points at the relocated optional MIR model
  and register scaffolds.  Source count and canonical link order are exact.

  The initial LowIR directory move and the native directory move left
  `cppgm++`, `lowir2cy86`, `lowiropt`, and `lowir2native` byte-for-byte exact.
  The LowIR leaf rename changed only path/symbol/build-id material: after
  stripping symbols and the GNU build-id, all four old/new executables were
  exact.  Their allocated sizes remained 7,085,510/21,712/6,792 bytes for
  `cppgm++`, 412,387/3,736/1,728 for `lowir2cy86`,
  1,127,673/3,904/1,736 for `lowiropt`, and
  1,400,829/4,864/2,496 for `lowir2native` (text/data/bss).  Performance
  measurement is not applicable because all meaningful executable contents
  are exact.

  Focused PA13, PA29-PA31, PA37, and PA38 gates passed, and all 18 frozen
  output surfaces remained exact.  The three-commit cumulative checkpoint
  passed 5471/5471; LowIR remained 124/99, semantic ownership remained 850/0,
  lowering ownership remained 100/13/45/0, layout remained 0/0/0/18, and the
  file audit remained zero-fatal/32-warning.  R10 remains open for native leaf
  and identifier vocabulary plus the explicit mixed-owner audit.  Push: with
  this amended checkpoint.
- `2b8fed43`, `5ec67431`, and this commit — R10 native leaf vocabulary
  checkpoint.  Removed the redundant `lowir_native_`, `mir_model`, and
  `x86_register_model` leaf prefixes from the MIR, analysis, driver,
  allocation, frame, encoding, EH, and object directories.  Ambiguous owners
  received responsibility names, including `mir/construction`,
  `mir/optimize`, `encoding/instructions`, `eh/host_regions`,
  `object/elf_format`, and `object/elf_writer`; simple domain-qualified names
  such as `frame/layout` and `driver/session` stayed short.  PA29 and PA38 now
  describe the optional scaffolds at `native/mir/model.h` and
  `native/mir/registers.h`.  Source count, translation-unit contents, and
  canonical link order are unchanged.

  Against the pre-rename native tools, all four executables are byte-exact
  after stripping symbols and the GNU build-id.  Allocated text/data/bss sizes
  remain 7,085,510/21,712/6,792 for `cppgm++`,
  412,387/3,736/1,728 for `lowir2cy86`, 1,127,673/3,904/1,736 for `lowiropt`,
  and 1,400,829/4,864/2,496 for `lowir2native`; therefore a performance run is
  not applicable to this mechanical batch.

  Focused PA29-PA31 and PA38 gates passed and all 18 frozen output surfaces
  remained exact.  The three-commit checkpoint passed 5471/5471; LowIR
  remained 124/99, semantic ownership remained 850/0, lowering ownership
  remained 100/13/45/0, layout remained 0/0/0/18, and the file audit remained
  zero-fatal/32-warning.  The lowering leaves and native mixed-owner manifest
  remain for the R10 closure.  Push: with this amended checkpoint.
- `4e242f24`, `375ebcb0`, and `c3d46ebc` — R10 native ownership and tier
  closure.  Removed the redundant prefix and lowering suffix from every
  operation-family leaf under `native/lowering`.  Added a native symbol-owner
  audit covering the mixed public interface and private hot lowerer; its 14
  initial routes all queued explicit R10 disposition.  The ownership increment
  then moved telemetry to `native/driver/stats.h`, relocatable-object data to
  `native/object/relocatable.h`, the public lowering session and
  `lower_program` to `native/driver/session.h`, ELF writer entrypoints to
  `native/object/elf_writer.h`, and the coherent private `FunctionLowerer` to
  `native/lowering/function.cpp`.  The existing implementations were already
  in the correct session and ELF writer translation units, so no function body
  crossed a translation-unit boundary.  The final owner audit passes with 14
  routed symbols and zero pending repair.  A provisional
  `native/object/model.h` name raised the file audit from 32 to 33 warnings;
  it was corrected within the increment to the specific `relocatable.h`
  owner, restoring the established warning count.

  Against the pre-native-vocabulary baseline, `cppgm++`, `lowir2cy86`,
  `lowiropt`, and `lowir2native` are all byte-exact after removing symbols and
  the GNU build-id.  Allocated text/data/bss sizes remain
  7,085,510/21,712/6,792, 412,387/3,736/1,728,
  1,127,673/3,904/1,736, and 1,400,829/4,864/2,496 respectively.  Source count
  and canonical link order are unchanged, and all 18 frozen output surfaces
  are exact.

  The corrected current-revision O1 matrix used 32-way object builds and
  balanced producer positions.  Six self/GCC lanes measured wall medians of
  32.780/21.855 seconds (1.499886x), aggregate CPU medians of
  916.955/598.470 seconds (1.532165x), and RSS medians of 231,714/230,704 KiB.
  Three Clang lanes measured 22.240 seconds wall and 615.110 seconds aggregate
  CPU, giving self/Clang ratios of 1.473921x/1.490717x.  Fully optimized
  current GCC-O3 and Clang-O3 compilers on the immutable frozen `-O0` workload
  produced deterministic objects at 4.580/4.810-second wall and
  4.070/4.315-second user medians, retaining the established optimized-host
  guard.

  The final report passed 5471/5471; LowIR remained 124/99, semantic ownership
  remained 850/0, lowering ownership remained 100/13/45/0, layout remained
  0/0/0/18, and the file audit remained zero-fatal/32-warning.  Fresh
  explicit-O1 inception with 32/32/32 settings matched all 218 shared source
  objects, the runner, the entry object, and the final compiler (220 total) in
  57.03 seconds wall and 1,450.50 seconds aggregate CPU.  The final self and
  inception compiler SHA-256 is
  `ce46c09b71a262fbfb6a8eeb20552a0fa35707fa02d90b76f3bbd3f0098a1ac6`.
  R10 exits with LowIR/native contracts, output, layout, and performance
  intact and the self/GCC wall ratio below 1.50x.  Push: with this amended
  tier-closure checkpoint.
- `79a55059` and this commit — R11 migration-residue closure.  Renamed the
  final two PA-bearing internal helpers and seven PA-numbered include guards,
  then removed the now-empty legacy layout exceptions.  The layout audit now
  reports zero production PA paths, includes, namespace declarations, and
  identifiers.  One generic mock comment was rewritten in architectural
  terms; the remaining assignment-number comments all describe a staged
  contract or compatibility boundary.

  Replaced the broad comment/error allowlist expressions with 109 exact
  kind/path/token entries: 12 stable diagnostic keys, 45 diagnostic-prose
  sites, and 52 assignment-boundary comment sites.  The audit observes and
  approves 12/123/60 occurrences respectively, rejects any unlisted site, and
  rejects stale entries.  A deliberate `PA99` comment was rejected.  Added
  `make audit-frontend-source-sets`; it independently expands all 47 ordered
  responsibility variables, rejects duplicates and missing/stale paths,
  cross-checks the 14 tools against `dev/Makefile`, and proves that all 229
  production translation units have an intended owner.  A deliberate
  duplicate source ID was rejected through every affected expansion.

  Removed 632 obsolete rebuildable object/depfile artifacts (22,329,359
  bytes) from the shared object cache, leaving zero artifacts without a
  current source owner.  A fresh 32-way build in a separate object root then
  built all 14 tools using only current source IDs.  `cppgm++` and `ctrlexpr`
  are byte-exact against their pre-rename baselines after removing symbols and
  the GNU build-id; their allocated text/data/bss sizes remain
  7,085,510/21,712/6,792 and 163,218/6,192/5,408.  Therefore this batch has no
  executable-performance mutation.  All 18 frozen output surfaces remained
  exact.

  The normal file audit remains zero-fatal/32-warning.  The refreshed
  report-only tight scan contains 141 duplicate and 30 division advisories:
  74 include/namespace/type preambles, 51 unchanged same-file structural
  symmetries, and 16 already-independent cross-owner semantic patterns.  The
  seven-warning increase from R0 is six path-induced preamble matches and one
  shifted cross-owner representative; the same-file implementation count is
  unchanged.  The full checkpoint passed 5471/5471; LowIR remains 124/99,
  semantic ownership 850/0, lowering ownership 100/13/45/0, and native
  ownership 14/0.  R11 exits with no compatibility header or migration alias,
  no duplicate source ID, and no dependency on a removed path.  Push: with
  this checkpoint.
- `3e2b022f`, `0f5ad715`, and this commit — R12 ownership and final closure.
  Completed the old-to-new path manifest for all 215 starting PA-prefixed
  production files and added an exact audit which rejects a missing baseline
  row, a surviving old path, a missing current owner, an invalid PA-numbered
  owner, a duplicate owner in one row, or an invalid disposition.  The final
  manifest classifies 206 paths as renamed, seven as split, and two as merged.
  The source-set audit covers all 14 tools, 49 responsibility variables, and
  228 current production translation units; the layout audit covers 469 files
  with zero legacy paths, includes, namespaces, or identifiers.

  The final semantic ownership increment removed the arbitrary
  `semantic/declarations/names.cpp` split.  The unchanged class-name builder
  now lives beside `AnalyzeClass`, and the unchanged enum-name builder beside
  `AnalyzeEnum`.  The semantic-input token classifier moved path-only from
  `semantic/presentation/vocabulary.cpp` to
  `semantic/analysis/vocabulary.cpp`.  One translation unit was removed, so
  `cppgm++` now has 217 shared source objects plus its runner and entry object
  (219 total).  The enum owner is an explicit responsibility group immediately
  after the semantic-primary group; the vocabulary, lambda-presentation, and
  LowIR-debug owners retain their canonical late positions.

  Several mechanically plausible alternatives were measured and rejected.
  Converting the high-fanout syntax tag headers to `#pragma once` produced a
  six-lane self/GCC wall ratio of 1.511509x.  Merging semantic vocabulary into
  the analyzer produced 1.527837x, and merging the two small demand-statistics
  owners into `demand.cpp` produced 1.543375x.  Moving enum, vocabulary, and
  LowIR debug together passed one O1 sample at 1.489145x but regressed the
  optimized Clang frozen guard by 0.83% wall and 0.93% aggregate CPU.  Restored
  canonical order remained too slow at 1.525154x, and moving only LowIR debug
  remained too slow at 1.511722x.  All rejected source and ordering changes
  were restored; they are not hidden behind the retained increment.

  The retained enum-only ordering passed the optimized-host same-source ABBA
  guard.  GCC-O3 old/new frozen medians were 4.550/4.525 seconds wall
  (-0.55%), 4.540/4.520 seconds aggregate CPU (-0.44%), and
  368118/370078 KiB RSS (+0.53%).  Clang-O3 medians were 4.760/4.740 seconds
  wall (-0.42%), 4.750/4.735 seconds aggregate CPU (-0.32%), and
  369428/370616 KiB RSS (+0.32%).  Both compilers produced byte-identical
  frozen objects in every lane.  Against the pre-R12 GCC-O3 compiler, the
  final compiler changes from 7,085,510/21,712/6,792 to
  7,085,554/21,704/6,792 text/data/bss bytes.  Symbol-size comparison confines
  the 44-byte text and eight-byte data delta to GCC's expected cross-TU
  recompilation and link layout around the co-located semantic functions; no
  feature or optimizer implementation changed.

  The corrected final same-revision O1 matrix used 32-way object builds and 12
  balanced self/GCC lanes because the result was close to the boundary.  Self
  and GCC wall medians were 31.545/21.315 seconds, a 1.479944x ratio; the
  median of paired wall ratios was 1.484839x.  Aggregate CPU medians were
  907.725/591.765 seconds (1.533928x), and RSS medians were
  231924/230504 KiB.  Three Clang lanes measured 22.210 seconds wall,
  608.430 seconds aggregate CPU, and 230544 KiB RSS, for self/Clang ratios of
  1.420306x wall and 1.491914x CPU.  The wall-time exit criterion therefore
  passes below 1.50x; the higher self/GCC CPU ratio is recorded explicitly.
  Current GCC-O1 and Clang-O1 compiler hashes are respectively
  `bbdd945768d8ff2db2474a4d63429d1caf42ce97f93f0c948d3094996e5c6ee4`
  and
  `66aad5a8cea9cfc58e2fe8bfa5c339ece2742c0be715079b8f80b5161d4b4267`.

  Fresh explicit-O1 32/32/32 inception matched every current object and the
  final compiler exactly at SHA-256
  `a836d867d7adaaa7679ff8ad5e8fd0546a526dc5e7c62ed122310ac6cfb7fba4`.
  It completed in 51.33 seconds wall, 1,409.24 seconds aggregate CPU, and
  232364 KiB maximum RSS.  The complete through-PA38 report passed 5471/5471,
  all 18 frozen output surfaces were exact, LowIR remained 124/99, semantic
  ownership remained 850/0, lowering ownership remained 100/13/45/0, native
  ownership remained 14/0, and the normal file audit remained
  zero-fatal/32-warning.  The final report-only tight scan has 139 duplicate
  and 30 division advisories, two fewer duplicate representatives than R11
  because the declaration-name fragment was consolidated.

  No Cachegrind, Valgrind, perf, inception, or detached benchmark process was
  left running.  Removed all 782 rename-plan scratch entries (12 GiB) from
  `/dev/shm`, the plan's registered scratch worktree, all plan-owned
  `/tmp/v3rename-*` data (reducing `/tmp` from 2.0 GiB to 55 MiB), and eight
  obsolete declaration-name/vocabulary object or depfile artifacts.  Dirty
  pre-existing `/tmp/v3codex-*` worktrees were deliberately preserved.  R12
  and the complete rename consolidation plan exit with every retained commit
  pushed.  Push: with this closure ledger.
- Post-closure O3 inception comparison — this commit.  Measured the complete
  current 219-object `cppgm++` workload at `-O3` with 32-way object builds,
  comparing the O3 self-hosted compiler against the same-revision GCC-O3-built
  `cppgm++`.  Six scored lanes gave each producer three first and three second
  positions.  Self/GCC medians were 32.425/18.575 seconds wall
  (1.745626x), 924.400/511.530 seconds aggregate CPU (1.807128x), and
  232222/230486 KiB RSS (1.007532x).  Median paired ratios were 1.769392x wall
  and 1.826101x aggregate CPU.  The first pair was a retained host outlier at
  70.24/23.62 seconds; the other five lanes were stable at 32.12-33.10 seconds
  self and 17.86-19.61 seconds GCC.

  Every lane produced all 219 expected objects.  All twelve object manifests
  were identical, and every resulting compiler was byte-identical at SHA-256
  `f9aedfb6c438a2252f474632fd4000de4f135494f8c0f4906860b9a4eb8e60f2`.
  The self-hosted and GCC-built producer hashes were respectively that hash
  and
  `2ed30e75190dfdb28a9571a27300780cb10314248e710b95d4b9a42ca0ff2800`.
  The raw diagonal comparison against the earlier O1 matrix showed O3 self
  wall time 2.8% higher while the GCC-built compiler improved 12.9%, widening
  the wall ratio from 1.479944x to 1.745626x.  Because that diagonal changes
  both producer build level and workload level, it establishes the best-case
  gap but does not by itself attribute the gap to either variable; the 2x2
  follow-up below supersedes that causal interpretation.  The 460 MiB
  measurement roots were removed after recording the results.  Push: with
  this measurement entry.
- Post-closure O1/O3 producer-by-workload matrix — this commit.  Built four
  same-source producer compilers, then crossed each producer build level with
  full 219-object O1 and O3 workloads.  Every cell used 32-way builds and three
  order-rotated lanes; the close self-produced O3-workload comparison was
  extended to six position-balanced lanes.  Wall/aggregate-CPU medians were:

  | Producer origin and build level | O1 workload | O3 workload |
  | --- | ---: | ---: |
  | self O1 | 31.250 / 905.400 s | 32.365 / 910.690 s |
  | self O3 | 33.080 / 926.500 s | 32.525 / 924.710 s |
  | GCC O1 | 20.960 / 589.670 s | 20.960 / 590.460 s |
  | GCC O3 | 18.430 / 502.220 s | 19.640 / 547.370 s |

  Holding workload level fixed isolates producer-code quality.  On the O1
  workload, the self O3 producer regresses 5.856% wall and 2.330% aggregate
  CPU relative to self O1.  On the O3 workload, self O3 regresses 0.494% wall
  and 1.539% aggregate CPU; paired medians are +0.409% wall and +1.404% CPU.
  Thus the current post-O1 pipeline's combined net effect on compiler
  throughput is detrimental, substantially so for the O1 workload and mildly
  so for O3.  This does not establish that every O2/O3 pass is harmful; an O2
  producer row followed by individual pass dosing is required to locate the
  first harmful increment.

  The GCC control moves in the opposite direction.  A GCC-O3-built producer
  improves over GCC-O1 by 12.071% wall/14.830% CPU on the O1 workload and by
  6.298% wall/7.298% CPU on the O3 workload.  Workload-level changes alone are
  much smaller than these producer effects.  All 219 objects and the final
  compiler were byte-exact across every producer at a fixed workload level:
  O1 SHA-256
  `a836d867d7adaaa7679ff8ad5e8fd0546a526dc5e7c62ed122310ac6cfb7fba4`
  and O3 SHA-256
  `f9aedfb6c438a2252f474632fd4000de4f135494f8c0f4906860b9a4eb8e60f2`.
  Removed all 1,021 MiB of matrix scratch data after recording the results.
  Push: with this measurement entry.

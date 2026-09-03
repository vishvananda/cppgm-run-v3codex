## CPPGM Programming Assignment 19 (`cppgm++ --emit-lowir`)

### Overview

Write a C++ application called `cppgm++` that takes as input a set of C++ Source Files,
executes translation phases 1 through 7, parses them as PA10/PA19 translation units,
reuses the PA11-PA12 semantic foundation, builds on the PA15-PA18 LowIR lowering path,
adds the first template-instantiation layer, and writes LowIR text.

PA19 adds the first usable template tier on top of the completed PA18 procedural/object/
polymorphic compiler. It extends PA18 with:

- function templates
- class templates
- type template parameters
- default type template arguments
- dependent names and current-instantiation lookup in supported template bodies
- basic function-template argument deduction for direct calls
- on-demand template instantiation for the supported class/function cases
- template-backed operator overloads where the non-template PA12-PA18 machinery
  already exists

### Prerequisites

You should complete Programming Assignment 18 before starting this assignment.

You will want to reuse:

- the preprocessing and tokenization pipeline from PA1-PA6
- the PA10 AST as the syntax boundary
- the PA11 declarator/type model
- the PA12 call-resolution layer
- the PA15-PA18 LowIR lowering path
- the PA13 LowIR contract
- the PA13 LowIR -> CY86 path as an optional secondary scaffold
- the PA16-PA18 class metadata, constructor/destructor machinery, and polymorphic lowering

The intended direction is:

- PA10 provides syntax
- PA11 provides scope/type lookup
- PA12 provides the procedural expression/call core
- PA15 lowers the procedural subset
- PA16 adds the basic non-virtual object model
- PA17 adds the non-polymorphic value-semantics layer
- PA18 extends that object model with scoped polymorphism
- PA19 adds first-tier templates on top of that existing semantic/codegen stack

### Starter Kit

The starter kit contains:

- a `cppgm++.cpp` assignment entry point, linked to the editable compiler source
  in `../dev/cppgm++.cpp`
- the standard assignment `Makefile` and harness scripts
- the grammar for this assignment called `pa19.gram`
- an HTML grammar explorer of `pa19.gram` in the sub-directory `grammar/`
- a checked-in local test suite under `tests/`

In the starter kit, the editable `../dev/cppgm++.cpp` file is seeded from the
`cppgm++` scaffold and is the file you extend for this assignment.

Unlike PA1-PA9, there is no external reference binary for PA19. The checked-in `.ref`
files are the default oracle.

### Input / Command-Line Arguments

The PA19 invocation is the unoptimized LowIR mode:

    $ cppgm++ --emit-lowir -O0 -o <outfile> <srcfile1> <srcfile2> ... <srcfileN>

Behaviour is undefined unless the command-line arguments match that shape, with
the same source-file ordering and `-o` relaxations as PA18. Other `--emit-*`
modes, driver mode, and optimized LowIR output are not part of PA19.

### Nondependent template bases

A base class that does not depend on a template parameter is resolved and
validated when the class template is defined, even if no specialization of the
derived template is ever used.  It must already name a complete, non-union,
non-final class that is accessible from the class-template definition.  A
friend class-template declaration grants that access before any concrete
specialization exists.  Selecting a class-template specialization as a fixed
base also instantiates and validates that specialization's definition; an
error in its body cannot be hidden merely because no derived specialization is
used.  A genuinely dependent base remains deferred until a concrete
specialization is formed.

### Output Format

On success, `cppgm++` shall write LowIR text to `<outfile>` and exit
`EXIT_SUCCESS`.

The authoritative LowIR definition is `../pa13/lowir.md`. PA19 extends the PA18
template-free object/polymorphism subset of that IR with the instantiated lowering needed by
this milestone.

PA19 writes a single concatenated LowIR program consisting of:

- zero or more `global` definitions
- zero or more `function` definitions

LowIR top-level declaration/definition order is a presentation convention, not
a dependency order. Reference outputs and canonical dumps use the order defined
in `../pa13/lowir.md`: `declare global`, `declare function`, `global`, then
`function`, but the relaxed LowIR comparison canonicalizes top-level entries
before comparison. Your output must still be repeatable for the same
inputs; `../pa13/lowir.md` defines the canonical reference presentation and
notes where internal LowIR symbol names are only a presentation tie-breaker.
Your output must also preserve order-sensitive LowIR regions when they are present: instruction order inside
blocks, item order inside structured globals, vtable slot order, and action
order inside generated initialization, finalization, constructor, destructor,
and cleanup bodies.

Template instantiation in PA19 should produce ordinary instantiated declarations which then
lower through the existing PA15-PA18 LowIR conventions.

The test harness checks that the generated LowIR is well formed and matches the
checked-in `.ref` files after canonicalizing presentation details that are not
part of the assignment contract. Exact textual LowIR matching is not a PA19
grading requirement.

### Error Handling

If an error occurs during preprocessing, tokenization, parsing, semantic analysis, or LowIR
generation, `cppgm++` shall `EXIT_FAILURE`.

The output file is not required to be meaningful on failure.
Diagnostics are not part of the grading contract.

### Standard Output / Error

Standard output and standard error are ignored for automated testing of `cppgm++`.

You are free to use them for debugging, tracing, or diagnostic messages.

### Testing

Testing uses checked-in golden outputs, not a reference binary.

For each test case `x`:

- `cppgm++` is executed to produce `x.my`
- the exit status is recorded in `x.my.exit_status`
- `x.my` is compared against `x.ref`
- `x.my.exit_status` is compared against `x.ref.exit_status`

`make test` runs the checked-in local suite under `tests/`. The suite is split
by test role:

- `tests/spec/` contains N3485/spec-anchored first-tier template tests. Each
  provided C++ language test in this directory starts with a leading comment of the
  form `// N3485 focus: 14.x.y [clause.name] ...` so a reviewer can find the
  governing text in `../doc/n3485.txt`.
- `tests/general/` contains broader first-tier generic-program tests that are
  useful for PA19 but are not one-rule spec probes.

The `make test` target runs both directories through the LowIR validator. For
successful tests, the validator checks the reference LowIR and your generated
LowIR for basic structural correctness, then compares the canonicalized LowIR
against the checked-in reference. For rejected tests, the exit status is the
checked result; exact diagnostic text is not checked.

PA19 is tested against the generated LowIR text. That LowIR is intended to
become input for the later PA29 `lowir2native` backend, but that future native
path is not the PA19 grading contract.

### Optional Student Test Ideas

When adding your own tests, useful PA19 themes include class/function template
instantiation, default type template arguments, dependent versus non-dependent
lookup, current-instantiation names, and direct function-template calls. Keep
any such tests within the PA19 boundary below; parameter packs,
template-template parameters, member/friend templates, non-type template
arguments, partial specialization, full deduction, and SFINAE behavior belong
to later assignments.

### PA19 Syntax Spec

The authoritative source syntax is the shared `cppgm++` source grammar, exposed
for this assignment as `pa19.gram`. The grammar defines accepted syntax only;
the PA19 semantic and lowering requirements are defined by the Assignment
Boundary and Out Of Scope sections below.

As in the earlier assignments, that grammar defines accepted input syntax only. The output
format for `cppgm++` is specified by this README, PA13 `lowir.md`, and the checked-in
`.ref` files.

Template declarations, template-parameter clauses, common template-id syntax,
and later-template syntax such as non-type template parameters were already
preserved by PA10; PA19 is the first milestone that gives a supported
type-parameter subset of template syntax semantic/code-generation meaning.

Passing PA18 is necessary but not sufficient for passing PA19: an input may be syntactically
valid for PA10-PA18 and code-generation-valid for PA18 and still be outside the PA19
template slice described below.

A checked-in HTML grammar explorer for that grammar lives in `grammar/`. Treat
`pa19.gram` as the source of truth.

`pa19.gram` uses the same token vocabulary and the same extended BNF operators as
`../pa6/pa6.gram`.

If this README and `pa19.gram` appear to disagree about source syntax, treat `pa19.gram`
as authoritative. If this README and PA13 `lowir.md` appear to disagree about LowIR syntax,
treat `lowir.md` as authoritative. If they disagree about the PA19 lowering slice, treat the
`Assignment Boundary` and `Out Of Scope` sections below as authoritative.

### Assignment Boundary

PA19 supports the following in addition to the PA18 subset:

- class templates whose parameters are:
  - type parameters
- function templates whose parameters are:
  - type parameters
- default template arguments for the supported type-parameter forms, including
  defaults that refer to earlier parameters in the same template head
- dependent type/value names in the supported declaration and expression forms
- current-instantiation lookup in the supported class-template cases
- `typename` and `template` disambiguators where they are needed by the PA19
  dependent-name subset
- explicit template-id use for supported class templates and function templates
- explicit type arguments using the ordinary PA11 declarator/type forms,
  including function types
- ordinary lookup and using-declaration behavior for supported templates,
  including repeated using-declarations of the same template and preservation
  of an ordinarily visible function template when hidden non-template friends
  share its name, and replay of a dependent-base using-declaration that names an
  enumerator used by another enumerator; ordinary lookup must retain ambiguity
  when distinct declarations are introduced by using-directives
- basic template argument deduction for direct supported function-template calls
  from ordinary argument types, without function-template partial ordering or
  SFINAE
- on-demand instantiation of the supported class-template and function-template cases,
  including deferring unused conversion-function bodies and dependent defaulted
  special-member dependencies, and waiting until an out-of-line owning destructor
  is defined before demanding a completeness-dependent member destructor
- a static data member named only in an unevaluated operand, such as the operand
  of `sizeof`, does not demand storage or instantiate its out-of-class definition;
  an evaluated value use or address use continues to demand the required definition
- instantiated specializations reuse supported PA17 rvalue-reference return
  paths and PA18 virtual-destructor lifetime without changing their value
  category or object-lifetime behavior
- distinct local-class identities for separate function-template
  specializations
- declaration-owned template-parameter scopes across nested instantiations and
  out-of-class member-definition bodies, including definition-time rejection
  of a declaration that redeclares an active template parameter even when the
  member is never instantiated
- definition-time semantic checking of unused supported function-template and
  qualified inline member bodies, including ordinary block and condition
  scopes and the distinction between type names and value names; class bit-field
  members remain valid value expressions in those bodies
- qualified class-template-ids in function declarators are parsed in their own
  declaration context rather than being captured by an unrelated function
  template with the same unqualified name
- class-template instantiation preserves the class-scope rule that a name which
  has become a typedef-name cannot be redefined by another typedef declaration
- compatible function-template declarations and definitions in either order
- dependent-base lookup provenance attached to the particular base-specifier,
  so a nested class with a fixed base keeps ordinary base lookup and a local
  class in a function template independently classifies its own dependent base
- out-of-class definitions of nested classes declared inside the supported class templates,
  when those nested classes stay within the already supported PA16-PA18 class/value/
  polymorphic machinery
- distinct nested types from different class-template specializations remain distinct in
  overload resolution, and a dependent local type alias used by `new` resolves to the
  concrete specialization before constructor selection
- ordinary PA10 function declarator forms, including trailing return types, on the supported
  function-template cases
- instantiated specialization names that then participate in the ordinary PA16-PA18
  class/method/codegen machinery
- template-backed overload participation where the non-template PA12-PA18 machinery already
  exists, including function-template operator overloads
- ordinary member typedef hiding and injected-name lookup during re-entrant
  class-template instantiation

Within this milestone, PA19 should produce valid LowIR for ordinary generic code over the
supported PA18 subset. That LowIR is intended to be accepted by the later PA29
`lowir2native` backend for the supported cases. PA13 `lowir2cy86` remains an
optional execution scaffold.

### Out Of Scope

The following are explicitly out of scope for PA19:

- template-template parameters
- template parameter packs and pack expansions
- member templates and friend templates
- semantic support for non-type template parameters and non-type template
  arguments
- partial specialization
- explicit specialization
- full standard two-phase lookup
- function-template partial ordering
- substitution-failure candidate dropping and SFINAE
- full `constexpr` evaluation
- alias templates and variable templates
- hosted/vendor-only template traits and intrinsics
- template-aware virtual dispatch beyond ordinary instantiated class reuse
- templates whose definitions rely on unsupported PA16-PA18 class/value/polymorphic features

Inputs that rely on those features have undefined behaviour for this milestone.

### Stage Handoff

The intended next stage is PA20, which adds the first practical metaprogramming layer on top
of the basic PA19 template machinery:

- integral non-type template parameters and arguments
- explicit specialization of supported class/function templates
- integral constant-expression template arguments
- `static_assert`-style metaprogramming support

So PA19 should leave behind a clean first-tier instantiation layer rather than trying to
solve the full template language at once. Partial specialization, SFINAE
metaprogramming, and full `constexpr` evaluation remain later work.

### Design Notes (Non-Normative)

The important point is to add templates as an extension of the existing PA16-PA18 language
behavior rather than building a separate generic-only compiler with different rules. Whether
that reuse happens through shared code, shared data structures, or a careful
reimplementation is up to you.

The same monotonic-extension rule applies here:

- PA19 should add template behavior only when the source actually uses the supported
  template feature set
- it should not perturb PA18 outputs for programs that remain entirely within the PA18
  subset
- in practice, template lookup and instantiation should stay on-demand rather than eagerly
  changing the behavior of ordinary earlier-milestone programs that do not use the PA19
  template subset

Useful intermediate representations include:

- explicit template declarations stored separately from ordinary instantiated declarations
- template-parameter scopes that can be rebound during instantiation
- instantiated class/function records that reuse the ordinary PA16-PA18 metadata/lowering
- typed template arguments and bindings rather than source-text template replay
- a clear separation between:
  - parsing template syntax
  - collecting template declarations
  - deducing or resolving template arguments
  - instantiating ordinary specialized declarations

## CPPGM Programming Assignment 22 (`cppgm++ --emit-lowir`)

### Overview

Write a C++ application called `cppgm++` that takes as input a set of C++
source files, executes translation phases 1 through 7, parses them as PA10/PA22
translation units, reuses the PA11-PA21 semantic foundation, builds on the
PA15-PA21 LowIR lowering path, and writes LowIR text.

PA22 is the first half of template completion. Its job is to finish the
template declaration and specialization model so the compiler knows:

- what template entities exist
- how template-template parameters, member templates, and friend templates are
  represented
- what specializations exist
- which specialization is selected
- which declarations/definitions own the selected specialization

PA22 still produces LowIR. It does not introduce a new output format.

### Prerequisites

You should complete Programming Assignment 21 before starting this assignment.

You will want to reuse:

- the preprocessing and tokenization pipeline from PA1-PA6
- the PA10 AST as the syntax boundary
- the PA11-PA12 semantic foundation
- the PA15-PA21 LowIR lowering path
- the PA13 LowIR contract
- the PA19-PA20 template and metaprogramming machinery
- the PA21 full constant-evaluation layer

### Starter Kit

The starter kit contains:

- a `cppgm++.cpp` assignment entry point, linked to the editable compiler source
  in `../dev/cppgm++.cpp`
- the standard assignment `Makefile` and harness scripts
- the PA22 specialization/entity test suite under `tests/`

In the starter kit, the editable `../dev/cppgm++.cpp` file is seeded from
the `cppgm++` scaffold and is the file you extend for this assignment.

Unlike PA1-PA9, there is no external reference binary for PA22. The checked-in
`.ref` files are the default oracle.

### Input / Command-Line Arguments

The PA22 invocation is the unoptimized LowIR mode:

    $ cppgm++ --emit-lowir -O0 -o <outfile> <srcfile1> <srcfile2> ... <srcfileN>

Behaviour is undefined unless the command-line arguments match that shape, with
the same source-file ordering and `-o` relaxations as the earlier source-to-LowIR
milestones. Other `--emit-*` modes, driver mode, and optimized LowIR output are
not part of PA22.

### Dependent qualified types

Within a retained template definition, a qualified member type whose qualifier
depends on a template parameter requires the C++11 `typename` introducer.  This
is checked when the template is defined, even if no specialization is requested.
A type member already established in the current instantiation retains the
standard exception; a different specialization of the same class template does
not.  The exception applies to parameter types after an out-of-class member's
qualified declarator, but not to a dependent return type written before that
declarator.  A dependent template name used as a template-template
argument uses the `template` introducer and is not misclassified as a member
type requiring `typename`.  In a type position, a dependent member template-id
requires both introducers: `typename` establishes that the result is a type,
while `template` establishes that the qualified member is a template.  A
template-id whose argument contains a pack expression such as
`sizeof...(Pack)` is dependent even when a later substitution gives it the
same specialization as a fixed template-id.  Because a dependent
qualified-id without `typename` does not name a type, a declarator whose
parenthesized operand is only such a name declares a variable with a
direct-initializer rather than a function with an unnamed parameter.

### Qualified template-name hiding

Function and variable templates declared directly in a class participate in
the ordinary member-name lookup namespace and hide same-named declarations
inherited from a base class.  This lookup rule applies before overload ranking:
a hidden base non-template must not be admitted merely because the derived
declaration has no concrete binding until template deduction.  Naming a
variable template without its required template arguments is invalid; lookup
does not fall through to a hidden base data member.

### Output Format

On success, `cppgm++` shall write LowIR text to `<outfile>` and exit
`EXIT_SUCCESS`.

The authoritative LowIR definition is `../pa13/lowir.md`. PA22 extends the
PA21 lowering surface only by making more of the C++ source language lower into
the already-defined LowIR family.

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

The test harness checks that the generated LowIR is well formed and matches the
checked-in `.ref` files after canonicalizing presentation details that are not
part of the assignment contract. Exact textual LowIR matching is not a PA22
grading requirement.

### Error Handling

If an error occurs during preprocessing, tokenization, parsing, semantic
analysis, or LowIR generation, `cppgm++` shall `EXIT_FAILURE`.

The output file is not required to be meaningful on failure.
Diagnostics are not part of the grading contract.

### Standard Output / Error

Standard output and standard error are ignored for automated testing of
`cppgm++`.

You are free to use them for debugging, tracing, or diagnostic messages.

### Testing

PA22 tests live under `tests/`. The suite is split by test role:

- `tests/spec/` contains N3485/spec-anchored specialization/entity tests. Each
  provided C++ language test in this directory starts with a leading comment of the
  form `// N3485 focus: 14.x.y [clause.name] ...` so a reviewer can find the
  governing text in `../doc/n3485.txt`.
- `tests/general/` contains broader cross-feature and realistic
  template-entity examples that are useful for PA22 but are not one-rule spec
  probes.

The `make test` target runs both directories through the LowIR validator. For
successful tests, the validator checks the reference LowIR and your generated
LowIR for basic structural correctness, then compares the canonicalized LowIR
against the checked-in reference. For rejected tests, the exit status is the
checked result; exact diagnostic text is not checked.

This split assignment intentionally focuses on the specialization/entity half of
template completion:

- class partial specialization and specialization selection
- alias and variable template entity modeling
- explicit specialization and explicit-instantiation ownership
- the declaration/instantiation behavior required to make that model coherent

### PA22 Syntax Boundary

The authoritative source syntax is the shared `cppgm++` source grammar, exposed
for this assignment as `pa22.gram`. The grammar defines accepted syntax only;
the PA22 semantic and lowering requirements are defined by the Assignment
Boundary and Out Of Scope sections below.

### Optional Student Test Ideas

When adding your own tests, useful PA22 themes include alias/variable template
entities, template-template parameters, member templates, friend templates,
class partial specialization selection, explicit-instantiation ownership,
constructor/member-template specialization ownership, and partial ordering
boundaries.

### Assignment Boundary

PA22 owns the template declaration graph and specialization model over the
implemented language surface, including:

- alias templates
- variable templates
- template-template parameters and template-template argument matching
- member templates, including templated member operators and templated call
  operators
- access checking for member class templates, alias templates, and nested type
  paths, including inherited access and template-template arguments
- friend templates in the supported class-template/function-template subset, including a
  friend type template-id whose non-type argument is a dependent constant expression
- class partial specialization
- partial-specialization ordering and specialization selection
- current-specialization identity in the supported class-template and
  specialization cases
- explicit-instantiation declarations and definitions over the supported surface
- an explicit-instantiation declaration for a class specialization suppresses
  non-inline instantiation but keeps a locally used in-class inline or defaulted
  member definition available
- integration with PA20 explicit specialization declarations/definitions when
  they interact with the PA22 specialization graph
- collection/ownership behavior for constructor/member-template specializations
  and namespace-scope friend-template declarations
- an explicit definition of a member template for a concrete class-template
  specialization replaces the definition instantiated from the primary and may
  not itself be defined more than once
- an out-of-class member-template definition attaches to specializations selected
  by earlier calls in the translation unit and remains available for their demand
- the dependent-name and instantiation behavior strictly required to make the
  specialization model work

### Out Of Scope

The following are explicitly out of scope for PA22:

- full function-template deduction over the intended language surface
- function-template partial ordering
- SFINAE and substitution-failure completion
- the remaining no-eager-instantiation / dependent-call timing work that is
  better framed as substitution behavior
- initializer-list template behavior
- hosted/vendor-only extensions that happen to use templates
- post-C++11 template-language features
- broad multi-feature integration cases whose main assertion is that several
  completed template features compose

Inputs that rely on those features have undefined behaviour for this milestone.

### Stage Handoff

The intended next stage is PA23, which finishes deduction, substitution, and
SFINAE over the now-complete specialization model.

So PA22 should leave behind:

- a stable template declaration/specialization graph
- deterministic specialization selection
- specialization ownership that lowers through the ordinary LowIR path
- no remaining "template entity model later" gap before full template
  completion

### Design Notes (Non-Normative)

The useful shape for PA22 is a canonical template-entity graph. Alias templates,
variable templates, primary class templates, partial specializations, explicit
instantiations, and PA20 explicit specializations should refer to the same
semantic entities instead of being tracked as unrelated source-text forms.

Useful intermediate representations include:

- canonical specialization keys built from typed template arguments
- explicit template-template parameter bindings that point at template entities,
  not source text
- an ordered partial-specialization candidate set with deterministic selection
- explicit ownership links from constructor/member-template specializations back
  to the class or namespace entity that owns the generated declaration
- reuse of PA21 constant values for value-dependent specialization keys

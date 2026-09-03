## CPPGM Programming Assignment 23 (`cppgm++ --emit-lowir`)

### Overview

Write a C++ application called `cppgm++` that takes as input a set of C++
source files, executes translation phases 1 through 7, parses them as PA10/PA23
translation units, reuses the PA11-PA22 semantic foundation, builds on the
PA15-PA22 LowIR lowering path, and writes LowIR text.

PA23 is the second half of template completion. Its job is to finish the
remaining single-feature deduction/substitution behavior so ordinary generic
C++11 code stops depending on a pragmatic template subset.

PA23 still produces LowIR. It does not introduce a new output format.

### Prerequisites

You should complete Programming Assignment 22 before starting this assignment.

You will want to reuse:

- the preprocessing and tokenization pipeline from PA1-PA6
- the PA10 AST as the syntax boundary
- the PA11-PA12 semantic foundation
- the PA15-PA22 LowIR lowering path
- the PA13 LowIR contract
- the PA19-PA22 template machinery
- the PA21 full constant-evaluation layer

### Starter Kit

The starter kit contains:

- a `cppgm++.cpp` assignment entry point, linked to the editable compiler source
  in `../dev/cppgm++.cpp`
- the standard assignment `Makefile` and harness scripts
- the PA23 deduction/substitution test suite under `tests/`

In the starter kit, the editable `../dev/cppgm++.cpp` file is seeded from
the `cppgm++` scaffold and is the file you extend for this assignment.

Unlike PA1-PA9, there is no external reference binary for PA23. The checked-in
`.ref` files are the default oracle.

### Input / Command-Line Arguments

The PA23 invocation is the unoptimized LowIR mode:

    $ cppgm++ --emit-lowir -O0 -o <outfile> <srcfile1> <srcfile2> ... <srcfileN>

Behaviour is undefined unless the command-line arguments match that shape, with
the same source-file ordering and `-o` relaxations as the earlier source-to-LowIR
milestones. Other `--emit-*` modes, driver mode, and optimized LowIR output are
not part of PA23.

### Output Format

On success, `cppgm++` shall write LowIR text to `<outfile>` and exit
`EXIT_SUCCESS`.

The authoritative LowIR definition is `../pa13/lowir.md`. PA23 extends the
PA22 lowering surface only by making more of the C++ source language lower into
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
part of the assignment contract. Exact textual LowIR matching is not a PA23
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

PA23 tests live under `tests/`. The suite is split by test role:

- `tests/spec/` contains N3485/spec-anchored deduction, substitution, and
  SFINAE tests. Each provided C++ language test in this directory starts with a
  leading comment of the form `// N3485 focus: 14.x.y [clause.name] ...` so a
  reviewer can find the governing text in `../doc/n3485.txt`.
- `tests/general/` contains broader generic-program examples that are useful
  for PA23 but are not one-rule spec probes.

The `make test` target runs both directories through the LowIR validator. For
successful tests, the validator checks the reference LowIR and your generated
LowIR for basic structural correctness, then compares the canonicalized LowIR
against the checked-in reference. For rejected tests, the exit status is the
checked result; exact diagnostic text is not checked.

This split assignment intentionally focuses on the deduction/substitution half
of template completion:

- full function-template deduction
- function-template partial ordering
- non-deduced contexts and braced-init, array-bound, and conversion deduction
  corners
- SFINAE and substitution failure
- no-eager-instantiation timing and dependent-call behavior

### PA23 Syntax Boundary

The authoritative source syntax is the shared `cppgm++` source grammar, exposed
for this assignment as `pa23.gram`. The grammar defines accepted syntax only;
the PA23 semantic and lowering requirements are defined by the Assignment
Boundary and Out Of Scope sections below.

### Optional Student Test Ideas

When adding your own tests, useful PA23 themes include explicit template
arguments mixed with deduced ones, function-address deduction, conversion
function template deduction, constructor-template participation, richer
non-deduced contexts, and compact `enable_if` / `void_t` / detector patterns.

### Assignment Boundary

PA23 owns the remaining advanced single-feature standard template behavior over
the implemented surface, including:

- full function-template deduction over the intended C++11 subset
- function-template partial ordering
- substitution behavior and candidate dropping
- `enable_if`, `void_t`, and detected-idiom style SFINAE behavior
- conversion function template deduction
- constructor template deduction and overload participation
- non-deduced contexts and explicit template-id deduction edge cases
- braced-init deduction in the supported template-call subset
- pointer, reference, enum, and static-member non-type template argument values
  over the supported constant-expression subset
- template deduction from arguments whose types come from already-resolved
  member-function calls, including the implicit-object overload selection from
  PA16/PA17
- an argument that names a set of overloaded functions participates in
  deduction the same way whether the called template was found by ordinary
  lookup or only by argument-dependent lookup
- dependent-call, dependent-alias, and no-eager-instantiation behavior when the
  primary assertion is a single PA23-owned feature rather than a broad
  multi-feature composition
- dozens of distinct dependent function-template result types may coexist in
  one translation unit, and equivalent redeclarations retain the same result
  meaning after all of those declarations have been processed

### Out Of Scope

The following are explicitly out of scope for PA23:

- `std::initializer_list` library semantics and initializer-list overload
  machinery
- member-pointer template behavior that depends on later member-pointer support
- hosted/vendor-only extensions that happen to use templates
- post-C++11 template-language features
- backend/toolchain ownership that belongs to the later native and toolchain
  milestones

Inputs that rely on those features have undefined behaviour for this milestone.

### Stage Handoff

The intended template follow-up is PA25, which checks that the PA19-PA23
template features compose in realistic programs. The later backend stage retargets
the language-complete front-end from the CY86 scaffold path to the real native
backend.

So PA23 should leave behind:

- a complete standard template semantic layer
- instantiated declarations that lower through the ordinary LowIR path without
  template subset special-casing
- a clean handoff to PA24 for multi-feature template integration before
  backend/toolchain work

### Design Notes (Non-Normative)

The useful shape for PA23 is a typed substitution and deduction engine that
works on semantic declarations, types, expressions, and template arguments. A
substitution failure should be represented as candidate state during overload
resolution rather than as a diagnostic unless no viable candidate remains.

Useful intermediate representations include:

- deduction bindings that record which template parameter each typed argument
  constrained
- explicit non-deduced-context markers in the type/expression forms that need
  them
- a substitution result type that can carry success, SFINAE discard, or hard
  error
- deferred instantiation records for dependent calls and bodies that must not be
  forced before their template arguments are known

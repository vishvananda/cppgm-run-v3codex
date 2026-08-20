## CPPGM Programming Assignment 25 (`cppgm++ --emit-lowir`)

### Overview

Write a C++ application called `cppgm++` that takes as input a set of C++ Source Files,
executes translation phases 1 through 7, parses them as PA10/PA25 translation units,
reuses the PA11-PA12 semantic foundation, builds on the PA15-PA24 LowIR lowering path,
adds the PA25 core-language slice, and writes LowIR text.

PA25 closes the first large batch of ordinary C++11 language features that were deferred
while the compiler was still building its type, object, template, and backend layers.

This milestone focuses on:

- `auto` variable type deduction
- ordinary `auto` function return type deduction for non-template function definitions and
  non-template member function definitions with visible bodies
- direct braced initialization of supported scalar and array objects
- captureless lambdas plus the supported by-reference local / `this`-capture subset
- range-for over bounded arrays, braced-init lists, and supported user-defined `begin` / `end`
  ranges

PA25 still produces LowIR. It does not introduce a new output format.

### Prerequisites

You should complete Programming Assignment 24 before starting this assignment.

You will want to reuse:

- the preprocessing and tokenization pipeline from PA1-PA6
- the PA10 AST as the syntax boundary
- the PA11-PA12 semantic foundation
- the PA15-PA24 LowIR lowering path
- the PA13 LowIR contract
- the PA29 native validation path
- the PA13 LowIR -> CY86 path as an optional secondary scaffold

The intended direction is:

- PA10 provides syntax
- PA11-PA12 provide typed semantic analysis
- PA15-PA24 lower the supported language subsets to LowIR
- PA25 extends that same lowering path with the remaining first-tier core-language features

### Starter Kit

The starter kit contains:

- `pa25/README.md`, `pa25/Makefile`, and the test scripts in `pa25/scripts/`
- a student-editable `dev/cppgm++.cpp` starter scaffold
- the `pa25/cppgm++.cpp` symlink back to `../dev/cppgm++.cpp`
- shared support sources and headers under `dev/src/`
- a local test suite under `pa25/tests/`
- the grammar for this assignment called `pa25.gram`
- an HTML grammar explorer of `pa25.gram` in the sub-directory `grammar/`
- a checked-in local test suite under `tests/`

Students should implement the assignment in `dev/cppgm++.cpp` and any reusable
student-owned helpers they add under `dev/src/`. The assignment directory, grammar files,
test fixtures, comparison scripts, and checked-in reference outputs are support
files, not implementation files to edit for normal solutions. The shared support files
provide reusable infrastructure and earlier assignment machinery; they do not implement the
new PA25 source-to-LowIR language slice for you.

Unlike PA1-PA9, there is no external reference binary for PA25. The checked-in `.ref`
files are the default oracle.

### Input / Command-Line Arguments

Behaviour is undefined unless the command-line arguments match:

    $ cppgm++ --emit-lowir -O0 -o <outfile> <srcfile1> <srcfile2> ... <srcfileN>

`-O0` is the PA25 test mode. Other optimization levels are later optimizer work and
are not required for this milestone.

### Output Format

`cppgm++` shall write LowIR text to `<outfile>`.

The authoritative LowIR definition is `../pa13/lowir.md`. PA25 extends the PA24 lowering
surface only by making more of the C++ source language lower into the already-defined LowIR
family.

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

The generated LowIR must be well-formed and must match the checked-in `.ref` files under
the relaxed LowIR comparison used by the harness. That comparison still checks the
semantic LowIR shape and required IR facts, but it does not make helper metadata
presentation or other non-semantic text details part of the student contract.

### Error Handling

If an error occurs during preprocessing, tokenization, parsing, semantic analysis, or LowIR
generation, `cppgm++` shall `EXIT_FAILURE`.

The output file is not required to be meaningful on failure.

### Standard Output / Error

Standard output and standard error are ignored for automated testing of `cppgm++`.

You are free to use them for debugging, tracing, or diagnostic messages.

### Testing

Testing uses checked-in golden outputs, not a reference binary. The `Makefile` invokes
`cppgm++` with `--emit-lowir -O0`.

The local checked-in tests live in `tests/general/`. That directory contains
PA25 source-to-LowIR tests, cross-feature combinations, and boundary cases over
the broad core-language closure slice. PA25 has no `tests/spec/` directory
because these tests focus on the combined language-to-LowIR contract.

For each test case `x`:

- `cppgm++` is executed to produce `x.my`
- the exit status is recorded in `x.my.exit_status`
- `x.my` is compared against `x.ref`
- `x.my.exit_status` is compared against `x.ref.exit_status`

PA25 is tested against generated LowIR text using the relaxed LowIR comparator described
above. A useful manual validation path is:

- feed that LowIR into PA29 `lowir2native`
- optionally cross-check by feeding that same LowIR into PA13 `lowir2cy86`
- then feed the generated CY86 into PA9 `cy86 --target linux`

The shipped PA25 tests are the contract for this milestone.

### PA25 Syntax Spec

The authoritative source syntax is the shared `cppgm++` source grammar, exposed
for this assignment as `pa25.gram`. The grammar defines accepted syntax only;
the PA25 semantic and lowering requirements are defined by the Assignment
Boundary and Out Of Scope sections below.

As in the earlier assignments, that grammar defines accepted input syntax only. The output
format for `cppgm++` is specified by this README, PA13 `lowir.md`, and the checked-in
`.ref` files.

PA25 does not add a new source-language grammar format. It instead enables more
of the already-accepted C++11 syntax to participate in semantic analysis and
lowering.

A checked-in HTML grammar explorer for that grammar lives in `grammar/`. Treat
`pa25.gram` as the source of truth.

`pa25.gram` uses the same token vocabulary and the same extended BNF operators as
`../pa6/pa6.gram`.

If this README and `pa25.gram` appear to disagree about source syntax, treat `pa25.gram`
as authoritative. If this README and PA13 `lowir.md` appear to disagree about LowIR syntax,
treat `lowir.md` as authoritative. If they disagree about the PA25 lowering slice, treat the
`Assignment Boundary` and `Out Of Scope` sections below as authoritative.

### Assignment Boundary

PA25 supports the following in addition to the PA24 subset:

- `auto` in variable declarations when exactly one declarator is present and an initializer
  is provided
- `const auto` and similar cv-qualified `auto` variable declarations over the same subset
- direct braced initialization for supported scalar objects
- direct braced-init expressions over the supported scalar / array / class subset when the
  earlier PA16-PA24 object/value semantics already define the target
- braced initialization of bounded arrays with compile-time known size
- arrays of aggregate elements whose array members receive nested braced
  sub-lists, including zero-initialization of omitted member elements
- direct aggregate construction when the target aggregate type is already supported by the
  earlier object-model assignments
- ordinary function-call argument conversion through non-explicit converting constructors
  and conversion operators over the supported class subset
- explicit non-class functional casts between the supported integral and enum forms
- `reinterpret_cast` between the supported pointer and integer forms
- captureless lambda expressions plus the supported by-reference local and explicit/implicit `this`
  capture subset
- range-for statements over:
  - bounded arrays
  - braced-init lists that can be materialized as hidden arrays
  - supported class/member and ADL `begin` / `end` ranges whose iterator operations stay
    within the already-supported call/operator subset

Within this milestone, PA25 should produce valid LowIR for ordinary source programs over
that subset. That LowIR should be accepted by PA29 `lowir2native` for the supported cases.
PA13 `lowir2cy86` remains a secondary scaffold backend for cross-checking.

To complete PA25, implement these goals:

1. `auto` variable deduction.
   The compiler should deduce the declared type from the initializer and lower the resulting
   variable just like an equivalent explicit declaration, including ordinary pointer and
   reference declarators such as `auto*`, `auto&`, and `auto&&`.

2. Direct braced initialization.
   Supported scalar and array declarations should lower cleanly from `{...}` source forms,
   not only from `=` initializer syntax.

3. Captureless lambda lowering.
   Captureless lambdas should become callable lowered entities with deterministic LowIR.
   A function-local static declared in a lambda has distinct storage for each
   lambda expression and each enclosing function-template specialization.

4. Range-for lowering.
   Range-for over arrays, braced-init lists, and supported user-defined `begin` / `end`
   ranges should lower into ordinary loop/control-flow structure in LowIR, including
   ordinary reference loop declarations such as `const int&` and `const auto&`. A
   materialized class prvalue used as the range remains alive through the loop and is
   destroyed when the complete range-for statement ends.

The test suite also exercises a small remaining ordinary-language closure cluster here:
direct braced-init expressions, direct aggregate construction, supported integral / enum
functional casts, and pointer / integer `reinterpret_cast`.

### Out Of Scope

The following are explicitly out of scope for PA25:

- capturing lambdas other than the supported by-reference local / `this`-capture subset
- `std::initializer_list` semantic interoperation
- RTTI and `typeid`
- `dynamic_cast`
- placeholder return-type declarations without a visible definition body
- template bodies that require deferred placeholder return deduction
- range-declarations that require unsupported user-defined iterator or reference semantics
- any PA25 feature path that depends on unsupported PA16-PA24 semantics

Inputs that rely on those features have undefined behaviour for this milestone.
The PA25 test suite therefore does not require those inputs to fail deterministically; it
only checks the defined PA25 feature subset above.

### Stage Handoff

The intended next stage is PA26, which finishes the remaining deferred advanced-language
corners over the current single-inheritance object model before PA27 tackles the remaining
ABI and inheritance closure work.

So PA25 should leave behind:

- a stable first-tier language-closure semantic layer
- LowIR lowering for the ordinary non-advanced C++11 forms added here
- explicit remaining deferrals only where PA26 really needs to take over

### Design Notes (Non-Normative)

PA25 should extend the existing semantic and lowering path, not replace it.

The same monotonic-extension rule applies here:

- PA25 should add its new behavior only when the source actually uses the supported PA25
  feature set
- it should not perturb PA24 outputs for programs that remain entirely within the PA24
  subset
- in practice, lambda helper synthesis and `auto` deduction should stay on-demand rather
  than eagerly changing the behavior of ordinary earlier programs that do not use those
  features

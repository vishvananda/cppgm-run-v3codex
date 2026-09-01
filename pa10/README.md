## CPPGM Programming Assignment 10 (`cppgm++ --emit-ast`)

### Overview

Write the first `cppgm++` source-compiler mode:

```sh
cppgm++ --emit-ast -o <outfile> <srcfile1> [<srcfile2> ...]
```

The program reads one or more C++ source files, runs translation phases 1
through 7 for each file, parses each translation unit using the PA10 syntax
subset, and writes a deterministic text dump of the parsed syntax tree.

PA10 is a syntax assignment. It replaces the PA6 recognizer boundary with a
real tree-building parser that later assignments can consume directly. It does
not perform type checking, overload resolution, or semantic classification.

### Prerequisites

Complete PA9 before starting PA10. You should expect to reuse:

- the PA1-PA5 preprocessing and tokenization pipeline
- the PA6 grammar and parsing approach as a starting point
- the PA9 compiler-style `-o <outfile> <srcfile...>` command shape

PA10 is not a new recognizer. The required deliverable is a structured AST and
a stable text dump of that AST.

### Starter Kit

The PA10 starter kit contains:

- `README.md`, this assignment handout
- `Makefile`, which builds `cppgm++` and runs the PA10 tests
- `cppgm++.cpp`, a link to the editable `dev/cppgm++.cpp` entry point
- the grammar for this assignment called `pa10.gram`
- an HTML grammar explorer of `pa10.gram` in the sub-directory `grammar/`
- `scripts/run_all_tests.pl` and `scripts/compare_results.pl`
- `tests/spec/`, clause-anchored syntax tests
- `tests/general/`, broader parser tests
- checked-in `.ref` and `.ref.exit_status` files used as the oracle

Your main editable file is `dev/cppgm++.cpp`. You may add or change other
implementation files under `dev/` as needed. Do not edit the test inputs,
reference outputs, harness scripts, or grammar files unless course staff
explicitly asks for that.

The starter `dev/cppgm++.cpp` is a command-line scaffold for the long-lived
`cppgm++` binary. It establishes the expected mode flags and help path; the AST
parser, AST data model, and AST output behavior are your assignment work.

There is no required `cppgm++-ref` binary for PA10. The checked-in
reference files under `tests/` are the grading oracle.

### Build And Test Commands

From the `pa10/` directory:

```sh
make
make test
```

`make` builds `cppgm++`. `make test` runs the local PA10 suite. If a
`course/pa10` extension suite is present, the Makefile runs it after the local
suite using the same harness contract.

### Required Driver Surface

PA10 requires:

- `--emit-ast`
- `-o <outfile>`
- one or more source-file operands

The following modes and driver features are not part of PA10:

- `--emit-types`
- `--emit-semantics`
- `--emit-lowir`
- native compile or link driver behavior such as `-c`, `-E`, `-I`, `-L`, or
  `-l`
- hosted-toolchain query flags such as `--version`, `-v`, `-dumpmachine`, or
  `-print-search-dirs`

Behavior is undefined unless the command line has this form:

```sh
cppgm++ --emit-ast -o <outfile> <srcfile1> [<srcfile2> ...]
```

### Input Contract

The authoritative source syntax is the shared `cppgm++` source grammar, exposed
for this assignment as `pa10.gram`. The grammar defines accepted syntax only;
the PA10 AST-output requirements are defined by the Required Features and Out Of
Scope sections below.

The grammar uses the same token vocabulary and extended BNF operators as the
PA6 grammar. Where the grammar still needs a syntactic name category before
full semantic lookup exists, the PA6 mock-name convention remains course-defined
scaffolding for this assignment. PA10 should preserve those names in structured
syntax nodes instead of treating the original source text as an opaque span.

The shared grammar intentionally includes syntax whose semantic meaning is added
by later assignments. PA10 is responsible for structured syntax preservation,
not for deciding whether a later semantic or lowering stage would accept the
program.

Behavior is undefined for input that:

- does not match `pa10.gram`
- requires semantic disambiguation beyond the PA10 requirements
- depends on ill-formed-program diagnostics that PA10 is not required to issue

If this README and `pa10.gram` disagree about accepted source syntax, use
`pa10.gram`.

### Output Format

On success, `cppgm++` writes the AST dump to `<outfile>`.

The first line is:

```text
<n> translation units
```

where `<n>` is the number of source files on the command line.

For each translation unit, in command-line order, the output contains:

```text
start translation unit <k>
...
end translation unit
```

where `<k>` is the 1-based translation-unit index.

Between those wrapper lines, write a deterministic line-oriented tree dump
rooted at:

```text
translation-unit
```

The dump must use explicit syntax nodes for supported constructs. The checked-in
`.ref` files define the exact indentation, ordering, spelling, and leaf-token
format expected by the tests. Common node families include:

- declarations such as `empty-declaration`, `simple-declaration`,
  `function-definition`, `class-specifier`, `enum-specifier`,
  `template-declaration`, and `static-assert-declaration`
- declarator and type-id syntax
- statement nodes such as `compound-statement`, `if-statement`,
  `return-statement`, and `expression-statement`
- expression nodes such as `id-expression`, `literal`, `call-expression`,
  `new-expression`, `cast-expression`, and `lambda-expression`
- unresolved syntax nodes that later semantic assignments will classify

PA10 output is a syntax tree, not a semantic dump. For example, base specifiers
and constructor mem-initializers should preserve the unresolved names and
argument syntax, but PA10 does not decide whether a name denotes a base, member,
or delegating constructor target.

Standard output and standard error are ignored by the automated PA10 tests.
They may contain diagnostics or tracing text, but they are not part of the
grading contract.

### Error Handling

If preprocessing, tokenization, or parsing fails, `cppgm++` must exit with
`EXIT_FAILURE`.

The contents of `<outfile>` are unspecified on failure. For failing tests, the
harness compares only the named exit status, not diagnostic text and not the
output file.

### Required Features

PA10 must build structured AST nodes for the source subset that later PA11 and
PA12 passes consume, including:

- translation units in command-line order
- namespace definitions and aliases
- using directives and using declarations
- alias declarations and `typedef`
- simple declarations and function definitions
- class, struct, union, enum, and scoped enum declarations
- class members, access labels, bit-fields, base clauses, and constructor
  initializer syntax
- template declarations with type, non-type, and template-template parameters
- dependent template arguments whose qualified operands participate in
  parenthesized logical expressions, even when the prefix could also begin a
  type-id
- common template-id, explicit-instantiation, and explicit-specialization syntax
- `static_assert`
- declarator-derived syntax, including pointers, references, arrays, function
  parameter clauses, exception specifications, cv/ref qualifiers, default
  arguments, and trailing return types
- structured `type-id` syntax in casts, `new`, `typeid`, `alignof`,
  `sizeof(type)`, and `noexcept`
- compound statements, selection statements, iteration statements, labels,
  `break`, `continue`, `goto`, `return`, `throw`, and `try` / `catch`
- literals, identifiers, parenthesized expressions, calls, subscripts, member
  access, unary and binary operators, conditional expressions, assignments,
  comma expressions, keyword casts, `new` / `delete`, lambdas, `typeid`,
  `alignof`, `sizeof`, `noexcept`, and braced init lists

Unsupported statement or expression forms inside the PA10 boundary should fail
during parsing rather than being preserved as opaque placeholder nodes.

### Out Of Scope

PA10 does not require:

- name lookup
- type checking
- overload resolution
- conversion ranking
- template argument deduction
- semantic validation or constant evaluation of non-type template parameters
  and arguments
- complete parsing of every C++11 corner case
- semantic resolution of `template-id` versus `<`
- semantic resolution of ambiguous template arguments such as `foo<T*p>`

Those topics belong to later semantic and template assignments.

### Testing And Grading Contract

The PA10 harness discovers every `.t` file under the requested test root.
For each test case `x.t`, it runs:

```sh
cppgm++ --emit-ast -o x.my x.t
```

and records `x.my.exit_status`.

Comparison rules:

- `x.my.exit_status` must match `x.ref.exit_status`.
- If the reference status is `EXIT_FAILURE`, the test passes after the exit
  status comparison.
- If the reference status is `EXIT_SUCCESS`, `x.my` must match `x.ref` exactly.
- Standard output and standard error are not compared.

The local suite is split by role:

- `tests/spec/` contains small tests tied to specific C++11 syntax clauses.
  These files begin with an `N3485 focus` comment.
- `tests/general/` contains broader parser tests, cross-feature syntax
  combinations, and useful intake cases that are not a single-clause oracle.

### Design Notes (Non-Normative)

The simplest successful design keeps these concerns separate:

- preprocessing and token preparation
- recursive-descent or table-driven parsing
- AST node ownership
- deterministic AST printing

Plan for ambiguity. Some C++ syntax cannot be classified fully until later
semantic passes exist. PA10 should still preserve the required structure in the
tree so later assignments can classify it without reparsing source text.

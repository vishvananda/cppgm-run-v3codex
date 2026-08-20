## CPPGM Programming Assignment 11 (`cppgm++ --emit-types`)

### Overview

Extend `cppgm++` with the PA11 type/scope dump mode:

```sh
cppgm++ --emit-types -o <outfile> <srcfile1> [<srcfile2> ...]
```

The program reads one or more C++ source files, runs translation phases 1
through 7, parses them using the PA10 syntax boundary, and writes a
deterministic description of the first semantic layer: scopes, declarations,
bindings, and canonical types.

PA11 builds on PA10. The `--emit-ast` mode remains required, and PA11 adds
`--emit-types`.

### Prerequisites

Complete PA10 before starting PA11. You should expect to reuse:

- the PA1-PA5 preprocessing and tokenization pipeline
- the PA10 AST as the syntax boundary
- canonical type and declaration helpers from PA7/PA8 where they fit
- scope and lookup machinery from earlier semantic assignments

PA11 is not a program-image or initialization assignment. Keep the focus on
scope formation, lookup, declaration binding, and declarator-derived type
construction.

### Starter Kit

The PA11 starter kit contains:

- `README.md`, this assignment handout
- `Makefile`, which builds `cppgm++` and runs the PA11 tests
- `cppgm++.cpp`, a link to the editable `dev/cppgm++.cpp` entry point
- the grammar for this assignment called `pa11.gram`
- an HTML grammar explorer of `pa11.gram` in the sub-directory `grammar/`
- `scripts/run_all_tests.pl` and `scripts/compare_results.pl`
- `tests/spec/`, clause-anchored scope/type tests
- `tests/general/`, broader scope/type tests
- checked-in `.ref` and `.ref.exit_status` files used as the oracle

Your main editable file is `dev/cppgm++.cpp`. You may add or change other
implementation files under `dev/` as needed. Do not edit the test inputs,
reference outputs, harness scripts, or grammar files unless course staff
explicitly asks for that.

The starter `dev/cppgm++.cpp` is the same long-lived `cppgm++` dispatcher used
from PA10 onward. For PA11, extend it so `--emit-types` runs your scope/type
analysis and dump path.

There is no required `cppgm++-ref` binary for PA11. The checked-in
reference files under `tests/` are the grading oracle.

### Build And Test Commands

From the `pa11/` directory:

```sh
make
make test
```

`make` builds `cppgm++`. `make test` runs the local PA11 suite. If a
`course/pa11` extension suite is present, the Makefile runs it after the local
suite using the same harness contract.

### Required Driver Surface

Previously required:

- `--emit-ast`
- `-o <outfile>`

New in PA11:

- `--emit-types`

No new compile or link driver flags are introduced in PA11. Behavior is
undefined unless the command line has this form:

```sh
cppgm++ --emit-types -o <outfile> <srcfile1> [<srcfile2> ...]
```

### Input Contract

The authoritative source syntax is the shared `cppgm++` source grammar, exposed
for this assignment as `pa11.gram`. The grammar defines accepted syntax only;
the PA11 scope/type requirements are defined by the Required Features and Out Of
Scope sections below.

Passing PA10 syntax is necessary but not sufficient for PA11: a program may
parse successfully and still require declaration, type, or constant-expression
behavior that this assignment does not define.

The PA6/PA10 mock-name convention still applies where pure syntax needs a
type-like name before full semantic disambiguation exists.

Behavior is undefined for input that:

- does not match the PA11 grammar
- requires PA11 semantic features outside the assignment boundary below
- is ill formed in a way PA11 is not required to diagnose

If this README and `pa11.gram` disagree about accepted source syntax, use
`pa11.gram`. If they disagree about the PA11 semantic slice, use this README.

### Output Format

On success, `cppgm++` writes the PA11 semantic dump to `<outfile>`.

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

Between those wrapper lines, write a deterministic semantic dump rooted at:

```text
translation-unit
```

The body of the dump is a scope tree. Scope lines include:

```text
scope namespace <name>
scope template-parameters
scope class <name>
scope enum <name>
scope function <name>
scope block
```

Bindings inside a scope are written one per line, using forms such as:

```text
type <name> <type>
type-alias <name> <type>
enumerator <name> <type> <value>
function <name> <type>
variable <name> <type>
parameter <name> <type>
```

`<type>` uses the canonical type spelling established in the earlier semantic
assignments and fixed by the PA11 `.ref` files.

Using declarations that introduce a visible type name are emitted as
`type-alias` bindings in the current scope. Supported value-name using
declarations are emitted through the resulting `function`, `variable`,
`parameter`, or `enumerator` binding rather than through a separate
`using-declaration` line.

Namespace aliases and using directives affect lookup. They do not need their
own output line unless the checked-in reference format for a test requires one.

Standard output and standard error are ignored by the automated PA11 tests.

### Error Handling

If preprocessing, tokenization, parsing, or PA11 semantic analysis fails,
`cppgm++` must exit with `EXIT_FAILURE`.

The contents of `<outfile>` are unspecified on failure. For failing tests, the
harness compares only the named exit status, not diagnostic text and not the
output file.

### Required Features

PA11 must support:

- namespace, template-parameter, class, enum, function, and block scopes
- named namespace reopening
- named class declarations and forward declarations, including compatible
  `struct` / `class` redeclarations of the same non-union class
- named enum declarations and scoped opaque enum declarations
- elaborated class and enum type specifiers in supported declarations;
  elaborated class lookup may find a type hidden by an ordinary-name binding,
  while an elaborated enum specifier must name an existing enumeration
- namespace-scope anonymous class, union, and enum specifiers when the same
  declaration immediately introduces a usable type name
- template declarations with type and template-template parameter scopes
- simple declarations and function definitions
- `typedef` and alias declarations
- namespace aliases
- using directives
- using declarations that introduce supported type or value names
- storage and function specifiers that do not change the PA11 type model:
  `extern`, `static`, `thread_local`, `inline`, `virtual`, and `constexpr`
- free-function declarators with supported exception specifications
- unqualified lookup of visible type aliases, template type parameters,
  namespaces, and supported value names
- qualified lookup through namespaces, class scopes, and scoped enum scopes for
  the supported declaration forms
- fundamental, class, enum, cv-qualified, pointer, reference, array, and
  function types
- array bounds formed from positive integer literals, `sizeof(type-id)`, and
  `alignof(type-id)`
- semantic disambiguation of `sizeof(T)` when lookup determines that `T` names a
  type
- enumerator values with the supported simple integral constant-evaluation
  subset, including short-circuit `&&` and `||` evaluation that does not
  evaluate an unselected operand
- `static_assert` over the supported integral constant-expression subset
- `constexpr` object declarations treated as `const` objects for PA11 type and
  constant-value purposes
- the supported `decltype(...)` forms listed in the tests and reference output
- deterministic scope-tree output

The PA11 output should preserve enough declaration and type information for PA12
to add expression and call semantics without reparsing the source.

### Out Of Scope

PA11 does not require:

- overload sets or overload resolution
- expression typing
- general `sizeof`, `alignof`, or `decltype` beyond the type-forming cases
  required by this assignment
- template-aware semantic disambiguation of PA10 syntax ambiguities
- non-type template parameter binding, template specialization modeling, or
  template instantiation semantics
- floating-point or pointer constant evaluation
- full constant-expression semantics
- opaque unscoped enum declarations such as `enum E;`
- semantic analysis of statements beyond creating nested block scopes

Inputs that rely on those features have undefined behavior for PA11.

### Testing And Grading Contract

The PA11 harness discovers every `.t` file under the requested test root.
For each test case `x.t`, it runs:

```sh
cppgm++ --emit-types -o x.my x.t
```

and records `x.my.exit_status`.

Comparison rules:

- `x.my.exit_status` must match `x.ref.exit_status`.
- If the reference status is `EXIT_FAILURE`, the test passes after the exit
  status comparison.
- If the reference status is `EXIT_SUCCESS`, `x.my` must match `x.ref` exactly.
- Standard output and standard error are not compared.

The local suite is split by role:

- `tests/spec/` contains small tests tied to specific C++11 scope, declaration,
  lookup, or type-formation clauses. These files begin with an `N3485 focus`
  comment.
- `tests/general/` contains broader PA11 scope/type tests, cross-feature
  semantic combinations, and useful intake cases that are not a single-clause
  oracle.

### Design Notes (Non-Normative)

A good PA11 design keeps these pieces separate:

- AST traversal from PA10
- declaration collection
- scope ownership and parent/child relationships
- lookup
- declarator-derived type construction
- deterministic printing

Keep the semantic graph and analyzer reusable by later modes. The PA11 dump
can be a source-facing view over that shared graph: distinguish source
declarations from implicit implementation facts with typed metadata, and keep
lookup/type identity separate from presentation. Avoid rebuilding semantic
names by parsing rendered strings.

Avoid baking PA8 image-construction or initialization behavior into the PA11
core. Those ideas become useful again later, but this assignment should leave
behind a reusable scope/type model.

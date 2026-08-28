## CPPGM Programming Assignment 12 (`cppgm++ --emit-semantics`)

### Overview

Extend `cppgm++` with the PA12 call-semantics dump mode:

```sh
cppgm++ --emit-semantics -o <outfile> <srcfile1> [<srcfile2> ...]
```

The program reads one or more C++ source files, runs translation phases 1
through 7, parses them using the PA10 syntax boundary, applies the PA11
scope/type model, and writes a deterministic semantic dump for the procedural
expression, statement, conversion, and non-template call subset.

PA12 builds on PA10 and PA11. The earlier `--emit-ast` and `--emit-types` modes
remain required, and PA12 adds `--emit-semantics`.

### Prerequisites

Complete PA11 before starting PA12. You should expect to reuse:

- the PA1-PA5 preprocessing and tokenization pipeline
- the PA10 AST
- PA11 scope formation and lookup
- PA11 declarator-derived type construction
- the canonical type spelling used by the earlier semantic assignments

PA12 is the first call-semantics milestone. It is deliberately limited to the
procedural, non-template, non-class-aware subset. Class-aware calls,
constructors, user-defined conversions, overloaded operators, and template
functions are assigned later.

### Starter Kit

The PA12 starter kit contains:

- `README.md`, this assignment handout
- `Makefile`, which builds `cppgm++` and runs the PA12 tests
- `cppgm++.cpp`, a link to the editable `dev/cppgm++.cpp` entry point
- the grammar for this assignment called `pa12.gram`
- an HTML grammar explorer of `pa12.gram` in the sub-directory `grammar/`
- `scripts/run_all_tests.pl` and `scripts/compare_results.pl`
- `tests/spec/`, clause-anchored call/conversion/control-flow tests
- `tests/general/`, broader call-semantics tests
- checked-in `.ref` and `.ref.exit_status` files used as the oracle

Your main editable file is `dev/cppgm++.cpp`. You may add or change other
implementation files under `dev/` as needed. Do not edit the test inputs,
reference outputs, harness scripts, or grammar files unless course staff
explicitly asks for that.

The starter `dev/cppgm++.cpp` is the same long-lived `cppgm++` dispatcher used
from PA10 onward. For PA12, extend it so `--emit-semantics` runs your resolved
semantic-analysis and dump path.

There is no required `cppgm++-ref` binary for PA12. The checked-in
reference files under `tests/` are the grading oracle.

### Build And Test Commands

From the `pa12/` directory:

```sh
make
make test
```

`make` builds `cppgm++`. `make test` runs the local PA12 suite. If a
`course/pa12` extension suite is present, the Makefile runs it after the local
suite using the same harness contract.

### Required Driver Surface

Previously required:

- `--emit-ast`
- `--emit-types`
- `-o <outfile>`

New in PA12:

- `--emit-semantics`

No new compile or link driver flags are introduced in PA12. Behavior is
undefined unless the command line has this form:

```sh
cppgm++ --emit-semantics -o <outfile> <srcfile1> [<srcfile2> ...]
```

### Input Contract

The authoritative source syntax is the shared `cppgm++` source grammar, exposed
for this assignment as `pa12.gram`. The grammar defines accepted syntax only;
the PA12 procedural semantic requirements are defined by the Required Features
and Out Of Scope sections below.

Passing PA10 and PA11 is necessary but not sufficient for PA12: a program may
parse and form declarations successfully while still relying on call or
expression semantics outside this assignment.

Behavior is undefined for input that:

- does not match the PA12 grammar
- requires PA12 semantic features outside the assignment boundary below
- is ill formed in a way PA12 is not required to diagnose

If this README and `pa12.gram` disagree about accepted source syntax, use
`pa12.gram`. If they disagree about the PA12 semantic slice, use this README.

### Output Format

On success, `cppgm++` writes the PA12 semantic dump to `<outfile>`.

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

Top-level nodes include:

```text
type-alias <name> <type>
variable <name> <type>
function-declaration <name> <type>
function-definition <name> <type>
namespace-definition <name>
```

Function definitions contain resolved statement and expression nodes such as:

```text
parameter <name> <type>
compound-statement
simple-declaration
return-statement
if-statement
while-statement
for-statement
break-statement
continue-statement
condition
condition-declaration
call-expression <value-category> <type>
callee <name> <type>
id-expression <value-category> <type> <name>
literal <value-category> <type> <token>
unary-expression <value-category> <type> <operator>
binary-expression <value-category> <type> <operator>
subscript-expression <value-category> <type>
conditional-expression <value-category> <type>
sizeof-expression <value-category> <type>
assignment-expression <value-category> <type> OP_ASS:=
constructor-action <name>
destructor-action <name>
```

`<type>` uses the canonical type spelling from PA11. `<value-category>` is one
of:

```text
lvalue
prvalue
xvalue
```

The PA12 tests primarily exercise `lvalue` and `prvalue`.

Namespace aliases, using directives, and using declarations affect lookup, but
they do not necessarily have dedicated output lines. Their effect is visible in
the resolved declarations and expression subtrees.

Standard output and standard error are ignored by the automated PA12 tests.

### Error Handling

If preprocessing, tokenization, parsing, or PA12 semantic analysis fails,
`cppgm++` must exit with `EXIT_FAILURE`.

The contents of `<outfile>` are unspecified on failure. For failing tests, the
harness compares only the named exit status, not diagnostic text and not the
output file.

### Required Features

PA12 must support:

- namespace-scope simple declarations, alias declarations, function
  declarations, and function definitions
- named, inline, and unnamed namespace definitions, namespace aliases, using
  directives, and using declarations, with same-scope namespace/ordinary-name
  conflicts rejected
- type aliases used by the PA12 slice
- fundamental, pointer, reference, array, and function types
- function parameter scopes, nested block scopes, and the separate scopes of
  unbraced selection/iteration substatements
- local simple declarations
- block-scope using declarations and using directives
- supported ordinary anonymous-union local declarations
- unqualified and qualified lookup of namespace-scope non-template functions
- unqualified lookup extended by using directives, using declarations, namespace
  aliases, and unnamed-namespace visibility
- calls through function names, function references, and function pointers
- target-directed resolution of overloaded function names in contexts such as
  function-pointer initialization and function-pointer arguments
- overload resolution using the assignment's limited standard-conversion subset:
  identity, lvalue-to-rvalue, top-level cv stripping for by-value arguments,
  array-to-pointer, function-to-pointer, common integral promotions and
  conversions, pointer-to-bool, `nullptr_t` to pointer, pointer qualification,
  object pointer to cv-qualified `void*`, and the supported lvalue-reference
  bindings
- function redeclaration matching after top-level parameter cv normalization,
  with conflicting return types and duplicate definitions rejected
- recursive pointer-qualification conversion checks, including rejection when
  the intermediate const qualification required by a deep conversion is absent
- copy-initialization for local variables, condition declarations, and returns
  using that same conversion subset
- integer literals, `true`, `false`, and `nullptr`
- id-expressions for parameters, locals, and supported globals
- parenthesized expressions
- unary `+`, `-`, `!`, `~`, `&`, `*`, prefix `++`, and prefix `--`
- postfix `++` and postfix `--`
- built-in arithmetic, bitwise, shift, logical, comparison, equality,
  conditional, comma, assignment, and compound-assignment expressions over the
  supported operand categories
- conditional-expression typing and value-category selection for the supported
  scalar cases, including mixed `bool` lvalue/prvalue operands
- pointer arithmetic and pointer comparisons in the ordinary object-pointer
  cases required by the tests
- built-in subscript expressions on arrays and pointers
- explicit casts over the supported integral, enum, pointer, and `nullptr`
  subset
- `sizeof(expr)` and `sizeof(type-id)`
- compound statements, `if` / `else`, `switch`, `while`, `do`, `for`, `break`,
  and `continue`
- expression conditions and declaration conditions of the form `T x = expr`
- supported integral `constexpr` complete objects, enumerator constants, the
  course-supported `__builtin_constant_p` query over propagated integral
  expressions, and semantic recognition of a zero-argument `__builtin_abort`
  call (without requiring its later control-flow lowering); passing arguments
  to `__builtin_abort` is rejected
- rejection of type, call-arity, and control-flow violations within this
  supported slice, including mismatched indirect-call arity, nonconstant case
  labels, `break` or `continue` outside a permitted statement, `default`
  outside a switch, a value returned from a `void` function, invalid
  scoped-enum conditions, and invalid pointer/integer equality or pointer
  multiplication
- deterministic resolved-expression output

The PA12 output should preserve enough information for later assignments to add
class-aware conversion ranking and richer overload resolution without reparsing
the source.

### Out Of Scope

PA12 does not require:

- class-aware call resolution
- member function calls or implicit object parameters
- overloaded operators
- constructor selection
- user-defined conversions
- reference binding beyond the basic cases listed above
- full standard conversion ranking
- template functions or template-aware overload resolution
- floating-point, string, or user-defined literals
- general callable-object semantics beyond plain functions and function
  pointers
- statement forms beyond the supported control-flow subset, including `goto`,
  `throw`, and `try`
- semantic support for classes, enums, templates, or `decltype` beyond what is
  needed by this assignment

Inputs that rely on those features have undefined behavior for PA12.

### Testing And Grading Contract

The PA12 harness discovers every `.t` file under the requested test root.
For each test case `x.t`, it runs:

```sh
cppgm++ --emit-semantics -o x.my x.t
```

and records `x.my.exit_status`.

Comparison rules:

- `x.my.exit_status` must match `x.ref.exit_status`.
- If the reference status is `EXIT_FAILURE`, the test passes after the exit
  status comparison.
- If the reference status is `EXIT_SUCCESS`, `x.my` must match `x.ref` exactly.
- Standard output and standard error are not compared.

The local suite is split by role:

- `tests/spec/` contains small tests tied to specific C++11 calls,
  conversions, initialization, overload-resolution, or control-flow clauses.
  These files begin with an `N3485 focus` comment.
- `tests/general/` contains broader PA12 call-semantics tests,
  cross-feature semantic combinations, and useful intake cases that are not a
  single-clause oracle.

### Design Notes (Non-Normative)

A good PA12 design keeps these pieces separate:

- PA11 scope/type analysis
- expression analysis
- conversion classification
- overload candidate collection
- overload ranking for the limited PA12 subset
- statement-scope construction
- deterministic semantic printing

Treat the PA12 call layer as a base that later class and template assignments
will extend. Avoid hard-coding assumptions that only work before member
functions, constructors, user-defined conversions, or templates are introduced.

Keep compiler-generated identities separate from ordinary source lookup.
Anonymous entities should receive stable typed identities rather than names
that are re-parsed or inserted into the source identifier namespace.

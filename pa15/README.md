## CPPGM Programming Assignment 15 (`cppgm++ --emit-lowir`)

### Overview

Write a C++ application called `cppgm++` that takes as input a set of C++ Source Files,
executes translation phases 1 through 7, parses them as PA10 translation units, applies the
PA12 procedural semantic layer, and writes LowIR text.

PA15 is the point where `cppgm++` gains its first LowIR output mode. The earlier
`--emit-ast`, `--emit-types`, and `--emit-semantics` modes remain required.

The goal of this assignment is to establish the compiler's real backend boundary before the
later object-model and template milestones extend lowering further. PA15 completes the
non-class procedural lowering stage over the current PA12 semantic boundary: it lowers
namespace-scope functions, procedural expressions, control flow, and the supported
scalar/pointer global state into the PA13 LowIR subset.

### Prerequisites

You should complete Programming Assignment 14 before starting this assignment.

You will want to reuse:

- the preprocessing and tokenization pipeline from PA1-PA6
- the PA10 AST as the syntax boundary
- the PA11 declarator/type model
- the PA12 procedural semantic analysis as the source of truth for resolved functions,
  locals, and expressions
- the PA13 LowIR contract
- the PA13 LowIR -> CY86 path as an optional secondary scaffold
- the PA14 typed ABI-name model and encoder

The intended direction is:

- PA10 provides syntax
- PA11 provides scopes and types
- PA12 resolves procedural expressions and calls
- PA13 defines the backend boundary and runnable validation scaffold
- PA14 provides ABI names from typed semantic facts
- PA15 lowers the resolved procedural subset into LowIR

### Starter Kit

The starter kit contains:

- the student-editable `../dev/cppgm++.cpp` entry point, initially seeded from the course
  `cppgm++` scaffold and reached from this directory through the `cppgm++.cpp` symlink
- shared `../dev/` and `../dev/src/` support code from the earlier compiler pipeline
- a local test suite
- the grammar for this assignment called `pa15.gram`
- an HTML grammar explorer of `pa15.gram` in the sub-directory `grammar/`
- checked-in golden output files under `tests/`
- a checked-in local test suite under `tests/`

The provided scaffold and shared support files establish the driver shape and previous
frontend modes. They do not implement the PA15 source-to-LowIR lowering work.

Unlike PA1-PA9, there is no external reference binary for PA15. The checked-in `.ref`
files are the default oracle.

### Driver Surface For This Assignment

Previously required:

- `--emit-ast`
- `--emit-types`
- `--emit-semantics`
- `-o <outfile>`

New in PA15:

- `--emit-lowir`
- `-O0` as the unoptimized LowIR test mode

No practical compile/link driver flags are introduced here yet. That later
surface starts in PA14.

### Input / Command-Line Arguments

The same as PA12 `cppgm++ --emit-semantics`, with the new LowIR emit mode. The PA15 test mode is unoptimized LowIR generation. `make test` passes `--emit-lowir -O0`
through the harness, so individual test files do not spell those flags themselves.

Behaviour is undefined unless the command-line arguments match:

    $ cppgm++ --emit-lowir -O0 -o <outfile> <srcfile1> <srcfile2> ... <srcfileN>

with the same relaxations as PA12.

Accepting `--emit-lowir` without an explicit `-O0` as the same unoptimized mode is fine,
but optimized LowIR output is not part of PA15.

### Output Format

`cppgm++` shall write LowIR text to `<outfile>`.

The authoritative LowIR definition is `../pa13/lowir.md`. PA15 only needs the procedural
subset of that IR, but it must emit valid PA13 LowIR.

Example:

    function @main() -> i64 {
      block ^entry:
        return i64 0
    }

PA15 writes a single concatenated LowIR program consisting of:

- zero or more `global` definitions
- zero or more `function` definitions

LowIR top-level declaration/definition order is a presentation convention, not
a dependency order. Reference outputs and canonical dumps use the order defined
in `../pa13/lowir.md`: `declare global`, `declare function`, `global`, then
`function`, but the relaxed LowIR comparison canonicalizes top-level entries
before comparison. Your output must still be repeatable for the same
inputs; `../pa13/lowir.md` defines the canonical reference presentation and
notes where internal LowIR symbol names are only a presentation tie-breaker.
Externally meaningful C++ symbols must be produced through PA14's shared typed
ABI encoder. Build the encoder target from resolved declarations and types;
the ABI fact-file parser is a standalone-tool adapter and is not part of the
source-to-LowIR path.

The in-memory LowIR program uses compact semantic identity. Assign each
top-level symbol one `SymbolId`, store its presentation spelling once through a
program `StringId`, and use that `SymbolId` in declarations, definitions,
operands, structured-global addresses, and alias targets. Render the spelling
only when writing LowIR text or a diagnostic; do not copy an owning symbol name
into each record or reference.

Your output must also preserve order-sensitive LowIR regions when they are present: instruction order inside
blocks, item order inside structured globals, vtable slot order, and action
order inside generated initialization, finalization, constructor, destructor,
and cleanup bodies.

PA15 is still a purely procedural lowering stage. Its LowIR output should not include
class/object-model helper definitions such as synthesized constructors, destructors, copy
helpers, or class-lifetime startup/shutdown hooks. Tests that require those belong in PA16
or later.

The checked-in `.ref` files define the required LowIR facts for the tests. The
test harness checks exit status, LowIR well-formedness, and the
course-defined normalized LowIR output rather than requiring students to match every
non-semantic helper spelling or presentation choice.

For supported scalar conversions, PA15 may canonicalize widened integral immediates directly
to their final LowIR literal value instead of spelling those same conversions through
intermediate `binary shl` / `binary shr` sign-extension shells.

For built-in `&&` / `||` used directly as statement conditions (`if`, `while`, `do`, `for`),
the expected LowIR shape is direct short-circuit control flow. In that condition context,
the compiler should branch through the operand blocks rather than first materializing a
separate `land__*` / `lor__*` boolean slot.

The generated LowIR is intended to become input for the later PA29
`lowir2native` backend. That future native path is not the PA15 grading
contract, but PA15 should avoid emitting LowIR that only works for this one
text comparison.

The PA13 scaffold path is useful as an optional manual execution check:

    cppgm++ --emit-lowir -> LowIR
    lowir2cy86 -> CY86
    cy86 -> executable

That runnable path is a debugging aid, not the primary PA15 output contract.

### Error Handling

If an error occurs during preprocessing, tokenization, parsing, semantic analysis, or LowIR
generation, `cppgm++` shall `EXIT_FAILURE`.

The output file is not required to be meaningful on failure.

### Standard Output / Error

Standard output and standard error are ignored for automated testing of `cppgm++`.

You are free to use them for debugging, tracing, or diagnostic messages.

### Testing

Testing uses checked-in golden outputs, not a reference binary.

For each test case `x`:

- `cppgm++` is executed to produce `x.my`
- the exit status is recorded in `x.my.exit_status`
- `x.my` is validated as LowIR and compared against `x.ref` using the normalized
  LowIR comparison
- `x.my.exit_status` is compared against `x.ref.exit_status`

`make test` runs the checked-in local suite under `tests/` and supplies
`--emit-lowir -O0` through the harness.

The PA15 test suite uses:

- `tests/general/`: the default PA15 LowIR oracle suite. These tests cover the procedural
  lowering contract and integration cases that are validated by generated LowIR
  text and exit status. The covered source features are namespace functions and
  globals, procedural statements, condition declarations, scalar expressions,
  references, arrays, pointer operations, enums, built-in casts, and resolved
  calls over the PA12 semantic subset.

PA15 is tested against the generated LowIR text.

### PA15 Syntax Spec

The authoritative source syntax is the shared `cppgm++` source grammar, exposed
for this assignment as `pa15.gram`. The grammar defines accepted syntax only;
the PA15 semantic and lowering requirements are defined by the Assignment
Boundary and Out Of Scope sections below.

As in the earlier assignments, that grammar defines accepted input syntax only. The output
format for `cppgm++` is specified by this README, PA13 `lowir.md`, and the checked-in
`.ref` files.

Because PA15 is a code-generation assignment layered directly on PA10-PA12, the
grammar keeps parser/AST behavior stable while the `Assignment Boundary` below
defines which already-parsed constructs PA15 must analyze and lower.

Passing PA12 is necessary but not sufficient for passing PA15: an input may be syntactically
valid for PA10 and semantically valid for PA12 and still be outside the PA15 code-generation
subset described below.

A checked-in HTML grammar explorer for that grammar lives in `grammar/`. Treat
`pa15.gram` as the source of truth.

`pa15.gram` uses the same token vocabulary and the same extended BNF operators as
`../pa6/pa6.gram`.

If this README and `pa15.gram` appear to disagree about source syntax, treat `pa15.gram`
as authoritative. If this README and PA13 `lowir.md` appear to disagree about LowIR syntax,
treat `lowir.md` as authoritative. If they disagree about the required PA15 lowering slice,
treat the `Assignment Boundary` and `Out Of Scope` sections below as authoritative.

### Assignment Boundary

This PA15 milestone supports the following:

- namespace-scope function definitions and declarations in a single generated program,
  including named namespaces, C language linkage, and deduplication of repeated
  compatible declarations
- a required `main` definition
- functions returning integral, pointer, or `bool` results from the supported PA12 subset
- up to four parameters in the supported PA12 procedural type subset
- global integral/pointer/function-pointer objects with constant initializers or zero-init,
  including object addresses and constant array-element addresses
- internal namespace-scope `const` scalar objects represented as
  `storage=readonly` in LowIR when they are neither volatile nor
  `thread_local`; volatile scalars, class objects, and thread-local objects
  retain their respective conservative storage contracts
- volatile scalar lvalue-to-rvalue conversions and stores represented by
  `load volatile` and `store volatile` at the LowIR boundary, including local,
  pointer-indirect, and class-member access; the marker belongs to the access,
  while an adjacent nonvolatile member access remains ordinary
- recognized memory builtins use ordinary pointer parameters and preserve the
  function-level runtime identity and effects needed by later stages;
  non-overlapping `memcpy` boundaries carry `alias=noalias`, while potentially
  overlapping `memmove` boundaries do not
- local scalar objects, scalar/function references, function pointers/references, and bounded
  arrays in the supported PA12 procedural type subset; an omitted array bound is inferred
  from its initializer, missing elements are zero-initialized, and excess elements are
  rejected; an `extern` array of unknown bound may be referenced without requiring its
  layout in the current translation unit
- expression statements
- `return`
- `if` / `else`
- condition declarations in `if` and `switch`, including the lifetime of the
  condition-scope binding
- `switch`
- `while`
- `do`
- `for`
- `break` / `continue`
- direct calls to resolved non-template namespace-scope functions, including supported
  default arguments resolved in the declaration context where the default was introduced
- calls through function pointers and function references in the PA12 subset
- lvalue references, including reference parameters, reference locals, reference
  returns, and aliasing through supported calls
- reference parameters use LowIR's shared `ptr [pass=by_address]` boundary:
  callers preserve the required addressable-storage behavior without retaining
  a separate source-reference passing label
- array-to-pointer decay, subscript expressions, pointer arithmetic, one-past
  pointer values, pointer compound assignment with element-size scaling, and
  pointer differences measured in elements; because the byte difference of
  two pointers into the same array is exactly divisible by the element size,
  a positive power-of-two size may be lowered as an arithmetic right shift,
  while other element sizes retain signed division
- array-to-pointer and function-to-pointer decay produce an ordinary LowIR
  `ptr` using the existing address, index, parameter, or `copy ptr` operations;
  do not add a decay-specific unary operation or parameter-passing annotation
- scoped and unscoped enums, enum constants, enum promotion/comparison, and
  enum lowering
- built-in casts over the supported scalar, function, reference, and pointer
  types, including C-style casts, `static_cast`, and `const_cast`
- source-to-LowIR floating scalar literals and conversions among supported
  scalar types, including float/integer conversions needed for calls, returns,
  comparisons, and branch conditions
- C-style variadic function calls over supported scalar arguments, including
  source-to-LowIR default argument promotion before the call
- expressions:
  - integer literals, floating literals, and `true` / `false`
  - id-expressions naming supported locals, globals, and resolved functions
  - `sizeof(expr)` and `sizeof(type-id)` when PA12 has resolved them
  - unary `+`, `-`, `!`, `~`, `&`, `*`, prefix `++`, and prefix `--`
  - postfix `++` and postfix `--`
  - simple assignment to supported lvalues
  - built-in arithmetic, bitwise, shift, logical, comparison, conditional, comma, and
    subscript forms from the PA12 procedural subset

As required by PA13, every LowIR `cmp` instruction produces an `i64` truth
value. When a comparison or logical expression must be materialized as the
course `bool` representation (`u8`) for storage, an argument, or a return,
emit an explicit conversion from that `i64` result. A branch may consume the
canonical comparison result directly.

Compiler-generated slots and helper names must remain distinct from source
identifiers so a source declaration cannot redirect an internal temporary.
The source lowering path should carry compact value, slot, block, and symbol
identities into the shared typed LowIR model. Store required display spellings
once in the program string pool, and retain a numeric ordinal for generated
temporaries; do not construct or hash a presentation string for every operand
reference.

The generated LowIR for this supported subset is intended to be accepted by the
later PA29 `lowir2native` backend. PA13 `lowir2cy86` remains a useful optional
execution scaffold, not the primary validation path.

### Out Of Scope

The following are explicitly out of scope for this PA15 milestone:

- string literals and string-literal-backed object initialization
- global or local initialization forms that require a richer constant-evaluation or aggregate
  initialization layer than PA12 currently provides
- function-local static objects and guard variables
- class/object semantics
- synthesized class helper output of any kind
- template code generation
- exception-aware control flow
- fully general shadowing-sensitive lowering of same-name local bindings
- native backend/runtime parity for floating-point conversions and variadic promotions
- hosted or vendor integer extensions such as 128-bit integer types

Inputs that rely on those features have undefined behaviour for this milestone.

### Stage Handoff

The intended next stages are:

- PA16: extend this procedural lowering path into the basic non-virtual object model:
  object layout, methods, constructors/destructors, lifetime, and single inheritance
- PA17: build on that PA16 object model with non-polymorphic value semantics:
  copy construction/assignment, pass-by-value, return-by-value, and the common
  user-defined operator paths needed by value types
- PA18: add the polymorphic machinery on top of the PA16/PA17 class model:
  virtual dispatch, vtables, and virtual destructors
- later template-aware assignments: reuse the same procedural lowering path for instantiated
  template code once template semantics exist

So PA15 should leave behind a reusable procedural `C++ -> LowIR` lowering path rather than
trying to absorb class or template semantics early.

### Design Notes (Non-Normative)

The cleanest reuse path is to keep PA12 as the semantic source of truth and lower from that
resolved procedural representation rather than rebuilding expression semantics again inside
PA15.

Useful intermediate representations include:

- a resolved procedural tree shared with PA12
- explicit object identities for globals, locals, references, arrays, and
  function objects
- explicit local slot/layout information
- a centralized type-to-LowIR lowering and conversion layer
- a stable mapping from resolved expressions to LowIR values and stack locations

It is useful for the lowering layer to derive result types from the LowIR
operation as it creates a temporary. In particular, keeping the canonical
`i64` comparison result there avoids duplicating result-type decisions at
each later use.

## CPPGM Programming Assignment 17 (`cppgm++ --emit-lowir`)

### Overview

Write a C++ application called `cppgm++` that takes as input a set of C++ Source Files,
executes translation phases 1 through 7, parses them as PA10/PA17 translation units,
reuses the PA11-PA12 semantic foundation, builds on the PA15-PA16 LowIR lowering path, and
writes LowIR text.

PA17 finishes the non-polymorphic class model so ordinary user-defined value types work
cleanly before virtual dispatch is added. It extends PA16 with the common value-semantics
paths:

- copy construction/assignment and the common move-construction/move-assignment cases
  needed by those same value paths
- pass-by-value and return-by-value of class objects
- temporary materialization in the common call/return/initialization paths
- delegating constructors
- out-of-class constructor and destructor definitions
- the ordinary user-defined copy/move constructors and assignment operators directly
  needed by that value-semantics work

### Prerequisites

You should complete Programming Assignment 16 before starting this assignment.

You will want to reuse:

- the preprocessing and tokenization pipeline from PA1-PA6
- the PA10 AST as the syntax boundary
- the PA11 declarator/type model
- the PA12 call-resolution layer
- the PA15/PA16 LowIR lowering path
- the PA13 LowIR contract
- the PA13 LowIR -> CY86 path as an optional secondary scaffold
- the PA16 class metadata, constructor/destructor machinery, and lifetime lowering

The intended direction is:

- PA10 provides syntax
- PA11 provides scope/type lookup
- PA12 provides the procedural expression/call core
- PA15 lowers the procedural subset
- PA16 adds the basic non-virtual object model
- PA17 extends that same object model into usable value semantics

### Starter Kit

The starter kit contains:

- the student-editable `../dev/cppgm++.cpp` entry point, initially seeded from the course
  `cppgm++` scaffold and reached from this directory through the `cppgm++.cpp` symlink
- shared `../dev/` and `../dev/src/` support code from the earlier compiler pipeline
- a local test suite
- the grammar for this assignment called `pa17.gram`
- an HTML grammar explorer of `pa17.gram` in the sub-directory `grammar/`
- a checked-in local test suite under `tests/`

The provided scaffold and shared support files establish the driver shape and previous
frontend modes. They do not implement the PA17 value-semantics LowIR lowering work.

Unlike PA1-PA9, there is no external reference binary for PA17. The checked-in `.ref`
files are the default oracle.

### Input / Command-Line Arguments

The same as PA16 `cppgm++ --emit-lowir`. The PA17 test mode is unoptimized LowIR
generation. `make test` passes `--emit-lowir -O0` through the harness, so individual test
files do not spell those flags themselves.

Behaviour is undefined unless the command-line arguments match:

    $ cppgm++ --emit-lowir -O0 -o <outfile> <srcfile1> <srcfile2> ... <srcfileN>

with the same relaxations as PA16.

Accepting `--emit-lowir` without an explicit `-O0` as the same unoptimized mode is fine,
but optimized LowIR output is not part of PA17.

### Output Format

`cppgm++` shall write LowIR text to `<outfile>`.

The authoritative LowIR definition is `../pa13/lowir.md`. PA17 extends the PA16 object-model
subset of that IR with the value-semantics lowering needed by this milestone.

PA17 writes a single concatenated LowIR program consisting of:

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

For supported class value types, PA17 extends the PA16 lowering convention by introducing:

- indirect LowIR parameters for pass-by-value class objects
- indirect LowIR return destinations for return-by-value class objects
- explicit LowIR-level materialization of supported copy/move/value transfers

Every pointer boundary that denotes a complete object also carries the PA13
`object_bytes=N` extent in emitted O0 LowIR. This includes the implicit object
parameter of supported member functions, class references and indirect class
arguments, and indirect result storage. An ordinary source pointer does not
gain an extent merely from its pointee type. The extent is semantic LowIR
metadata and must survive text and compiler-object replay; it is not a native
calling-convention annotation.

Synthesized copy/move constructors, assignment helpers, and related
temporary-materialization support are part of the PA17 semantic model, but `cppgm++` only
needs to emit the helper definitions that the lowered program actually requires. Unused
copy/move/value helpers do not need to appear just because they are synthesizable.

The indirect destination and source parameters of a same-class copy or move
constructor denote distinct live objects during construction. Their emitted
LowIR boundary metadata may therefore mark both parameters `alias=noalias`.
Assignment operators do not have that property and must remain conservative.

Focused course controls validate these boundary facts and temporary-lifetime
relationships without comparing a complete LowIR module. They inspect only
the two constructor parameters versus the assignment-operator control, or the
ordering and identity of construction, selected use, and destruction within
one full expression. The lifetime reducer is also compiled and executed.

When a same-type conditional class prvalue is materialized in a private
temporary and then selected for copy or move construction into its final
complete-object destination, PA17 records the standard source-language
permission on that outer direct call as `[elision=copy]`.  This is serialized
optimization information, not an O0 elision: the emitted O0 LowIR retains the
distinct temporary, transfer call, and normal/exceptional destruction.  The
focused control checks those relationships and executes the O0 behavior
without prescribing complete generated LowIR text.

For supported indirect return-by-value cases, PA17 may also lower an eligible top-level
named local directly in `%ret` instead of building a separate local object and then
copying or moving it into the return destination. That direct return-slot form is part of
the accepted PA17 output contract.

Ref-qualified member functions extend the PA16 member-call model: overload resolution still
uses the implicit object argument, and the object expression's value category participates in
viability and ranking for supported `&` and `&&` qualified members.

The ABI identity of a member function is built from its declared source parameters; the
implicit object used by LowIR lowering is not part of that declared parameter list. In
particular, an out-of-class move-assignment definition retains its rvalue-reference parameter
in the ABI identity.

Nested operand and overload analysis must preserve the identity of the
enclosing binary operator and the lifetime of its full expression. Interning
additional candidate or conversion spellings while resolving an overloaded
operator must not change the enclosing operator or its result.

For supported synthesized copy/move special members, PA17 may lower a leading trivially
copyable storage prefix directly as `copyobj <span> <src>, <dst>` instead of spelling that
prefix as separate field operations or a `__builtin_memcpy` helper call in the emitted
LowIR. That direct storage-copy form is also part of the accepted PA17 output contract.

When a synthesized copy/move constructor or assignment body handles adjacent
nonvolatile bit-fields in one supported 8-, 16-, 32-, or 64-bit allocation
unit, it shall transfer that allocation unit once.  A zero-width bit-field or
a change of storage offset or width starts a new unit.  Fields whose layout
cannot be transferred safely shall retain field-wise value semantics.

For supported trivially copy-constructible class value transfers, PA17 may also lower the
copy/move construction step itself directly as `copyobj <span> <src>, <dst>` instead of
spelling a call to a synthesized trivial copy/move constructor helper. That direct
value-transfer form is part of the accepted PA17 output contract.

Supported synthesized constructors, destructors, and copy/move assignment operators may
also carry LowIR boundary metadata such as `[unwind=no]` when the compiler can determine
that the synthesized body is semantically non-throwing. That metadata is part of the
accepted PA17 output contract when it appears in the checked-in `.ref` files.

PA17 also recognizes the argument-free GNU function attribute
`cppgm_stable_prefix` (and its double-underscore spelling). It is valid on a
fixed-arity function with a supported scalar result and a final integer
parameter. The frontend emits the PA13 `[query=stable_prefix]` boundary fact;
`-O0` preserves the call and program behavior. The attribute is a semantic
promise that a normally returning query at a higher or equal final index
preserves the observable result at an already queried lower index for the same
earlier arguments. It does not request an optimization by itself.

For supported synthesized destructors, trivial union subobject destructor steps may be
omitted from enclosing synthesized destructors.

The checked-in `.ref` files define the required LowIR facts for the tests. The
test harness checks exit status, LowIR well-formedness, and the
course-defined normalized LowIR output rather than requiring students to match every
non-semantic helper spelling or presentation choice.

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

The PA17 suite is split by test role:

- `tests/general/`: the default PA17 LowIR oracle suite. These tests cover value-semantics
  lowering, copy/value helper emission, temporary materialization, ABI-shape
  cases, and cross-feature cases whose primary contract is generated LowIR plus
  exit status.
- `tests/spec/`: focused C++ language-contract cases that cite a specific N3485 clause.
  Each source test in this directory starts with a comment of the form:

    // N3485 focus: <clause> [<stable-name>] <short topic>

`tests/spec/` covers the PA17 value-semantics contract: defaulted/deleted
special members, copy/move construction and assignment, ref-qualified member
functions, delegating constructors, allocation expressions, unions, conversion
operators, and class value ABI behavior. `tests/general/` covers
value-semantics and LowIR-shape cases that are not tied to one specific C++11
clause.

PA17 is tested against the generated LowIR text.

### PA17 Syntax Spec

The authoritative source syntax is the shared `cppgm++` source grammar, exposed
for this assignment as `pa17.gram`. The grammar defines accepted syntax only;
the PA17 semantic and lowering requirements are defined by the Assignment
Boundary and Out Of Scope sections below.

As in the earlier assignments, that grammar defines accepted input syntax only. The output
format for `cppgm++` is specified by this README, PA13 `lowir.md`, and the checked-in
`.ref` files.

Syntax for class value-semantics forms, including out-of-class constructor and
destructor definitions, is already part of that grammar; PA17 gives the
supported value-semantics subset semantic and lowering meaning.

Passing PA16 is necessary but not sufficient for passing PA17: an input may be syntactically
valid for PA10-PA16 and code-generation-valid for PA16 and still be outside the PA17
value-semantics slice described below.

A checked-in HTML grammar explorer for that grammar lives in `grammar/`. Treat
`pa17.gram` as the source of truth.

`pa17.gram` uses the same token vocabulary and the same extended BNF operators as
`../pa6/pa6.gram`.

If this README and `pa17.gram` appear to disagree about source syntax, treat `pa17.gram`
as authoritative. If this README and PA13 `lowir.md` appear to disagree about LowIR syntax,
treat `lowir.md` as authoritative. If they disagree about the PA17 lowering slice, treat the
`Assignment Boundary` and `Out Of Scope` sections below as authoritative.

### Assignment Boundary

PA17 supports the following in addition to the PA16 subset:

- implicit copy constructors in the common field-wise/base-wise cases
- implicit copy assignment in the common field-wise/base-wise cases
- implicit move constructors in the common field-wise/base-wise cases needed by the
  supported value-semantics paths
- implicit move assignment in the common field-wise/base-wise cases needed by the
  supported value-semantics paths
- user-declared copy/move constructors and copy/move assignment operators in the ordinary
  non-template class cases needed by the supported value-semantics paths
- ordinary defaulted/deleted move-constructor and move-assignment cases in the supported
  non-template class patterns used by this assignment
- value passing of complete class objects to supported functions
- return-by-value of complete class objects from supported functions
- demand-driven LowIR emission of the copy/move/value helpers required by those supported
  paths
- raw `copyobj` lowering of a supported leading trivial storage prefix inside synthesized
  copy/move special members when the remaining suffix still needs ordinary field-wise
  lowering
- direct `copyobj` lowering of supported trivial class copy/move construction at the call
  site instead of forcing a separate synthesized trivial constructor call
- empty class objects and subobjects use the same address-based class copy paths as
  other class objects; lowering must not invent a scalar payload for an empty class
- an xvalue class glvalue bound to a reference through a derived-to-base
  conversion designates the existing base subobject; it is not materialized as
  a new complete object
- temporary class-object materialization in the common cases required by:
  - copy initialization from function results
  - pass-by-value call arguments
  - return forwarding through the supported value paths
- when a direct-register class call initializes a temporary whose destination
  is already known, its result is copied directly into that temporary; a
  conditional or full-expression cleanup boundary must not introduce a second
  call-result object
- direct reuse of the indirect return destination for supported `return local;` cases when
  the named local is the returned complete object
- ref-qualified member functions and out-of-class definitions of ref-qualified
  members, including xvalue propagation through non-static data-member access;
  ref-qualifiers are rejected on free functions, static members, constructors,
  and destructors, and an otherwise-identical member overload set cannot mix an
  unqualified declaration with a ref-qualified declaration
- rvalue-reference overload ranking after supported scalar pointer conversions,
  including null-pointer and pointer-qualification conversions
- delegating constructors; the delegating mem-initializer must be the only
  mem-initializer, and a delegation chain must not contain a cycle
- out-of-class constructor definitions
- out-of-class destructor definitions
- scalar `new` / `delete` expressions over the supported object subset,
  including class-specific allocation/deallocation selection, explicit global
  qualification, and suppression of scalar initialization after a supported
  non-throwing allocation returns null
- array `new` / `delete[]` expressions over the supported object subset
- union definitions and union object lifetime in the supported non-template
  class subset, including block-scope anonymous-member injection and an
  explicit variant initializer taking precedence over another variant's
  default member initializer; at most one variant may have a default member
  initializer
- conditional class-value cases in the supported copy/move subset, including
  cv-combined glvalue operands, lvalue/prvalue conversion, and destruction of a
  containing branch temporary only after its selected member result has been
  materialized
- serialized `[elision=copy]` permission on the outer copy/move construction
  from a private same-type conditional prvalue, while retaining its ordinary
  O0 transfer and cleanup
- equal temporary-destruction suffixes in the same full-expression and unwind
  context use shared LowIR cleanup continuations, including conditional
  lifetime guards where the guarded object identity is the same
- class temporaries created earlier in an enclosing full expression remain
  alive across nested conditional and short-circuit branch edges, and are
  destroyed at the end of that full expression
- a class prvalue bound directly to a local reference remains alive until the
  reference's scope ends and is destroyed there rather than at the end of the
  declaration's full expression
- class-valued `if` condition declarations are constructed only on paths that
  reach the declaration and remain alive through the complete selection
  statement, including braceless nested statements
- non-template conversion operators that participate in the existing overload
  and conversion machinery

Within this milestone, PA17 should produce valid LowIR for ordinary non-polymorphic value
types over the supported PA16 procedural/class subset. That LowIR is intended
to be accepted by the later PA29 `lowir2native` backend for the supported
cases. PA13 `lowir2cy86` remains an optional execution scaffold, not the
primary validation path.

### Out Of Scope

The following are explicitly out of scope for PA17:

- virtual functions, vpointers, and vtables
- RTTI and `dynamic_cast`
- multiple inheritance
- member pointers
- generalized operator overloading beyond the supported value-semantics paths
- copy-elision perfection and the full set of standard temporary-materialization rules
- advanced move-generation rules beyond the common supported field-wise/base-wise cases
  above, and the full standard move-semantics corner cases
- exception-aware cleanup during value transfers
- template-aware value semantics
- lambda expressions, range-for, and later general convenience syntax that is not
  needed by the PA17 value-semantics tests

Inputs that rely on those features have undefined behaviour for this milestone.

### Stage Handoff

The intended next stage is PA18, which adds the polymorphic machinery that PA17
intentionally leaves out:

- virtual dispatch
- virtual destructors
- vtables
- override/final behavior

So PA17 should leave behind a clean non-polymorphic value-semantics object model and LowIR
lowering path rather than mixing virtual dispatch into the same milestone.

### Design Notes (Non-Normative)

The important point is to extend the existing PA16 behavior rather than inventing a second,
incompatible model just for copy/value behavior. Whether that reuse happens through shared
code, shared data structures, or a careful reimplementation is up to you.

An important implementation rule for this milestone is monotonic extension:

- PA17 should add value-semantics behavior only when the source actually requires it
- it should not perturb PA16 outputs for programs that remain entirely inside the PA16
  subset
- in practice, that means copy constructor / copy-assignment support should only become
  semantically visible when the program actually needs it, rather than eagerly changing the
  behavior or emitted output for every class
- "PA16 would have treated this as out of scope" is not a sufficient reason to let PA17
  change the observable output of a still-valid PA16 program

Useful intermediate representations include:

- class metadata that distinguishes ordinary methods, constructors, destructors, and
  synthesized special members
- explicit constructor/destructor/copy actions attached to declarations and returns
- for supported indirect-value local objects, those attached destructor actions should remain
  the source of truth for scope cleanup during LowIR lowering rather than being recomputed
  later from the lowered storage type alone
- a calling-convention layer that can lower class values indirectly without changing the
  source-level semantic types
- a stable way to identify the supported temporary-materialization points without requiring
  a fully general temporary lifetime model yet
- a value-category check that distinguishes prvalues needing storage from
  xvalue glvalues that already designate storage before applying a base
  projection for reference binding
- a destination-aware class-call lowering path that accepts the already
  planned temporary address for both direct-register and indirect-result ABI
  classes, then marks the temporary live only after the call and required
  direct-result copy complete
- extension of PA16 cleanup-state identities with temporary object and
  conditional-lifetime facts, built from the terminal backward so equal
  suffixes can be reused in expected constant time per action
- allocation expressions lowered as ordinary construction/destruction actions
  over explicit storage, rather than as a separate object model
- conversion operators represented through the same typed overload-resolution
  and conversion machinery used for ordinary calls
- a single linear classifier over completed member-layout facts that marks the
  first transferable bit-field in an allocation unit and suppresses later
  fields covered by that transfer

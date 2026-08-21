## CPPGM Programming Assignment 28 (`cppgm++ --emit-lowir`)

### Overview

Write a C++ application called `cppgm++` that takes as input a set of C++ Source
Files, executes translation phases 1 through 7, parses them as PA10/PA28 translation units,
reuses the PA11-PA12 semantic foundation, builds on the PA15-PA27 LowIR lowering path,
adds the PA28 multi-vtable / virtual-base ABI slice, and writes LowIR text.

PA28 extends PA27 with the first supported object layouts that require more than the
earlier single-vptr, non-virtual-base ABI:

- virtual inheritance for shared base-subobject layout and access
- polymorphic multiple inheritance with more than one active vtable view
- pointer-form `dynamic_cast` across sibling polymorphic bases
- RTTI / `typeid` through non-primary polymorphic base views

PA28 still produces LowIR. It does not introduce a new output format.

### Prerequisites

You should complete Programming Assignment 27 before starting this assignment.

You will want to reuse:

- the preprocessing and tokenization pipeline from PA1-PA6
- the PA10 AST as the syntax boundary
- the PA11-PA12 semantic foundation
- the PA15-PA27 LowIR lowering path
- the PA13 LowIR contract
- the PA29 native validation path
- the PA13 LowIR -> CY86 path as an optional secondary scaffold

### Starter Kit

The starter kit contains:

- `pa28/README.md`, `pa28/Makefile`, and the test scripts in `pa28/scripts/`
- a student-editable `dev/cppgm++.cpp` starter scaffold
- the `pa28/cppgm++.cpp` symlink back to `../dev/cppgm++.cpp`
- shared support sources and headers under `dev/src/`
- a local test suite under `pa28/tests/`
- the grammar for this assignment called `pa28.gram`
- an HTML grammar explorer of `pa28.gram` in the sub-directory `grammar/`
- a checked-in local test suite under `tests/`

Students should implement the assignment in `dev/cppgm++.cpp` and any reusable
student-owned helpers they add under `dev/src/`. The assignment directory, grammar files,
test fixtures, comparison scripts, and checked-in reference outputs are support
files, not implementation files to edit for normal solutions. The shared support files
provide reusable infrastructure and earlier assignment machinery; they do not implement the
new PA28 source-to-LowIR ABI slice for you.

Unlike PA1-PA9, there is no external reference binary for PA28. The checked-in `.ref`
files are the default oracle.

### Input / Command-Line Arguments

Behaviour is undefined unless the command-line arguments match:

    $ cppgm++ --emit-lowir -O0 -o <outfile> <srcfile1> <srcfile2> ... <srcfileN>

`-O0` is the PA28 test mode. Other optimization levels are later optimizer work and
are not required for this milestone.

### Output Format

`cppgm++` shall write LowIR text to `<outfile>`.

The authoritative LowIR definition is `../pa13/lowir.md`. PA28 extends the PA27 lowering
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

The local checked-in tests live in `tests/general/`. They exercise PA28
source-to-LowIR behavior over virtual inheritance, non-primary polymorphic
views, sibling `dynamic_cast`, and RTTI through adjusted base views.

For each test case `x`:

- `cppgm++` is executed to produce `x.my`
- the exit status is recorded in `x.my.exit_status`
- `x.my` is compared against `x.ref`
- `x.my.exit_status` is compared against `x.ref.exit_status`

PA28 is tested against generated LowIR text using the relaxed LowIR comparator described
above. The generated LowIR is also intended to remain acceptable to the native
backend path introduced in PA29:

- feed that LowIR into PA29 `lowir2native`
- optionally cross-check by feeding that same LowIR into PA13 `lowir2cy86`
- then feed the generated CY86 into PA9 `cy86 --target linux`

The shipped PA28 tests are the contract for this milestone.

### PA28 Syntax Spec

The authoritative source syntax is the shared `cppgm++` source grammar, exposed
for this assignment as `pa28.gram`. The grammar defines accepted syntax only;
the PA28 semantic and lowering requirements are defined by the Assignment
Boundary and Out Of Scope sections below.

As in the earlier assignments, that grammar defines accepted input syntax only. The output
format for `cppgm++` is specified by this README, PA13 `lowir.md`, and the checked-in
`.ref` files.

PA28 does not add a new source-language grammar format. It instead enables more
of the already-accepted C++11 syntax to participate in semantic analysis and
lowering.

A checked-in HTML grammar explorer for that grammar lives in `grammar/`. Treat
`pa28.gram` as the source of truth.

`pa28.gram` uses the same token vocabulary and the same extended BNF operators as
`../pa6/pa6.gram`.

If this README and `pa28.gram` appear to disagree about source syntax, treat `pa28.gram`
as authoritative. If this README and PA13 `lowir.md` appear to disagree about LowIR syntax,
treat `lowir.md` as authoritative. If they disagree about the PA28 lowering slice, treat the
`Assignment Boundary` and `Out Of Scope` sections below as authoritative.

### Assignment Boundary

PA28 supports the following in addition to the PA27 subset:

- virtual inheritance for shared base-subobject layout in complete objects
- field access through shared virtual bases
- supported constructor and hidden-argument forwarding cases that carry virtual-base
  subobject addresses through existing value-semantics machinery
- polymorphic multiple inheritance with separate vtable views for non-primary polymorphic
  bases
- virtual dispatch through primary views whose virtual-base ABI carries
  function/adjustment rows, and through non-primary polymorphic base pointers
  and references
- pointer-form `dynamic_cast<T*>` across sibling polymorphic bases in the supported object
  model
- `typeid(expr)` through supported non-primary polymorphic base lvalue views

Within this milestone, PA28 should produce valid LowIR for ordinary source programs over
that subset. That LowIR should be accepted by PA29 `lowir2native` for the supported cases.
PA13 `lowir2cy86` remains a secondary scaffold backend for cross-checking.

To complete PA28, implement these goals:

1. Shared virtual-base layout.
   Complete objects with a virtual diamond should expose one shared base-subobject at a
   deterministic offset.

2. Polymorphic dispatch over adjusted vtable views.
   Calling a virtual through a class with virtual-base adjustment rows must select the
   requested logical slot. Calling through a later polymorphic base must lower through the
   correct vtable view and apply the required `this` adjustment. A final overrider inherited
   from a non-primary or virtual base must also occupy its required slot in the derived
   class's primary vtable group, in addition to any adjusted secondary-view entry. Each
   vtable segment must contain only the vcall-offset and virtual-base-offset rows owned by
   that segment; in particular, vcall rows belonging to a secondary virtual-base view must
   not enlarge the primary segment or its address point.

3. Sibling cross-cast support.
   Pointer-form `dynamic_cast` across sibling polymorphic bases should lower into the
   supported RTTI / vtable-view scan.

4. RTTI through non-primary views.
   `typeid(expr)` should observe the dynamic type through a supported non-primary
   polymorphic base reference.

### Out Of Scope

The following are explicitly out of scope for PA28:

- virtual-base constructor, copy, assignment, and destructor sequencing beyond the already
  supported simple generated cases
- polymorphic multiple inheritance with virtual destructors
- reference-form `dynamic_cast`
- `dynamic_cast` and RTTI cases that require `bad_cast` / `bad_typeid`
- virtual inheritance combined with the unsupported special-member or exception cases
- toolchain-driver and host-linker integration

Inputs that rely on those features have undefined behaviour for this milestone.

### Stage Handoff

The intended next stage is PA29, which lowers the completed LowIR family to
native code before PA30 turns the source pipeline into a practical `cppgm++`
toolchain driver and standard object-output flow.

So PA28 should leave behind:

- a stable multi-vtable / virtual-base LowIR lowering path
- deterministic lowering for the supported sibling-cast and RTTI-view cases
- explicit remaining deferrals only where the practical toolchain and remaining ABI/runtime
  work need to take over
- enough stable source behavior that PA30 can start carrying simple PA9-style complete
  programs as C++ end-to-end tests through the practical driver/link path

### Design Notes (Non-Normative)

PA28 should extend the existing object-model and RTTI lowering path, not replace it.

The same monotonic-extension rule applies here:

- PA28 should add its new behavior only when the source actually uses the supported PA28
  feature set
- it should not perturb PA27 outputs for programs that remain entirely within the PA27
  subset
- in practice, the richer vtable / RTTI layout should stay source-driven rather than
  changing earlier single-vptr cases unnecessarily

For Itanium-layout vtable segments, emit any vcall-offset rows before the
virtual-base-offset rows, followed by offset-to-top, RTTI, and the function
slots. Track each segment's address point from the rows actually emitted for
that segment instead of using one class-wide negative-row count.

Keep a synthesized constructor or destructor base entry's ABI identity
separate from inlining policy. The entry may need its own object symbol or
retained definition, but it gains `no_inline=yes` only when the source-level
function has the corresponding prohibition.

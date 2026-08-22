## CPPGM Programming Assignment 29 (lowir2native)

### Overview

Write a C++ application called `lowir2native` that takes as input a set of LowIR source
files and writes a native executable program.

PA29 replaces PA13 `lowir2cy86` as the primary backend path. The input language is still
LowIR, but the required output is no longer CY86 text. Instead, PA29 lowers LowIR directly
to native code and native program data.

The intent of this milestone is:

- keep LowIR as the long-term compiler backend boundary
- reuse the PA9 native backend knowledge without making CY86 the compiler IR
- leave room for later optimization and additional native backends

### Prerequisites

You should complete Programming Assignment 28 before starting this assignment.

You will want to reuse:

- the PA13 LowIR parser and LowIR specification
- the PA9 native backend pieces: instruction encoding, executable container writing, and
  startup/runtime glue
- any shared lowering or assembler abstractions that help you separate:
  - LowIR -> native instruction selection
  - native code/data emission
  - final executable image construction

PA29 tests execute generated native programs. Your development host therefore
needs an x86-64 Linux execution environment. With no `--target`, the tool should
emit a Linux executable. The target name used by the course is `linux`.

### Starter Kit

The starter kit contains:

- `pa29/README.md`, `pa29/Makefile`, and the test scripts in `pa29/scripts/`
- a student-editable `dev/lowir2native.cpp` starter scaffold
- the `pa29/lowir2native.cpp` symlink back to `../dev/lowir2native.cpp`
- shared support sources and headers under `dev/src/`
- optional typed LowIR and machine-IR model scaffolding in
  `dev/src/lowir_model.h` and `dev/src/mir_model.h`, with shared
  exported-symbol and register support in `dev/src/ir_symbol_model.h` and
  `dev/src/x86_register_model.h`
- a local test suite under `pa29/tests/`
- the grammar for this assignment called `pa29.gram`
- the authoritative LowIR specification in `../pa13/lowir.md`
- an HTML grammar explorer of `pa29.gram` in the sub-directory `grammar/`
- checked-in golden result files under `tests/`
- `tests/strict/` for raw-MIR oracle tests
- `tests/structural/` for canonical-MIR oracle tests
- `tests/behavior/` for generated-program behavior tests without a machine-IR oracle

Students should implement the assignment in `dev/lowir2native.cpp` and any reusable
student-owned helpers they add under `dev/src/`. The assignment directory, grammar files,
test fixtures, comparison scripts, and checked-in reference outputs are support
files, not implementation files to edit for normal solutions. The shared support files
provide reusable infrastructure and earlier assignment machinery; they do not implement the
new PA29 native lowering contract for you.

Unlike PA1-PA9, there is no external reference binary for PA29. The checked-in `.ref`
result files are the default oracle.

### Driver Surface For This Assignment

Required in PA29:

- `--help` / `-h`
- `-o <outfile>`
- `--dump-machine-ir <mirfile>`
- `--target <target>`

Not yet required here:

- separate compilation through `-c`
- link-map dumping
- the private exception/runtime ABI path

Those later pipeline surfaces are owned by the later `cppgm++` object,
compile/link, and host-EH assignments.

### Input / Command-Line Arguments

Behaviour is undefined unless the command-line arguments match one of:

    $ lowir2native --dump-machine-ir <mirfile> <srcfile1> <srcfile2> ... <srcfileN>
    $ lowir2native -o <outfile> <srcfile1> <srcfile2> ... <srcfileN>
    $ lowir2native --dump-machine-ir <mirfile> -o <outfile> <srcfile1> <srcfile2> ... <srcfileN>
    $ lowir2native --target <target> --dump-machine-ir <mirfile> <srcfile1> <srcfile2> ... <srcfileN>
    $ lowir2native --target <target> -o <outfile> <srcfile1> <srcfile2> ... <srcfileN>
    $ lowir2native --target <target> --dump-machine-ir <mirfile> -o <outfile> <srcfile1> <srcfile2> ... <srcfileN>

where each `<srcfileK>` is a LowIR source file and `<target>` is `linux`.

With no `--target`, `lowir2native` should emit a native Linux executable.

### Output Format

If `-o <outfile>` is provided, `lowir2native` shall write a native executable
program to `<outfile>`.

If `--dump-machine-ir <mirfile>` is provided, `lowir2native` shall also write a
deterministic machine-IR dump to `<mirfile>`.

The machine-IR dump is the serialized form of the backend model used for native
emission. You may keep a typed MIR internally, and the optional
`dev/src/mir_model.h` scaffold gives one possible representation, but the dump
must describe the same program that native emission consumes.

Integer immediates must preserve the complete input value, including values
that use the high half of an `i128`. Floating immediates must be interpreted as
their stated f32, f64, or f80 type so that globals, computations, and native
data contain the correct target value. The same values must appear in the
deterministic MIR dump and reach native encoding without loss.

The deterministic MIR dump must retain the required parameter names,
frame-slot display names, literal spellings, and debug source locations.

Frame metadata is part of that final MIR contract. In particular, the
callee-saved `preserve` list should name the callee-saved registers that the
final instruction body actually uses after local setup/copy cleanup, and the
stack size should match that final frame layout.

That MIR dump path must work even for helper-only LowIR inputs that have no
entry function. In that case the dumped MIR should simply omit the optional
`startup` section.

For the native path, that means an ELF executable.

The exact binary encoding is not directly compared by the PA29 tests. Instead, the tests
compare:

- the compiler exit status
- the canonical machine-IR oracle for successful compilations
- the generated program exit status
- the generated program standard output

### Error Handling

If an error occurs while parsing LowIR, validating LowIR, lowering LowIR, or writing the
native output, `lowir2native` shall `EXIT_FAILURE`.

The output file is not required to be meaningful on failure.

### Standard Output / Error

Standard output and standard error are ignored for automated testing of `lowir2native`.

You are free to use them for debugging, tracing, or diagnostic messages.

### Testing

Testing is based on execution of the generated native program.

For each test case `x`:

- `lowir2native` is executed to produce `x.my.program`
- `lowir2native` is also executed with `--dump-machine-ir` to produce `x.my.mir`
- the compiler exit status is recorded in `x.my.impl.exit_status`
- if compilation succeeded, `x.my.program` is executed
- its standard output is recorded in `x.my.program.stdout`
- its numeric exit status is recorded in `x.my.program.exit_status`

The checked-in `.ref` files are compared the same way for the outputs that are
part of that test's oracle:

- `x.ref.impl.exit_status`
- `x.ref.mir` for tests with a raw MIR dump oracle
- `x.ref.program.stdout`
- `x.ref.program.exit_status`

The `--dump-machine-ir` output remains the raw debugging dump.

For a successful compilation, the tested raw MIR dump is a plain-text file with this overall
shape:

```text
machine_ir x86_64 <target>

startup
    ...

global @name
  ...

function @name
  abi
    ...
  frame
    ...

  block ^label
    ...
```

The exact instruction inventory is target- and lowering-dependent, but the output format used
for testing is still this textual machine-IR form:

- one `machine_ir x86_64 <target>` header
- an optional `startup` section
- zero or more `global @...` definitions
- one or more `function @...` definitions
- per-function `abi`, `frame`, and ordered `block ^...` sections
- one instruction or metadata line per indented row beneath those sections

Machine operands use the following target-specific forms:

- `rax` through `r15` for general-purpose registers
- `xmm0` through `xmm7` for scalar floating-point registers
- an integer or floating literal for an immediate value
- `@name` or `^label` for a symbol or block label
- `[register]` or `[register+displacement]` for register-based memory
- `[base+index*scale+displacement]` for indexed memory, where `scale` is
  `1`, `2`, `4`, or `8`; `*1` and a zero displacement are omitted
- `[rbp+displacement]` for a frame location

A direct call names a symbol, while an indirect call prefixes its register or
memory target with `*`.  A call may also carry bracketed machine facts:

```text
call @consume [args=(rdi,rsi,xmm0), stack=16]
call *r10 [args=(rdi), variadic, unwind=no]
```

`args=(...)` lists the physical argument registers read by the call.  An empty
list is written as `args=()`.  `stack=N` records the bytes of caller-owned stack
arguments, `variadic` records a variadic call boundary, `unwind=no` records a
non-unwinding call, and `returns=noreturn` records a call that does not return.
These facts are part of MIR because register liveness, stack cleanup, exception
handling, and native emission all depend on them.

For strict and structural MIR tests, the raw `.ref.mir` file is still checked in because it
is the debugging-oriented dump students see directly from `--dump-machine-ir`.
Structural tests also keep `x.ref.cmir`, the canonical oracle used for grading.

For testing, PA29 uses three explicit comparison modes, split by directory:

1. `tests/strict/` compares the raw checked-in `.ref.mir` against the generated `.my.mir`,
   after only normalizing the host-target tag in the `machine_ir x86_64 <target>` header.
2. `tests/structural/` compares the checked-in `.ref.cmir` against a canonicalized form of
   the generated `.my.mir`.
3. `tests/behavior/` checks compilation and generated-program behavior only.
   It retains the reference machine-IR dump as an informational example for
   students, but does not compare generated MIR with it.

The structural canonicalization pass is intentionally conservative. It hides:

- the host-target tag in the MIR header
- exact stack/frame displacement numbers in memory operands
- interchangeable free GPR choices where the structural MIR shape is otherwise the same
- interchangeable free XMM choices where the structural MIR shape is otherwise the same

It still preserves:

- opcode family and width
- direct vs indirect call shape
- direct compare-to-branch vs materialized-bool shape
- register vs stack vs immediate location class
- floating operation family and explicit conversion family

So the assignment keeps a structural backend oracle without freezing exact
frame-layout details into every checked-in reference.

That means a successful `PA29` test anchor now validates exactly these output files:

- `x.ref.impl.exit_status`: exact compiler success/failure result
- `x.ref.program.exit_status`: exact generated-program exit status
- `x.ref.program.stdout`: exact generated-program standard output
- plus either:
  - `x.ref.mir` with strict raw-MIR comparison and header normalization only
  - `x.ref.mir` plus `x.ref.cmir`, with structural canonical-MIR comparison using
    checked-in `x.ref.cmir`
  - an informational `x.ref.mir` that is present but not compared for
    `tests/behavior/`

In other words, `PA29` is not just "program behavior matches." The tests also validate the
shape of the lowered backend output through one of those two explicit MIR oracles.

For structural failures, the harness leaves behind:

- `x.my.cmir`

Those are debugging artifacts only. Students are not expected to emit `.cmir` files. They
only need to implement `--dump-machine-ir` and produce raw `.mir`.

The `tests/behavior/` directory is for correctness cases where several reasonable
register-allocation or spill strategies are acceptable. Those tests still require
successful compilation and matching generated-program behavior, but they intentionally do
not compare machine IR. The checked-in reference MIR remains useful for inspection
and manual comparison.

The course extension suite uses the same three directories under
`course/pa29/`: `strict/`, `structural/`, and `behavior/`. Successful cases in
all three lanes retain raw reference MIR. Only strict and structural tests use
it as a grading oracle.

`make test` recursively runs the checked-in local suites:

- `tests/strict/`
- `tests/structural/`
- `tests/behavior/`

These directories contain PA29-specific backend oracle tests, not source-standard tests.
PA29 has no `tests/spec/` directory because the tested contract is the
compiler-owned LowIR-to-native backend surface rather than an N3485 C++ source-language
clause.

The PA29 suite is intentionally mixed:

- hand-written PA13-style LowIR tests
- selected LowIR programs copied from the outputs of PA16-PA28

That ensures PA29 is tested both on the core LowIR forms and on the richer LowIR that later
lowering assignments now produce.

The PA29 test suite exercises:

- startup/lowering correctness for simple programs, globals, direct calls, and indirect calls
- register and stack calling-convention handling for:
  - integer-only calls
  - mixed GPR/XMM direct calls
  - mixed GPR/XMM indirect calls
- short-circuit-style branch diamonds expressed directly in LowIR control flow
- unary logical-not lowering when the result feeds control flow
- direct compare-fed branches over integer, pointer, and floating inputs
- compare-as-value materialization for integer, pointer, and floating cases
- trivial integer and floating leaf chains that should stay register-resident
- mixed integer/float conversion chains
- signed and unsigned narrow integer reload/widen paths from both frame and global storage
- conservative `f80` arithmetic, comparison, and call/data lowering
- atomic load/store, exchange, compare-exchange, fetch-add, and fence operations across
  multiple scalar widths

The shipped PA29 tests are the contract for this milestone.

### PA29 Syntax Spec

The authoritative input-language syntax for PA29 is `pa29.gram`.

As in the earlier assignments, that grammar defines accepted input syntax only. The native
program behaviour contract is specified by this README, PA13 `lowir.md`, and the checked-in
`.ref` files.

PA29 does not add new LowIR syntax beyond PA13. It reuses the same LowIR language and adds
a new backend target for it.

That means the expected PA29 input surface is the current PA13 LowIR surface, not the older
pre-metadata subset. In particular, handwritten PA29 inputs may now use:

- explicit function role metadata such as `[role=entry]`, `[role=init]`, and `[role=fini]`
- top-level declaration forms such as `declare function` and `declare global`
- structured global data plus explicit global storage metadata where relevant
- optional call-boundary, parameter, and instruction-debug metadata accepted by PA13

A checked-in HTML grammar explorer for that grammar lives in `grammar/`. Treat
`pa29.gram` as the source of truth.

If this README and `pa29.gram` appear to disagree about LowIR syntax, treat `pa29.gram` as
authoritative. If this README and `../pa13/lowir.md` appear to disagree about the full LowIR
definition, treat `lowir.md` as authoritative. If they disagree about the required PA29
implementation subset, treat the `Assignment Boundary` and `Out Of Scope` sections below as
authoritative.

### Assignment Boundary

PA29 must support native lowering for the LowIR family already defined for PA13 and used by
PA15-PA28, including:

- scalar globals and structured global data
- functions, blocks, slots, temporaries, and runtime hooks
- direct and indirect calls
- control flow, integer operations, and pointer/index operations
- `phi` value merges as parallel transfers on their incoming control-flow
  edges, including loop backedges and critical edges
- floating scalar operations and comparisons over `f32`, `f64`, and `f80`
- explicit scalar conversions:
  - `sitofp`
  - `uitofp`
  - `fptosi`
  - `fptoui`
  - `fpext`
  - `fptrunc`
- atomic scalar operations and fences over `i1`, `i8`, `i16`, `i32`, `i64`, and `ptr`
- bulk memory operations:
  - `copyobj`
  - `zeroinit`
- object-lowered ABI forms emitted by source-to-LowIR assignments:
  - hidden destination-pointer returns
  - lowered object parameters carried as `ptr`
- direct one- and two-eightbyte object parameters and results in the supported
  x86-64 ABI, including padded homes for partial second eightbytes
- supported variadic calls and `va_start` register-save state for GPR and XMM
  arguments, including the caller-provided vector-register count
- structured vtable/global table data emitted by source-to-LowIR lowering
- structured global alignment derived from typed data items; raw `zero` byte
  padding inside mixed data does not independently raise alignment

Within this milestone, PA29 should successfully compile the LowIR emitted by PA15-PA28 into
host-native executables, without requiring CY86 as the primary output format.

PA29 must also expose a deterministic machine IR for successful compilations. That dump is
the structural proof that lowering is happening directly from LowIR into a target-specific
backend representation rather than only through a CY86 scaffold.

The LowIR input path should parse the same LowIR text accepted by PA13 rather
than relying on a private object, semantic, or source-level backchannel. Any
backend fact needed below PA29 belongs either in LowIR text or in the
target-specific MIR produced from that LowIR.

Within the supported subset, PA29 should lower:

- direct function calls to direct machine-IR call sites
- block control flow to direct machine-IR conditional and unconditional branches
- startup and shutdown hooks to direct machine-IR call sites in the startup path
- bulk object-memory operations to first-class machine-IR `copy_bytes` / `zero_bytes`
  instructions
- truly indirect LowIR calls to machine-IR indirect calls, rather than forcing all calls
  through the same lowered shape
- structured global data to machine-IR global data blocks rather than flattening everything
  through a CY86-style scalarized path
- atomic scalar LowIR to first-class machine behavior rather than silently dropping the
  atomic contract in the direct backend
- the LowIR arithmetic and conversion forms needed for native execution parity on the
  backend-owned subset of the old PA9 execution envelope, especially:
  - signed/unsigned integer division, modulus, ordered comparisons, and right shift
  - integer/float conversion operations
  - float-width extension and truncation operations
  - `f32`/`f64`/`f80` arithmetic and comparison behavior

To complete PA29, implement these goals:

1. Direct control-flow lowering.
   LowIR branches, first-class `switch` dispatch, and direct calls should become
   first-class machine-IR branches and direct calls, not a normalized CY86-style
   fallback.

2. Direct startup/runtime wiring.
   The startup path should call `@__cppgm_init`, `@main`, and `@__cppgm_fini` as direct
   machine-IR call sites where those hooks exist.

3. First-class bulk object-memory lowering.
   `copyobj <bytes>x<align>` and `zeroinit <bytes>x<align>` should survive as meaningful
   machine-IR operations such as `copy_bytes <bytes>x<align>` and
   `zero_bytes <bytes>x<align>`, rather than being expanded only through the old CY86
   lowering path.

   The operands of `copy_bytes` and `zero_bytes` name their address registers
   directly, without preceding MIR copies into fixed registers.

4. Preserve the distinction between direct and indirect calls.
   The direct backend should still emit indirect machine-IR calls for truly indirect LowIR
   calls, such as virtual dispatch, instead of collapsing all calls into one lowered form.
   That includes pointer-valued global cells: if a call target comes from a scalar `ptr`
   global, PA29 should call through the pointer stored in that global, not through the
   address of the global storage itself.

5. Preserve richer LowIR data layout.
   Structured global data and later vtable-like globals should remain structured in the
   direct backend rather than being forced through a scalarized compatibility path.

6. Exercise backend-owned execution behavior directly.
   PA29 is the right home for LowIR-native execution tests that validate the basic machine
   semantics inherited from PA9 without waiting for the later source-driver/toolchain
   milestones. The important cases are arithmetic, signedness-sensitive integer behavior,
   scalar conversions, and floating execution. Those tests should be expressed in LowIR,
   not by reintroducing CY86 or a host-lib-dependent source harness.

   In particular, PA29 should already treat unsigned LowIR arithmetic/predicate forms such as
   `udiv`, `umod`, `ushr`, `ult`, `ule`, `ugt`, and `uge` as first-class backend behavior,
   not as optional later cleanups.

7. Preserve direct compare-fed branch lowering for ordinary scalar cases.
   When a compare result feeds exactly one branch, PA29 should lower that as a direct
   machine compare plus conditional branch rather than materializing a boolean temporary
   and branching on that temporary afterward.

8. Keep simple scalar and floating work on the appropriate machine path.
   Small leaf scalar expressions should normally stay in registers, and ordinary `f32` /
   `f64` operations should stay on the floating-register path. A conservative stack spill
   is acceptable when pressure or an ABI boundary requires it, as long as the generated
   program is correct and the checked structural MIR cases still match their oracles.
   Lowering operations with fixed scratch registers, including integer comparisons,
   division, and shifts, must preserve still-live frame addresses and incoming parameters
   before reusing those registers.

   A numeric immediate written without a decimal point still follows the declared LowIR
   type in a floating store or return. It must be materialized as the requested floating
   value rather than routed through an integer-only move path.

   An integer LowIR immediate stored to a frame, global, dereference, or indexed
   destination remains an immediate value operand in MIR.  This includes an
   `i64` value outside the sign-extended 32-bit encoding range; native emission
   may materialize that value in a scratch register when encoding the store.

   The right operand of integer `add`, `sub`, `and`, `or`, and `xor` remains a
   frame, global, dereference, or indexed memory operand when that location is
   already selected.  Two-operand `imul` follows the same rule at 16, 32, and
   64 bits, and an integer comparison may retain one memory operand.  The
   result of a binary operation remains register-resident.  Division,
   variable shifts, byte multiplication, floating operations, and a form that
   would overwrite an address register before reading it still materialize the
   required value.

   A shift with a constant count retains that count as an immediate operand in
   machine IR and uses the target's immediate-count instruction.  A variable
   shift instead places its count directly in the target-required count
   register.

   A scalar copy may keep a stable source location, including an intact
   incoming parameter register, when the copied result's complete interval
   crosses no clobber and its source and result have the same machine
   representation. This includes a bit-preserving `ptr`/`i64` copy; width- or
   sign-changing conversions remain explicit. Such a copy should not add a
   machine move.

   A compiler-created scalar that is live across one adjacent block edge
   should remain in its selected register when the source has only that
   successor, the destination has only that predecessor, and the register
   survives every intervening operation.  A value crossing a call may use a
   callee-saved register for this purpose.  Joins, backedges, exception edges,
   address-taking uses, and clobbered registers still require conservative
   placement.

   Instructions with fixed register effects must preserve an unrelated live
   value already carried by one of those registers.  In particular, widening
   a scalar to signed `i128` uses both halves of the target register pair, so a
   later scalar operand in the high-half register must first receive another
   stable location.

9. Implement call-boundary correctness without requiring a clever allocator.
   PA29 must respect the native calling convention for direct calls, indirect calls,
   mixed GPR/XMM arguments, variadic register-save state, stack arguments, scalar and
   direct-object returned values, and values that remain live across calls. The tests
   intentionally check some high-pressure call cases by program behaviour only; those
   cases should compile and run correctly but do not require the exact spill/register
   strategy used by the reference implementation.

   An integer-only call still clobbers caller-saved XMM registers, so a live `f32` or
   `f64` value must survive that call even when no floating argument or result is present.

   Hidden indirect-result arguments can shift ordinary pointer and reference parameters
   into different ABI registers. Forwarding those parameters after earlier scratch-using
   operations must preserve their original values too.

   Eliminating a scalar parameter's initial store to and later load from a local slot
   must preserve the parameter across every intervening instruction that clobbers its
   incoming register, including a call. This requirement also applies at six or more
   integer or pointer parameters, where all incoming argument registers are occupied.
   While such a parameter remains live, its incoming register must not be assigned
   to a temporary result merely because the function has a wide parameter boundary.
   The register becomes reusable only after the parameter's final selected use.

   When a frame-resident object or wide-integer chunk is assigned to a GPR
   argument, MIR should load that chunk directly from its frame location. It
   should not materialize the object's base address solely for the load.

   Copying a stack-passed object may use the target's copy registers while
   preparing a call. A scalar argument whose source occupies either copy
   register must retain its value until its register or stack argument is
   written, including when the scalar follows the object on the call stack.

   Direct object returns follow the same rule in both directions: returning a
   frame-resident object loads its chunks directly into the ABI result
   registers, and storing a direct object call result writes those registers
   directly to its frame destination. A destination address created before
   the call should remain frame-shaped through the later result copy rather
   than occupying a register across the call.

   Atomic operations are subject to the same pressure correctness requirement. Producing
   an atomic operation's returned old value in a loop must remain executable when its
   address and source values occupy the available general-purpose registers.

   The same correctness requirement applies through control-flow joins and loop
   backedges. Incoming parameters, values computed before a loop, and values recomputed
   on each iteration must retain their current value across calls without a later
   iteration overwriting an earlier spill home.

   A `phi ptr` edge transfer whose source is a retained frame address transfers
   the address value, not the scalar contents stored at that frame location.
   The generated machine code must materialize the address before placing it in
   the phi destination.

   A representation-preserving scalar copy or decay may share its source's
   physical location, but that location remains live until the final use of
   every value that shares it.

10. Keep mixed-width conversion and floating-bool materialization explicit.
   Mixed integer/float conversion chains should keep their conversion family and width
   visible in MIR, and floating compare results used as values may materialize booleans
   in registers without an unnecessary stack round-trip.

11. Preserve narrow integer width behavior in MIR.
   Ordinary `i8`/`u16` compare-fed branches should stay visibly narrow, and small signed
   or unsigned integer arithmetic should show the expected post-operation normalization
   instead of silently widening into an untyped 64-bit path.

   A typed integer load defines the complete logical register value: narrow
   signed loads sign-extend and narrow unsigned loads zero-extend as part of
   the load itself.  Do not add a separate normalization instruction after
   such a load.  Narrow values returned across a call boundary still require
   explicit normalization before a wider comparison or `switch`; stale upper
   bits must not affect branch or case selection.  A call result used only by
   an immediate same-width store or return may remain in its ABI result carrier
   without that normalization because only its low result bits cross the
   boundary.  An immediately following explicit integer extension or
   truncation also performs the required normalization itself.

   A canonical typed integer immediate, a materialized Boolean, or an
   identical preceding integer extension already establishes the complete
   narrow value and should not be followed by a duplicate normalization.

12. Keep the conservative `f80` path explicit rather than implicit.
   PA29 does not need to treat `f80` like ordinary XMM-resident `f32`/`f64`, but its
   conversions and truncation/extension path should still stay visible and testable in
   MIR.

13. Cover direct compare-fed branch lowering at 64-bit and 128-bit integer widths too.
   The direct compare/branch quality rule is not limited to `i32` and `u32`. PA29 should
   show the same direct branch shape for straightforward `i64` and `i128`
   comparisons. An `i128` ordering comparison decides unequal high words with
   the predicate's signed or unsigned ordering and compares the low words as
   unsigned only when the high words are equal. It should branch without first
   allocating a scalar Boolean result. When an `i128` comparison is instead
   used as a value, register pressure must not make lowering fail; the
   materialized Boolean may use a temporary frame home when no GPR is free.
   The core ordinary-width oracle is the `500-i64-direct-compare-branch`
   family, and the course suite supplies the wide structural and pressure
   cases.

14. Keep pointer/null comparisons on the direct machine compare/branch path.
   Ordinary pointer/null tests should remain visibly pointer-typed in MIR and branch
   directly rather than degrading into a less explicit scalarized path, including
   null values first introduced through `const ptr 0`.

15. Keep pointer/index address calculation visible as pointer arithmetic.
   Pointer indexing and pointer-difference behavior should stay structurally visible in MIR
   rather than being hidden behind an unrelated compatibility path. When a one-use
   `index` feeds the following scalar load or store, that memory operand should
   contain the base, index, scale, and displacement directly. A zero index
   should not introduce a register copy or `lea`, and an unused `index` should
   not emit an instruction. When an indexed address must remain as a value, the
   MIR should use one `lea` rather than separate copy, multiply, and add
   instructions. An address of frame storage used only by the following scalar
   load, store, or constant index should remain frame-shaped: the memory
   operation should use `[rbp+displacement]` directly, and the constant index
   should incorporate its displacement without first materializing the base.

16. Preserve mixed integer/floating call ABI classification.
   Calls that mix GPR and XMM arguments should keep that classification visible in MIR so
   students can tell whether the backend is respecting the native calling convention.

17. Keep ordinary `f80` arithmetic and comparison behavior executable and visible.
   Even though `f80` remains the conservative floating special case, simple `f80`
   arithmetic and `cmp` behavior should still run correctly and remain explicit in MIR.

18. Exercise non-64-bit atomic widths explicitly.
   The PA29 atomic contract is not only about `i64`; smaller-width atomic load/store
   behavior should survive through the direct native backend. An atomic load
   result that remains live across a call must also have a frame fallback when
   every preserved GPR is occupied.

The PA29 tests intentionally include all of those cases so students can tell whether they
have actually implemented a direct `LowIR -> machine IR -> native` path, rather than only
matching program behaviour through a hidden CY86-based route.

### Out Of Scope

The following are explicitly out of scope for PA29:

- separate compilation and linking
- relocatable object-file output
- exception-aware native runtime metadata
- optimization passes
- non-x86 native instruction selection
- source-level end-to-end runtime programs that depend on the later `cppgm++ -c`
  driver and object/library flow

Inputs that rely on those features have undefined behaviour for this milestone.

### Stage Handoff

The intended next stages are:

- PA31 host EH facts, which validates the host-EH metadata emitted in
  `cppgm++ -c` objects
- PA30 `cppgm++` compile/link mode, which adds source-driven separate
  compilation and linking on top of the native backend path

So PA29 should leave behind:

- a stable `LowIR -> native` lowering boundary
- a stable `LowIR -> machine IR` boundary that later optimization passes can target
- reusable target-specific code/data emission layers
- no renewed dependence on CY86 for the main compiler path
- a backend test corpus that already catches the basic execution-level
  arithmetic and conversion bugs before the source-driven toolchain stages

### Design Notes (Non-Normative)

The cleanest PA29 structure is:

- parse LowIR into a structured internal representation
- lower that representation into a structured machine-IR program
- dump that machine-IR program deterministically for testing
- lower that machine-IR program into target-specific code/data
- write the final executable image from that lowered form

The important architectural constraint is that PA29 should reuse PA9 knowledge without
re-coupling the compiler to CY86. CY86 may remain useful as a secondary validation path, but
the primary backend boundary should now be LowIR to machine IR and native code/data.

For the compact MIR shapes used by the checked fixtures, useful implementation
strategies include:

- keep incoming parameters and call results in their ABI registers until an
  emitted instruction invalidates that location
- reserve every allocator-managed incoming register that still carries a live
  parameter, including on a wide scalar boundary, and release it through the
  ordinary typed use count after its final selected consumer
- represent each instruction's fixed-register writes as a compact register
  mask and keep a scalar in an incoming register only when its live interval
  crosses none of those writes
- when a full-width scalar call result also needs a stable later home, let an
  earlier GPR call-argument use read its intact `rax` carrier directly
- omit parameter homes and setup transfers when slot selection removes every
  use that would have consumed them
- omit a transfer to a stable parameter home when every selected consumer can
  read the still-intact incoming ABI register
- let a promoted or forwarded parameter-slot load continue to name the
  parameter's stable selected home; its consumer can apply any required
  register constraint directly, after accounting for clobbers between the
  eliminated store and load
- let a representation-preserving scalar copy or decay share an intact parameter
  location when the copied result's interval crosses no clobber
- record each block's sole predecessor and successor in dense CFG facts so a
  compiler-created scalar can retain its selected register across one exact
  adjacent edge without constructing per-block live-value sets
- lower `phi` values to parallel edge transfers; split a critical edge before
  MIR selection so copies for an untaken successor never execute
- retain a compact address-value bit on a parallel phi source so location
  equality and cycle scheduling do not confuse a frame address with a scalar
  stored at the same frame location; rematerialize that source with `lea`
- select the signed or unsigned extending memory form directly for a typed
  narrow integer load instead of emitting a partial load followed by a
  register-only normalization
- use the sole-use next instruction to recognize a narrow call result consumed
  by a same-width store, return, or explicit integer conversion, while
  retaining explicit normalization for wider consumers
- carry a typed immediate's signed range and the result fact of Boolean or
  integer-extension instructions into the adjacent normalization decision
- retain integer constants as typed MIR immediates, letting native emission
  materialize a scratch only when the concrete x86 encoding requires it, and
  place division or variable-shift operands directly in their required
  registers while keeping a fixed shift count on the shift instruction
- encode immediate memory stores directly at 8, 16, and 32 bits and for
  sign-extended 32-bit values at 64 bits; choose an encoder scratch that does
  not overlap a dereference base or index for other 64-bit values
- compare the exact target-byte cost of a fixed small `zero_bytes` with its
  `rep stosb` setup and use direct zero stores only when they are smaller
- encode a 1-, 2-, 4-, or 8-byte `copy_bytes` as one complete scalar load and
  store when that is cheaper than string-instruction setup; choose the scratch
  from the MIR instruction's declared clobber set and keep both logical
  address registers intact until their last use
- carry a sole-use load's typed frame, global, dereference, or indexed address
  into an immediately following legal integer right operand, keeping its
  address inputs live until the consuming instruction
- keep an immediately returned quotient in `rax` and an immediately returned
  remainder in `rdx`; the return instruction may name that selected result
  carrier directly
- lower a sole-use `i128` comparison and branch as high-word decisions plus an
  unsigned low-word tie-break, and give a comparison used as a value a frame
  fallback instead of requiring a free GPR
- give an atomic load result a typed temporary frame home when its live range
  requires a register class with no free member
- prefer an available caller-saved register to adding a callee-saved register
  to the frame's `preserve` list
- reuse compatible compiler-created temporary frame locations when their value
  lifetimes do not overlap, while keeping source slots and parameter slots
  distinct
- fold a one-use `index` into the following memory operand, omit work for an
  unused or zero-displacement index, and use one `lea` when an indexed address
  must remain as a value
- retain a one-use frame address through a scalar memory consumer so the
  selected memory operand names the frame location directly
- load frame-resident object chunks directly into their ABI argument registers
  instead of materializing a temporary object address
- transfer direct-object return chunks between their frame locations and ABI
  result registers without materializing a temporary object address
- keep an indirect-call target in its selected register when argument setup
  does not overwrite that register
- use the address result's recorded final consumer to keep a nonadjacent
  direct-object call-result destination frame-shaped without rescanning the
  intervening instructions
- count returns once during final native layout and share a restore/teardown
  sequence only when its encoded bytes exceed the added branch bytes; keep
  every semantic return and its result carrier in MIR

These are suggestions, not required internal data structures or algorithms.
Any implementation is acceptable if it preserves program behavior and produces
the checked strict or structural MIR output.

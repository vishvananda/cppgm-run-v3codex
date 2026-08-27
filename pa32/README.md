## CPPGM Programming Assignment 32 (`cppgm++ -c`)

### Overview

Write one C++ application called `cppgm++`.

PA32 is the host object/toolchain interoperability assignment. It does not add
new language features. Instead, it combines the PA30 compile-mode driver with
the PA14 ABI naming layer. PA14 built ABI names from normalized fact files;
PA32 connects that same naming work to the compiler's semantic/linkage facts
and writes the resulting names into real object files:

- `cppgm++ -c` must emit ordinary relocatable object files for the current host
  object format.
- Those objects must be accepted by the host C++ compiler driver when it performs
  the final link.
- Objects produced by `cppgm++` must interoperate with host-built C and C++
  objects, static archives, and shared libraries in the practical subset tested
  here.
- Header-emitted inline/template definitions must use host-correct symbol
  spelling and duplicate-definition/coalescing rules.

The PA32 harness does not normally use `cppgm++` as the final linker. It runs:

1. `cppgm++ -c` once for each C++ translation unit.
2. The host C++ compiler driver on the generated objects and any helper objects
   supplied by the test.
3. The linked program, when the link succeeds.

The main PA32 question is: can `cppgm++` produce ordinary host-linkable object
files?

### Prerequisites

Complete PA31 before starting this assignment.

You will want to reuse:

- the full C++ language pipeline through PA30
- the PA30 `cppgm++ -c` driver path
- the PA14 ABI naming layer
- the PA29 native backend and PA30 object-emission path
- the PA30 cross-translation-unit compile/link model

The tests assume a POSIX-like shell environment with `make`, `bash`,
`perl`, and a working host C/C++ toolchain. The harness selects host tools from
environment variables first:

- `CPPGM_HOST_CXX` or `CXX` for the host C++ compiler/link driver
- `CPPGM_HOST_CC` or `CC` for host C helper objects

If those are not set, the harness searches for common compilers such as
`clang++`, `g++`, `c++`, `clang`, `gcc`, and `cc`. Archive and inspection tests
also require `ar`, `nm`, and `readelf`. The checked-in tests assume the normal
x86_64 Linux host object ABI.

### Starter Kit

The starter kit provides:

- `dev/cppgm++.cpp`, populated from the `cppgm++` scaffold for the cumulative
  PA10+ compiler driver
- the shared `dev/` sources needed by the scaffold
- `pa32/cppgm++.cpp`, a link to `../dev/cppgm++.cpp`
- `pa32/Makefile`
- `pa32/scripts/`, the host-interoperability test harness
- `pa32/tests/general/`, the PA32 tests and checked-in reference files

Put your code changes in `dev/`, especially `dev/cppgm++.cpp` and the
shared implementation files it calls. Do not edit generated `.my` files. Test
inputs and references are part of the handout unless your instructor asks you
to add or update tests.

There is no separate PA32 reference binary in the starter kit. The checked-in
`.ref.*` files are the oracle.

### Command-Line Contract

PA32 does not introduce new command-line flags. It strengthens the PA30
compile-mode surface on the host-compatible path and uses the PA14 ABI naming
layer for C++ object symbols.

Required forms:

```sh
cppgm++ -c -o <objfile> <srcfile>
cppgm++ -c --target <target> -o <objfile> <srcfile>
cppgm++ -c -I <dir> -o <objfile> <srcfile>
cppgm++ -c -I<dir> -o <objfile> <srcfile>
cppgm++ -c --target <target> -I <dir> -o <objfile> <srcfile>
cppgm++ -c --target <target> -I<dir> -o <objfile> <srcfile>
```

`<target>` may be `linux` or the corresponding x86_64 Linux host triple form
accepted by your implementation. PA32 only requires compile mode. The normal
PA32 final link is performed outside `cppgm++` by the host C++ compiler driver.

### Output Format

`cppgm++ -c` shall write one host-linker-compatible relocatable object file to
`<objfile>`.

The PA32 tests do not compare object bytes directly. They observe:

- `cppgm++ -c` exit status
- host final-link exit status
- final program exit status
- final program standard output
- optional object-inspection output for tests that include `.inspect.*` sidecars

### Error Handling

If preprocessing, parsing, semantic analysis, lowering, object emission, or
output writing fails, `cppgm++` shall exit with failure.

For negative tests, exact diagnostics are not the grading contract. The harness
compares exit status first. If the reference compile/link path fails, stdout and
stderr are diagnostic side effects rather than required output.

### Testing

Run the PA32 suite with:

```sh
make test
```

To run one test through the shared check target:

```sh
make check TEST=tests/general/100-host-main-argv.t
```

The local tests live in `tests/general/`. They cover host object
interoperability, host final-link behavior, symbol spelling/coalescing, and
object inspection where the object surface is directly checked. They are
not direct N3485 clause tests.

For each test anchor `x.t`, companion C++ sources are named:

```text
x.t.1
x.t.2
...
```

Optional sidecars control or check the host flow:

- `x.system-includes`: system include search directories passed to `cppgm++`
  as `-isystem <dir>`
- `x.link.flags`: extra flags passed to the host link driver
- `x.lib.*`: host-built C or C++ helper sources
- `x.argv`: program arguments for the runtime check
- `x.inspect.cmd`, `x.inspect.expect`, or `x.inspect.plan`: object-inspection
  checks that use the host symbol tools

The checked-in PA32 tests cover:

- hosted `main(argc, argv)` behavior through the host CRT
- host linking across multiple `cppgm++`-generated objects
- host linking against host-built objects
- host linking against static archives and shared libraries
- import/export of host-built `thread_local` variables in the tested subset
- GNU `section` attributes on global objects whose nonempty ELF section name
  contains only ASCII letters, digits, `_`, and `.`
- duplicate-definition/coalescing behavior for inline and template output
- host symbol spelling for user-defined entities and selected template cases

PA32 does not require hosted standard-library header support. Your compiler does
not need hosted include search, hosted preprocessor compatibility, or semantic
support for hosted headers such as `<exception>` or `<typeinfo>` yet. The PA32
tests cover object emission, host linking, symbol spelling, and cross-object
interoperability through declarations and helper objects that expose the object
boundary directly. Hosted header compatibility begins in a later assignment, and
hosted exception-library runtime behavior is introduced after hosted headers
compile.

### Using PA14 ABI Names

PA32 does not require the broader host C++ ABI/runtime behavior exercised later,
but ordinary host object interoperability already requires correct raw symbol
spelling for user-defined entities.

The object-file requirement is that visible symbol names match the configured host
ABI. The PA14 ABI naming layer is the recommended path for producing those
names:

- the host linker sees raw symbol names, not demangled intent
- function templates must encode template-parameter references with the same
  `T_`, `T0_`, and related forms the host compiler uses
- repeated components inside one mangled name must reuse Itanium substitution
  slots in host-compatible order
- canonical qualified names matter, including inline namespaces when they are
  part of the ABI name

Reference:

- Local copy of Itanium C++ ABI, Chapter 5.1 "External Names (a.k.a.
  Mangling)": [`../doc/itanium-mangling.txt`](../doc/itanium-mangling.txt)

### Required Implementation Surface

To complete PA32, implement ordinary host-toolchain interoperability of emitted
object files within the supported subset:

1. Emit host-linker-compatible relocatable objects, including PIE-safe GOT
   materialization for the addresses of imported data and functions.
2. Expose a hosted entrypoint through the host CRT.
3. Preserve cross-translation-unit behavior under host link.
4. Emit target-correct duplicate-definition semantics for header and template
   code.
5. Interoperate with host-built objects, archives, shared libraries, and tested
   `thread_local` variables through practical function/global boundaries.
6. Preserve the earlier class-value semantics in host-object mode. Compiler-object
   metadata must not append a whole-object representation copy after a nontrivial
   memberwise copy or move body.
7. Omit an unreferenced translation-unit-local function definition from the
   native object while retaining local functions whose addresses or bodies are
   required by emitted runtime code.
8. Retain a reachable constructor or destructor base-entry definition needed
   at the object boundary even when optimization removes its last direct call.
   Retention does not by itself prohibit inlining that entry at other call
   sites.
9. For the supported GNU `section` attribute on a global object, require one
   string-literal name, reject conflicting redeclarations and names outside the
   token-safe Linux subset above, carry the name as global LowIR
   `section=<name>` metadata, and place the symbol and its relocations in that
   ELF section. Function sections, segment syntax, and quoted LowIR metadata
   are not part of PA32.

If the host linker rejects generated objects as ordinary objects, fix the
host-compatible object-emission path.

### Out Of Scope

The PA32 tests do not require:

- host C++ ABI/runtime behavior after link
- hosted standard-library header/source compatibility
- hosted header-emitted link/runtime behavior
- bootstrap or self-host builds

### Design Notes (Non-Normative)

A simple implementation strategy is to keep PA30's source-to-LowIR path, use
PA14 to derive concrete C++ object symbols, and retarget only the object
emission details needed by the host object format. The observable result is
whether the host toolchain can consume and link the result, not whether your
internal object pipeline has the same structure as the course implementation.

One practical integration point is the compiler semantic/linkage layer: after
semantic analysis determines the entity, owner scopes, function type, template
arguments, local context, ABI tags, and special-name kind, that layer can feed
those facts to the PA14 mangler and store the resulting raw symbol on the
LowIR/object symbol. This keeps lower object-format and toolchain-driver code
focused on preserving the spelling it was given instead of reconstructing C++
ABI names from text fragments.

### After PA32

Later host-link tests keep the same object path and then exercise richer host
C++ ABI/runtime behavior.

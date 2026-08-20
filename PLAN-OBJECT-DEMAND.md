# Plan: Native Object Separation and Demand-Driven Emission

Status: implemented and validated

Date: 2026-08-17

## 0. Implementation result

The plan was implemented as a conservative native-output boundary rather than
as cross-object compiler metadata. Native `.o` files no longer carry the
serialized cppgm object. Private `.obj` files retain that representation for
the PA30 compiler-link contract, while direct native compilation and final
conversion from textual LowIR use the same native reachability policy.

The demand work was split into independently gated changesets. Validation no
longer publishes object-definition demand by itself. After force inlining, the
final native path retains typed semantic roots and their lifecycle variants,
then removes unowned weak definitions that are outside the reachable closure.
Private objects and textual LowIR remain stable until the final native object
boundary. Explicit function specializations are not misclassified as weak
merely because they have template arguments; explicit class-instantiation
definitions and required destructor variants retain their required ownership.

On the frozen `semantic_overload.cpp` input, the final native object is
7,590,800 bytes, contains no `.cppgm_object` section or `CPPGMOBJ` marker, and
has 13 strong plus 4,491 weak default-visible function definitions. The final
candidate has 4,578 typed functions and 180,729 typed LowIR instructions. The
post-inline pass removes 683 unreachable weak functions before native
lowering, leaving 135,151 native LowIR instructions and 181,290 MIR
instructions. Three candidate runs had wall times of 7.60, 7.61, and 8.90
seconds (7.61-second median); the third sample coincided with heavier host
load.

An immutable pre-prune compiler from commit `64d11564` and the final compiler
were also compared in six interleaved runs each using the frozen benchmark's
ABBA runner. Median wall time changed from 7.674 to 7.619 seconds (-0.72%),
while object size fell from 8,086,872 to 7,590,800 bytes (-6.13%). Typed
function count fell from 5,260 to 4,578, typed instructions from 187,737 to
180,729, native LowIR instructions from 138,011 to 135,151, and MIR
instructions from 185,468 to 181,290. Candidate outputs were deterministic.

Final validation completed with:

- root `make test-report`: 5,163 / 5,163 passed;
- PA39 file audit: zero fatal findings (23 pre-existing advisory warnings);
- clean 8-worker self build: 38.18 seconds wall, 346,428 KB maximum RSS;
- clean 8-worker inception compare: 4:24.32 wall, 318,960 KB maximum RSS;
- clean 32-worker inception compare: 2:03.74 wall, 316,112 KB maximum RSS;
  and
- 152 / 152 inception objects matched, and the self/inception binaries were
  byte-identical with SHA-256
  `f21a9c98687f07983bf27665a5de7d4ec761092c17a8f1ff8000fe982dec6cf3`.

## 1. Objectives

This project has two related goals:

1. Ordinary Linux `.o` output must stop carrying the serialized cppgm LowIR
   object in a `.cppgm_object` section.
2. Host object emission must define only the functions required by the current
   translation unit and its runtime-definition closure, with correct strong,
   weak, and local bindings.

The changes must preserve the staged assignment contracts, keep the production
source-to-object path consistent with `spec.md`, and leave a clean full
`make test-report` between every accepted compiler changeset. The final clean
commit must pass a from-scratch root `make inception` comparison.

This plan does not use PGO, a host compiler as an implementation shortcut, a
benchmark-specific rule, or a persistent cache.

## 2. Measured baseline

The frozen input is:

`~/cppgm-extended-pa39-source-layout/benchmarks/self_compile/stable/semantic_overload.cpp`

Seven interleaved `-std=gnu++11 -O0` runs, after one excluded warm-up per
compiler, produced:

| Compiler | Median wall | Object bytes | Default-visible defined functions |
| --- | ---: | ---: | ---: |
| GCC 15.2 | 8.93 s | 3,196,024 | 4,248 |
| Clang 21.1 | 6.98 s | 2,477,128 | 2,907 |
| cppgm++ | 8.89 s | 119,916,880 | 9,927 |

For this table, a defined function is an ELF `STT_FUNC` symbol with `GLOBAL`
or `WEAK` binding, default visibility, and a non-`UND` section.

The cppgm++ binding split is:

| Binding | Count |
| --- | ---: |
| Strong/default-visible | 20 |
| Weak/default-visible | 9,907 |
| Local function symbols | 10,610 |

The local-symbol number includes internal compiler labels and is not itself an
emission-body count. Compiler telemetry is the reliable body count:

| Counter | Current `-O0` value |
| --- | ---: |
| Demand worklist pushes | 9,320 |
| Completed demanded-function records | 9,307 |
| Lowered LowIR functions | 9,390 |
| LowIR instructions | 282,097 |

The object-size breakdown is:

| Component | Bytes |
| --- | ---: |
| Whole cppgm++ object | 119,916,880 |
| `.cppgm_object` | 103,710,451 |
| Everything outside that payload | 16,206,429 |
| Allocatable text/data/bss reported by `size` | 2,748,337 |

The payload is 86.49% of the file. An instrumented run attributed about
0.421 seconds to payload serialization, in addition to allocation and output
traffic. The payload contains a binary serialization of the complete optimized
`LowirProgram`; it is not text.

The host compiler counts are diagnostic comparisons, not a requirement for
exact parity. GCC and Clang make different inlining and COMDAT choices. The
invariant for cppgm++ is that every emitted definition has a valid language or
ABI demand reason and that a semantic-only operation cannot become an emission
root.

## 3. Current architecture

The compile path currently does this for every `-c` output name:

```text
source or textual LowIR
    -> typed LowIR
    -> optimized LowIR
    -> SerializeCompilerObject(LowirProgram)
    -> native machine lowering and ELF encoding
    -> ELF .o containing native sections plus .cppgm_object
```

The `.cppgm_object` section has no ELF allocation flags, so a host linker does
not need it. It exists because cppgm++'s PA30 link mode detects the `CPPGMOBJ`
magic, reads the serialized LowIR back, combines LowIR programs, and then emits
an executable.

The assignment boundary is important:

- PA30 permits an implementation-defined compiler-object format. Its harness
  deliberately names separate-compilation outputs `.obj`.
- PA31 and PA32 require ordinary host-linker-compatible relocatable objects.
  Their harnesses use `.o` and the host linker.
- PA37 requires direct source-to-object output to match output reconstructed
  from serialized LowIR text. It does not require a private LowIR payload in
  the resulting ELF object.
- PA39 uses `.o` files and a host C++ linker for checkpoint and inception
  binaries. It does not consume `.cppgm_object`.

The current ELF import path is not a general replacement linker. Its
`ReadElfRelocatableObject` support is limited to `.text*`, `.data*`,
`.rodata*`, and `.bss*`, and to a small relocation subset. It does not preserve
the full weak/COMDAT, EH/LSDA, TLS, init/fini, visibility, and section-group
surface emitted by the modern compiler.

## 4. Object-format decision

### 4.1 Recommended first implementation: explicit `.obj`/native split

Use the already-established PA30 filename convention as an explicit format
selection:

- `cppgm++ -c -o x.obj x.cpp` writes the private `CPPGMOBJ` compiler object.
- `cppgm++ -c -o x.o x.cpp`, and compile output with any non-`.obj` name,
  writes only the host-native ELF relocatable object.

For `.obj`, call the existing `WriteCompilerObject` directly. Do not first
generate native code and then append a second copy of the program. PA30's own
linker consumes the private object and performs native lowering once at final
link time.

For native `.o`, do not call `SerializeCompilerObject` and do not create even
an empty `.cppgm_object` section. Add a native-object writer API that cannot
accidentally accept a compiler payload.

Keep input detection magic-based. `ReadCompilerObject` should continue to read
both new raw `CPPGMOBJ` files and older ELF files that embedded the same magic,
so existing private objects remain readable during development.

This split is the preferred first change because it:

- matches PA30 versus PA31/PA32 ownership;
- removes 103.7 MB from the measured `.o` immediately;
- removes payload serialization from every normal hosted and PA39 compile;
- avoids duplicating native code and LowIR inside one output;
- preserves PA30 separate/direct/mixed linking without a host-tool shortcut;
- preserves PA37's textual LowIR object boundary; and
- does not require a second general-purpose ELF linker project before the
  demand work can begin.

The driver must diagnose a native `.o` passed where the private cppgm link mode
requires a compiler object. It must not fall through and try to parse ELF bytes
as C++ source.

### 4.2 Alternative: make cppgm++ link native `.o` directly

This is architecturally attractive as a separate project, but it is not the
first optimization changeset. A complete implementation would need to extend
the internal linker to preserve at least:

- ELF allocatable section types, flags, alignment, and `NOBITS` behavior;
- local/global/weak definitions, visibility, undefined symbols, and duplicate
  resolution;
- COMDAT/SHT_GROUP selection and section garbage ownership;
- all relocations emitted by cppgm++ for code, data, GOT/PLT, TLS, EH, and
  lifecycle arrays;
- `.eh_frame`, `.gcc_except_table`, `.init_array`, `.fini_array`, TLS, RTTI,
  vtables, and object aliases;
- the executable startup/entry contract currently derived from LowIR; and
- deterministic section and symbol ordering.

Invoking `g++`, `ld`, or another host linker from cppgm++ is not an acceptable
substitute: it would violate the self-contained production requirement in
`spec.md` and the repository rules.

A partial native-object importer just for PA30 examples would create another
mode-dependent shortcut. Therefore, retain the explicit private `.obj` path
until a complete native linker is separately planned and tested.

## 5. Strong-binding diagnosis

GCC and Clang each emit 13 strong default-visible functions for the frozen
translation unit. Those are the 13 ordinary out-of-line functions defined by
`semantic_overload.cpp`.

cppgm++ emits the same 13 plus seven synthesized inherited-constructor entry
symbols:

- one `SemanticSoftFailure` constructor entry;
- two `NoViableConstructorError` entries;
- two `NoViableOverloadError` entries; and
- two `UnknownFunctionError` entries.

`SemanticAnalyzer::InheritConstructors` creates these synthesized functions but
does not call `PublishInlineFunctionFacts`. Implicit/in-class/inherited
constructor definitions are ODR-coalescible. The later ABI aliases correctly
copy the target linkage, so the missing semantic linkage fact is the first bug
to fix; relabeling symbols in the ELF writer would only hide it.

The fix must publish the inline/weak-ODR fact on the canonical inherited
constructor identity before complete/base entry aliases are formed. Then audit
`HasWeakLinkage` and lifecycle alias creation to ensure every entry point uses
the canonical ownership fact.

The earliest language surface for inherited constructors is PA16, while the
first host-object coalescing surface is PA32. Reuse the existing PA32
multi-translation-unit link and object-inspection test type for the regression:
a shared header supplies an inherited constructor, two translation units use
it, the host link succeeds, and the constructor entries inspect as weak rather
than strong. Do not add a new harness type.

## 6. Weak-definition and demand diagnosis

The excess is not primarily an ELF-binding problem. Marking 9,907 definitions
local or weak in a different way would not remove their semantic analysis,
LowIR, optimization, machine lowering, relocations, or code bytes.

The current design has useful pieces of a demand system, but it conflates four
different operations:

1. declaring a callable;
2. validating or completing its definition;
3. recording that generated runtime code references it; and
4. emitting its definition in this object.

The main conflation points are:

- `FunctionInfo::demand_state` is a numeric state used for both definition
  processing and emission work;
- `DemandRuntimeFunction` immediately publishes `emission_demanded` for inline
  functions and also triggers exception, class-member, vtable, and lifecycle
  work;
- demand call sites do not record a typed reason or a caller-to-callee edge;
- some expression paths use `pending_runtime_call_demand` and later subtree
  rescans, while others demand immediately;
- host-object lifetime and synthesized-special-member paths correctly demand
  definitions for ODR-use, but cannot distinguish an emitted caller from a
  body being checked only for semantic validity; and
- `IsFunctionEmissionDemanded` treats every non-inline definition as an output
  root and uses one Boolean for the inline/template decision.

The frozen counts show that almost the entire lowered function set has passed
through this broad demand machinery. A demangled symbol comparison also shows
that most cppgm-only weak definitions are hosted `std::` helper/template
specializations. Some difference is expected from host inlining, but more than
5,000 unique demangled weak names appear only in cppgm++ output. This is enough
to require a reasoned reachability audit rather than isolated symbol pruning.

## 7. Target demand model

### 7.1 Separate monotonic states

Replace the numeric combined state with named, monotonic facts owned by the
canonical binding:

- definition not started / queued / in progress / complete / failed;
- runtime reference absent / required;
- object definition absent / required;
- emitted or intentionally declaration-only; and
- explicit-instantiation suppression/preemption as a separate fact.

Semantic validation may complete a body without requesting an object
definition. Emission demand may arrive before or after definition completion.
Each transition is deduplicated.

### 7.2 Typed roots and edges

Every object-definition transition must carry a compact reason, including:

- externally visible out-of-line definition;
- evaluated direct call;
- function address or member-function address escape;
- constructor/destructor materialization or ODR-use;
- namespace/local-static initializer or finalizer;
- vtable slot, deleting entry, thunk, RTTI, or exception cleanup;
- explicit instantiation definition;
- required inline/template definition closure; and
- compiler-generated ABI/runtime support.

Store caller binding, callee binding, and reason in a contiguous per-translation
unit edge arena with per-binding ranges or intrusive heads. Do not allocate one
vector per function. Global initialization and ABI roots use explicit synthetic
root IDs rather than a null caller with implicit meaning.

### 7.3 Analyze once, propagate only from demanded code

While analyzing a definition, record runtime dependency edges as selected
declarations and typed action facts become known. Do not recursively demand the
callee merely because the caller body is being validated.

When a binding first becomes an object-definition root:

1. ensure its required definition facts are complete;
2. traverse only its recorded outgoing runtime edges;
3. enqueue newly required definitions once; and
4. retain unresolved declarations where ownership is external or explicitly
   suppressed.

Replace broad `DemandRetainedRuntimeCalls` subtree rescans with the explicit
edge list once equivalent coverage is proven. Existing pending-call flags can
serve as a temporary verification path: in an audit build, recompute the old
closure and assert that the new closure contains every required edge before
removing the fallback.

### 7.4 Root policy

The initial policy should be conservative and language-driven:

- externally visible non-inline definitions in the translation unit remain
  roots;
- internal functions are roots only when referenced or required by a runtime
  action, unless an existing assignment oracle requires their presence;
- inline and template definitions are emitted on ODR-use, address use, virtual
  ownership, explicit instantiation, or another typed ABI reason;
- constexpr calls used only for constant evaluation do not request runtime
  definitions unless a retained runtime recipe or function address requires
  one;
- declarations owned by another object remain unresolved declarations;
- vtable, RTTI, EH, TLS, initialization, and finalization roots remain explicit;
  and
- trivial lifecycle operations remain elided only where the existing typed
  object-transport rules prove that no callable body is required.

### 7.5 Post-inline closure

After force-inline rewriting, run a bounded reachability cleanup over weak or
internal definitions only. If a definition has no remaining call, address,
vtable, lifecycle, EH, or export edge, remove it before native lowering.

Do not remove an externally visible strong definition, an explicit
instantiation definition, or any address-observable weak definition. This is a
worklist over the already-recorded emission graph, not a whole-program retry
loop and not general PA37 interprocedural optimization.

## 8. Observability required before behavior changes

Extend explicit `--stats` telemetry before pruning demand. Record:

- callable declarations and definitions discovered;
- bodies completed for validation only;
- bodies completed after emission demand;
- root requests and unique root transitions by reason;
- dependency-edge attempts, unique edges, and traversed edges by reason;
- references resolved to local definitions versus external declarations;
- emitted strong, weak, and local function bodies;
- generated ABI entry points and aliases separately from bodies;
- definitions removed after force inlining;
- functions and instructions entering LowIR, PA37, native lowering, and ELF;
  and
- native object bytes, private payload bytes, symbol count, relocation count,
  and major section sizes.

Add an explicit diagnostic-only demand-graph dump if aggregate counters cannot
identify a bad root. It must be deterministic, disabled by default, and use
canonical IDs internally; rendered names are presentation only.

The first telemetry changeset is observational. The frozen object must remain
byte-identical before the object-format split, and the normal non-`--stats`
path must not retain the diagnostic edge rendering.

## 9. Staged implementation and gates

Each numbered changeset is committed separately only after its full gate. A
failed experiment is reverted and recorded; it is not carried into the next
changeset.

### Changeset 1: object-output policy and observability

- Add explicit native-object versus private-compiler-object output policy.
- Keep policy code out of the already-large driver where practical; place the
  format decision and writing interface in the PA30 object/output modules.
- Add counters for native bytes and optional private payload bytes.
- Preserve magic-based reading of both raw and legacy embedded compiler
  objects.

Tests and gates:

1. Existing PA30 separate, direct-source, and mixed `.obj` paths.
2. Existing PA31/PA32 host object/link/inspection suites.
3. Existing PA37 object-roundtrip suite at `-O0`, `-O1`, `-O2`, default, and
   `-O3` where already configured.
4. Add a PA32 object-inspection regression using the existing inspect-plan type
   to require no `.cppgm_object` section in native `.o` output.
5. Run root `make test-report` and require a full pass.
6. Run the file audit with zero fatal findings.
7. Re-run the frozen benchmark and confirm unchanged exported symbol counts,
   deterministic native sections, zero payload bytes, and the expected size,
   time, and RSS reductions.

### Changeset 2: inherited-constructor linkage

- Publish inline/weak-ODR ownership when inherited constructors are created.
- Ensure complete/base entries and object aliases copy the canonical fact.
- Do not change demand closure in this changeset.

Tests and gates:

1. Existing PA16 inherited-constructor semantic tests.
2. New PA32 multi-TU inherited-constructor coalescing reducer using the existing
   host-link and inspect-plan harness.
3. Existing PA32/PA33 constructor, alias, COMDAT, and vtable inspections.
4. Root `make test-report` full pass and zero-fatal file audit.
5. Frozen inspection must show 13, not 20, strong default-visible functions;
   the seven corrected entries move to weak ownership without changing runtime
   behavior.

### Changeset 3: demand telemetry and named states

- Replace magic numeric demand states with named state values without changing
  behavior.
- Add reason counters and verification-only edge recording.
- Retain the old demand closure as the behavior source.

Gates:

1. Exact native object bytes relative to Changeset 2.
2. Owner suites for semantic demand, PA32 emission, PA35 compile, PA36 link,
   PA37 round-trip, and PA38 native behavior.
3. Root `make test-report` full pass and zero-fatal file audit.
4. Frozen counters must account for every current emission transition.

### Changeset 4: separate validation from emission

- Complete required non-template bodies for diagnostics without publishing
  object-definition demand.
- Record their potential runtime dependencies.
- Promote and replay dependencies if the caller later becomes demanded.
- Keep template instantiation, explicit-instantiation, exception-specification,
  and class-completion states separate.

Gates:

1. Existing earlier semantic negative tests must still reject invalid required
   definitions; this change may not skip language diagnostics.
2. Existing PA32 positive tests for noop, identity, side-effect, address-taken,
   inline, and template wrapper emission.
3. Existing PA35 heavy-header compile and PA36 hosted link/runtime suites.
4. Add reducers at the earliest owning PA for every newly exposed failure.
5. Root `make test-report` full pass and zero-fatal file audit.
6. Frozen counters must show that validation-only bodies no longer create
   object-definition roots.

### Changeset 5 and later: migrate one dependency family at a time

Migrate in small, independently gated changesets:

1. evaluated direct calls and function-address uses;
2. constructors, destructors, temporaries, and synthesized special members;
3. namespace and local-static initialization/finalization;
4. vtable, RTTI, thunk, deleting-entry, and virtual-base dependencies;
5. EH cleanup and exception-runtime dependencies; and
6. post-force-inline weak/internal reachability cleanup.

For each family:

- run the old closure in verification mode first;
- require the new closure to contain all old required definitions;
- explain and test every removed edge;
- remove that old fallback only after parity;
- run the narrow owner test while editing;
- run the complete root `make test-report` before committing; and
- run the file audit with zero fatal findings.

Do not batch several dependency families into one commit merely to obtain a
larger timing result.

## 10. Performance acceptance

Use immutable baseline and candidate compilers and interleaved A/B runs because
the host is intermittently loaded. For each accepted behavior changeset:

- record wall, user, system, peak RSS, load, and CPU pressure;
- record object size, section sizes, symbol/binding counts, and SHA-256;
- record demand roots/edges, emitted bodies, LowIR instructions, native
  functions, and relocations;
- require deterministic candidate output;
- require a structural work reduction, not only a favorable wall-time sample;
- require median user-time improvement above the measured noise floor for a
  performance claim; and
- reject unexplained RSS growth.

The payload-removal changeset intentionally changes whole-object bytes. Native
allocatable sections and symbols should otherwise remain unchanged. Demand
changes intentionally alter code and symbol bytes; they must remain
deterministic and pass all object/link/runtime oracles.

GCC and Clang counts remain comparison points. Do not hard-code their exact
function counts as a compiler policy.

## 11. Final validation

The optimization is complete only when one clean committed tree satisfies all
of the following:

1. Native `.o` output contains no `.cppgm_object` section or `CPPGMOBJ`
   payload.
2. Private `.obj` separate/direct/mixed behavior passes PA30.
3. PA37 direct-versus-textual-LowIR object round trips pass at every configured
   optimization level.
4. Every emitted definition is attributable to a typed root or dependency.
5. The frozen strong set contains only justified strong definitions; the known
   inherited-constructor entries are weak.
6. Weak/body counts and output size are materially reduced, with all required
   positive emission tests still passing.
7. A full root `make test-report` passes after the final changeset.
8. The PA39 file audit has zero fatal findings.
9. Starting with clean PA39 objects, timed root `make inception` completes and
   `cppgm++-self` matches `cppgm++-inception` byte for byte.
10. The final frozen benchmark is measured in a predeclared interleaved window,
    and all implementation changes, regressions, counters, and measurements
    are committed before the result is declared complete.

## 12. Deferred post-inline weak-definition pruning

Do not remove an inline weak definition merely because all of its calls in the
current object disappeared after optional O1/O2 inlining. The source-level
reducer was an inline `weak_leaf(int)` called only by `main`; an experimental
inspection expected `_Z9weak_leafi` to be absent after inlining. That local
criterion is insufficient because the weak definition may still be the
translation unit's required COMDAT contribution for another object.

The prototype disturbed approximately 50 PA32/PA33 object and symbol fixtures.
Before reconsidering it, define a cross-object ownership rule that preserves
address observability, explicit instantiation, virtual/lifecycle/EH roots, and
the obligation to contribute an ODR definition. Add a fresh active PA32 or
optimization-owner course reducer only when that rule is implemented; there is
no dormant test anchor for this experiment.

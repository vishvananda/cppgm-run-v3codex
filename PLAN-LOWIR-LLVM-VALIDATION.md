# Plan: Investigate LowIR Through an Independent LLVM IR Export

Status: completed; results are in `REPORT-LOWIR-LLVM-VALIDATION.md`

Date: 2026-08-22

## 1. Objective

Validate the design and implementation of LowIR by comparing it with LLVM IR
produced from the same C++11 programs and the same libstdc++ implementation.
The primary deliverable is an investigation report, not a new production
backend and not an optimization campaign.

The investigation adds one bounded compiler capability:

```text
canonical semantic graph -> experimental LLVM IR module
```

That sibling output is compared with both existing LowIR and Clang's LLVM IR:

```text
                                      -> experimental LLVM IR
source -> cppgm++ canonical semantics
                                      -> typed LowIR -> native object

same source -> Clang frontend -> LLVM IR
```

This three-way view must answer four questions.

1. Does LowIR carry every source, object-model, ABI, lifetime, control-flow,
   and memory fact needed after semantic analysis?
2. Does the existing C++-to-LowIR lowerer use those facts correctly and at the
   right abstraction level?
3. Which LLVM attributes or metadata expose useful information that LowIR
   does not currently represent?
4. Which LowIR facts have no direct LLVM counterpart, and are they necessary
   backend contracts, useful frontend facts, redundant encodings, or facts
   that should live in a different compiler layer?

The final output is `REPORT-LOWIR-LLVM-VALIDATION.md`. It will contain the
evidence, differences, dispositions, and prioritized recommendations for:

- the public LowIR specification;
- the semantic-to-LowIR lowering implementation;
- backend-only handling that should not change LowIR; and
- follow-on investigations where the evidence is not yet decisive.

No LowIR specification, assignment contract, fixture, or lowering behavior is
changed merely to make it resemble Clang. Those changes require evidence from
this investigation and separately owned implementation work.

## 2. Nature and scope of the work

This is primarily an investigation. The LLVM exporter and comparison tooling
are measurement instruments needed to perform it.

The expected effort is divided accordingly:

| Work | Purpose | Intended result |
| --- | --- | --- |
| LLVM IR export layer | Make the canonical semantic graph observable through a second low-level representation | Valid, executable, deliberately unoptimized LLVM IR for the supported course language |
| Corpus and sweep tooling | Reproduce both compiler paths under controlled conditions | Per-assignment artifact and coverage manifests |
| Static fact crosswalk | Compare the LowIR contract with the LLVM IR contract independent of any one test | Bidirectional LowIR/LLVM fact matrix |
| Dynamic comparison | Find differences exercised by real course and hosted code | Categorized findings with minimized witnesses |
| Analysis and recommendations | Decide whether each difference is a defect, useful omission, redundancy, or implementation choice | Final report and ranked follow-on work |

The exporter is not intended to:

- replace typed LowIR, MIR, or direct ELF emission;
- become an alternative production object backend;
- link `cppgm++` against LLVM libraries;
- reproduce Clang's internal AST, CodeGen classes, optimization pipeline, or
  debug information;
- provide general LLVM-version portability in its first form;
- translate serialized LowIR into LLVM IR; or
- make raw LLVM text byte-identical to Clang.

The last two boundaries are important. A `LowIR -> LLVM IR` converter would
inherit facts already lost or misrepresented by C++-to-LowIR lowering. It
would be useful for another backend experiment, but it would not independently
test whether LowIR is an adequate boundary. This plan instead consumes the
same immutable semantic graph as the existing LowIR lowerer.

## 3. Normative references and version policy

### 3.1 LLVM IR is defined by the Language Reference

The normative LLVM source for the initial investigation is the
[LLVM 21.1 Language Reference Manual](https://releases.llvm.org/21.1.0/docs/LangRef.html).
The [LLVM 21.1 release notes](https://releases.llvm.org/21.1.0/docs/ReleaseNotes.html)
are also required because they identify IR vocabulary changes made for this
release. For example, LLVM 21 replaces the older `nocapture` spelling with the
`captures(none)` parameter attribute. The exporter must not copy an older
LLVM example when the version-matched specification says otherwise.

The Language Reference, not Clang output, decides:

- whether a module is well formed;
- type, SSA, dominance, and PHI placement rules;
- target triple and data-layout interpretation;
- linkage, visibility, preemption, COMDAT, alias, TLS, and global rules;
- function, return, parameter, call-site, and global attributes;
- allocated-object, lifetime, pointer capture, aliasing, and memory rules;
- poison, `undef`, `freeze`, undefined behavior, and integer overflow flags;
- volatile and atomic semantics and legal orderings;
- exception control-flow requirements for `invoke`, `landingpad`, and
  `resume` on the Itanium EH path;
- instruction and intrinsic semantics; and
- named metadata, instruction metadata, and module flags.

Clang remains the comparison frontend. Its output shows one mature lowering
choice, but the absence or presence of a Clang construct is not by itself a
requirement on LowIR.

### 3.2 ABI references are separate from the LLVM language

The report must distinguish LLVM well-formedness from target ABI correctness.
The x86-64 Linux comparison additionally uses the
[Itanium C++ ABI](https://itanium-cxx-abi.github.io/cxx-abi/abi.html) and the
[x86-64 System V psABI](https://gitlab.com/x86-psABIs/x86-64-ABI) as normative
references for:

- source-name mangling and entity variants;
- class layout, vtables, VTTs, RTTI, thunks, and member pointers;
- constructors, destructors, guard variables, and TLS wrappers;
- aggregate argument and result classification;
- exception personalities and runtime entry points; and
- register/stack call boundaries represented by LLVM attributes or types.

### 3.3 Pin the exact comparison environment

At plan creation, the local environment reports:

```text
Clang:       Ubuntu clang 21.1.8
target:      x86_64-pc-linux-gnu
host C++:    GCC 15.2.0
STL headers: GCC 15 libstdc++
```

Clang's probed C++ include roots begin with:

```text
/usr/include/c++/15
/usr/include/x86_64-linux-gnu/c++/15
/usr/include/c++/15/backward
```

The standalone `llvm-as`, `opt`, `llvm-dis`, and `llvm-link` tools are not
currently on `PATH`. The baseline verifier therefore must work through the
installed Clang IR reader. If version-matched standalone tools are later made
available, the investigation may run them as additional checks, but it must
not silently mix LLVM major versions.

A local positive/negative probe confirmed that this Clang reader accepts a
well-formed module and rejects a module whose definition does not dominate its
use. Phase I0 retains equivalent trust probes rather than assuming that any
command which accepts `.ll` text necessarily ran the verifier.

Phase I0 writes a machine-readable manifest containing:

- full `clang++ --version`, target, resource directory, and driver path;
- the LLVM Language Reference version and URL;
- full host compiler command and `CPPGM_STDLIB_FLAGS`;
- cppgm++'s generated hosted target, version, search directories, predefined
  macro digest, and include-path list;
- Clang's predefined macro digest and effective include-path list;
- target triple and data-layout string from a minimal Clang LLVM module;
- effective `-std`, optimization, PIC/PIE, exception, RTTI, and debug flags;
- the libstdc++ and C++ ABI libraries used at link time; and
- repository revision and working-tree state.

Patch-level LLVM changes are accepted only after regenerating the manifest and
rerunning the exporter trust probes. Major or minor LLVM changes require a
fresh LangRef audit before results are combined with the original report.

## 4. Architectural boundary for the exporter

### 4.1 Sibling semantic consumer

The exporter is a synchronous `SemanticGraphConsumer`, parallel to the PA15
lowering consumer. It consumes the borrowed `SemanticGraphView` before that
translation unit is released.

```text
ConsumeSemanticTranslationUnit
  |
  +-- existing LowerSemanticGraph -> pa15 TypedProgram -> LowIR
  |
  `-- experimental LowerSemanticGraphToLLVM -> LlvmModule -> .ll
```

The implementation may share immutable semantic facts and pure helpers whose
results are independently required by both outputs, including:

- canonical `TypeId`, `BindingId`, layout, and value-category facts;
- typed Itanium name construction;
- class and subobject layout queries;
- function and object demand decisions already finalized by semantics;
- literal bit representations; and
- target facts from the frozen x86-64 hosted profile.

It must not call `BuildTypedLowIRProgram`, consume `pa15_lowir_detail::TypedProgram`,
consume `lowir_model::LowirProgram`, parse serialized LowIR, or mechanically
translate existing LowIR instruction sequences. Otherwise the supposedly
independent path would repeat the behavior under investigation.

Object-capable comparisons must request the same semantic-analysis options as
the current object path, including complete constructor unwind and host-object
emission facts. The assignment-view `--emit-lowir` path and the object-capable
`--emit-lowir -O0` path are captured separately where those semantic options
make their output differ. An LLVM module built from host-object facts must not
be compared as though it came from the narrower assignment-only graph.

Every shared lowering helper introduced during the work is recorded in a
small independence ledger with one of these dispositions:

| Disposition | Meaning |
| --- | --- |
| semantic fact | Computed before either IR and safe to share |
| ABI fact | Defined by the target ABI and safe to share |
| presentation only | Naming or deterministic rendering with no lowering decision |
| existing LowIR policy | Must not be reused without an independent justification |
| exporter-only | Experimental LLVM representation or lowering choice |

### 4.2 Compiler surface

Add an experimental driver mode consistent with the existing dump modes:

```sh
dev/cppgm++ --emit-llvm-ir -O0 source.cpp -o source.ll
```

The first version accepts one source translation unit. LLVM modules are
translation-unit scoped, whereas the current `--emit-lowir` mode can combine
multiple sources into one program. Silently concatenating LLVM modules would
produce an invalid or misleading comparison. Multi-translation-unit cases are
handled by the investigation runner, which invokes the exporter once per
source and links the resulting objects through the existing host-link lane.

The mode must accept the same relevant source options as object-capable
`--emit-lowir`: language mode, includes, macro actions, hosted preprocessing,
exceptions, RTTI, target-relevant flags, and `-O0`. Unsupported options fail
explicitly.

Normal `cppgm++ -c`, link, `--emit-lowir`, LowIR optimization, MIR, and native
ELF paths remain unchanged. The production compiler must never invoke Clang or
another host compiler to create the requested LLVM output.

### 4.3 No LLVM library dependency

Use a compact typed module model and a textual boundary writer implemented in
C++11. Do not add libLLVM to the compiler link or depend on LLVM C++ headers.
This keeps the experimental path self-hostable, avoids a large version-coupled
runtime dependency, and preserves the repository's typed-boundary rule.

The minimum model owns:

- module target facts and named type declarations;
- global, alias, COMDAT, constructor/destructor-list, and function records;
- LLVM types, constants, operands, attributes, and metadata references;
- functions, basic blocks, SSA values, PHIs, and terminators; and
- declarations for LLVM and C++ runtime intrinsics used by the module.

Names and textual constants are rendered only by the `.ll` writer. Semantic
identity remains typed and compact inside the exporter.

### 4.4 Deliberately unoptimized lowering

The primary lane is frontend LLVM IR at `-O0`, before ordinary LLVM passes.
The Clang command begins with:

```sh
clang++ -std=gnu++11 -stdlib=libstdc++ -O0 \
  -S -emit-llvm -Xclang -disable-llvm-passes \
  source.cpp -o source.clang.ll
```

Phase I0 freezes the remaining target, PIC/PIE, include, exception, RTTI, and
debug flags explicitly after inspecting both drivers. Debug information is
off in the primary lane because it would dominate the metadata comparison
without testing LowIR.

An optimized Clang lane is a secondary sensitivity check, never the primary
oracle. It begins only after the unoptimized comparison is understood. The
report must not describe a difference introduced by mem2reg, inlining,
constant propagation, GVN, loop passes, or backend preparation as a missing
LowIR semantic fact.

## 5. LLVM Language Reference coverage checklist

Before a feature family is declared exportable, the relevant LLVM 21.1
LangRef sections must be reviewed and linked from the implementation ledger.
The following checklist is mandatory; it is intentionally broader than the
LLVM syntax observed in the first Clang samples.

I0 begins with a full table-of-contents audit of the versioned LangRef. Every
section receives an applicability disposition: `required`, `observed`,
`supported-not-yet-observed`, `target-inapplicable`, `language-inapplicable`,
or `future-scope`. Relevant sections are read in full and receive an exporter
mapping and witness. Explicitly marking subjects such as alternate EH models,
GC/statepoints, non-integral address spaces, scalable vectors, `indirectbr`,
`callbr`, constrained floating point, and target-specific intrinsics as
applicable or inapplicable prevents the checklist from silently inheriting
only the edges Clang happened to emit for the initial corpus.

### 5.1 Module and symbol structure

- identifiers, escaped names, and string constants;
- module structure, source filename, target triple, and data layout;
- declarations versus definitions;
- external, internal, private, weak, weak ODR, linkonce ODR, available
  externally, common, and external-weak linkage;
- visibility, `dso_local`, runtime preemption, unnamed-address state, TLS
  models, sections, alignments, and address spaces;
- globals, constants, aliases, COMDAT selection kinds, and ifunc recognition;
- function, global, return, parameter, and call-site attribute placement;
- personality functions and operand bundles;
- `llvm.used`, `llvm.compiler.used`, `llvm.global_ctors`, and
  `llvm.global_dtors`; and
- named metadata and module-flag merge behavior.

### 5.2 Types, values, and correctness hazards

- opaque `ptr` syntax and pointee types carried by memory instructions;
- integer widths, `x86_fp80`, arrays, literal and packed structures, function
  types, and zero-sized/empty C++ objects;
- constants, aggregate constants, symbol addresses, constant expressions, and
  relocation-bearing initializers;
- dominance, use-before-definition, PHI predecessor completeness, and the
  rule that PHIs precede non-PHI instructions;
- `poison`, `undef`, well-defined values, and `freeze`;
- exact, `nsw`, `nuw`, `inbounds`, fast-math, and other flags whose use can
  introduce poison or strengthen behavior; and
- padding versus value bits, especially for `noundef` and aggregates.

The exporter starts conservatively: it emits no overflow, exactness,
in-bounds, aliasing, dereferenceability, or no-undefined-value promise unless
the semantic or ABI evidence proves the complete LLVM precondition. A
stronger-looking Clang annotation is recorded as a candidate and checked
against the LangRef before being copied.

### 5.3 Memory, object lifetime, and calls

- allocated objects, `alloca`, alignment, dynamic stack allocation, and object
  lifetime;
- loads, stores, GEP, volatile access, aliasing, pointer capture, and pointer
  provenance constraints;
- `llvm.lifetime.start/end`, `llvm.memcpy`, `llvm.memmove`, and `llvm.memset`;
- C++ reference and by-address passing versus LLVM pointer values;
- target calling convention, `sret`, `byval`, `byref`, `inalloca`, `inreg`,
  `signext`, `zeroext`, `noext`, alignment, `captures(...)`, alias, access, and
  dereferenceability attributes;
- caller/callee agreement for ABI-impacting types and attributes;
- variadic calls and `llvm.va_start`, `llvm.va_end`, and `va_arg`; and
- `call`, `invoke`, tail-call markers, `noreturn`, unwind attributes, memory
  effects, and unreachable continuations.

### 5.4 Control flow, exceptions, and concurrency

- `ret`, `br`, `switch`, `unreachable`, `phi`, `select`, and `freeze`;
- Itanium-style `invoke`, `landingpad`, ordered catch/filter clauses,
  cleanup landing pads, exception selection, and `resume`;
- personality and runtime helper declarations;
- nesting and edge restrictions for exception-handling blocks;
- atomic load/store, `cmpxchg`, `atomicrmw`, and `fence`;
- success/failure ordering restrictions, single-thread versus system scope,
  and the LLVM memory model; and
- compiler fences and GNU inline assembly used by the supported hosted code.

### 5.5 Metadata and attributes

Inventory all Clang-emitted attributes and metadata, including:

- ABI-required attributes;
- semantic promises that affect correctness;
- optimizer-only facts such as alias scopes, TBAA, ranges, invariant loads,
  loop hints, profiles, and lifetime markers;
- target/code-generation policy such as frame-pointer, unwind-table, target
  CPU, and target-feature strings;
- debug/provenance information; and
- module bookkeeping such as wchar width, PIC/PIE level, unwind-table level,
  and compiler identification.

Each item is classified from its LangRef semantics before the report evaluates
whether LowIR needs an equivalent. The mere spelling `metadata` does not mean
an item is optional, and the absence of a dedicated LLVM metadata node does
not mean LLVM discarded the behavior; it may be encoded by types,
instructions, linkage, globals, attributes, or control flow.

## 6. Reproducible comparison profiles

### 6.1 Controlled language profile

Use small and medium course inputs that both compilers accept without hosted
headers. These cases isolate source lowering and provide the clearest evidence
for scalar operations, control flow, lifetime, class layout, templates, and
exceptions.

For every case, retain these generated artifacts outside the source tree:

```text
case.semantic.txt
case.lowir
case.ours.ll
case.clang.ll
case.ours.llvm-summary.json
case.clang.llvm-summary.json
case.crosswalk.json
case.run.json
```

Raw artifacts are addressed by digest in the final report. They are not
checked in as assignment references.

Capture both LowIR views where relevant:

- ordinary `--emit-lowir` for the assignment-visible durable contract; and
- object-capable `--emit-lowir -O0` for a semantic configuration aligned with
  the LLVM and native-object paths.

The report labels them explicitly and never attributes a difference caused by
the two semantic configurations to the LowIR grammar.

### 6.2 Same-libstdc++ hosted profile

Clang is invoked with `-stdlib=libstdc++` and the GCC 15 header roots selected
by the repository's hosted configuration. Objects from both LLVM paths are
linked against the same libstdc++/libsupc++ runtime family.

The two frontends still have different predefined macros and builtin feature
sets. libstdc++ can consequently select compiler-specific conditional code
even when the header files are identical. The runner records:

- include paths and resolved header identities;
- macro-set digests;
- preprocessed output digests where both preprocessors can provide them; and
- compiler-specific branches detected in a minimized witness.

Such a difference is classified as `host-preprocessing` until the same
semantic construct is reproduced in the controlled language profile. It is
not immediately attributed to LowIR.

### 6.3 Behavioral triangle

For runnable cases, compare three executions:

1. the existing cppgm++ LowIR/native object path;
2. the experimental cppgm++ semantic/LLVM object, compiled from `.ll` by the
   version-matched Clang backend; and
3. the Clang C++/LLVM object.

Use identical support objects, linker, libraries, environment, stdin, and
timeouts. Record exit status, stdout, and stderr separately.

This triangle narrows attribution:

| Observation | Initial interpretation |
| --- | --- |
| semantic/LLVM and Clang agree; LowIR/native differs | LowIR lowering, LowIR expressiveness, or native backend candidate |
| LowIR/native and Clang agree; semantic/LLVM differs | Experimental exporter defect or legitimate alternative lowering |
| both cppgm++ paths agree; Clang differs | semantic gap, source/toolchain difference, UB, or legitimate Clang choice |
| all execute alike but IR differs | representation, metadata, ABI shape, or optimization investigation |

Execution agreement is necessary but not sufficient. ABI, weak/COMDAT
emission, atomics, alias promises, and undefined-behavior differences can be
latent in a passing run.

## 7. Comparison method

### 7.1 Do not use a raw textual diff as the verdict

LLVM IR permits arbitrary local names, type names, declaration order,
attribute-group numbering, and metadata numbering. Clang also makes valid
frontend-specific choices about allocas, temporary names, CFG shape, and
module bookkeeping.

The runner produces a canonical inventory rather than treating line equality
as correctness. It preserves semantic order where LLVM requires it, including
instructions, PHI incoming edges, aggregate layout, vtable slots, ctor/dtor
arrays, landing-pad clauses, and atomic orderings.

The canonical summary includes:

- target and module facts;
- symbol definitions, declarations, linkage, visibility, COMDAT membership,
  aliases, TLS, and section placement;
- exact function and call signatures with attributes in their legal positions;
- named type sizes, alignments, field offsets, padding, and global initializer
  topology;
- normalized CFG edges and exception edges;
- opcode and intrinsic inventories by typed operand shape;
- memory access sizes, alignments, volatility, atomicity, and ordering;
- call graph, runtime helper, constructor/destructor, vtable, and RTTI edges;
- function, parameter, return, call-site, global, and instruction attributes;
  and
- metadata by LangRef-defined semantic category rather than numeric node ID.

### 7.2 Static bidirectional fact crosswalk

Build a complete static matrix from the public PA13 LowIR specification and
the typed `lowir_model` surface. Every LowIR type, top-level field, symbol
metadata field, function-boundary field, parameter field, global-storage
field, instruction, operand property, projection kind, exception construct,
atomic ordering, and debug field receives one LLVM disposition:

| LLVM disposition | Meaning |
| --- | --- |
| direct | One LLVM type, opcode, attribute, metadata attachment, or module construct represents the fact |
| patterned | LLVM represents the fact through a required sequence or graph shape |
| ABI-external | The fact belongs to the target ABI and is represented only after target lowering or object emission |
| frontend-private | LLVM does not need the high-level label after behavior has been encoded |
| optimizer-derived | LLVM normally infers the fact instead of requiring frontend representation |
| no known equivalent | Candidate redundancy or a deliberate LowIR-specific contract |
| unresolved | More specification or implementation evidence is required |

Then reverse the matrix for every LLVM construct observed in the Clang
corpus. This prevents the investigation from only asking whether LLVM can
encode LowIR and missing information that flows in the other direction.

Expected areas of many-to-one mapping include:

- LowIR `role=init/fini` versus `llvm.global_ctors/dtors` and emitted helper
  functions;
- LowIR object and projection metadata versus byte/structure types and GEP
  paths;
- LowIR call effects/unwind/return fields versus function and call-site
  attributes plus CFG;
- LowIR reference, by-address, indirect-result, access, alias, and capture
  fields versus LLVM ABI and optimization attributes;
- LowIR object copy/zero operations versus memory intrinsics or expanded
  accesses; and
- LowIR exception regions versus LLVM `invoke`/landing-pad graphs.

Expected LowIR-only candidates include runtime roles, object-demand roots,
generated-definition presentation policy, explicit source projection kinds,
and lifecycle intent. These are not presumed redundant: the report must check
whether the native backend needs them, whether LLVM encodes their effect
structurally, and whether they remain useful at LowIR's intentionally higher
abstraction level.

### 7.3 Dynamic finding records

Every material difference receives a stable ID and a record containing:

```text
finding ID
earliest assignment owner
source witness and corpus cases
semantic facts used by each cppgm++ path
existing LowIR representation
experimental LLVM representation
Clang LLVM representation
relevant LLVM 21.1 LangRef and ABI rule
verification and runtime result
classification, severity, confidence, and recommendation
```

Use these finding classes:

| Class | Meaning |
| --- | --- |
| `lowir-spec-gap` | Required post-semantic fact cannot be represented faithfully in public LowIR |
| `lowir-lowering` | LowIR can represent the fact, but source lowering omits or misuses it |
| `semantic-gap` | The canonical semantic graph does not expose enough information for either lowerer |
| `native-only` | Difference belongs to MIR, instruction selection, object emission, or linking |
| `llvm-exporter` | Difference is a defect or limitation in the investigation adapter |
| `llvm-required` | LangRef well-formedness or ABI rule required in LLVM but not necessarily in LowIR |
| `optimization-fact` | Useful optional information that may improve later analysis or code generation |
| `clang-choice` | Valid Clang lowering with no demonstrated LowIR consequence |
| `lowir-purposeful-extra` | LowIR retains a useful higher-level fact LLVM encodes structurally or discards |
| `lowir-redundancy-candidate` | LowIR fact may be derivable or misplaced; removal needs separate proof |
| `host-preprocessing` | Header, macro, builtin, extension, or runtime-profile difference |
| `unresolved` | Current evidence does not distinguish the alternatives |

Severity is independent of textual size:

- `C0`: invalid IR, wrong runtime behavior, ABI break, or unsound semantic
  promise;
- `C1`: unsupported valid construct, missing boundary fact, or substantial
  systematic lowering divergence;
- `C2`: optimization opportunity, redundant representation, or maintainability
  issue;
- `C3`: presentation, debug, toolchain bookkeeping, or explained Clang choice.

## 8. Assignment sweep

The sweep follows assignment ownership rather than selecting only visually
interesting Clang output. For each PA, record total successful course cases,
Clang-compatible cases, exporter attempts, valid LLVM modules, object builds,
runnable cases, and unresolved failures. Unsupported exporter cases remain in
the denominator.

| Sweep | Assignment ownership | Primary evidence |
| --- | --- | --- |
| S0 | PA13 LowIR contract and PA14 ABI naming | Complete static LowIR/LLVM crosswalk; symbol and type probes |
| S1 | PA15 procedural lowering | scalar types, locals, expressions, calls, CFG, PHIs, globals, variadics |
| S2 | PA16 basic classes | object layout, field/base projections, construction, destruction, cleanup edges |
| S3 | PA17 value semantics | copy/move operations, assignment, object transfers, arrays, lifetime ordering |
| S4 | PA18 virtual dispatch | vptr setup, vtables, thunks, virtual calls, weak/COMDAT emission |
| S5 | PA19 basic templates | instantiation identity, weak ODR bodies, template functions and classes |
| S6 | PA20 specialization/metaprogramming | specialization selection, constant folding, emitted body demand |
| S7 | PA21 constant evaluation | constant objects, aggregate initialization, readonly data, runtime fallback |
| S8 | PA22 template entity model | entity identity, explicit/implicit instantiation, partial specialization |
| S9 | PA23 deduction/substitution/SFINAE | selected specializations, overload results, dependent member calls |
| S10 | PA24 template integration | composed template features, recursive demand, large instantiated graphs |
| S11 | PA25 core language closure | lambdas, captures, range-for lowering, local/static lifetime surfaces |
| S12 | PA26 advanced language closure | exceptions, RTTI queries/casts, initializer lists, allocation/deallocation |
| S13 | PA27 non-virtual multi-base model | base adjustment, member pointers, thunks, layout and call boundaries |
| S14 | PA28 virtual/RTTI completion | virtual bases, VTTs, construction vtables, dynamic casts, complex EH cleanup |
| S15 | PA29 behavior bridge | behavioral triangle for representative LowIR-era cases; no LLVM-to-LowIR claim |
| S16 | PA30-PA33 object and host ABI | separate TUs, linkage, aliases, TLS, EH facts, host interoperation |
| S17 | PA34-PA36 hosted compatibility | same-libstdc++ headers, emitted templates, library ABI calls, link/runtime |
| S18 | PA37-PA38 optimization sensitivity | raw versus optimized IR shape; findings kept separate from semantic completeness |

PA19 through PA24 are a required full successful-test census, not a sample.
Templates cease to exist as high-level LLVM entities after instantiation, so
the comparison focuses on instantiated body identity, emission demand,
linkage, COMDAT grouping, constant materialization, call boundaries, and
resulting object-model code.

PA34 through PA36 use both curated representatives and the largest practical
hosted cases. They are not allowed to replace the controlled template sweep:
large STL output is useful for frequency and impact, but poor evidence for the
cause of an individual difference until reduced.

Invalid course inputs remain rejection tests. They do not need LLVM output.
Cases using intentional project extensions or accepted behavior that Clang
rejects are recorded as non-comparable rather than removed from coverage.

## 9. Investigation phases

### I0. Freeze the protocol and specification checklist

Deliver:

- toolchain/environment manifest;
- exact cppgm++ and Clang command templates;
- LLVM 21.1 LangRef checklist with section links;
- assignment corpus manifest and coverage denominator rules;
- artifact schema and finding schema; and
- a scratch-output policy that leaves generated `.ll`, objects, and reports
  out of assignment fixtures.

Exit when two repeated runs produce identical manifests and raw artifact
digests for a small scalar, class, template, exception, and hosted probe.

### I1. Build and trust the LLVM export instrument

Implement:

- `--emit-llvm-ir` driver routing;
- the typed LLVM module/output model;
- scalar types, constants, globals, functions, blocks, SSA values, PHIs,
  ordinary calls, loads/stores, GEPs, conversions, and terminators;
- target triple/data layout and minimal legal module flags;
- deterministic text rendering; and
- parse/verify/object checks through the version-matched Clang IR reader.

Add small course-owned tests only where the driver surface or exporter itself
needs a durable regression. New tests belong to the earliest relevant
`cppgm.tests/course/paN/` directory. Generated Clang text is not a `.ref`
oracle.

Exit when the S1 procedural corpus emits well-formed LLVM, compiles to objects,
and matches existing behavior for runnable cases.

### I2. Complete object, ABI, EH, and hosted exporter coverage

Extend only as needed to run the investigation:

- class and aggregate storage;
- C++ ABI signatures and parameter/return attributes;
- aliases, weak ODR/linkonce behavior, COMDATs, TLS, init/fini lists;
- vtables, VTTs, RTTI, thunks, member pointers, and global constant data;
- memory intrinsics, stack allocation, varargs, atomics, and inline assembly;
- Itanium exception control flow and runtime helpers; and
- source constructs whose semantic nodes require distinct LLVM lowering.

Each extension carries a LangRef citation and at least one verification and
behavior witness. If the exporter cannot support a feature without creating a
second general-purpose backend, record the limitation and continue static
crosswalk analysis rather than silently approximating it.

Exit when every successful PA15-PA28 course case is either validly exported or
has a specific, categorized exporter limitation, and the selected PA30-PA36
hosted cases have complete artifacts.

### I3. Build the fact inventories and bidirectional crosswalk

Complete the static LowIR surface census, LLVM observed-construct census, and
normalized per-module summaries. Cross-check each exporter mapping against the
versioned LangRef rather than only against Clang syntax.

Exit when:

- every public LowIR field has a disposition;
- every Clang LLVM construct observed in the sweep has a disposition;
- no `unknown` parser bucket hides a frequent instruction, attribute, or
  metadata family; and
- summary generation is deterministic on repeated raw inputs.

### I4. Sweep, reduce, and classify

Run S0 through S18 in order. Templates receive the full census required above.
For each candidate difference:

1. reproduce it on clean artifacts;
2. determine whether input tokens and semantic selection agree;
3. read the exact LangRef and ABI rule;
4. minimize to the earliest owning assignment when practical;
5. run the behavioral triangle;
6. determine whether current LowIR can already express the correct result;
7. classify the finding and confidence; and
8. record frequency and impact in the full corpus.

Do not change LowIR or its current lowering while gathering the baseline.
Otherwise later comparisons would be against a moving subject. Exporter and
analysis-tool fixes are allowed and must be recorded so affected cases can be
rerun.

### I5. Write the final report and recommendations

Produce `REPORT-LOWIR-LLVM-VALIDATION.md` with:

1. executive conclusions;
2. exact toolchain, specification, commands, and corpus coverage;
3. confidence assessment for the LLVM exporter;
4. complete LowIR-to-LLVM and LLVM-to-LowIR fact summaries;
5. findings by assignment and by class;
6. metadata/attribute findings, separated into required, optimization,
   debug/provenance, target-policy, and bookkeeping groups;
7. lowering-shape findings, including major CFG, lifetime, object, call, EH,
   and template-emission divergences;
8. LowIR-only facts and whether each should be kept, moved, derived, or
   reconsidered;
9. recommended LowIR specification changes;
10. recommended semantic-to-LowIR lowering changes;
11. backend-only and optimization follow-ups that should not alter LowIR;
12. explained no-change findings and Clang-specific choices;
13. unresolved questions and exporter limitations; and
14. a prioritized action table with earliest PA ownership and expected
    reference impact.

Each recommendation uses one disposition:

- `accept-now`: demonstrated correctness or ABI defect with a clear owner;
- `follow-up`: strong evidence, but implementation scope needs a separate plan;
- `measure-more`: plausible benefit or redundancy without enough evidence;
- `keep`: LowIR intentionally and usefully differs from LLVM;
- `reject`: copying Clang would lower the abstraction incorrectly or add an
  unjustified promise; or
- `not-lowir`: fix semantics, MIR, native lowering, object emission, hosted
  configuration, or the investigation adapter instead.

The report recommends changes; it does not make fixture migrations part of
the investigative sweep.

## 10. Validation and trust gates

These gates validate the instrument and the evidence. They are not
optimization acceptance gates.

### 10.1 Per-module validity

Every exported module must:

- parse under the pinned LLVM reader;
- pass LLVM well-formedness verification;
- compile to an x86-64 object with no disabled verifier;
- contain the pinned target triple and compatible data layout;
- have complete blocks, terminators, dominance, PHIs, and EH edges;
- use legal atomic orderings and attribute positions; and
- fail explicitly for an unsupported construct.

The installed baseline command is:

```sh
clang -x ir -c case.ours.ll -o case.ours.o
```

If a matching `llvm-as` and `opt` become available, also run:

```sh
llvm-as case.ours.ll -o case.ours.bc
opt -passes=verify -disable-output case.ours.bc
```

### 10.2 Behavioral validity

Runnable cases compare status and observable output across the behavioral
triangle. Multi-TU and hosted cases also compare symbol resolution, weak/COMDAT
coalescing, TLS behavior, exceptions crossing object boundaries, static
initialization/destruction, RTTI, and allocation/runtime interaction.

### 10.3 Existing repository correctness

Exporter implementation must not regress the existing assignment path. At
each implementation milestone run the focused assignment command and the
through-target report appropriate to the earliest touched compiler behavior:

```sh
make test-paN
make test-report-through-paN
```

Before the investigation report is finalized, run the clean full through-PA38
report with `make test-report-through-pa38`. Because the exporter is linked
into `cppgm++`, also run root `make inception` before declaring the
implementation instrument complete.

The reference binaries and checked-in fixtures remain the assignment oracle.
Clang does not replace them. Do not edit tests or `.ref` files to hide an
exporter limitation or to force Clang parity.

### 10.4 Evidence quality

A report recommendation that changes public LowIR needs:

- a minimized valid C++ witness;
- a LangRef or ABI rule explaining the required behavior;
- evidence that the existing LowIR contract cannot express it, or that the
  existing lowerer fails to use an already expressible fact;
- confirmation beyond one raw Clang spelling;
- earliest PA ownership and expected downstream fixture census; and
- a statement of why the fact belongs above MIR and object emission.

An optimization-only recommendation additionally needs frequency or impact
evidence. A single extra Clang attribute is not sufficient.

## 11. Anticipated analysis areas

The following are hypotheses to investigate, not conclusions.

### 11.1 Clang/LLVM information that may be absent from LowIR

- precise ABI parameter/return attributes such as sign/zero extension,
  structure-return type and alignment, by-value aggregate contracts, and
  pointer capture/access guarantees;
- global visibility, preemption, unnamed-address, COMDAT selection, TLS model,
  and constructor priority details;
- per-access alignment, volatility, atomic sync scope, and richer memory
  effects;
- poison-sensitive arithmetic and GEP facts;
- alias analysis, TBAA, invariant, range, dereferenceability, no-undefined,
  loop, profile, and lifetime annotations;
- exact `invoke`/landing-pad relationship and personality-facing types; and
- module flags or target policy necessary for compatible object generation.

The report must separate information required for correctness or ABI from
information Clang emits only to help LLVM optimize.

### 11.2 LowIR information that may not appear directly in LLVM

- runtime symbol roles such as allocation, exception, RTTI, termination, and
  entry/init/fini ownership;
- explicit call arity/effects/unwind/return enums;
- source-level object/reference passing and projection categories;
- construction, destruction, exception-cleanup, and demand roots;
- generated-definition ordering and presentation identity;
- object aliases and internal/object symbol duality; and
- higher-level `copyobj`/`zeroinit` operations.

For each, determine whether LLVM:

1. represents the same fact directly;
2. represents only its lowered behavior;
3. expects a later analysis to infer it;
4. leaves it to the target ABI/backend; or
5. genuinely does not need it.

Only the fifth outcome makes a LowIR removal candidate, and even then the
native backend and durable text contract may justify keeping it.

### 11.3 Major lowering-shape comparisons

Compare, without assuming identity is desirable:

- stack-based versus SSA value placement;
- common return and cleanup continuations;
- short-circuiting, conditional values, switches, and loops;
- aggregate copy/zero versus scalarized accesses;
- constructors, destructors, temporary materialization, copy elision, and
  lifetime markers;
- direct, indirect, virtual, thunk-adjusted, and member-pointer calls;
- vtable, VTT, RTTI, guard, TLS, and initializer emission;
- template instantiation demand and COMDAT grouping;
- exception regions, landing-pad sharing, catch ordering, and resume paths;
- atomics and compiler fences; and
- intrinsic recognition versus runtime calls.

When Clang is more compact, first identify whether the difference is mandatory
frontend canonicalization, an LLVM optimization pass, a target-codegen choice,
or a source fact. Do not label it a LowIR optimization opportunity until that
boundary is established.

## 12. Repository placement and change discipline

Expected implementation ownership is:

- `dev/cppgm++.cpp` for the experimental flag and driver routing;
- new C++11 files under `dev/src/` for the LLVM module model, semantic
  consumer, ABI mapping, verifier-facing rendering, and statistics;
- `dev/frontend_source_sets.mk` for every new `dev/src/*.cpp` linked into
  `cppgm++`;
- scripts under `scripts/` for toolchain manifests, corpus execution,
  canonical inventories, crosswalks, and report tables; and
- new durable tests only under the earliest owning
  `cppgm.tests/course/paN/` directory.

Generated LLVM, object, JSON, and runtime artifacts go to an explicit scratch
directory. Do not fill `paN/`, `dev/`, or the repository root with generated
comparison output.

Keep these changes separate:

1. exporter model and driver surface;
2. exporter feature families;
3. comparison and inventory tooling;
4. investigation data/report updates; and
5. any later LowIR or lowering fix approved from the report.

During I0-I5, fix the exporter and analysis tools as needed, but freeze the
LowIR subject being measured. If a pre-existing LowIR correctness defect must
be fixed immediately, record the before/after revision and rerun every affected
assignment band rather than combining results from two baselines.

## 13. Risks and controls

| Risk | Control |
| --- | --- |
| The exporter repeats the LowIR lowerer and confirms the same bug | Direct semantic consumer; independence ledger; no typed or serialized LowIR input |
| Invalid LLVM looks plausible in text | Version-matched LangRef checklist plus parser, verifier, and object compilation for every module |
| Clang implementation details are mistaken for LLVM requirements | Classify every finding using the LangRef and ABI before recommending a change |
| LLVM version drift invalidates syntax or attributes | Pin LLVM 21.1 environment and rerun trust probes on changes; note `captures(none)` and other versioned vocabulary |
| Raw diffs are dominated by identifiers and presentation | Canonical semantic inventories; raw text retained only as evidence |
| Optimizer effects hide frontend lowering | Primary Clang lane disables LLVM passes; optimized results reported separately |
| Same headers do not mean same preprocessed program | Record paths, macros, preprocessed digests, and compiler-specific branches; reproduce in controlled inputs |
| Passing execution hides UB or ABI defects | LangRef/ABI audit, symbol/CFG/attribute checks, and multi-TU host interoperation |
| Exporter scope grows into a second production backend | Fixed x86-64/Linux/Itanium/LLVM-21 target, explicit unsupported records, no LLVM libraries, report-first completion |
| Large STL modules overwhelm manual analysis | Frequency summaries followed by minimized assignment-owned witnesses |
| LowIR changes move during the sweep | Freeze the LowIR baseline; rerun affected bands after any unavoidable correction |
| Extra LowIR facts are removed merely because LLVM omits them | Require backend-use and derivability proof; use `keep` and `lowir-purposeful-extra` dispositions |

## 14. Completion criteria

The investigation is complete when all of the following are true:

1. `cppgm++ --emit-llvm-ir` consumes the canonical semantic graph directly
   and emits deterministic, verifier-clean LLVM IR for the supported sweep.
2. Every successful PA15-PA28 course case was attempted, with complete
   coverage counts and explicit exporter limitations; PA19-PA24 have a full
   successful-case census.
3. Representative PA30-PA36 same-libstdc++ multi-TU and hosted cases compile,
   link, and run through the behavioral triangle.
4. Every public LowIR type, operation, and metadata field has a static LLVM
   disposition.
5. Every LLVM instruction, intrinsic, attribute, and metadata family observed
   in Clang output has a LowIR disposition grounded in LLVM 21.1 LangRef
   semantics.
6. Material lowering divergences have stable finding IDs, assignment owners,
   witnesses, runtime evidence, specification citations, and classifications.
7. The final report clearly separates LowIR specification gaps, existing
   lowering defects, semantic gaps, backend-only issues, optional optimization
   facts, useful LowIR-only facts, Clang choices, and unresolved cases.
8. Recommendations identify expected assignment, README, test, reference, and
   downstream-census impact without modifying those artifacts during the
   investigation.
9. Existing through-PA38 tests and PA39 inception remain clean after the
   exporter implementation.
10. A reader can reproduce every report table from the pinned manifest,
    corpus manifest, raw artifact digests, and checked-in analysis scripts.

Completion does not require cppgm++ LLVM IR to be textually identical to
Clang, nor does it require implementing every recommendation. It requires a
trusted comparison instrument and a defensible report explaining which
differences matter to LowIR and why.

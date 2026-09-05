# LLVM IR 21.1 Coverage Ledger

Status: completed for the reported corpus; future-scope rows remain explicit

Normative source: [LLVM 21.1 Language Reference Manual](https://releases.llvm.org/21.1.0/docs/LangRef.html)

This ledger prevents the LowIR/LLVM investigation from treating the LLVM IR
emitted by the first Clang samples as the complete language. `Required` means
the subject is part of the initial x86-64 Linux C++11 exporter. `Observed`
means Clang emitted it in the current corpus. `Future` means it is relevant to
a later assignment band. `Target-inapplicable` and `language-inapplicable`
mean the section was reviewed but is outside this investigation's fixed
target/language boundary.

The detailed instruction, attribute, intrinsic, and metadata rows grow as the
assignment sweep advances. A family may not be marked complete until its
LangRef section has been read, its LLVM 21.1 syntax and semantic preconditions
are represented by the exporter, and a verifier-clean witness is recorded.

## High-level structure

| LangRef subject | Disposition | Initial LowIR question | Status |
| --- | --- | --- | --- |
| Well-formedness | Required | LowIR validation is not sufficient to establish LLVM dominance, PHI, EH, or attribute validity | Clang positive/negative verifier probe complete |
| Identifiers and strings | Required | Determine which LowIR presentation spellings need LLVM quoting | Renderer and verifier-clean escaping witnesses complete |
| Module structure and source filename | Required | Module-per-TU differs from concatenated LowIR programs | One-module-per-TU policy implemented; multi-TU link witness complete |
| Linkage types | Required | Crosswalk LowIR internal/strong/weak and template emission to LLVM linkage | Scalar/template census complete; weak versus weak-ODR precision recorded |
| Calling conventions | Required | Crosswalk LowIR boundary metadata and SysV/Itanium ABI | Scalar SysV boundary implemented; aggregate ABI is an explicit exporter limit |
| Visibility and runtime preemption | Future | LowIR currently separates binding and object-output preferences | Static disposition complete; full object exporter deferred |
| DLL storage classes | Target-inapplicable | Fixed ELF target | Reviewed as outside target |
| Thread-local storage models | Future | LowIR records TLS intent but not every LLVM TLS model | Clang use observed; backend-policy disposition recorded |
| Structure types | Future | Compare byte-object LowIR types with LLVM aggregate layout | Static crosswalk complete; class lowering remains an exporter limit |
| Non-integral pointers/address spaces | Target-inapplicable | Fixed x86-64 integral pointer profile | Applicability recorded |
| Globals, functions, aliases, and COMDATs | Required | Compare declarations, definitions, aliases, weak ODR, vtables, RTTI, and templates | Globals/functions implemented; aliases/COMDAT/object graphs statically crosswalked and dynamically limited |
| IFuncs | Future | No current LowIR construct; detect any hosted occurrence | Not observed in 2,663 Clang modules |
| Personality functions | Future | Crosswalk LowIR EH roles with LLVM personalities | Static Itanium mapping complete; exporter reports EH limitation |
| Operand bundles | Future | Determine whether any supported Itanium or hosted lowering needs them | Not observed in the current corpus |
| Module-level inline assembly | Future | GNU asm is supported later; ordinary asm calls are the likely mapping | Not observed in the current corpus; future scope |
| Data layout and target triple | Required | Required external facts not represented by serialized LowIR | Clang baseline captured and exporter matches pinned x86-64 values |
| Allocated objects and object lifetime | Required | Compare LowIR slots/object operations and LLVM allocas/lifetime intrinsics | Procedural storage complete; lifetime intrinsics classified optional |
| Pointer aliasing and capture | Required | Crosswalk LowIR alias/access/capture fields and LLVM 21 `captures(...)` | Static mapping complete; conservative exporter emits no unproved promise |
| Volatile and memory model | Required | Verify cv/atomic intent survives LowIR lowering | Correctness gap confirmed: volatile is absent and O2 removes accesses |
| Atomic ordering constraints | Future | Crosswalk all LowIR atomic orders and legal LLVM success/failure pairs | Static mapping complete; dynamic LLVM exporter coverage deferred |
| Floating-point environment and semantics | Required | Compare LowIR scalar operations without inventing fast-math promises | Scalar strict-FP operations and exact literals verifier-clean |
| Fast-math flags | Required-negative | Exporter emits none without semantic proof; inventory Clang occurrences | No flags emitted or observed in the pinned pristine lane |

## Types, constants, and values

| LangRef subject | Disposition | Initial LowIR question | Status |
| --- | --- | --- | --- |
| Void, function, integer, floating, pointer, array, vector, and structure types | Required/Future | Establish exact source/storage/boundary mapping and padding | Scalar/array exporter implemented; object/vector ABI coverage limited and crosswalked |
| Label, token, and metadata types | Required/Future | Label is CFG-only; token/metadata applicability comes from intrinsics | Labels covered; token not observed; metadata inventoried |
| Scalable vectors | Language-inapplicable initially | Current hosted vector facts target fixed-size x86 vectors | Applicability recorded |
| Constants and global addresses | Required | Compare scalar bits, aggregates, symbol addends, and relocations | Scalar/array/address constants implemented and verifier-clean |
| `undef`, poison, well-defined values, and `freeze` | Required-negative | Avoid accidentally strengthening semantics or triggering LLVM UB | LangRef reviewed; no poison promises introduced; uninitialized alloca witness complete |
| Block addresses | Future | Required only if supported computed-goto lowering reaches LLVM | Corpus audit pending |
| Constant expressions | Required | Limit use to LLVM 21 forms; removed constant-expression opcodes must not be copied | Current constant GEP/address forms verifier-clean |
| Inline assembler expressions | Future | Map supported GNU asm and constraints | Not observed in selected lane; future scope |

## Metadata and module flags

| LangRef subject | Disposition | Initial LowIR question | Status |
| --- | --- | --- | --- |
| Metadata strings/nodes | Required | Needed to inventory Clang without treating all metadata as optional | Deterministic inventory complete; zero unknown instruction families |
| TBAA and TBAA struct | Future | Potential optimization fact absent from LowIR | Not observed with disabled LLVM passes; optional disposition recorded |
| Alias scope and noalias metadata | Future | Compare with LowIR parameter alias/access facts | Not observed; parameter attributes observed and crosswalked |
| Range, dereferenceable, nonnull, align, and noundef metadata | Future | Separate correctness promises from optional analysis facts | Attribute/attachment census complete; conservative omission recommended |
| Loop/access-group metadata | Future | Compare after PA37 without confusing optimizer output with frontend facts | `llvm.loop` observed and classified optimizer metadata |
| Invariant/type/associated metadata | Future | Relevant to vtables, RTTI, and optimizer reasoning | Not observed in current pristine corpus; full object lane limited |
| Profile/callsite/memprof metadata | Language-inapplicable initially | No profiling input in primary lane | Applicability recorded |
| Debug metadata and debug records | Language-inapplicable initially | Debug is disabled in primary comparison | Separate future lane |
| Module flags | Required | Classify ABI, target-policy, and bookkeeping flags separately | Observed once per Clang module; classified target-policy/bookkeeping |
| Automatic linker/dependent-library metadata | Future | Determine hosted/link-driver relevance | Not observed in selected hosted lane |
| ThinLTO summaries | Language-inapplicable | No LTO in the fixed investigation profile | Applicability recorded |

## Instruction families

| LangRef subject | Disposition | Initial LowIR question | Status |
| --- | --- | --- | --- |
| `ret`, `br`, `switch`, and `unreachable` | Required | Direct CFG crosswalk | Implemented and verifier-clean |
| `indirectbr` and `callbr` | Future | Relevant only if computed goto/asm-goto reaches supported semantics | Not observed; future scope |
| `invoke` and `resume` | Future | Crosswalk LowIR exception regions to explicit exceptional CFG | Observed in Clang and statically crosswalked; exporter coverage deferred |
| Windows funclet terminators/pads | Target-inapplicable | Fixed Itanium EH/ELF target | Applicability recorded |
| Integer and floating binary operations | Required | Check types, signedness, overflow/fast-math promises, and UB | Implemented scalar subset; no unjustified flags |
| Vector and aggregate operations | Future | Compare hosted vectors and aggregate value representation | `extractvalue`/`insertvalue` observed; object exporter remains limited |
| `alloca`, load, store, GEP | Required | Compare slots, storage types, alignments, projections, and pointer rules | Implemented for procedural/array subset; volatile gap recorded |
| `fence`, `cmpxchg`, and `atomicrmw` | Future | Crosswalk LowIR atomics and ordering constraints | Static crosswalk complete; exporter implementation deferred |
| Conversions | Required | Preserve signedness and pointer/value distinctions | Implemented and verifier-clean |
| `icmp`, `fcmp`, `phi`, `select`, and `freeze` | Required | Compare boolean representation and SSA/CFG choices | Compare/PHI implemented; select expressible; freeze deliberately absent |
| `call` and `va_arg` | Required | Compare direct/indirect signatures, attributes, and variadics | Calls implemented; vararg calls observed; va-list operations remain limited |
| `landingpad` | Future | Itanium catch/filter/cleanup clauses and personality result | LangRef restrictions reviewed; Clang occurrences inventoried; exporter deferred |

## Intrinsic families

| LangRef subject | Disposition | Initial LowIR question | Status |
| --- | --- | --- | --- |
| Variable-argument intrinsics | Required | Map LowIR `va_start`/`va_arg`; ensure `va_end` behavior | Static mapping complete; portable `va_end` omission recorded |
| GC/statepoint intrinsics | Language-inapplicable | No garbage-collected language/runtime in scope | Applicability recorded |
| Code-generator intrinsics | Future | Audit stack save/restore, return/frame address, and target needs | `threadlocal.address` observed; stack-save/restore not observed |
| Memory intrinsics | Required | Crosswalk `copyobj`/`zeroinit` and hosted memory builtins | `memcpy`, `memset`, and `memmove` observed and crosswalked |
| Overflow intrinsics | Future | Canonical semantic graph already identifies supported overflow builtins | `umul.with.overflow` observed; hosted exporter deferred |
| Bit-manipulation intrinsics | Future | Compare compiler builtin lowering | `fabs`/FP-class probes observed; scalar expansion or intrinsics both valid |
| Lifetime and invariant intrinsics | Future | Decide whether LowIR needs intent or LLVM can infer it | Not observed with disabled passes; optional/backend disposition |
| Exception-handling intrinsics | Future | Distinguish Itanium runtime calls from LLVM EH constructs | `eh.typeid.for` observed and crosswalked |
| Constrained floating-point intrinsics | Future | Required only if supported source semantics enable non-default FP environment | Not observed; no constrained-FP source profile |
| Fixed/scalable vector intrinsics | Future | Hosted fixed-vector subset only in initial target | Not observed in selected modules; future scope |
| Coroutine, sanitizer, profiling, and experimental GC intrinsics | Language-inapplicable initially | Not emitted by the fixed source profile | Applicability recorded |

## Version-specific decisions

- Use opaque `ptr` syntax for LLVM 21.1.
- Map a proven LowIR `capture=nocapture` fact to LLVM 21.1
  `captures(none)`, not the removed older `nocapture` spelling.
- Do not use constant-expression instruction forms removed by LLVM 21.
- Do not emit `nsw`, `nuw`, `exact`, `inbounds`, fast-math, alias,
  dereferenceability, or `noundef` promises until their complete LangRef
  preconditions are proven from semantic or ABI facts.

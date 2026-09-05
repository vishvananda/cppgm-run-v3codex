# LowIR / LLVM IR 21.1 Fact Crosswalk

Status: complete static census for the current public LowIR model; dynamic
coverage is reported separately.

Normative inputs are the public [PA13 LowIR specification](../pa13/lowir.md)
and the [LLVM 21.1 Language Reference
Manual](https://releases.llvm.org/21.1.0/docs/LangRef.html). This is a fact
crosswalk, not a proposal to make LowIR look like LLVM. An LLVM spelling is
listed only when the same semantic, ABI, target, or presentation fact exists.

Disposition meanings:

- **direct**: one LowIR fact has a direct LLVM representation;
- **lowered**: a LowIR fact expands into several LLVM constructs;
- **derived**: the backend can safely recover the LLVM fact from other LowIR
  and fixed-target facts;
- **optional**: LLVM can carry an optimization promise which LowIR need not;
- **LowIR-only**: useful source/backend intent which LLVM normally erases;
- **backend-only**: object, target, or tool policy rather than a LowIR fact;
- **gap**: information required for correct behavior is not durable in LowIR;
- **implementation-only**: typed-model identity or presentation state, not
  part of the serialized semantic contract.

## Program, identity, and type facts

| LowIR surface/model field | LLVM 21.1 representation | Disposition | Conclusion |
| --- | --- | --- | --- |
| One `Program` containing declarations and definitions | One LLVM module per translation unit; multiple modules are linked later | lowered | Keep LowIR concatenation. The investigation exporter intentionally emits one LLVM module per source. |
| `symbol_names`, `SymbolId`, `StringPool` | Global identifiers and symbol-table identity | implementation-only | Compact identity is an implementation choice. LLVM quoted-name escaping is renderer work. |
| `%temp` / `ValueId` | SSA values | direct | LLVM additionally requires dominance; LowIR's defined-before-use rule is weaker than full SSA dominance. |
| `$slot` / `SlotId` | `alloca` plus loads/stores | lowered | A LowIR slot is addressable storage, not an SSA value. LLVM `alloca` storage starts uninitialized. |
| `BlockId`, labels, presentation order | Basic block labels and function block list | direct | Order is mostly presentation; PHI predecessor and EH placement rules remain semantic. |
| `void` | `void` | direct | No difference. |
| `i1` | `i1` | direct | At a C++ ABI boundary, `zeroext` may also be required. |
| `i8/u8`, `i16/u16`, `i32/u32`, `i64`, `i128` | Signless `iN` types | lowered | LowIR retains some source signedness in narrow type names and always retains signedness in operations. LLVM puts signedness in opcodes, predicates, and boundary attributes. Keep LowIR. |
| `f32`, `f64`, `f80` | `float`, `double`, `x86_fp80` | direct under the pinned x86-64 profile | `f80` has a 10-byte value and 16-byte object storage on this target. |
| `ptr` | Opaque `ptr` in address space zero | direct | Pointee type is deliberately absent in both IRs. Non-integral pointers and other address spaces are outside the fixed target. |
| `obj<bytes x align>` / `LowType.storage_size` and `.alignment` | Usually an LLVM struct/array or an ABI-coerced scalar/struct boundary type | lowered | Keep the byte-object abstraction. ABI classification must use semantic/layout facts before LLVM rendering; LLVM's aggregate type is not a portable replacement. |
| No vector LowType | Fixed/scalable vector types | future-scope | Hosted vector extensions need a separate crosswalk if they become durable LowIR values. |
| `LowType` equality | Structural LLVM type equality | direct | LLVM integers are signless, so LowIR `i8` and `u8` converge. |
| `source_bytes`, `token_count` | No module semantic equivalent | implementation-only | Telemetry only. |
| `presentation_policy`, generated-name reservations, pooled spelling flags | Printed-name and object-path policy | implementation-only | Do not add LLVM metadata for these. |

## Symbols, definitions, and object facts

| LowIR fact | LLVM representation | Disposition | Conclusion |
| --- | --- | --- | --- |
| Declaration versus definition | LLVM `external` declaration versus initializer/body | direct | Declarations may not use definition-only linkage. This rule caught an exporter defect during the census. |
| `linkage=c/cpp` | Name mangling and language-level collision rules, not LLVM linkage | ABI fact | Keep separate from LLVM linkage. |
| `binding=internal` | `internal` linkage | direct | Valid on definitions; unresolved declarations need external linkage or must not be emitted. |
| `binding=strong` | Default `external` linkage | direct | No explicit keyword is normally printed. |
| `binding=weak` | Conservatively `weak`; C++ ODR entities can use `weak_odr`/`linkonce_odr` plus COMDAT | derived with lost precision | Correct ELF emission is possible, but serialized LowIR does not distinguish source `weak` from ODR-mergeable weak definitions. Measure before extending the spec. |
| `object=<spelling>` | LLVM global/function name | direct ABI fact | LowIR's internal semantic name and object spelling separation is useful. |
| `alias object` / `ObjectAlias` | LLVM alias or multiple ELF symbols for one body/object | lowered/backend-only | Keep. Whether an LLVM alias is legal depends on linkage and aliasee definition ownership. |
| `keep_alias=yes` | Extra LLVM alias or object symbol-table alias | backend-only | Object-emission request, not optimizer metadata. |
| `prefer_local=yes` | Local binding/visibility/preemption choice | backend-only | Keep as an object-policy fact. Do not translate mechanically to `dso_local` without the link profile. |
| `object_root=yes` | `@llvm.used`, retained linkage, or driver/object reachability root | lowered | Keep. Clang emitted `llvm.used` in the observed corpus. |
| `trivial_lifecycle=yes` | No standard LLVM attribute | LowIR-only | Useful to native lowering and pruning; keep. |
| `force_inline=yes` | `alwaysinline` | direct policy | It remains a compiler policy, not a semantic guarantee that inlining succeeds. |
| `inline_hint=yes` | `inlinehint` | direct policy | Keep distinct from required inlining. |
| `no_inline=yes` | `noinline` | direct policy | Clang also adds `optnone` at O0; LowIR should not. |
| Function/global `role` | `main`; `llvm.global_ctors/dtors`; runtime declarations; personality, RTTI, allocation, termination, and trap symbols | lowered/LowIR-only | Roles are a compact, useful runtime contract. LLVM usually expresses the consequence rather than retaining the role name. |
| `tls_for` and `storage=thread_local` | `thread_local` globals, TLS model, and possibly `llvm.threadlocal.address`/wrapper functions | lowered | Keep semantic TLS identity. Selection of local-exec/general-dynamic/etc. is target/link policy. |
| `storage=readonly` | `constant` global | direct when the complete object is immutable | Do not infer LLVM `invariant.load` metadata merely from this fact. |
| `storage=writable/default` | `global` | direct | No difference. |
| `section_name` / `section_segment` in `SymbolMetadata` | LLVM `section` plus target object-section selection | gap in serialized LowIR | The in-memory/object path carries these fields, but the public serializer/parser does not. Either serialize the supported ELF section name or explicitly remove it from the claim that text is the durable object boundary. |
| `inferred_legacy_role` | None | implementation-only | Backward-compatible parser state. |
| `ExportedSymbol` | LLVM/ELF symbol table, linkage, and TLS wrapper names | derived/backend-only | It is prepared object-output state, not a second public LowIR semantic layer. |

## Function and parameter boundaries

| LowIR fact | LLVM representation | Disposition | Conclusion |
| --- | --- | --- | --- |
| Fixed parameters and return type | LLVM function type | direct after ABI classification | Class/object boundaries can require coercion, hidden parameters, or indirect return. |
| `arity=variadic` | Varargs function type and calls | direct | Default argument promotions happen before emission. |
| `arity=prototype_relaxed` | No exact C++ LLVM function-type equivalent; a varargs-compatible boundary is conservative | lowered | Keep for the LowIR boundary mode. |
| `effects=readnone` | `memory(none)` | direct | LLVM 21's unified memory attribute is preferred. |
| `effects=readonly` | `memory(read)` / legacy `readonly` | direct | LowIR intentionally carries a coarse whole-boundary fact. |
| `effects=readwrite/default` | No restrictive attribute | direct-negative | Do not emit a promise. |
| `unwind=no` | `nounwind` | direct | Omission correctly means may unwind. |
| `return=noreturn` | `noreturn` and normally an `unreachable` continuation | direct/lowered | A call still needs a terminated LLVM block. |
| `pass=indirect_result` | `sret(<type>)`, usually `noalias`, plus ABI alignment/dereferenceability attributes | lowered | `sret` is ABI-relevant; the stronger optimizer attributes require separate proof. |
| `pass=by_address` | `byval(<type>)` when the callee receives a private copy, otherwise an ordinary pointer | lowered | Passing mode plus ABI facts must distinguish copy-in semantics. |
| `pass=reference` | Pointer parameter | direct | `nonnull`, `dereferenceable`, and `align` may follow from valid C++ reference use, but they are optional LLVM promises and poison/UB boundaries. |
| `pass=decay` | Pointer parameter | LowIR-only | LLVM normally erases whether a pointer came from array/function decay. Keep. |
| `capture=nocapture` | LLVM 21 `captures(none)` | direct | Do not use the removed `nocapture` spelling for LLVM 21. |
| `capture=maycapture/default` | No restrictive capture attribute | direct-negative | Correct conservative default. |
| `access=none/read/write/readwrite` | `memory(...)`, `readnone`, `readonly`, or `writeonly` parameter effects where representable | lowered | LLVM's memory effects are richer; LowIR's categories are sufficient for current optimizations. |
| `alias=noalias` | Parameter/return `noalias` | direct only when the precise LLVM scoped guarantee is proven | LowIR defines disjointness among similarly annotated incoming parameters; renderer must not strengthen it beyond that domain. |
| Scalar signedness at boundaries | `signext` / `zeroext` where the ABI requires it | derived | Derive from semantic source type and target ABI. Applying it merely because the source is `bool` is wrong when the actual LLVM type is not `i1`; the verifier caught this adapter defect. |
| No LowIR `noundef`, `nonnull`, `dereferenceable`, `range`, `returned`, `inreg`, `nest`, or `immarg` | Same-named LLVM parameter/return attributes | optional/ABI-specific | Their absence is not a LowIR correctness gap. Add only if a downstream optimization needs the promise and all LangRef preconditions are durable. |

## Constants, globals, operands, and memory

| LowIR fact | LLVM representation | Disposition | Conclusion |
| --- | --- | --- | --- |
| Integer literal low/high words and type | Arbitrary-width integer constant | direct | Preserve bits; source spelling is presentation only. |
| Floating literal raw bits | Hexadecimal LLVM floating constant of the target type | direct | Exact bits avoid host decimal conversion drift. |
| Global scalar `zero`, integer/float, address plus addend | `zeroinitializer`, scalar constant, global address/GEP/ptr arithmetic constant | direct/lowered | Use only LLVM 21-legal constant expressions. |
| Structured `ITEM_INTEGER`, `ITEM_ADDR`, `ITEM_ZERO`, byte spans | Array/struct constants, byte arrays, relocatable symbol references, and `zeroinitializer` | lowered | LowIR retains object bytes and relocation intent without imposing LLVM source aggregate types. |
| `OP_TEMP`, `OP_SLOT`, `OP_GLOBAL`, `OP_LABEL` | SSA value, `alloca`, global value, block label | direct/lowered | Address binding and symbol identity are separate facts. |
| `ADDRESS_LOCAL/PREEMPTIBLE` | `dso_local`, GOT/preemption-aware addressing, or ordinary global reference | backend-only target fact | Keep in backend preparation; do not infer semantic aliasing from it. |
| Default-initialized slot | Uninitialized `alloca`, not a zero store | direct-negative | This was checked directly against the LangRef and corrected in the exporter. |
| No `undef`/poison literal surface | LLVM `undef`, poison, and `freeze` | direct-negative | Prefer concrete control/data flow. Never introduce poison or `noundef` merely to mimic Clang. |
| `index <type>` element scaling | `getelementptr <type>` | direct | Do not add `inbounds` without proving all LangRef object/provenance preconditions. |
| `projection=array_element/field/base_subobject/reference_field` | Usually erased after GEP; sometimes reflected in TBAA or debug metadata | LowIR-only | Keep. It is useful source/object-layout intent and helps later backends avoid reconstructing frontend facts. |
| Per-type storage size/alignment | LLVM allocated type and explicit `align` operands | derived/conservative | A backend can always use alignment 1 for an access. LowIR currently has no per-access stronger alignment promise. |
| Volatile source access | LLVM `volatile` on load/store/atomic operations | **gap** | LowIR load/store has no volatile bit. A scratch witness showed `lowiropt -O2` deleting both volatile local stores. This is a correctness-affecting spec/lowering gap. |
| `load` / `store` | `load` / `store` | direct | Explicit alignment must never exceed what the pointer access proves. |
| Atomic load/store | Atomic `load`/`store` with ordering and optional sync scope | direct/lowered | LowIR order `consume` must conservatively map to LLVM `acquire`, which has no consume ordering. |
| `atomic_exchange` | `atomicrmw xchg` | direct | LLVM returns the old value, matching LowIR. |
| `atomic_add_fetch` | `atomicrmw add` followed by addition of the returned old value | lowered | LLVM atomicrmw returns the old value; LowIR returns the updated value. |
| `atomic_compare_exchange` | `cmpxchg`, `extractvalue`, conditional expected-pointer update | lowered | LLVM returns `{old, success}`. Validate legal success/failure ordering pairs. |
| Thread fence | LLVM `fence` | direct under system scope | LowIR does not currently expose custom sync scopes. |
| Signal fence | Compiler barrier (often side-effect inline asm), not necessarily LLVM `fence` | backend-only lowering | Keep it first-class; target lowering chooses the compiler barrier. |
| `copyobj` | `llvm.memcpy`, scalarized copies, or ABI-coerced aggregate movement | lowered | Clarify in the LowIR spec that semantic object copies are non-overlapping. Overlap-capable source `memmove` remains a call unless a distinct operation is added. |
| `zeroinit` | `llvm.memset` or scalar stores | lowered | Alignment and volatility boundaries must remain truthful. |
| `stack_alloc` | Dynamic `alloca` | direct | Stack restore is function/stack discipline; stack-save/restore intrinsics are needed only for more complex lifetime placement. |

## Operations and control flow

| LowIR operation/instruction | LLVM representation | Disposition | Notes |
| --- | --- | --- | --- |
| `const` | Usually an LLVM constant operand, with no instruction | lowered | LLVM constants are values, not necessarily instructions. |
| `copy` | SSA renaming, PHI, or an explicit memory/register copy selected later | LowIR-only | Keep for optimization and machine lowering. It must not become `freeze`. |
| `addr` | Global value, function value, or `alloca` result | lowered | Opaque pointers erase the source entity type. |
| `unary neg/not/bitnot` | `sub 0,x`, boolean compare/xor, `xor -1,x`; `fneg` for float | lowered | No overflow flags without proof. |
| `bswap` | `llvm.bswap.*` | direct intrinsic | Width restrictions align. |
| `decay` | No-op pointer value | LowIR-only | Keep semantic intent. |
| Integer `add/sub/mul/and/or/xor/shl` | Same LLVM opcode | direct | Do not emit `nsw`, `nuw`, or `exact` from ordinary LowIR. |
| `div/mod`, `udiv/umod`, `shr/ushr` | `sdiv/srem`, `udiv/urem`, `ashr/lshr` | direct | Signedness is in the operation. Division UB preconditions remain important. |
| Floating `add/sub/mul/div` | `fadd/fsub/fmul/fdiv` | direct | Emit no fast-math flags. |
| Integer/pointer `cmp` | `icmp` with signed/unsigned predicate | direct | Pointer ordering remains target/language constrained. |
| Floating `cmp` | Ordered predicates except C++ `!=`, which is unordered-not-equal | direct | Predicate selection must preserve NaN behavior. |
| `sext/zext/trunc/sitofp/uitofp/fptosi/fptoui/fpext/fptrunc` | Same LLVM conversion opcodes | direct | Source and destination types are explicit. |
| `phi` | `phi` | direct | LLVM requires one incoming value per actual predecessor, PHIs first, and dominance on each edge. |
| `jump` / `branch` / `switch` | `br` / conditional `br` / `switch` | direct | LowIR uses a canonical integer truth value; LLVM branch conditions are `i1`. |
| `return` | `ret` | direct | ABI-coerced aggregate results are classified before rendering. |
| No LowIR `select` | LLVM `select` | expressible | CFG plus PHI is semantically sufficient. Clang's frequent `select` use is a shape choice. |
| No `indirectbr` / `callbr` | Same LLVM terminators | future-scope | Needed only for supported computed goto or asm-goto. |
| No `freeze` | `freeze` | direct-negative | Not needed unless LowIR gains poison-producing values that must be stabilized. |

## Calls, variadics, EH, and runtime

| LowIR fact | LLVM representation | Disposition | Conclusion |
| --- | --- | --- | --- |
| Direct call | `call` to a global function | direct | Call-site ABI attributes must match the declaration. |
| Indirect call with explicit `as (...) -> ...` signature | `call` through `ptr` with an explicit function type | direct | The explicit LowIR signature is essential because LLVM pointers are opaque. |
| `va_start` | `llvm.va_start` | direct intrinsic | The address must designate compatible `va_list` storage. |
| `va_arg` | `va_arg` instruction or target ABI sequence | direct/lowered | LLVM's instruction supports a typed extraction; ABI representation remains target-specific. |
| Source `va_end` lowered to no operation | `llvm.va_end` | fixed-target omission | Correct for the pinned SysV x86-64 implementation only. Add a LowIR `va_end` operation before claiming a portable backend boundary. |
| `eh_try`, `eh_cleanup`, clauses, catch/filter/all, `eh_end` | Itanium `invoke` edges, `landingpad` clauses, selector tests, and cleanup CFG | lowered | LowIR models language/runtime regions; LLVM models exceptional control-flow constraints. A mechanical one-instruction mapping is not expected. |
| `throw` | Allocation/init plus `__cxa_throw` or another runtime call, ending `unreachable` | lowered | Typeinfo, destructor, and ownership are ABI facts. |
| `exception`, `exception_selector` | Values extracted from `landingpad` and calls such as `llvm.eh.typeid.for` | lowered | Keep explicit exception-state operations in LowIR. |
| `resume` | LLVM `resume` with the landingpad aggregate | lowered | LLVM requires the aggregate exception representation; LowIR intentionally hides it. |
| EH roles/personality | Personality function and Itanium runtime declarations | lowered/backend ABI | Runtime symbol roles are useful and should remain. |
| `unreachable` role/instruction consequence | `unreachable` or `llvm.trap` followed by `unreachable`, depending semantics | lowered | Source UB and an actual trap are not interchangeable. |

## Debug and metadata

| LowIR fact | LLVM representation | Disposition | Conclusion |
| --- | --- | --- | --- |
| Function/instruction `!dbg(file,line,column)` | `DILocation`, `DISubprogram`, compile unit, file, type, and scope graph | lowered | LowIR deliberately retains only durable source locations. Rich debug type/scope construction belongs to a debug backend lane. |
| No TBAA | `!tbaa`, `!tbaa.struct` | optional | Lack of TBAA affects optimization, not correctness. LowIR projections could seed it in a future LLVM backend. |
| No alias scopes/access groups | `!alias.scope`, `!noalias`, `!access.group` | optional | Parameter noalias remains available; loop/scoped alias metadata needs stronger analysis. |
| No loop metadata | `!llvm.loop` | optional | PA37/PA38 loop transformations need not serialize LLVM-specific loop hints. |
| No profile metadata | `!prof` | optional/profile-input | Outside the no-profile comparison lane. |
| No sanitizer metadata | `!nosanitize` and instrumentation controls | backend/tool policy | Outside the language LowIR contract. |
| No module flags/ident | `!llvm.module.flags`, `!llvm.ident` | backend-only | Clang emitted both once per observed module. PIC/PIE, unwind tables, compiler identity, and wchar/ABI policy belong to the object/LLVM module producer. |

## Reverse census: LLVM constructs observed from Clang

The final raw corpus contains 2,664 verifier-clean Clang modules and 1,090
verifier-clean experimental modules across PA15-PA28 plus curated later probes.
The inventory parser has no unknown instruction bucket after reprocessing the
raw modules with the current parser.

| Observed LLVM family | LowIR source/disposition |
| --- | --- |
| `alloca`, `load`, `store`, GEP | Slots, scalar/object memory, and `index`; direct/lowered. |
| Integer/floating arithmetic, shifts, conversions, `icmp`, `fcmp` | Corresponding typed LowIR operations; direct. |
| `ret`, `br`, `switch`, `phi`, `unreachable` | Corresponding CFG forms; direct. |
| `select` | Expressible as branch plus PHI; no LowIR gap. |
| `extractvalue`, `insertvalue` | ABI-coerced aggregates or object movement; no scalar PA15 gap, but a complete LLVM object exporter needs ABI aggregate values. |
| `call` | Direct/indirect LowIR calls plus boundary metadata. |
| `invoke`, `landingpad`, `resume` | LowIR EH regions/runtime state; substantial lowering required. |
| `llvm.memcpy`, `llvm.memset`, `llvm.memmove` | `copyobj`, `zeroinit`, or overlap-capable runtime memory operations. |
| `llvm.eh.typeid.for` | Exception selector/type matching. |
| `llvm.global_ctors` | `role=init` and initialization demand/order. |
| `llvm.threadlocal.address` | Thread-local storage and wrapper facts. |
| `llvm.trap` | Trap builtin/runtime policy; not ordinary source UB. |
| `llvm.is.fpclass`, `llvm.fabs.f80`, `llvm.umul.with.overflow` | Hosted builtin semantics or ordinary expanded LowIR operations; future hosted exporter work. |
| `llvm.used` | `object_root`, aliases, and retained object symbols. |
| `zeroext`, `signext`, `sret`, `byval` | ABI classification, derived from semantic source type/layout and LowIR pass mode. |
| `noundef`, `nonnull`, `dereferenceable`, `returned` | Optional correctness/optimization promises; omission is conservative. |
| `captures`, `noalias`, `readonly`, `writeonly`, `memory(...)` | LowIR parameter capture/access/alias and boundary effect facts where guarantees match. |
| `nounwind`, `noreturn` | LowIR boundary facts. |
| `noinline`, `alwaysinline` | LowIR policy metadata; Clang's `optnone` is O0 policy. |
| `mustprogress`, `willreturn`, `norecurse`, `nofree`, `nosync`, `speculatable` | Optimizer/language promises not required in LowIR; add only with durable proof and demonstrated value. |
| `uwtable` | Target/object unwind-table policy. |
| `allocsize`, `nocallback`, `immarg`, `nobuiltin`, `builtin`, `cold` | Library/intrinsic/optimization properties; not general LowIR requirements. |
| `!nonnull`, `!align` attachments | Optional facts on particular loads/results; do not infer from type alone. |
| `!nosanitize`, `!llvm.loop`, `!prof` | Tool, loop, and profile metadata outside the primary semantic contract. |
| `llvm.module.flags`, `llvm.ident` | Module target-policy and bookkeeping. |

## Crosswalk conclusions

The static census supports four concrete conclusions:

1. LowIR's extra semantic metadata—roles, projections, pass origin,
   trivial-lifecycle state, and object-emission preferences—is useful and
   should not be deleted merely because LLVM erases or lowers it.
2. Most extra Clang attributes and metadata are optimization, debug, module,
   or object-policy facts. Their absence is not evidence of a LowIR defect.
3. Volatile access is a correctness-bearing fact that LowIR currently loses.
   It is the highest-priority specification and lowering recommendation.
4. Section placement exists only in the in-memory/object path, while public
   LowIR text claims to be the durable boundary. That mismatch should be
   resolved explicitly. Weak-versus-weak-ODR precision, per-access alignment,
   and a portable `va_end` are lower-priority follow-ups, not reasons to adopt
   LLVM wholesale.

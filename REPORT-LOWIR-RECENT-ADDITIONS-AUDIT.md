# Recent LowIR Additions Audit

Date: 2026-08-22

## Purpose

This report audits recent additions to the public LowIR contract for facts that
are redundant, overly frontend-specific, or better represented by ordinary IR
constructs. It complements [REPORT-LOWIR-LLVM-VALIDATION.md](REPORT-LOWIR-LLVM-VALIDATION.md).

The central question is whether LowIR is carrying information that:

1. has a direct LLVM IR representation and should remain explicit;
2. cannot safely be reconstructed by a backend and must remain explicit;
3. is only an intermediate frontend classification that should be normalized
   before the public LowIR boundary; or
4. expresses a real semantic fact through an unnecessarily indirect encoding.

## Scope and cutoff

The cutoff used for “since the last big bulk merge” is commit `df9231d0`
(`Merge upstream assignment test-runner acceleration`, 2026-08-20).

The public LowIR model and text-contract changes after that merge are:

- the `phi` instruction;
- `inline_hint=yes` function metadata;
- `role=terminate`; and
- `role=unreachable`.

The same period also promoted the existing `object_root`,
`trivial_lifecycle`, `force_inline`, and `no_inline` fields into PA13's
documented required metadata families. They are included because this made them
part of the durable public contract even when their storage predates the
cutoff.

Two additional changes propagated facts through existing LowIR constructs:

- compiler-generated constant data now preserves `storage=readonly`; and
- GNU function memory-effect attributes now preserve `effects=readnone` or
  `effects=readonly`.

These are not new LowIR syntax, but they were reviewed for necessity.

## Executive conclusion

Most recent additions are justified. Two encodings should be simplified, and
one runtime-role decision should be made explicitly:

1. Replace `role=unreachable` plus a synthetic function call with a first-class
   `unreachable` terminator.
2. Remove `trivial_lifecycle` from public LowIR by normalizing it into existing
   `force_inline` and `object_root` policy before serialization.
3. Decide whether runtime roles such as `terminate` are a deliberate
   ABI-neutral standalone-runtime interface. If they are, separate and document
   that interface; otherwise lower termination as an ordinary runtime call.

## Classification matrix

| LowIR construct or fact | LLVM IR analogue | Backend-derivable? | Disposition |
|---|---|---:|---|
| `phi` | `phi` instruction | No | Keep |
| `inline_hint=yes` | `inlinehint` | No | Keep |
| `force_inline=yes` | `alwaysinline` | No | Keep |
| `no_inline=yes` | `noinline` | No | Keep |
| `storage=readonly` | global `constant` | Not safely in general | Keep |
| `effects=readnone` / `readonly` | `memory(none)` / `memory(read)` | Not for declarations | Keep |
| `object_root=yes` | `llvm.used` / `llvm.compiler.used` retention | No | Keep the fact |
| `trivial_lifecycle=yes` | No direct analogue | Current consumer derives existing policy | Remove from public LowIR |
| `role=unreachable` | `unreachable` terminator | The fact is not derivable, but the role/call encoding is avoidable | Replace encoding |
| `role=terminate` | Ordinary runtime calls plus `noreturn`/`nounwind` | Not from boundary attributes alone | Policy decision |

## Findings

### 1. `phi` is a necessary IR construct

LowIR now defines:

```text
%t = phi <type> [^predecessor: <value>, ...]
```

The contract is documented in [pa13/lowir.md](pa13/lowir.md), and the typed
instruction is represented by `Instruction::IK_PHI` in
[dev/src/lowir_model.h](dev/src/lowir_model.h).

This maps directly to LLVM's `phi` instruction. It records which value is
selected on each incoming CFG edge after slot promotion and other SSA-like
optimization. Reconstructing this only in the machine backend would either
repeat an optimizer analysis or require retaining the less optimized memory
form.

LLVM requires PHIs to appear first in a block and to supply incoming values for
predecessor blocks. LowIR's validation follows that model while deliberately
restricting its supported value types and excluding exception-handler targets.

Disposition: keep.

Reference: [LLVM Language Reference, `phi`](https://llvm.org/docs/LangRef.html#phi-instruction).

### 2. Inlining policy metadata is legitimate

The public metadata now distinguishes:

- `force_inline=yes`: eligible calls must be expanded;
- `inline_hint=yes`: source-level preference that adjusts profitability; and
- `no_inline=yes`: optional inlining is prohibited.

These correspond to LLVM's `alwaysinline`, `inlinehint`, and `noinline`
function attributes. In particular, LLVM defines `inlinehint` as recording a
source-code indication that inlining is desirable while imposing no mandatory
inlining requirement.

Clang 21.1.8 was also checked directly. An emitted, retained C++ `inline`
function received `inlinehint`; ordinary `-O0` policy separately introduced
`noinline` and `optnone`. LowIR is correct to keep source policy distinct from
optimization-level policy.

These facts cannot be inferred reliably from linkage. A weak or coalescable
definition need not be a source inline hint, and an inline or template
definition can lose the syntactic origin needed to distinguish the policy.

Disposition: keep all three as independent facts.

Reference: [LLVM Language Reference, function attributes](https://llvm.org/docs/LangRef.html#function-attributes).

### 3. Readonly storage and function effects must remain explicit

`storage=readonly` maps to an LLVM global `constant`. LLVM treats this as a
runtime immutability guarantee that enables optimization and read-only section
placement. It is stronger than merely observing no store in the current body.
Declarations, replaceable definitions, and externally visible objects make
backend-only inference unsafe.

Likewise, LowIR's coarse function effects map to LLVM's unified memory effects:

```text
effects=readnone  -> memory(none)
effects=readonly  -> memory(read)
```

A backend cannot infer these effects for a declaration without a body. Even for
definitions, preserving a valid frontend guarantee avoids repeating whole-body
and interprocedural analysis in every consumer.

Disposition: keep.

References:

- [LLVM Language Reference, global variables](https://llvm.org/docs/LangRef.html#global-variables)
- [LLVM Language Reference, `memory(...)`](https://llvm.org/docs/LangRef.html#memory)

### 4. `object_root` carries a real non-derivable obligation

`object_root=yes` records that a definition must be emitted even when no LowIR
instruction refers to it. The source can require this for explicit template
instantiation, lifecycle entry points, or other externally observable object
surfaces.

The LowIR reachability pass consumes the fact as an explicit root in
[dev/src/lowir_function_reachability.cpp](dev/src/lowir_function_reachability.cpp).
Without it, an ordinary use-def walk may legally conclude that the definition
is dead even though the language or object contract requires it.

LLVM represents the analogous retention obligation through mechanisms such as
`llvm.used`, `llvm.compiler.used`, linkage, and target object retention. LowIR's
per-symbol Boolean is a simpler representation of the same kind of obligation.
It could eventually be normalized into a module-level root set, but the fact
itself is necessary.

Disposition: keep the fact; a future format revision may prefer a module-level
retention list.

Reference: [LLVM Language Reference, `llvm.used`](https://llvm.org/docs/LangRef.html#the-llvm-used-global-variable).

### 5. `trivial_lifecycle` leaks a C++ classification into public LowIR

The specification describes `trivial_lifecycle=yes` as identifying a
semantically trivial constructor or destructor wrapper. The public field is
`SymbolMetadata::object_trivial_lifecycle` in
[dev/src/lowir_model.h](dev/src/lowir_model.h).

Its current backend consumer treats it as an alternate force-inline trigger:

```cpp
metadata.force_inline ||
    (metadata.object_trivial_lifecycle && !metadata.no_inline)
```

See [dev/src/lowir_force_inline.cpp](dev/src/lowir_force_inline.cpp).

No current LowIR optimization needs to understand C++ lifecycle semantics
beyond that policy choice. Retention is already carried independently by
`object_root`.

The frontend should therefore normalize the fact before the public boundary:

```text
trivial lifecycle and inlining permitted -> force_inline=yes
language-required emission                -> object_root=yes
```

This does not require asking the backend to rediscover whether a constructor or
destructor is semantically trivial. The frontend still makes that decision; it
publishes the resulting backend policy instead of leaking the source-language
classification.

Disposition: remove `trivial_lifecycle` from the public specification, parser,
serializer, and `lowir_model::SymbolMetadata` after migrating producers and
tests.

### 6. `role=unreachable` expresses the right fact indirectly

Current compiler-produced LowIR represents `__builtin_unreachable()` with a
synthetic declaration similar to:

```text
declare function @__builtin_unreachable() -> void
    [effects=readnone, unwind=no, return=noreturn,
     role=unreachable, object=cppgm_builtin_unreachable]
```

A block calls that symbol, and `lowir_unreachable_opt.cpp` indexes
`role=unreachable` declarations to identify impossible CFG edges.

LLVM represents the same semantic fact as the first-class terminator:

```llvm
unreachable
```

Clang 21.1.8 was checked on the repository's guarded
`__builtin_unreachable()` pattern. At `-O0`, Clang emitted `unreachable`
directly. At `-O1`, it was able to replace the condition with an `llvm.assume`
and eliminate the impossible branch.

The fact is not safely derivable from `return=noreturn`: an ordinary noreturn
function may terminate, trap, log, mutate state, or otherwise have observable
behavior. The unnecessary part is the synthetic symbol and special role, not
the semantic information.

Recommended representation:

```text
block ^undefined:
  unreachable
```

The native backend may lower this to no emitted continuation, a trap in debug
or diagnostic configurations, or another target-appropriate unreachable
sequence. Optimization can reason directly from CFG termination.

Disposition: add an `unreachable` terminator and remove `SR_UNREACHABLE`, the
synthetic declaration/call, and role-based edge recognition.

Reference: [LLVM Language Reference, `unreachable`](https://llvm.org/docs/LangRef.html#unreachable-instruction).

### 7. `role=terminate` depends on LowIR's runtime policy

The frontend currently emits a termination declaration with both semantic and
object identity:

```text
declare function @std_terminate() -> void
    [unwind=no, return=noreturn, role=terminate,
     binding=strong, object=_ZSt9terminatev]
```

The host object path can use the ordinary `_ZSt9terminatev` symbol. The
standalone backend instead consumes `SR_TERMINATE` in
[dev/src/lowir_native_eh.cpp](dev/src/lowir_native_eh.cpp) and supplies its own
runtime behavior.

Clang 21.1.8 does not use an LLVM termination role. For a potentially throwing
call inside a `noexcept` function, it emitted an `invoke`, a landing pad, a call
to `__clang_call_terminate`, a normal call to `_ZSt9terminatev`, and an
`unreachable` terminator.

`unwind=no` and `return=noreturn` alone do not identify termination: many
unrelated functions share those boundary properties. The backend needs either
the object/runtime name or an explicit intrinsic identity.

There are two coherent choices:

1. LLVM-like host/object LowIR: remove `role=terminate` and use the ordinary
   runtime declaration and `object=` spelling.
2. ABI-neutral standalone LowIR: retain explicit runtime identity, but model it
   as a documented runtime intrinsic/import interface rather than an unrelated
   member of general symbol-role metadata.

Disposition: do not remove until the intended LowIR runtime contract is chosen.

## Broader runtime-role observation

The new `terminate` role follows an older pattern that also covers exception
runtime functions, allocation, deallocation, pure virtual dispatch, dynamic
cast, bad-cast behavior, and RTTI data. LLVM generally represents these with
ordinary declarations, ABI runtime calls, personalities, intrinsics, and
well-known globals rather than a single `role` enum.

Those older roles are outside the cutoff for this audit, but the `terminate`
review shows that the family should eventually be classified as one of:

- semantic LowIR operations;
- ABI/runtime imports;
- object-emission identities; or
- temporary frontend labels.

Keeping all four meanings in one `SymbolRole` enum makes it harder to determine
which facts are portable semantics and which are backend implementation
choices.

## Non-public recent fact

`lifecycle_base_entry` was added to the typed PA15 lowering model during this
period, but it does not enter `lowir_model::SymbolMetadata` or serialized LowIR.
It remains a frontend/lowering implementation detail used to separate lifecycle
retention from inline policy. That placement is appropriate.

## Recommended cleanup sequence

### R1: First-class unreachable terminator

1. Add `unreachable` to the LowIR grammar, parser, serializer, verifier, and
   typed instruction model.
2. Lower `__builtin_unreachable()` directly to the terminator.
3. Teach optimizers and native/CY86 backends to consume the terminator.
4. Remove `SR_UNREACHABLE` and the synthetic builtin symbol path.
5. Add direct source, handwritten LowIR, optimized LowIR, native behavior, and
   text-roundtrip tests.

### R2: Normalize lifecycle metadata before serialization

1. At the semantic-to-LowIR boundary, translate the current lifecycle decision
   into `force_inline` and/or `object_root`.
2. Confirm `no_inline` precedence explicitly.
3. Remove `trivial_lifecycle` from text LowIR and the public typed model.
4. Re-run lifecycle object-roundtrip, inception, and all through-PA38 gates.

### R3: Define runtime identity policy

1. Inventory all `SymbolRole` consumers.
2. Separate semantic operations from runtime imports and object-only identity.
3. Decide whether standalone execution is a required property of arbitrary
   serialized LowIR.
4. Keep `role=terminate` only if that decision requires an ABI-neutral runtime
   import; otherwise use the normal runtime declaration.

## Validation gates for any implementation

Any cleanup should preserve the repository's existing assignment gates and add
focused durability checks:

```sh
make test-pa13
make test-report-through-pa13
make test-pa37
make test-report-through-pa37
make test-report-through-pa38
make inception
```

The focused tests should compare:

- direct source-to-object output;
- source to serialized LowIR to object output;
- `lowiropt -O0` fact preservation;
- optimized CFG shape for `unreachable`;
- lifecycle helper reachability and object symbol emission; and
- emitted LLVM IR accepted by the LLVM verifier.


# PA33 Implementation Plan

## Stage Design and Spec Alignment

PA33 extends the PA32 path `semantic entity/callable IDs -> typed LowIR ABI
artifacts -> per-function MIR -> direct ELF` with host-runtime facts for vtables,
VTTs, RTTI, casts, covariant thunks, and EH. It must preserve one owner for each
selected declaration, layout, ABI entry point, and emission unit (`spec.md` §§2,
4, 6-10); names are rendered only at ABI publication, and object writing does
not recover semantic facts from strings.

## Current Failure Map

Current result: **71/94 PA33 tests pass**, improved from the 68/94 checkpoint
baseline (and 59/94 stage-start baseline); PA1-PA32 pass (4291/4291).

| Owner / shared behavior | Count | Failing cases |
| --- | ---: | --- |
| Remaining ABI name publication | 2 | covariant layout-finalization symbols; nested-lambda owner identity |
| Remaining type/name frontend | 3 | transform alias, dependent NTTP expression, unnamed local-class constructor |
| Host EH semantic/lowering regions | 6 | dynamic exception specification, noexcept termination, rethrow outer cleanup, two switch/catch cases, out-of-line virtual-base RTTI catch |
| Polymorphic ODR ownership | 1 | duplicate inline-header polymorphic class |
| Virtual-inheritance RTTI/casts | 3 | lazy-template cross-cast, virtual-inheritance cast-to-void, typed cross-cast |
| Virtual-base/covariant runtime layout | 5 | external vbase reference, forwarded-reference condition, self-covariant result, virtual-base return, VTT base copy |
| Static/TLS lifecycle | 3 | local static dtor, local thread-local dtor, TLS wrapper access |

## Active Checkpoint

**Complete — typed stack and SysV vararg intrinsics.** Canonical builtin and
`__builtin_va_list` recognition now carries `stack_alloc`, `va_start`, and
scalar `va_arg` through typed LowIR. Native lowering owns dynamic frame
restoration and SysV register-save/overflow selection; `va_end` is a validated
target no-op and no builtin becomes an external symbol.

- Spec requirements: semantic entities and types remain canonical (§2); typed
  lowering preserves source operations instead of inventing symbol contracts
  (§6); target ABI decisions have one native owner (§§7-8); demand and compile
  work stay proportional to emitted operations (§9); no host compiler, source
  string, or test-specific path supplies behavior (§10). This also implements
  PA13's stable `stack_alloc` contract at the source-to-native boundary.
- Ownership/data flow: syntax owns the unusual `va_arg(expression, type-id)`
  shape; semantic analysis validates builtin arity, variadic context, canonical
  `va_list` storage, and scalar result type; dump nodes retain builtin enum and
  type IDs; typed LowIR owns `stack_alloc`, `va_start`, and `va_arg`; the native
  SysV owner initializes and advances the four-field list state. `va_end` is a
  validated no-op on this target and never creates an external symbol.
- Complexity: O(source tokens + semantic nodes + emitted intrinsic operations).
  Each alloca/vararg use lowers once; each variadic function allocates one fixed
  176-byte register-save area only when `va_start` is demanded. Scalar `va_arg`
  performs bounded register/overflow selection with no declaration scans,
  string lookup, per-operation heap allocation, or whole-program retry.
- Validation: all three focused fixtures pass; generated GP and SSE probes pass
  across register-save and overflow paths; a text-LowIR intrinsic round trip
  passes; PA33 is 71/94; PA1-PA32 are 4291/4291; file audit passes; the
  generated scaling series is below.

## Performance Evidence

One macro-generated variadic function applies N `va_arg`/`alloca` pairs.
`CPPGM_DRIVER_STATS=1` and GNU `time` show proportional semantic, typed-LowIR,
native-lowering, and object work:

| Pairs | Tokens | Semantic nodes | LowIR / MIR insns | Semantic peak bytes | Typed bytes | Object bytes | Wall s | RSS KiB |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 32 | 1,762 | 1,044 | 841 / 1,629 | 910,178 | 142,924 | 354,024 | 0.02 | 10,924 |
| 64 | 3,490 | 2,068 | 1,673 / 3,229 | 1,592,674 | 281,100 | 703,136 | 0.03 | 14,548 |
| 128 | 6,946 | 4,116 | 3,337 / 6,429 | 3,171,234 | 557,452 | 1,402,792 | 0.06 | 21,516 |
| 256 | 13,858 | 8,212 | 6,665 / 12,829 | 6,328,354 | 1,110,156 | 2,802,728 | 0.12 | 32,968 |

An 8x increase gives 7.9-8.0x tokens, nodes, instructions, typed bytes, and
object bytes; wall time rises 6x and RSS 3x. Each intrinsic is lowered once,
with fixed-size register-save state and no declaration scan or retry.

## Completed Checkpoints

| Checkpoint | Result |
| --- | --- |
| PA33 ABI-tag publication checkpoint | Pass — C1/C2, D1/D2, RTTI name/object, and vtable symbols carry canonical tags; PA33 59→62, PA1-PA32 4291/4291, file audit pass. |
| PA33 dependent callable-type recipe checkpoint | Pass — owner-prefix, qualified-member-owner, template-template, and local-result RTTI names are canonical; PA33 62→66, PA1-PA32 4291/4291, file audit pass. |
| PA33 dependent expression recipe checkpoint | Pass — alias-expanded `decltype`, template-id, unary, call, and non-type parameter expressions retain canonical ABI structure; PA33 66→68, PA1-PA32 4291/4291, file audit pass. |
| PA33 stack/SysV vararg intrinsic checkpoint | Pass — typed alloca and scalar varargs cover register-save/overflow paths with dynamic-frame restoration; PA33 68→71, PA1-PA32 4291/4291, file audit pass. |

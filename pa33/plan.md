# PA33 Implementation Plan

## Stage Design and Spec Alignment

PA33 extends the PA32 path `semantic entity/callable IDs -> typed LowIR ABI
artifacts -> per-function MIR -> direct ELF` with host-runtime facts for vtables,
VTTs, RTTI, casts, covariant thunks, and EH. It must preserve one owner for each
selected declaration, layout, ABI entry point, and emission unit (`spec.md` §§2,
4, 6-10); names are rendered only at ABI publication, and object writing does
not recover semantic facts from strings.

## Current Failure Map

Current result: **68/94 PA33 tests pass**, improved from the 66/94 checkpoint
baseline (and 59/94 stage-start baseline); PA1-PA32 pass (4291/4291).

| Owner / shared behavior | Count | Failing cases |
| --- | ---: | --- |
| Remaining ABI name publication | 2 | covariant layout-finalization symbols; nested-lambda owner identity |
| Builtin/type frontend and varargs lowering | 6 | `alloca`, transform alias, dependent NTTP expression, unnamed local-class constructor, `va_arg`, `va_start` |
| Host EH semantic/lowering regions | 6 | dynamic exception specification, noexcept termination, rethrow outer cleanup, two switch/catch cases, out-of-line virtual-base RTTI catch |
| Polymorphic ODR ownership | 1 | duplicate inline-header polymorphic class |
| Virtual-inheritance RTTI/casts | 3 | lazy-template cross-cast, virtual-inheritance cast-to-void, typed cross-cast |
| Virtual-base/covariant runtime layout | 5 | external vbase reference, forwarded-reference condition, self-covariant result, virtual-base return, VTT base copy |
| Static/TLS lifecycle | 3 | local static dtor, local thread-local dtor, TLS wrapper access |

## Active Checkpoint

**Complete — canonical dependent expression recipes.** The PA32 source-type DAG
now decodes alias-expanded identity atoms into immutable template-parameter,
unary, call, template-id, member, and `decltype` nodes. Dependent non-type
arguments remain parameter expressions instead of specialization literals.

- Spec requirements: canonical semantic identity/facts (§2); lowering consumes
  recorded facts without reparsing names (§6); compact shared child storage and
  explicit ownership (§8); demand and work proportional to retained/emitted
  nodes (§9); no test-name or rendered-source shortcuts (§10).
- Ownership/data flow: result-identity alias expansion remains the sole syntax
  owner; `AbiIdentityReader` validates its canonical atom stream and appends
  program-owned `FunctionTemplateAbiExpression`/type/argument nodes; recipe IDs
  flow through specialization bindings; `AbiFactBuilder` lowers those nodes to
  existing typed ABI expression facts and the mangler encodes them once.
- Complexity: O(identity atoms + published DAG nodes) once per template pattern
  and O(reachable expression nodes) per demanded specialization. Child and
  argument references are contiguous IDs; malformed streams roll publication
  back without retries or semantic lookup during lowering.
- Validation: both focused enable-if/`decltype` and nested ratio-expression
  fixtures pass; PA33 is 68/94; PA1-PA32 are 4291/4291; file audit passes. The
  generated dependent-expression series below confirms proportional work.

## Performance Evidence

The generated series declares N distinct function templates with the retained
`enable_if_t<is_reference<decltype(*declval<Iter&>())>::value, int>` parameter
and demands every specialization. `CPPGM_DRIVER_STATS=1` and GNU `time` show
proportional publication, lowering, and direct-object work:

| Templates | Source bytes | Semantic nodes | Functions | LowIR / MIR insns | Semantic peak bytes | Typed bytes | Object bytes | Elapsed s | RSS KiB |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 32 | 5,440 | 515 | 34 | 373 / 444 | 1,314,261 | 92,389 | 191,056 | 0.01 | 9,404 |
| 64 | 10,304 | 995 | 66 | 725 / 860 | 2,304,933 | 180,325 | 370,480 | 0.03 | 11,048 |
| 128 | 20,088 | 1,955 | 130 | 1,429 / 1,692 | 4,098,711 | 356,225 | 729,608 | 0.05 | 14,096 |
| 256 | 39,801 | 3,875 | 258 | 2,837 / 3,356 | 8,149,024 | 708,097 | 1,449,672 | 0.10 | 19,064 |

An 8x increase yields 7.5-7.7x nodes, functions, instructions, typed bytes, and
object bytes; elapsed time remains 0.10 s at N=256. Recipe publication is one
bounded identity walk per pattern, and lowering follows contiguous argument
slices without emitted-name lookup or whole-program retry.

## Completed Checkpoints

| Checkpoint | Result |
| --- | --- |
| PA33 ABI-tag publication checkpoint | Pass — C1/C2, D1/D2, RTTI name/object, and vtable symbols carry canonical tags; PA33 59→62, PA1-PA32 4291/4291, file audit pass. |
| PA33 dependent callable-type recipe checkpoint | Pass — owner-prefix, qualified-member-owner, template-template, and local-result RTTI names are canonical; PA33 62→66, PA1-PA32 4291/4291, file audit pass. |
| PA33 dependent expression recipe checkpoint | Pass — alias-expanded `decltype`, template-id, unary, call, and non-type parameter expressions retain canonical ABI structure; PA33 66→68, PA1-PA32 4291/4291, file audit pass. |

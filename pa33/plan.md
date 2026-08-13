# PA33 Implementation Plan

## Stage Design and Spec Alignment

PA33 extends the PA32 path `semantic entity/callable IDs -> typed LowIR ABI
artifacts -> per-function MIR -> direct ELF` with host-runtime facts for vtables,
VTTs, RTTI, casts, covariant thunks, and EH. It must preserve one owner for each
selected declaration, layout, ABI entry point, and emission unit (`spec.md` §§2,
4, 6-10); names are rendered only at ABI publication, and object writing does
not recover semantic facts from strings.

## Current Failure Map

Current result: **66/94 PA33 tests pass**, improved from the 62/94 checkpoint
baseline (and 59/94 stage-start baseline); PA1-PA32 pass (4291/4291).

| Owner / shared behavior | Count | Failing cases |
| --- | ---: | --- |
| Remaining ABI name publication | 4 | covariant layout-finalization symbols; dependent enable-if and ratio/function expressions; nested-lambda owner identity |
| Builtin/type frontend and varargs lowering | 6 | `alloca`, transform alias, dependent NTTP expression, unnamed local-class constructor, `va_arg`, `va_start` |
| Host EH semantic/lowering regions | 6 | dynamic exception specification, noexcept termination, rethrow outer cleanup, two switch/catch cases, out-of-line virtual-base RTTI catch |
| Polymorphic ODR ownership | 1 | duplicate inline-header polymorphic class |
| Virtual-inheritance RTTI/casts | 3 | lazy-template cross-cast, virtual-inheritance cast-to-void, typed cross-cast |
| Virtual-base/covariant runtime layout | 5 | external vbase reference, forwarded-reference condition, self-covariant result, virtual-base return, VTT base copy |
| Static/TLS lifecycle | 3 | local static dtor, local thread-local dtor, TLS wrapper access |

## Active Checkpoint

**Complete — canonical dependent callable-type recipes.** Extend the PA32
source-type DAG from template results and non-type parameter declarations to
nondeduced function parameter types. Preserve qualified dependent owners before
semantic analysis substitutes the placeholder, represent a template-template
parameter application by ordinal, and retain unresolved dependent result
members without replacing the established semantic path for ordinary template
specializations. Consume the typed recipes for ordinary symbols and local-type
contexts.

- Spec requirements: canonical semantic identity/facts (§2); lowering consumes
  recorded facts without reparsing names (§6); local entities use their retained
  callable context (§6); compact shared child storage and explicit ownership
  (§8); work proportional to syntax and emitted ABI nodes (§9); no test-name or
  spelling shortcuts (§10).
- Ownership/data flow: `BuildParameters` retains each nondeduced source root;
  function-template publication expands aliases once into immutable
  `FunctionTemplateAbiType` nodes and stores a contiguous per-recipe parameter
  slice; template-template proxy specializations retain their parameter ordinal;
  specialization bindings retain the recipe ID; `AbiFactBuilder` consumes the
  typed result/parameter nodes for ordinary symbols and local RTTI contexts.
- Complexity: O(source type syntax + published DAG nodes) once per template
  pattern and O(encoded nodes) per demanded specialization. Recipe slices are
  contiguous IDs; publication uses bounded iterative syntax walks and performs
  no emitted-name search.
- Validation: focused owner-prefix, qualified-member-owner, template-template,
  and local-result RTTI PA33 cases; the complete PA33 report; PA1-PA32 report;
  file audit; and an increasing generated series of dependent function-template
  declarations measuring time, RSS, retained nodes, and object size.

## Performance Evidence

The generated series declares N distinct dependent owner templates and N
function templates with nondeduced `typename IdN<T>::type *` parameters, then
demands each specialization. `CPPGM_DRIVER_STATS=1` and GNU `time` show
proportional publication, lowering, and direct-object work:

| Templates | Source bytes | Semantic nodes | Functions | LowIR / MIR insns | Semantic peak bytes | Typed bytes | Object bytes | Elapsed s | RSS KiB |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 32 | 4,382 | 426 | 33 | 261 / 303 | 1,237,329 | 85,610 | 133,560 | 0.01 | 9,120 |
| 64 | 8,766 | 842 | 65 | 517 / 591 | 2,464,321 | 170,154 | 263,704 | 0.03 | 10,084 |
| 128 | 17,674 | 1,674 | 129 | 1,029 / 1,167 | 4,919,101 | 339,298 | 524,480 | 0.05 | 12,512 |
| 256 | 35,850 | 3,338 | 257 | 2,053 / 2,319 | 9,825,201 | 677,730 | 1,047,232 | 0.10 | 17,264 |

An 8x increase yields approximately 8x nodes, functions, instructions, semantic
and typed bytes, object bytes, and elapsed time. Recipe publication is a single
bounded syntax walk per pattern; lowering follows contiguous node/argument
slices without emitted-name lookup or whole-program retry.

## Completed Checkpoints

| Checkpoint | Result |
| --- | --- |
| PA33 ABI-tag publication checkpoint | Pass — C1/C2, D1/D2, RTTI name/object, and vtable symbols carry canonical tags; PA33 59→62, PA1-PA32 4291/4291, file audit pass. |
| PA33 dependent callable-type recipe checkpoint | Pass — owner-prefix, qualified-member-owner, template-template, and local-result RTTI names are canonical; PA33 62→66, PA1-PA32 4291/4291, file audit pass. |

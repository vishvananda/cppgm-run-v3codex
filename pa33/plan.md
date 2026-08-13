# PA33 Implementation Plan

## Stage Design and Spec Alignment

PA33 extends the PA32 path `semantic entity/callable IDs -> typed LowIR ABI
artifacts -> per-function MIR -> direct ELF` with host-runtime facts for vtables,
VTTs, RTTI, casts, covariant thunks, and EH. It must preserve one owner for each
selected declaration, layout, ABI entry point, and emission unit (`spec.md` §§2,
4, 6-10); names are rendered only at ABI publication, and object writing does
not recover semantic facts from strings.

## Current Failure Map

Current result: **62/94 PA33 tests pass**, improved from the 59/94 turn-start
baseline; PA1-PA32 pass (4291/4291).

| Owner / shared behavior | Count | Failing cases |
| --- | ---: | --- |
| Remaining ABI name publication | 8 | covariant layout-finalization symbols; dependent enable-if, owner-prefix, qualified-member, ratio/function, local-result RTTI, nested-lambda, and template-template names |
| Builtin/type frontend and varargs lowering | 6 | `alloca`, transform alias, dependent NTTP expression, unnamed local-class constructor, `va_arg`, `va_start` |
| Host EH semantic/lowering regions | 6 | dynamic exception specification, noexcept termination, rethrow outer cleanup, two switch/catch cases, out-of-line virtual-base RTTI catch |
| Polymorphic ODR ownership | 1 | duplicate inline-header polymorphic class |
| Virtual-inheritance RTTI/casts | 3 | lazy-template cross-cast, virtual-inheritance cast-to-void, typed cross-cast |
| Virtual-base/covariant runtime layout | 5 | external vbase reference, forwarded-reference condition, self-covariant result, virtual-base return, VTT base copy |
| Static/TLS lifecycle | 3 | local static dtor, local thread-local dtor, TLS wrapper access |

## Active Checkpoint

**Complete — canonical ABI-tag publication for host artifacts.** Preserve GNU `abi_tag`
attributes from both class-key and function-declarator positions, publish each
tag set on the canonical semantic entity or function binding, and consume that
typed fact when mangling functions and class types. The existing lifecycle
alias owner must thereby emit identically tagged C1/C2 and D1/D2 names, while
the polymorphism owner derives tagged `_ZTI`, `_ZTS`, and `_ZTV` names from the
same class `TypeId`.

- Spec requirements: canonical semantic identity/facts (§2); lowering consumes
  recorded facts without name lookup (§6); every ABI entry point has stable
  identity and is lowered once (§6); direct ELF symbol publication (§7); compact
  shared child storage and explicit ownership (§8); work proportional to source
  declarations/tags (§9); no filename or spelling shortcuts (§10).
- Ownership/data flow: parser retains compact semantic-only attribute nodes;
  semantic analysis validates/decodes literals and stores interned `NameId`
  ranges on canonical `EntityRecord`/`BindingRecord`; `AbiFactBuilder` translates
  those ranges into existing typed ABI tag facts; lifecycle and polymorphism
  lowering continue to consume mangled output through their existing owners.
- Complexity: one scan of the directly owned attribute edges per affected
  declaration, O(attributes + tag bytes); ABI publication appends O(tags) per
  demanded symbol. Tags use interned names and contiguous program-owned ranges,
  with no global search or per-tag owning heap node.
- Validation: the three ABI-tag PA33 cases, representative untagged lifecycle
  and RTTI cases, PA33 report, PA1-PA32 report, file audit, and a generated
  increasing tagged-class series measuring time/RSS and semantic counters.

## Performance Evidence

The generated series declares N tagged polymorphic classes and one demanded
object per class, keeping tags/entity constant. `CPPGM_DRIVER_STATS=1` and GNU
`time` show proportional source, semantic, lowering, and direct-object work:

| Classes | Source bytes | Semantic nodes | Functions | LowIR / MIR insns | Semantic peak bytes | Object bytes | Elapsed s | RSS KiB |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 32 | 2,382 | 390 | 98 | 930 / 968 | 487,900 | 620,368 | 0.02 | 12,556 |
| 64 | 4,782 | 774 | 194 | 1,858 / 1,928 | 971,004 | 1,239,400 | 0.03 | 17,320 |
| 128 | 9,694 | 1,542 | 386 | 3,714 / 3,848 | 1,937,324 | 2,483,888 | 0.07 | 26,860 |
| 256 | 19,806 | 3,078 | 770 | 7,426 / 7,688 | 3,870,252 | 4,989,664 | 0.14 | 46,152 |

An 8x increase yields approximately 8x nodes, functions, instructions, bytes,
and elapsed time. Attribute collection visits only direct owned edges; tag sets
are interned-name slices, and special-symbol publication performs no semantic
lookup or whole-program retry.

## Completed Checkpoints

| Checkpoint | Result |
| --- | --- |
| PA33 ABI-tag publication checkpoint | Pass — C1/C2, D1/D2, RTTI name/object, and vtable symbols carry canonical tags; PA33 59→62, PA1-PA32 4291/4291, file audit pass. |

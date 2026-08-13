# PA30 Final Audit

## Findings and Changes

| Finding | Full ownership-path change | Evidence |
| --- | --- | --- |
| Constructor cleanup demand rescanned retained syntax for explicit `throw` and missed an indirectly throwing constructor | PA30 object mode now selects `InitializationActionsAreNonthrowing` over typed constructor/call facts and demands prefix destructors from the semantic action graph; PA15's staged textual fixture policy remains explicit | New indirect-call unwind regression failed before and passes after; earlier LowIR references remain stable |
| Native startup, allocator, and RTTI planning guessed symbol spellings | Added typed generic RTTI data and C allocator roles at semantic/LowIR boundaries; normalized PA13 legacy startup names in the textual parser; removed backend name fallbacks | Legacy/explicit startup, nested catch allocation, RTTI, dynamic-cast, and multi-TU lifecycle controls pass |
| Compiler-object write/read and link retained duplicate whole-program buffers and copied definitions | Streamed object fields, moved linked units/definitions, passed object vectors by move, and bounded every read by exact remaining bytes and minimum encoded element size | 20k-function RSS 90,972 -> 48,296 KiB; malformed object rejected at 4,376 KiB |
| Rename paths performed repeated hash probes and compile/link phase work was under-observed | Centralized single-probe renaming, discarded spent export tables, and added source/semantic/lowering/adapt/input/link/encode/write counters and timers | 20,001 symbols: 40,002 symbol and 20,002 rename probes |
| Production `cppgm++` retained all functions' MIR and duplicated the final ELF content buffer | Added a responsibility-owned lowering session; indexed cross-function facts once, then lowered/encoded/reclaimed each MIR function; wrote ELF header and content without a second image | Final outputs byte-identical; incremental path removes another ~10 MiB at 20k functions |
| Foreign relocations could not reach C++ ABI labels; linked alias facts were dropped; Clang emitted unsupported relaxable GOT relocations | Carried object symbols through MIR, installed definition/alias labels at final encoding, and safely relaxed R_X86_64_GOTPCRELX/REX_GOTPCRELX loads | New helper regression reaches C2 constructors and a namespaced global from foreign ELF |
| In-process scalar lowering reparsed integer text despite typed values | Added explicit integer-value presence, populated it in the direct adapter/object reader, and restricted parsing fallback to explicit textual LowIR | PA29 atomics, globals, i128, PA30 scaling, and full local suite pass |
| PA30 additions created duplicate operation maps and oversized member-pointer ownership | Moved operation spelling to the shared LowIR model and split function-member-pointer lowering from data-member-pointer lowering | File audit warning count returns to the PA29 baseline of 21 |

No test or reference was weakened. Two course regressions were added for the
previously untested constructor-unwind and foreign-ABI paths.

## Performance Evidence

The 5k/10k/20k one-object link series finishes in 0.05/0.11/0.23 s at
15,144/26,212/48,296 KiB, versus 0.08/0.16/0.34 s and
26,324/47,880/90,972 KiB before audit. Object sizes remain
3,173,495/6,348,495/12,748,497 bytes. Both serialized objects and 80,238/
160,238/320,238-byte executables compare byte-for-byte with pre-refactor
outputs.

Final 20k counters are 20,001 symbols/functions, 40,002 symbol probes, 20,002
rename probes, 20,009 LowIR instructions, and 40,019 MIR instructions. Input,
link, lower, and encode times are 109.47/43.13/40.01/18.80 ms. The exact 2x
counter slopes and near-2x times show linear object/link/native work; profiling
identified ownership, not an unexplained algorithmic hot loop, as the original
memory cost.

The demanded-template trace records one specialization request, one demand
push, and one demanded body in each TU, followed by one weak coalescence. The
data-relocation trace preserves a typed global address to an external function
through object serialization, canonical linking, MIR indirect call, absolute
fixup, and native exit 4. The foreign-ABI trace additionally resolves C2 ABI
labels and a C++ global from a Clang ELF object.

## Architecture Review

The full `spec.md` checklist was applied to source/token ownership, semantic
identity and demand, scope/overload/template counters, typed LowIR transport,
object/link indexes, per-function native ownership, fixup layout, allocation,
and self-containment. The production path contains no syntax/semantic clone,
LowIR text round trip, assembly, complete-program retry, lowering-time semantic
search, name fallback, or external compiler/reference subprocess. The only
string-keyed hot map in PA30 is the required cross-TU ABI symbol table at the
serialized object boundary.

The linked LowIR and final code/fixup buffer are necessarily program-wide.
Semantic graph state is gone before adaptation; typed PA15 and backend LowIR
overlap only during the direct boundary copy; function-local analyses and MIR
die after each function is encoded. The standalone PA29 textual/MIR surfaces
remain explicit assignment adapters and do not alter compiler production
ownership.

## Final Architecture Review

Representative source data was traced end to end for a demanded class-template
constructor and a cross-TU function-pointer global; both preserve canonical
demand/linkage/relocation facts and execute correctly. Additional traces cover
indirect constructor unwind, runtime RTTI/allocation roles, legacy startup
normalization, C++ ABI aliases, a foreign GOTPCRELX relocation, and scalar
integer/atomic facts.

All stage commits and their current owners were reviewed independently of the
checkpoint conclusions. No open correctness, architecture, performance,
self-containment, timeout, file-placement, or fatal file-audit finding remains.

## Checkpoint Ledger

| Checkpoint | Audit result |
| --- | --- |
| Typed object/driver/link | Pass after streamed bounded ownership, move linkage, and full telemetry |
| Exception/runtime | Pass after typed constructor unwind and runtime-role closure |
| Polymorphism/linkage | Pass after RTTI-data roles and final ABI symbol/alias labels |
| Numeric/helper | Pass after typed scalar transport and retained PA29 runtime controls |
| Scoped regions | Pass; prior typed region and scheduler audit remains valid |
| Aggregate/member pointer | Pass after responsibility split and existing ABI controls |
| Full-stage architecture | Pass after incremental MIR ownership, foreign relocation regression, scaling, and required gates |

## Validation

- `make test-pa30`: 92/92 pass.
- `perl scripts/cppgm_file_audit.pl --stage pa30 --paths dev/src`: pass; 21
  warnings are unchanged inherited header-division warnings.
- `make test-report-through-pa30`: 4,132/4,132 tests and 30/30 stages pass.
- `git diff --check`: pass before final gate and commit.

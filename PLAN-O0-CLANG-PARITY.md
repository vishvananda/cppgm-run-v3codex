# Plan: O0 Clang Code-Shape Parity

Status: ready after `PLAN-PROPOSED-TEST-NORMALIZATION.md`

Date: 2026-08-20

## Objective

Reduce the remaining `semantic_overload.cpp -O0` object and machine-code gap
against Clang when both compilers use GCC 15's libstdc++ headers.  The work
should improve canonical source lowering and mandatory x86-64 target selection
without turning `-O0` into a hidden optimizer.

The implementation must preserve the project's typed production path:

```text
source semantics -> typed LowIR -> typed MIR -> native ELF
```

Each accepted phase must remain linear or near-linear, must use compact typed
identity in hot paths, and must not add text parsing, string-keyed semantic
maps, repeated whole-function rescans, or fixed-point cleanup to baseline
compilation.

This plan is deliberately broader than `PLAN-EMISSION-EFFICIENCY.md`.  That
plan preserved all existing LowIR and MIR fixtures.  The measurements below
now justify a small number of intentional public LowIR and MIR migrations.
Those migrations require the owning assignment contract, student scaffold
where one exists, and authoritative references to move together.

Before P0, complete `PLAN-PROPOSED-TEST-NORMALIZATION.md`.  The implementation
phases below assume that every new reducer goes directly into an active course
suite and that `proposed/` no longer exists as a test location.

## Measured baseline

The frozen source is:

`~/cppgm-extended-pa39-source-layout/benchmarks/self_compile/stable/semantic_overload.cpp`

The Clang comparison used the same standard-library implementation and header
set as cppgm++:

```sh
clang++ -std=gnu++11 -O0 -stdlib=libstdc++ \
  -I ~/cppgm-extended-pa39-source-layout/dev/src \
  -c semantic_overload.cpp
```

Both paths resolve GCC 15 libstdc++ headers under `/usr/include/c++/15` and
`/usr/include/x86_64-linux-gnu/c++/15`.  Clang's default libstdc++ selection
and the explicit `-stdlib=libstdc++` build were byte-identical, with SHA-256
`ce641b7f4071208928869b95b7a251080c9dfe0845cbdcd5b00a2c5dcf68a212`.
The retained cppgm++ comparison object has SHA-256
`520af5ad2cc527d93df30616ee074ad5cfd906c301d9988b0ac0dc450b2f6af1`.

| Metric | cppgm++ `-O0` | Clang `-O0` | Gap |
| --- | ---: | ---: | ---: |
| ELF object bytes | 4,406,784 | 2,477,128 | 1,929,656 |
| all `.text*` bytes | 921,144 | 523,269 | 397,875 |
| base `.text` bytes | 593,488 | 308,743 | 284,745 |
| COMDAT `.text*` bytes | 327,656 | 214,526 | 113,130 |
| `.gcc_except_table` bytes | 46,307 | 19,992 | 26,315 |
| `.eh_frame` bytes | 137,648 | 115,024 | 22,624 |
| relocation bytes | 934,872 | 470,328 | 464,544 |
| relocations | 38,953 | 19,597 | 19,356 |
| decoded instructions | 227,464 | 124,116 | 103,348 |
| defined functions | 5,533 | 3,476 | 2,057 |
| weak defined functions | 4,491 | 2,897 | 1,594 |

The largest whole-file instruction differences are still movement and frame
traffic:

| Family | cppgm++ | Clang |
| --- | ---: | ---: |
| `mov` | 98,317 | 61,196 |
| `lea` | 25,942 | 10,191 |
| `push` | 8,061 | 3,477 |
| `pop` | 12,238 | 3,472 |
| `call` | 31,970 | 15,251 |
| `jmp` | 17,327 | 8,718 |

The gap is not uniform.  `analyze_call_expression` alone is 167,743 bytes and
34,245 instructions in the cppgm++ object, versus 40,947 bytes and 7,846
instructions in Clang.  It has 32 cppgm++ epilogue copies versus one Clang
epilogue.  Across the object, cppgm++ calls `_Unwind_Resume` 1,672 times versus
Clang's 292, and repeats several destructor calls by factors of three to five.

The nonloaded object-size gap also has one independent cause.  cppgm++ emits a
666,747-byte symbol string table and a 720,702-byte section-name string table;
Clang uses one 527,776-byte string table for both roles.  Sharing that table is
valuable, but it does not reduce executable code and therefore remains a
separate PA32 phase.

## Boundary decisions

### Changes that should be visible in LowIR

The following are source- or object-semantics facts.  Hiding them in PA29 would
preserve an unnecessarily verbose frontend contract and duplicate work in
every backend.

1. Synthesized copy/move construction should transfer one bit-field allocation
   unit once, not reconstruct each bit-field separately.
2. A fully evaluated automatic `constexpr` scalar array should be represented
   by readonly constant data plus one `copyobj` into the distinct automatic
   object.
3. Equal destructor/unwind suffixes with the same semantic continuation should
   use a shared LowIR cleanup continuation.
4. A semantically contiguous zero-initialized object or subobject may use one
   `zeroinit`; unrelated scalar zero stores must not be guessed into an object
   operation by a late pattern matcher.

These are intentional LowIR fixture migrations at the earliest assignment
that owns the source behavior.

### Changes that should be visible in MIR but not LowIR

LowIR already permits immediate store values and memory-valued binary
operands.  Whether x86-64 can consume them without a temporary register is a
PA29 target-selection fact.

1. `MI_STORE` should retain a typed immediate source.
2. Integer `add`, `sub`, `and`, `or`, `xor`, and two-operand `imul` should
   retain a legal frame/global/dereference right operand.

The current MIR operand model already represents immediates and indexed,
frame, global, and register-based memory.  These changes need no new operand
kind or string field, but they do change the documented opcode operand rules
and checked MIR shapes.

### Changes that must remain below MIR

1. Selecting one native epilogue and encoding other returns as jumps is a
   function-layout decision.  MIR should continue to show every semantic
   return.
2. Sharing ELF symbol and section-name storage is an object-writer layout
   decision.
3. Shortest displacement, fallthrough-branch deletion, packed scalar stores,
   and the existing bounded forwarding rules remain valid PA29 encoder
   fallbacks.  They do not need to be rolled back when canonical source
   lowering makes some occurrences unreachable.

### No PA13 LowIR extension is required

The constant-array case does not require a byte-string instruction, a new
global-data item, or a new textual grammar production.  PA13 already defines:

- structured `global` data;
- `storage=readonly`;
- `addr` of a global;
- `copyobj <bytes>x<align>`; and
- `zeroinit <bytes>x<align>`.

The core `lowir_model::GlobalDefinition` already carries
`GlobalStorageMode`, and `MirGlobalDefinition` already carries `readonly`.
`lowir_native_program.cpp` maps `GSM_READONLY` into MIR, and native ELF
emission clears `SHF_WRITE` for that global.  Therefore there should be no
PA13 README, `lowir.md`, parser, serializer, grammar, or PA13 reference change
for this plan.

The source-side `pa15_lowir_detail::Global` adapter does not yet expose
readonly storage.  Add one compact enum field there and map it in the LowIR
renderer and `pa30_lowir_adapter.cpp`; do not store the mode as text.  This is
an implementation capability used first by the PA21 change, not a new PA15 or
PA13 student requirement.  Existing string-literal globals should not be
silently reclassified in the same changeset, because that would create a
broad unrelated LowIR migration.

## Assignment, test, and student-facing ownership

| Change | Public boundary | Earliest owning tests | Student-facing contract/scaffold | Expected reference work |
| --- | --- | --- | --- | --- |
| Bit-field storage-unit construction | LowIR | PA17 course LowIR; retain PA16 layout/bit-field semantic tests | PA17 README normative requirement plus an implementation suggestion in `Design Notes`; no PA13 or MIR scaffold edit | PA17 LowIR reference migration; census PA18-PA28 downstream fixtures |
| Automatic `constexpr` constant array template | LowIR | PA21 course LowIR; PA29 behavior check only if needed for end-to-end `copyobj` execution | PA21 README normative requirement and `Design Notes`; no PA13 syntax edit and no new student LowIR model field | PA21 LowIR reference migration; census PA22-PA28; PA29 behavior refs only if a new runtime case is added |
| Lexical cleanup continuation base | LowIR CFG | PA16 course LowIR | PA16 README normative sharing rule and compact-state suggestion in `Design Notes` | PA16 reference migration and full downstream census |
| Temporary/value cleanup continuation | LowIR CFG | PA17 course LowIR | PA17 README extension of the PA16 rule and `Design Notes` | PA17 reference migration and downstream census |
| Handler/unwind cleanup continuation | LowIR CFG and EH behavior | PA26 course LowIR plus PA31 host-EH behavior | PA26 README normative context-safety rule and `Design Notes`; PA31 README only if its observable contract changes | PA26 LowIR migration; PA31 runtime/inspect refs; downstream census |
| Contiguous source-owned zero initialization | LowIR | PA16 course LowIR; use a later owner only for a distinct later feature | PA16 README normative rule and `Design Notes`; no PA13 scaffold edit | PA16 or later-owner reference migration after reducer classification |
| Immediate-to-memory stores | MIR/native | PA29 course exact/behavior reducers; existing PA29 strict/structural fixture census | PA29 README normative MIR operand rule; comments in the student `dev/src/mir_model.h`; PA29 `Design Notes` for encoding constraints | PA29 MIR migration and PA38 O1/O2 input-MIR census |
| Memory RHS integer operations | MIR/native | PA29 course exact/behavior reducers; existing PA29 strict/structural fixture census | PA29 README normative MIR operand rule; student `mir_model.h` comments; `Design Notes` | PA29 MIR migration and PA38 O1/O2 census |
| Shared native epilogue | x86 layout only | PA29 course behavior reducer; benchmark telemetry for physical epilogue count | PA29 `Design Notes` only; no normative MIR or scaffold change | No LowIR/MIR ref migration; behavior ref through the normal target |
| Further weak demand pruning | Typed demand/native object | Earliest semantic owner for each removed edge, plus PA32 multi-object/link/inspect coverage | No blanket README edit; update only the assignment whose observable demand contract changes | Per-edge refs; PA32 object/link refs for cross-object safety |
| Shared ELF string table | ELF layout | PA32 inspect test only if table sharing is deliberately made part of the course contract; otherwise benchmark telemetry only | At most a PA32 `Design Notes` suggestion; no LowIR/MIR scaffold change | PA32 inspect migration only if table sharing is made part of the course oracle |

New regressions belong under `cppgm.tests/course/paN/`.  Existing assignment
fixtures remain useful for the migration census, but they are not a reason to
place a new reducer later than the source feature's first owner.  PA38 is only
the owner for O1/O2 optimizer behavior; none of the baseline O0 changes above
should be introduced as a PA38 optimization test.

Student-facing main sections must state only the behavior a student must
implement to pass the updated tests.  Complexity advice, compact identity,
and suggested table shapes belong at the bottom under the
`Design Notes (Non-Normative)` section.  Benchmark history, old fixture shape,
migration notes, and maintainer audit policy remain in this plan, not in
student handouts.

The earliest-owner choices follow the staged language boundary.  PA16 owns
class layout, initialization, destruction, and the base lexical cleanup
machinery.  PA17 owns class value/copy behavior and temporary lifetimes, so
the synthesized bit-field construction rule begins there even though PA16
already tests bit-field layout.  PA21 owns constant evaluation of automatic
objects.  PA26 adds handlers and unwind-aware cleanup context.  PA29 owns the
typed MIR-to-x86 selection contract, and PA32 owns host-object ELF layout.

The local PA29 README and MIR scaffold contain later mainline improvements
that are not present in `~/cppgm-assignments`.  When handouts are synchronized,
merge the new operand rules into the current local files and port the same
student-facing contract to the assignment checkout; do not overwrite the
current files with the older scaffold.  Do not commit the other checkout as
part of implementation in this repository.

The private `pa15_lowir_detail::Global` helper is not the PA13 student LowIR
model and has no counterpart in the current assignment checkout.  Its new
typed storage-mode field therefore travels with the PA21 implementation
changeset.  If that helper is later added to a generated student starter,
mirror the enum field there as PA21 scaffolding; do not present it as a new
PA15 exercise or a PA13 syntax change.

## Reference migration protocol

The source-to-LowIR and MIR assignments use checked-in reference output as an
oracle.  An intentional public shape change is not permission to generate
fixtures from the implementation under test.

For each LowIR or MIR phase:

1. Build the minimal reducer and run the current pinned reference through the
   documented `make ref-test-paN TEST=...` target.
2. Record whether the current reference already expresses the desired shape.
3. If it agrees, add the reducer directly to the owning course suite and
   regenerate only through the documented target.
4. If it disagrees and the new shape is an approved contract change, rebuild
   the authoritative reference compiler and reference-binary bundle, update
   the pinned bundle manifest, and then add the reducer directly to the active
   course suite through the same `ref-test` target.  Do not commit a dormant
   test in another directory while waiting for the new oracle.
5. Use a before/after fixture census.  Migrate only files explained by the
   public change; any unrelated semantic, LowIR, MIR, inspect, or exit-status
   movement stops the phase.

For LowIR migrations, record every changed PA and whether the difference is a
new generated global, a `copyobj`/`zeroinit`, or shared CFG blocks.  For PA29
MIR migrations, record strict raw MIR, structural raw/canonical MIR, course
exact MIR, and PA38 O1/O2 raw/canonical changes separately.

## Implementation sequence

Each retained phase is a separate committed and pushed changeset.  Do not run
two compilers or two inception builds concurrently with a timed benchmark.

### P0: freeze reducers, counters, and comparison scripts

Before modifying output:

- retain the same-STL Clang and cppgm++ frozen objects and their SHA-256 values;
- add or preserve a script that reports section bytes, relocations, defined
  functions by binding, decoded instruction families, and selected function
  sizes;
- keep reducers for the `__to_chars_10_impl` constant array, the `CallSemNode`
  synthesized bit-field constructor, a many-return function, immediate stores,
  memory RHS arithmetic, and repeated unwind cleanup states;
- add typed telemetry for cleanup states created/reused, constant-template
  bytes/globals/copies, bit-field units transferred, immediate stores selected,
  memory RHS operations selected, native returns and physical epilogues, and
  final defined functions by demand reason; and
- make every counter optional and O(1) at its existing decision point.

The benchmark source remains read-only in the extended checkout.

### P1: transfer bit-field allocation units once during construction

The existing assignment path already marks the first bit-field in an
allocation unit with `storage_unit_transfer` for synthesized assignment.
Synthesized construction does not publish the same fact, so
`LowerConstructionStep` performs a read/modify/write sequence for every field.

Implement one shared semantic classifier for construction and assignment:

- group adjacent bit-fields by the completed `BindingLayoutFact` storage
  offset and `bit_storage_bits`;
- emit one construction action for the first field in each unit and skip the
  remaining fields in that unit;
- lower the action as one typed load from the source unit and one typed store
  to the destination unit;
- derive transfer width from the storage-unit layout fact, not the first
  field's declared value type; and
- retain the existing field-wise fallback for an unsupported storage width,
  volatile storage, a noncontiguous layout, or a semantic subobject that may
  not be representation-copied.

Do not use `copyobj` for ordinary 1/2/4/8-byte units merely to make the LowIR
shorter.  The typed load/store form selects a substantially smaller native
sequence than setting up `rep movsb` for a small unit.  Existing `copyobj`
storage-prefix and whole-trivial-object rules remain unchanged.

Add a PA17 reducer whose class has multiple bit-fields in one unit and a
nontrivial member, forcing emission of a synthesized copy or move constructor
body rather than taking the existing whole-object `copyobj` call-site path.
Include a second unit separated by a zero-width field so the test proves both
coalescing and the boundary.  The existing
`300-bit-field-copy-semantics` test covers the whole-trivial-object path and is
not sufficient by itself.

Complexity: O(members) while the synthesized body is built, O(1) comparison
with the previous layout fact, and O(1) lowering per emitted storage unit.

### P2: canonicalize automatic `constexpr` scalar arrays into constant data

The frozen libstdc++ function contains:

```cpp
constexpr char __digits[201] = "000102...9899";
```

cppgm++ currently lowers each specialization into scalar address/store work.
Clang emits 201 readonly bytes and one copy into the automatic array.  The
three cppgm++ specializations total 7,404 text bytes; Clang's total is 807
bytes plus three 201-byte readonly constants.

Implement the canonical source lowering in PA21:

1. Recognize a local array binding that owns a fully evaluated PA21 constant
   object and has trivial scalar element lifetime.  Initially limit the path
   to nonvolatile scalar arrays with no runtime/local-address relocation.
2. Reuse `StaticInitializerLowering`'s typed layout walk to build structured
   data.  Do not serialize and reparse LowIR, and do not reconstruct the bytes
   from rendered literal strings.
3. Emit an internal `storage=readonly` generated global with exact size and
   alignment.
4. Keep the automatic variable's distinct local storage and initialize it with
   one `copyobj <size>x<align> addr @template, <local-address>`.
5. Intern identical constant-data templates with a typed structural hash and
   equality check so template instantiations in one LowIR program can share
   data.  The hot key consists of typed data-item kind/value/symbol/addend and
   span facts, never rendered text.
6. Ensure function and global reachability follows the `copyobj` source symbol
   and that weak/COMDAT function selection cannot leave a live relocation to a
   discarded template.

The existing PA13 structured-global representation may use one `i8` item per
byte.  That is still much smaller than three LowIR/MIR instructions per byte
per specialization and avoids a new syntax.  A compact byte-run data item is
deferred unless measurements show the data-item vector itself is a remaining
compile-time bottleneck.

Add one PA21 course reducer for a block-scope `constexpr char[]` initialized by
concatenated string literals and read at runtime.  Include two template
instantiations with identical content to cover typed data interning, while
checking that the two automatic array addresses remain distinct.  The current
PA21 `300-constexpr-aggregate-array` test covers namespace-scope constant data,
not this automatic-storage path.

Keep the PA29 E4 constant-byte-store encoder peephole.  It remains a valid
target-specific fallback for handwritten LowIR and noncanonical scalar store
sequences, while the PA21 path removes the frozen occurrence before MIR.

Complexity: O(constant data items) to construct and hash a candidate, expected
O(1) table lookup, one structural comparison on a hash collision, and O(1)
LowIR instructions per accepted automatic object.

### P3: select immediate-to-memory stores directly

`emit_store_instruction` currently resolves every nonfloating scalar value to
a register before constructing `MI_STORE`, even when LowIR already supplies an
immediate.  Preserve the immediate in MIR and let native encoding choose the
legal form:

- C6 `/0 ib` for 8-bit stores;
- C7 `/0 iw` for 16-bit stores;
- C7 `/0 id` for 32-bit stores; and
- C7 `/0 id`, sign-extended, for encodable 64-bit stores.

An unencodable 64-bit immediate remains an `OP_IMM` in public MIR, just as a
large ALU immediate does today; the encoder materializes it into a scratch
register only at the concrete x86 constraint.  Scratch selection must not
overwrite the destination's base or index register or the temporary used to
materialize a preemptible global address.

Add PA29 course reducers for frame, dereference, indexed, and global
destinations at 8/16/32/64 bits, including a 64-bit value inside and outside
the sign-extended imm32 range.  Use active exact MIR only when the
authoritative reference agrees, and add behavior coverage that reads each
stored value.  Census the existing PA29 strict and structural fixtures rather
than treating those assignment-local suites as the home for new course
regressions.  Update the normative PA29 opcode rule and student MIR scaffold
comments; put opcode-selection suggestions in `Design Notes`.

Complexity: O(1) selection and encoding per store.

### P4: retain legal memory right operands for integer operations

The integer selector currently materializes a frame/global/dereference right
operand into a scratch even though x86-64 directly supports `r, r/m` forms.
Allow memory RHS operands for:

- `add`, `sub`, `and`, `or`, and `xor`;
- two-operand `imul`; and
- compare paths not already covered by the existing direct-memory comparison
  selector.

The destination remains a register because LowIR binary results are values,
not destructive memory updates.  Preserve the materialized path for
memory-to-memory forms, illegal widths, fixed-register division/shift paths,
floating operations, atomics, and an operand whose selected address carrier
would be clobbered before the instruction reads it.

Use the existing use census and selected location.  Do not add a backward
producer search or a second alias analysis.  A memory operand that is already
frame/global/dereference shaped can be appended directly in O(1).

Add PA29 course reducers covering all supported opcodes,
indexed/frame/global RHS forms, and negative cases that must still
materialize.  Use active exact MIR only when the authoritative reference
agrees, add behavior coverage for execution, and census the existing PA29
strict and structural fixtures.  Update PA29's normative MIR operand rules
and the student scaffold comments.  Census PA38 O1/O2 fixtures because their
input MIR begins with PA29 selection.

Complexity: O(1) per selected operation.

Before adding P3/P4 code, keep `lowir_native.cpp` and
`lowir_native_elf.cpp` below the fatal file-audit limits.  Extract integer
selection and scalar memory/ALU encoding into responsibility-named modules
rather than growing the two near-limit files.  Any new `.cpp` module must be
added to every appropriate tool list in `dev/frontend_source_sets.mk`.

### P5: emit one profitable native epilogue per function

MIR should retain every `MI_RET`/`MI_FRET`, including its result carrier.
During final function layout:

- perform one O(MIR instructions) return census;
- leave a single final return as the physical epilogue when possible;
- encode other returns as branches after performing their result transfer;
- place one shared restore/stack teardown/`ret` sequence at the selected end;
- let the existing branch relaxation compact the new local branches; and
- use a simple exact byte cost so a tiny two-return function is not made
  larger when branch bytes exceed the saved teardown.

EH call-site ranges, symbols, block offsets, and debug locations must use the
same final offset translation as other relaxed branches.  The epilogue itself
cannot unwind and must not accidentally inherit an active cleanup call-site
range.

Add a PA29 behavior reducer with multiple integer, floating, and void returns
and a preserved callee-saved register.  Native epilogue count is a benchmark
and telemetry assertion, not a MIR or object-byte fixture.

Complexity: one linear census plus O(1) work per return; branch relaxation
retains its current linear/binary-search offset translation.

### P6: build cleanup continuations as a compact typed DAG

The current source lowering has several partial sharing mechanisms.  PA17's
`CleanupDispatchCache` hashes vectors of seven words per action, conditional
cleanup interning keys one action separately from its tail, PA16 return and
destructor paths still spell many suffixes inline, and the PA26 exception
context is applied after several of those choices.  The O1 LowIR cleanup pass
only reduces `_Unwind_Resume` calls from 1,672 to 1,537 on the frozen object,
so running that postpass at O0 would add compile work without addressing the
construction-time duplication.

Introduce a PA16 cleanup-continuation foundation with compact identities:

```text
CleanupStateId = intern(action-id, tail-state-id, terminal-id, context-id, mode)
```

The key must include every semantic distinction that affects execution:

- object/destructor action identity;
- conditional-lifetime guard identity;
- normal versus unwind-only cleanup;
- active handler and exception-region context;
- required handler-exit/region-exit operations; and
- the terminal continuation: resume, return staging, branch target, or outer
  cleanup state.

Use dense IDs, one open-addressed typed table, and a state-to-block vector.
Create a LowIR block lazily on the first branch to a state and reuse it on
later branches.  Build suffix identity incrementally from the terminal
backward so no vector suffix is rescanned or copied.  The common resume
terminal is shared only when the completed exception context is identical.

Stage the public migration by assignment:

1. PA16: ordinary lexical destruction, destructor suffixes, and compatible
   return cleanup.  If scalar returns need one staging slot to share a
   profitable suffix, use one typed function-local slot and a byte-cost/action
   threshold; do not add store/reload traffic to a one-exit function.
2. PA17: temporary, copy/value, and conditional lifetime states extend the
   same continuation identity.
3. PA26: handler-aware and unwind cleanup adds the completed exception context
   and handler-exit facts to the key.
4. PA31: host-EH behavior verifies destructor order, catches, rethrow/resume,
   and LSDA behavior after the LowIR migration.

Add minimal course reducers at PA16, PA17, and PA26 rather than relying only on
the frozen source.  Reuse and migrate the existing PA26
`200-shared-call-unwind-hidden-temp` and
`200-destructor-unwind-shares-generated-suffix` coverage where appropriate.
The PA31 `310-shared-conditional-cleanup-resume` family is the fast host-EH
runtime lane.

Retain `lowir_cleanup_o1` for optimized handwritten/textual LowIR that did not
come through canonical source lowering.  Do not run a general LowIR cleanup
postpass in the source `-O0` path.

Place the interner and continuation records in a responsibility-named PA16
module.  Extract reusable state from the already large
`pa17_temporary_lifetime_lowering.h`; do not append another table to that file.

Complexity: expected O(actions + cleanup edges) per function, O(1) expected
interning per state, and O(unique cleanup states) storage.  The implementation
must expose probes, hits, unique states, blocks emitted, and destructor/resume
operations avoided.

### P7: canonicalize source-owned zero initialization in LowIR

The preferred public representation for a source-known, contiguous
zero-initialized object or subobject is the existing LowIR `zeroinit`
operation.  PA16 owns ordinary aggregate/class value initialization; a later
source feature keeps its own earliest owner.  This is a source/object fact,
so retaining a long scalar-store sequence in LowIR and rediscovering the
object boundary in PA29 would be both less reliable and less efficient.

Do not migrate fixtures until the target side has a cost-directed
`MI_ZERO_BYTES` encoding.  The current encoder always uses `rep stosb`, which
can make a small `zeroinit` larger than one or two immediate stores.  First,
in PA29, select direct immediate stores for profitable fixed small sizes and
retain `rep stosb` for larger or dynamic cases.  This choice is below MIR and
does not change the meaning or spelling of `zeroinit`.

Then build reducers for each observed bulk-zero-shaped site and route them by
semantic owner.  For PA16-owned initialization:

- emit one `zeroinit` only when semantic lowering already has the exact
  contiguous object/subobject span and alignment;
- preserve explicit member construction, lifetime, padding, volatile, union,
  and side-effect boundaries;
- leave arbitrary neighboring scalar zero stores scalar; and
- use the same typed layout span already computed for initialization rather
  than a rendered-operand key or a late instruction scan.

Add a PA16 course LowIR reducer for the accepted semantic class and migrate
the PA16 reference through the documented workflow.  Add later-owner reducers
only when a later feature creates a distinct initialization path.  Update the
PA16 normative requirement and put the efficient span-based implementation
suggestion in `Design Notes`.  No PA13 operation, parser, serializer, or
student scaffold change is needed.  If the census shows a broad PA16 and
downstream LowIR movement, perform it as one explicit reference migration;
do not hide the change in a PA29 peephole to avoid fixture updates.

Complexity: O(initializer elements) in the existing layout walk and O(1)
emission for each accepted contiguous span; no additional whole-function
pass.

### P8: re-audit weak definition demand after structural code reductions

The same-STL comparison leaves 1,594 more weak definitions in cppgm++.
Re-run `PLAN-OBJECT-DEMAND.md` telemetry after P1-P7 so code-shape changes do
not obscure the body census.

For each excess family:

- attribute the body to a typed semantic demand reason;
- distinguish declaration, semantic completion, runtime reference, and native
  definition roots;
- remove one unjustified edge at a time at its earliest semantic owner;
- prove positive demand and negative non-demand with course tests at that PA;
- use PA32 multi-translation-unit link/inspect coverage for ODR, weak, COMDAT,
  vtable, TLS, EH, and lifecycle safety; and
- retain the final native reachability closure as a safety check, not as a
  substitute for correct semantic demand.

Do not target Clang's exact weak count and do not add cross-object metadata to
LowIR merely to prune a local object.  Any cross-object change requires a
separate native-linking design review.

### P9: share the ELF symbol and section-name string table

After executable-code work, let PA32's host object writer intern symbol names
and section names into one ELF `SHT_STRTAB`.  Point both `e_shstrndx` and
`.symtab`'s `sh_link` to that section.  Use one byte-vector builder and one
typed spelling-to-offset interner; do not concatenate two completed tables or
perform a suffix search over existing bytes.

Validate with `readelf`/the repository's PA32 inspect lane and the host linker.
This is legal ELF layout, but it is not language behavior.  Make it an active
PA32 inspect requirement only when the authoritative reference bundle adopts
the same layout as an intentional course contract.  Otherwise retain the
measurement in the benchmark report without creating a dormant exact-layout
test.

Complexity: expected O(total unique name bytes) and one lookup per emitted
name.

## Validation and performance gates

For every phase:

1. Run the focused reducer.
2. Run the full owning assignment through the root report target so all
   failures are collected at once, for example:

   ```sh
   make test-report ACTIVE_TEST_REPORT_PAS='pa17 pa29 pa38'
   ```

3. Run `make test-paN` and `make test-report-through-paN` for the earliest
   owner before calling that assignment clean.
4. Run a full root `make test-report` before retaining and committing the
   changeset.  `make test` is not an acceptable substitute because it stops at
   the first failure.
5. Run the PA39 file audit with zero fatal findings.  Separate new
   responsibilities before a file crosses a fatal size/division boundary.
6. Produce deterministic frozen objects and record object bytes, `.text*`,
   EH/LSDA, relocation bytes/count, functions by binding, decoded instruction
   families, and the targeted function/call counts.
7. Compare immutable baseline and candidate compilers in sequential,
   interleaved ABBA rounds.  Record median wall/user/system time and peak RSS.
   Treat wall-only movement on the intermittently loaded host as noise; reject
   an unexplained median user-time or RSS regression above 3%.
8. Commit and push the isolated accepted changeset before beginning the next
   phase.

Use the frozen compile and `test-report` as the regular performance/correctness
signals.  Do not use PGO.  Do not run inception after every phase.

After the complete retained batch, and only after a clean full
`make test-report`:

1. perform a timed clean PA39 `cppgm++-self` build;
2. perform a clean timed 8-way inception comparison and record peak RSS;
3. clean its objects before a clean timed 32-way inception comparison;
4. record peak RSS for both worker counts;
5. require every inception object and final binary comparison to pass; and
6. rerun the full report and zero-fatal audit on the exact final commit.

The user-visible goal remains a frozen compile below 15 seconds under a fair
load window.  The current compiler is already comfortably below that on calm
runs, so accepted code-size work must not sacrifice the compile-time lead over
GCC at `-O0`.

## Change ledger

Fill one row after each independently retained phase.

| Phase | LowIR fixture effect | MIR/object fixture effect | Frozen size/structure | Compile time/RSS | Validation | Status/commit |
| --- | --- | --- | --- | --- | --- | --- |
| P0 baseline | none | none | cppgm++ 4,406,784 bytes; `.text*` 921,144; 5,533 functions; 38,953 relocations | pending immutable ABBA refresh | same-STL Clang object verified | ready |
| P1 bit-field units | expected PA17 migration | downstream native change | pending | pending | pending | planned |
| P2 constexpr arrays | expected PA21 migration | PA29 runtime only if added | pending | pending | pending | planned |
| P3 immediate stores | none | expected PA29/PA38 MIR migration | pending | pending | pending | planned |
| P4 memory RHS | none | expected PA29/PA38 MIR migration | pending | pending | pending | planned |
| P5 shared epilogue | none | MIR unchanged; native bytes change | pending | pending | pending | planned |
| P6 cleanup DAG | expected PA16/17/26 migration | PA31 EH/object change | pending | pending | pending | planned |
| P7 zero initialization | expected PA16 migration for source-known contiguous spans | PA29 encoding change; downstream native change | pending | pending | pending | planned after fixture census |
| P8 weak demand | avoid LowIR change unless semantic owner requires it | PA32 link/inspect change per edge | pending | pending | pending | discovery |
| P9 shared ELF strings | none | PA32 object layout only | expected nonloaded file-size reduction | pending | pending | planned |

## Completion criteria

This plan is complete only when:

- every retained LowIR change is assigned to PA16, PA17, PA21, PA26, or the
  later source owner proven by its reducer;
- no candidate has introduced a new PA13 syntax/model feature without a new
  analysis showing the existing structured globals, `copyobj`, `zeroinit`,
  and CFG are insufficient;
- PA29 MIR and student scaffold describe every retained immediate/memory
  operand shape consumed by native emission;
- student handouts contain only current requirements and non-normative design
  suggestions, with no migration or benchmark history;
- all active fixtures come from the documented authoritative reference
  workflow, with an approved disagreement landing only after the reference
  bundle is deliberately updated;
- the full root report and PA39 audit are clean after every committed phase;
- frozen compile time and RSS do not regress beyond the predeclared noise
  guardrail;
- object, text, relocation, instruction, cleanup, and weak-definition changes
  are recorded in the ledger; and
- the final clean self build, 8-way inception, and 32-way inception comparison
  all pass with recorded timings and peak RSS.

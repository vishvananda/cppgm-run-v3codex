# Plan: Eliminate LowIR-to-MIR Presentation Transit

Status: complete -- structural, timing, report, audit, self-host, and inception gates pass

Date: 2026-08-19

Implementation anchor: `635cfa13`

The preceding O0 value-placement slice reached its own clean, pushed boundary
at `9960ff68`.  The typed-operation preparation and its pressure correction
now form the clean implementation anchor above.  No later O0 placement phase
begins until the work in this plan is complete.

## 1. Objective

Finish the representation work that remains after
`PLAN-LOWIR-COMPACT-IDENTITY.md`.  Semantic values, slots, blocks, symbols,
types, registers, frame homes, and local fixups are already compact identities.
The remaining problem is presentation data that is still constructed, copied,
rehashed, or parsed while a source program moves through typed LowIR, prepared
LowIR, MIR, native encoding, and the host-object writer.

The production source-to-object path should satisfy the `spec.md` pipeline:

```
typed semantic facts -> typed LowIR -> per-function MIR -> direct ELF
```

LowIR text, MIR text, diagnostic text, and ELF string-table bytes are views or
boundary output.  They must not be transport representations between those
phases.  A normal object compile must not construct local display names or
literal spellings that only a dump tool would consume.

The cumulative implementation must produce a visible, repeatable reduction in
the frozen `semantic_overload.cpp -O0` compile.  A representation cleanup that
only relocates the same hashing and allocation is not complete.

This is an internal representation project.  Serialized LowIR, serialized MIR,
diagnostics relevant to tests, object bytes, and behavior remain unchanged
unless a separately identified correctness bug requires an owned regression.

## 2. What the earlier performance plans established

This direction was evaluated, but the completed work did not remove all
presentation transit.

### 2.1 Fable PERF evidence

`/home/vishvananda/work/fable/PLAN-PERF2.md` attributed approximately 0.35
seconds to backend maps keyed by rendered names such as `%tN`.  The cost was
split across LowIR validation, optimization, inlining, and LowIR-to-MIR
lowering.  It recommended dense identity.

The same plan's LowIR-inliner analysis found `memcmp`, string-keyed use counts,
and repeated instruction/name reconstruction among the dominant costs on
`post_token.cpp`.  That evidence applies to any remaining pass that recovers a
typed fact from presentation text.

### 2.2 The rejected side-index experiment

This repository's `PLAN-PERF.md` records a rejected dense LowIR value index.
It built an additional name-to-ID index beside the string-bearing model.
LowIR-local timings improved, but end-to-end time did not because construction,
string ownership, hashing, and the extra lookup layer remained.

That result rules out another compatibility side map.  It does not rule out
removing the text representation or keeping presentation in a cold sidecar.

### 2.3 The completed compact-identity milestone

`PLAN-LOWIR-COMPACT-IDENTITY.md` removed string-keyed semantic identity from
LowIR optimization, native value/slot analysis, MIR control flow, and typed
fixups.  Its final comparison reported a 13.60% median user-time and 14.08%
median wall-time improvement.

That milestone is real and should not be rolled back.  In particular, the
current native lowerer has dense `values_`, `slot_offsets_`, slot-state byte
vectors, register-occupant vectors, and `BlockId`/`SymbolId` operands.  There is
no current string-keyed `spill_offsets_` table to replace.

The prior plan's residual audit deliberately left output-boundary strings and
the LowIR-to-MIR pool remap.  The live trace below shows that some of those
strings are used before the true output boundary, and that several typed facts
are still downgraded to text and recovered later.  This plan addresses that
narrower residual without rebuilding the semantic-ID work.

## 3. Current representation trace

### 3.1 PA15 typed LowIR still owns names

`dev/src/pa15_lowir_types.h` uses `std::string` for:

- `Block::label`;
- `Parameter::name`;
- `Slot::name`;
- `Symbol::name`, `object_name`, and `section_name`; and
- the private-object alias spelling.

Operands and control-flow references are numeric, so these strings are
presentation rather than semantic identity.  They are nevertheless retained
through the complete typed program and copied again by the PA30 adapter.

`AdaptTypedLowIRForNative` constructs new `@`, `%`, `$`, and `^` prefixed
strings, hashes them into another pool, and renders every ordinary integer and
i128 value before the native path can consume the already-decoded bits.  It
also discovers a slot's parameter origin with a nested name-and-type comparison
instead of carrying the originating `ParameterId` or `ValueId`.

Efficient replacement: store one unprefixed pooled spelling ID in the typed
program, retain the sigil in the field kind rather than in the bytes, and carry
the slot/parameter relationship explicitly.  Move the resulting presentation
pool into compact LowIR; do not render and reintern it.

### 3.2 Prepared LowIR sometimes reconstructs semantic facts from text

The core LowIR records use compact IDs, but the following avoidable operations
remain:

- `canonicalize_frontend_symbol` constructs `"@" + object_name` to recognize
  redundant ABI spelling;
- export derivation constructs owning internal/object-name strings well before
  the ELF writer needs them;
- `canonicalize_serialized_lowir_facts` clears operand type facts because the
  serialized form can derive them; and
- `intern_lowir_program_literals` materializes missing presentation text at the
  end of every optimization run, including ordinary object compilation.

The native analysis then tests a zero index by comparing its spelling to
`"0"`, and native selection rediscovers a floating literal's type from its
suffix.  These are direct violations of the rule that lowering consumes typed
facts rather than rendered text.  They also make equivalent spellings such as
`0`, `00`, and `0x0` take different optimization paths.

Efficient replacement: preserve literal type and decoded value/bits as typed
facts through preparation.  Render a literal only from the serializer.  Keep
an optional exact spelling in the LowIR presentation sidecar when the explicit
text contract requires it.

### 3.3 LowIR-to-MIR remaps presentation by bytes

`session_detail::StringIdentityMap` allocates a second `StringPool` and a remap
vector.  On the first use of a LowIR `StringId`, it loads the bytes, hashes them,
copies them into the MIR pool, and records another ID.  The path is used by:

- the program symbol table;
- function object names and block labels;
- parameter and frame display names;
- debug files and variables;
- floating literals and wide global literals; and
- section, alias, and other ABI presentation.

The remap avoids copying the entire LowIR pool, which was better than the former
implementation, but it still treats presentation as a phase transport format.
Most local names are never read during a normal object compile.

Efficient replacement: seal one presentation store after LowIR optimization
and share that immutable store with MIR.  A source `StringId` remains the same
ID in MIR.  Compiler-generated MIR-only display names use compact tagged
ordinals or fixed enums, not a second string pool.  The one shared owner lives
at program/session granularity; hot operands and instructions retain only
32-bit IDs.

### 3.4 MIR still carries literal text needed only by encoding or dumping

MIR float immediates carry `StringId`.  Scalar and x87 encoding repeatedly
loads that string and calls `strtof`, `strtod`, or `strtold`.  MIR global i128
data likewise carries a spelling that is parsed back into low/high words even
though LowIR already held those words.

MIR optimization also compares float immediates by spelling identity.  This is
not the most efficient semantic representation and can distinguish spellings
that encode the same value.

Efficient replacement: decode a literal once at source construction or the
explicit LowIR parser boundary.  Carry a typed raw-bit payload through LowIR,
MIR, and global data.  Retain optional presentation only for LowIR/MIR dumps.
The existing 64-byte MIR operand budget can hold the low word, high word, and
an optional presentation ID by reusing the current union and reserved padding;
the compact operand must not grow.

### 3.5 Native encoding downgrades `SymbolId` back to string identity

`CodeBuffer` accepts typed symbol labels and fixups, but `label(SymbolId)`
immediately resolves the spelling and inserts it into
`unordered_map<string, offset>`.  The host-object pipeline continues that
downgrade through:

- string-bearing `HostFunctionLayout` records;
- string-keyed encoded-label indexes;
- declaration/object-name maps;
- weak/COMDAT object sets;
- required-local, object-only, defined, TLS, catch-type, and section sets;
- section indexes; and
- relocation targets that are strings even when they began as `SymbolId`.

Some names in this area are genuinely external.  Generated program symbols,
TLS wrappers, catch types, COMDAT owners, and internal relocations are not.

Efficient replacement: keep dense symbol locations indexed by `SymbolId`, keep
object spellings as pooled `StringId`, and use a tagged relocation target of
`SymbolId`, `SectionId`, or imported external-name ID.  Split generated labels
from arbitrary imported-object labels.  Render/sort bytes only while building
`.symtab`, `.strtab`, `.shstrtab`, COMDAT records, and final relocation entries.

### 3.6 Name text in optimizer collision handling

The force inliner and O1 inliner inspect pooled spellings to reserve generated
name ordinals and avoid collisions with explicit LowIR names.  This is required
for deterministic public dumps, but it need not be repeated string work in the
optimization loop.

Efficient replacement: classify explicit generated-name patterns once at the
input/construction boundary and retain sparse numeric reservations.  Inliners
allocate semantic IDs monotonically and consult those numeric reservations.
Only the serializer renders the chosen spelling.

### 3.7 Text boundaries that remain legitimate

The following text work is not phase transit and should remain isolated:

- the explicit `.lowir` lexer/parser resolving arbitrary user spellings;
- LowIR, MIR, CY86, diagnostic, and debug serialization;
- joining independently produced private or foreign objects by external name;
- actual ABI symbol, version, COMDAT, section, and relocation spellings; and
- the final ELF string and symbol tables.

Those boundaries may use a transient text index.  They must publish compact IDs
before ordinary analysis resumes, and their maps must die with the boundary.
The explicit LowIR parser can later replace owning token strings with source
spans and `StringId` keys, but that work does not affect the frozen C++ source
benchmark and is not allowed to obscure the source-path result.

## 4. Target representation

### 4.1 Semantic core and cold presentation sidecar

Keep the current semantic core:

- `SymbolId`, `ValueId`, `SlotId`, `BlockId`, and `LocalLabelId` remain strong
  32-bit identities;
- per-function facts remain dense vectors or fixed register tables; and
- MIR operands retain enum kinds and compact typed payloads.

Move presentation into a program-owned sidecar.  A name record stores one
unprefixed `StringId` plus a compact presentation kind when the owning table
does not already imply the kind.  `@`, `%`, `$`, and `^` are rendered, not
stored.  Compiler-generated names use a kind plus numeric ordinal.

The adapter receives an explicit presentation policy:

- `SERIALIZABLE` retains the names required by LowIR/MIR dump tools and debug
  diagnostics; and
- `OBJECT_ONLY` omits local parameter, slot, value, block, and frame spellings
  after all observable ordering/collision facts have been converted to compact
  metadata.

Both policies produce the same typed LowIR and MIR semantics.  The object-only
path is not a second backend representation; it is the same records with a
cold optional sidecar absent, as required by `spec.md`.

### 4.2 Sealed shared presentation storage

The LowIR string pool remains uniquely mutable while parsing, adapting, or
optimizing.  Before native lowering it is sealed.  Sealing exposes immutable
shared storage without copying its strings or rebuilding its hash table.

MIR holds one program-level shared reference to that sealed storage.  All
LowIR-origin `StringId` values retain their numeric identity.  The public
`lower_program` API may safely return a MIR program because the sealed storage
owns its lifetime.  There is no `StringIdentityMap`, remap vector, or byte
reinterning step.

The shared ownership is confined to the program shell.  No instruction,
operand, value fact, hash node, or other hot record acquires a `shared_ptr`.
Copies of mutable LowIR programs remain explicit deep copies; a read-only
presentation share is never an accidental copy semantic.

### 4.3 Typed literal payload

Define a compact literal payload sufficient for all native emission:

- integers carry low and high 64-bit words and their `LowType`;
- f32 and f64 carry their exact IEEE bits;
- f80 carries the exact emitted low/high words; and
- an optional `StringId` or presentation-format tag exists only for dumps.

Text parsing happens once at the source-literal adapter or explicit LowIR input
boundary.  Native analysis, MIR optimization, and encoding use bits.  Dumping
uses an exact retained spelling when required and a deterministic canonical
renderer otherwise.

### 4.4 Typed object-emission identity

Use dense or compact object records:

```
GeneratedLabel   = SymbolId + section/offset
ObjectName       = StringId
RelocationTarget = SymbolId | SectionId | ExternalNameId
SectionIdentity  = fixed SectionKind | pooled StringId
```

Imported objects may initially supply raw external strings.  Intern each once
at import and use `ExternalNameId` through joining and layout.  The final ELF
writer resolves these IDs while emitting the required byte tables.

Ordering that is observably textual remains a final ordering step over pooled
spellings.  It must not force string-keyed maps into instruction selection or
encoding.

## 5. Rejected alternatives

### 5.1 Another name-to-ID side index

Rejected.  The earlier PERF experiment already showed that adding dense facts
beside a string model pays both representations and does not improve the whole
compile reliably.

### 5.2 Copy the complete LowIR pool into MIR

Rejected.  It removes remap probes but copies unused local and LowIR-only
presentation, increases lifetime, and contradicts the object-only policy.

### 5.3 A mutable pool shared casually by LowIR and MIR

Rejected.  It makes copy semantics and ID stability hard to audit.  Sharing is
allowed only after an explicit seal, and post-seal additions must use compact
generated identities or a separately owned bounded presentation extension.

### 5.4 Replace string maps with `unordered_map<StringId, ...>` everywhere

Rejected as a default.  When a domain is bounded by `SymbolId`, `StringId`, or
`BlockId`, use a vector or flat compact table.  Hashing a smaller key is still
unnecessary work when direct indexing is available.

### 5.5 Remove presentation needed by the staged contracts

Rejected.  PA13 LowIR, PA29 MIR, PA37 object roundtrip, and PA38 optimized MIR
remain public assignment surfaces.  The serializers must remain complete and
must consume the same typed facts as the object path.

## 6. Implementation sequence

Each phase is a separate committed and pushed changeset.  Do not combine a
representation phase with O0 instruction-placement changes.

### Phase 0: clean anchor and observable work

1. Bring the paused three-file O0 slice to its own full test/inception commit
   boundary without extending its scope.
2. Copy the resulting compiler as the immutable A/B baseline.
3. Add disabled-by-default counters and non-overlapping timers for:
   - PA15 owned name entries and bytes;
   - adapter prefix renders, integer renders, string-pool calls/probes/bytes;
   - LowIR literal materializations;
   - LowIR-to-MIR maps, mapped bytes, hits, and MIR-pool storage;
   - native semantic string reads and literal text parses;
   - typed versus named code-buffer labels/fixups;
   - internal versus imported ELF string-map entries/probes; and
   - final `.strtab`/`.shstrtab` unique strings and bytes.
4. Record model sizes and peak live bytes at typed LowIR, compact LowIR, MIR,
   encoded-section, and final ELF boundaries.
5. Capture three interleaved frozen `-O0` baseline runs and exact LowIR, MIR,
   object, and executable hashes.  Do not build or run inception concurrently.

Phase 0 changes telemetry only and must preserve every byte.

### Phase 1: stop recovering literal facts from spelling

1. Preserve `Operand::literal_type` and decoded integer/floating facts through
   frontend and serialized-fact canonicalization.
2. Replace the zero-index spelling test with decoded integer comparison.
3. Replace floating suffix inspection in native selection with `LowType`.
4. Decode f32/f64/f80 raw bits once at source adaptation or explicit LowIR
   parsing.  Carry those bits into MIR float operands.
5. Carry both i128 words into MIR globals and structured data; delete the
   encoder's string-to-wide-integer round trip.
6. Make MIR float optimization and encoding compare/use typed bits.  Keep an
   optional spelling only for exact dump output.
7. Stop `intern_lowir_program_literals` from materializing text during an
   ordinary object compile.  Serialization renders missing presentation
   without mutating the semantic program.
8. Enforce the existing 64-byte MIR operand ceiling and record the final
   layouts.

Expected output change: none.  A changed LowIR/MIR fixture or object is treated
as a bug until reduced and explained.

Earliest tests: PA13 for explicit literal parsing/roundtrip and PA29 for MIR
literal/native behavior.  Add PA37 object-roundtrip coverage if preserving the
typed fact fixes a source-versus-text difference.

### Phase 2: one sealed presentation identity from LowIR through MIR

1. Give `StringPool` an explicit sealed immutable storage view with stable IDs.
   Keep mutable copy semantics explicit and deep.
2. Seal presentation only after LowIR optimization and any forced rewrite that
   can create public names.
3. Make MIR share the sealed store at program granularity.
4. Preserve every LowIR `StringId` numerically in MIR; remove
   `StringIdentityMap`, its remap vector, and every `strings.map(...)` call.
5. Represent fixed frame/debug helper names with bounded enums and generated
   names with tagged ordinals.  Do not create a second general pool.
6. Keep serializers and diagnostics as the only local-name readers.

Expected output change: none.  Structural counters must report zero mapped
strings and zero mapped bytes.

Earliest tests: PA29 raw and canonical MIR plus PA37 object roundtrip.  If the
student-visible model changes, update the PA13/PA29 scaffold comments and
README language to state the representation students must implement; put
migration history only in this plan.

### Phase 3: remove text ownership from typed LowIR construction

This phase has two ordered sub-slices so name absence cannot affect behavior.

#### Phase 3A: make name-sensitive algorithms explicitly numeric

1. At parse/construction, classify explicit names that collide with generated
   force-inline and O1-inline families into sparse ordinal reservations.
2. Make both inliners allocate semantic value, slot, and block IDs
   monotonically and consult only those numeric reservations.
3. Replace name-based parameter-slot discovery with an explicit
   `ParameterId`/`ValueId` origin recorded by typed lowering.
4. Replace textual canonicalization comparisons with pool-ID or range
   comparisons that do not allocate a prefixed temporary.
5. Prove with object-roundtrip tests that removing local display names cannot
   change optimization, register placement, EH ordering, or object bytes.

#### Phase 3B: pool and transfer typed presentation once

1. Replace PA15 `Block`, `Parameter`, `Slot`, `Symbol`, and alias owning names
   with compact presentation IDs backed by one typed-program pool.
2. Store sigils as field/table kind; pool unprefixed bytes once.
3. Use the same pool for literal presentation instead of a separate literal
   interner where lifetime permits.
4. Change `AdaptTypedLowIRForNative` to a numeric/move adapter.  It must not
   call `Prefix`, `IntegerText`, or reintern a typed-program name.
5. Add `SERIALIZABLE` and `OBJECT_ONLY` presentation policies.  The object path
   omits local names after Phase 3A has made them behavior-independent.
6. Keep the PA15 LowIR writer as a view over the same IDs.

Expected output change: none.  Existing PA15 normalized LowIR, PA29 MIR, and
PA37 direct-versus-serialized object tests are the primary contract gates.

Student-facing PA15/PA29 scaffold and README changes, if required, describe
compact name identity and lazy rendering directly.  They must not discuss the
old implementation or this migration.

### Phase 4: keep typed identity through encoding and ELF layout

1. Add a dense `SymbolId -> encoded location` table to `CodeBuffer`.
   `label(SymbolId)` and typed fixups must never enter `labels_` by spelling.
2. Split arbitrary imported/named labels from generated program symbols.
3. Change `HostFunctionLayout` to carry `SymbolId` and pooled object-name ID.
4. Change exported-symbol facts to carry semantic symbol and pooled object-name
   identity rather than owning internal/object strings.
5. Preserve typed relocation targets through text/data partitioning, LSDA,
   EH-frame generation, COMDAT grouping, and final section layout.
6. Replace weak, TLS, catch-type, required-label, object-only, defined-symbol,
   and generated-section string sets with dense flags or compact ID sets.
7. Use a fixed `SectionKind` for compiler-owned sections and pooled `StringId`
   for arbitrary custom/imported sections.
8. At the final ELF boundary only, render object names, perform any required
   lexical sort, assign string-table offsets, and emit relocation symbol names.

Expected output change: none.  Exact object sections, bindings, COMDAT groups,
relocations, symbols, and bytes must match the phase baseline.

Earliest tests: PA30 for compiler-object linking facts, PA31/PA32 for host ELF
and linking, and the earliest existing EH/TLS/weak/COMDAT owner for each
failure.  Use `make test-report` with all affected PA names to collect the full
failure set before reducing.

### Phase 5: boundary audit and optional explicit-input cleanup

1. Re-run a source-wide audit of `std::string`, `map<string>`,
   `unordered_map<string>`, and `unordered_set<string>` in LowIR/MIR/native
   files.
2. Classify every survivor as parser input, serializer/diagnostic output,
   imported-object joining, or final ELF byte construction.
3. Remove any survivor that consumes a generated `SymbolId`, `BlockId`,
   `ValueId`, `SlotId`, frame identity, or typed literal by first rendering it.
4. If explicit `.lowir` parsing is measurably important outside the frozen
   source benchmark, replace owning lexer token strings with source spans and
   use transient `StringId` resolvers.  Land this separately because it does
   not contribute to the frozen source-path goal.
5. Record all intentionally retained text containers and their lifetime in the
   implementation ledger below.

## 7. Correctness and fixture strategy

Representation-only phases are byte-neutral.  They should not require fixture
updates.  If a fixture changes:

1. stop the phase and identify the earliest assignment that owns the changed
   behavior;
2. use the assignment report target, not `make test`, to collect all related
   failures;
3. reduce the failure at that earliest PA;
4. compare with the checked-in reference and, where relevant, GCC/Clang;
5. add a course regression only for an actual behavioral or public-IR bug; and
6. keep the representation change separate from the correction.

Expected ownership by surface:

| Surface | Earliest primary owner |
| --- | --- |
| Explicit LowIR parsing, literal spelling, compact LowIR identity | PA13 |
| Source-to-LowIR construction and rendering | PA15 |
| LowIR-to-MIR identity, literal payload, frame/block/symbol presentation | PA29 |
| Private object joining and exported-symbol facts | PA30 |
| Host relocatable ELF symbols/relocations/COMDAT | PA31/PA32 and the first specific feature owner |
| LowIR optimization/inliner representation | PA37 |
| Optimized MIR representation | PA38 |

The PA37 object-roundtrip lanes are mandatory.  They prove that direct source
compilation does not rely on hidden presentation absent from serialized LowIR.
O1/O2-specific tests remain in PA38 when the changed contract is backend MIR
optimization, not in PA29.

If a new MIR representation contract needs exact coverage, place the test in
PA29 only when it is required at O0.  PA38 owns optimization-level behavior.
Update student-facing README/scaffold text only with implementer requirements;
record changed fixtures and migration rationale in this plan.

## 8. Performance protocol

### 8.1 Structural acceptance

The completed source-object path must show:

- zero generated-name string maps in LowIR/MIR analysis;
- zero LowIR-to-MIR byte remaps and zero remap-vector storage;
- zero native literal string parses;
- zero semantic string reads for literal type, zero-index, symbol location, or
  generated relocation identity;
- no prefixed local-name or ordinary-integer rendering in object-only
  adaptation;
- dense typed label/fixup counts equal to generated symbol uses; and
- string-keyed ELF entries attributable only to imported names or the final
  string/symbol-table construction boundary.

Adapter-build, presentation-bridge, native-selection, encoding, and object-
writing timers must be non-overlapping so a reduction cannot be hidden by
moving work between reported phases.

### 8.2 Timing acceptance

Use immutable baseline and candidate compiler copies.  The host is
intermittently loaded, so use at least three complete A/B/B/A blocks, reverse
the order within each block, and report medians of paired ratios as well as raw
wall/user/RSS medians.  Reject sessions with known concurrent builds or
sustained host-load discontinuities; retain the raw logs.

The cumulative target is:

- at least a 3% median frozen `-O0` user-time reduction, with 5% as the target;
- no median wall-time regression;
- at least a 50% reduction in combined adapter plus presentation-bridge time;
- no more than 3% peak-RSS growth; and
- exact output for every representation-only phase.

An individual phase may be timing-neutral when it eliminates proven work and
does not regress.  Do not retain a phase with a repeatable user-time regression
above 1% merely because its local microbenchmark improved.  Reprofile after
Phases 2, 3, and 4; do not assume the next item remains important.

### 8.3 Test and inception gates

For each accepted phase:

1. run the narrow owning PA report while iterating;
2. run `make test-report-through-paN` for the latest affected assignment;
3. run the full root `make test-report` and require a complete pass;
4. run the PA39 fatal file audit and require zero fatal findings;
5. commit and push the isolated changeset;
6. perform a clean timed 32-way self build; and
7. clean objects and perform a separate timed 32-way inception compare, with
   exact object and final-binary checks.

Do not build while timing.  Use `test-report` PA arguments for fast complete
failure collection; `make test` is not an acceptable substitute.  Inception is
the expensive final proof, not the iteration signal.  The final milestone also
gets the requested separate timed 8-way inception comparison.

## 9. Risks and controls

| Risk | Control |
| --- | --- |
| Sealed presentation outlives LowIR or is mutated after sharing | Program-level immutable owner, explicit seal point, mutation assertions, returned-MIR lifetime test |
| Raw float bits change sNaN/NaN or f80 output | Parse once with the existing accepted conversion, retain exact raw words, add PA13/PA29 edge fixtures, exact object gate |
| Removing names changes inliner collision choices | Boundary-built numeric reservation facts, exact LowIR/MIR gates, PA37 roundtrip |
| Local-name elision changes EH/COMDAT ordering | Carry an explicit compact presentation-order rank before elision; exact section/object comparison |
| Typed ELF identity cannot represent foreign labels | Separate generated `SymbolId` targets from imported `ExternalNameId`; keep import boundary text |
| Shared pool retains too much memory | Object-only presentation policy, storage counters, 3% RSS guardrail |
| New ID hash sets replace old string hash sets without removing allocation | Prefer vectors/flat tables; record entry count, capacity, and allocation count |
| Large cross-assignment fixture churn hides a semantic change | No fixture updates for representation phases; stop and reduce any change |

## 10. Implementation ledger

### 10.1 Retained text boundaries after Phase 5

The final source audit classifies the remaining string-keyed containers as
explicit boundaries rather than semantic transport:

- `lowir_parse.cpp` and `resolve_lowir_function_operands` use transient text
  indexes only while resolving arbitrary serialized LowIR spellings into
  `BlockId`, `SlotId`, `ValueId`, and `SymbolId`.  The maps do not survive the
  parser or private-object reader boundary.
- `lowir_serialize.cpp`, `mir_model.cpp`, and `lowir_cy86.cpp` read pooled
  spellings only to produce LowIR, MIR, CY86, or diagnostic text.
- `pa30_object.cpp` retains `RenameMap` and the external-name join only while
  combining independently serialized private objects.  Linked LowIR publishes
  compact symbol IDs before optimization resumes.
- `CodeBuffer` retains a named-label/fixup path for arbitrary imported runtime
  and executable labels.  Generated program, object, block, and local labels
  use typed IDs.  The frozen source has zero named labels, 5,440 typed labels,
  32,064 typed fixups, and 1,673 genuinely named/imported fixups.
- The ELF writer retains text maps while deduplicating and lexically ordering
  final ABI names and writing `.symtab`, `.strtab`, and `.shstrtab`.  Section,
  program, object, COMDAT, TLS, EH type-reference, and personality relocation
  targets remain numeric.  Only 11 frozen relocations require the transient
  final name lookup; the final string table itself necessarily contains 6,609
  names.

`LowOperation` has only its enum-kind constructor and typed equality/hash
operations.  `parse_lowir_operation(const std::string&)` is the explicit input
boundary, while `lowir_operation_text` and `operator<<` are explicit output
boundaries.  No construction, conversion, equality, indexing, or concatenation
compatibility with operation text remains.

The optional lexer-span cleanup for explicit `.lowir` input was not performed:
it does not occur on the frozen C++ source path, and the retained parser maps
already die at the correct boundary.

Consequently, the normal source-to-object path has no internal text identity
or text transit for LowIR semantic facts.  Its remaining strings are cold
presentation data or one of the explicit input, output, linking, import, or
ELF boundaries above.

### 10.2 Accepted and rejected experiments

Update one row after every accepted or rejected experiment.

| Phase/commit | Removed work | Structural counters | User/wall/RSS | Output gate | Reports/audit/inception | Decision |
| --- | --- | --- | --- | --- | --- | --- |
| Anchor `9960ff68` | Clean boundary after the paused O0 slice | Phase 0 counters pending | Frozen medians: 4.94 s user, 5.44 s wall, 365,846 KiB RSS | Frozen object 4,415,480 bytes, SHA-256 `25817b506e3444c9a89209ba81c7e5b8a9fee2ecd203a218321953d4aee324d1` | Prior full report/audit/32-way inception clean | Immutable baseline |
| Typed operation boundary `a18641ef` | Removed all LowOperation text equality, construction, conversion, indexing, and concatenation compatibility; PA15 adaptation is a fixed enum translation and only explicit readers/renderers cross text | Source audit finds zero legacy operation-text compatibility uses | 4.92 s user, 5.405 s wall, 365,214 KiB RSS; paired median user ratio 0.996 | Frozen object byte-identical to anchor | 5,204/5,204 report; zero-fatal audit | Retained; structurally removes repeated rendering and comparison without regression |
| Float-compare pressure correction `ee07a5f4` | Spills a Boolean float-comparison result when every managed GPR is live instead of aborting self-host compilation | PA29 reducer exercises two parameter registers plus seven edge-live global loads across a branch | Representation-only: frozen object remains byte-identical | Frozen object 4,415,480 bytes, SHA-256 `25817b506e3444c9a89209ba81c7e5b8a9fee2ecd203a218321953d4aee324d1`; self and inception binaries byte-identical at 16,739,992 bytes, SHA-256 `1de64f5a58eb0425c726aaa0c13c02f82b525a1396b85ec59d4952559ed559b6` | PA29 225/225; through PA29 4,117/4,117; full report 5,205/5,205; zero-fatal audit; clean j32 self 17.87 s wall/236,560 KiB; separate j32 inception 1:49.35 wall/236,060 KiB | Retained; closes the chunk at a clean self-host and inception boundary |
| Force-inline ordered-block correction `6d900738` | Stops cloning unordered lowering artifacts, including empty blocks which made emitted LowIR fail to parse | PA37 object-roundtrip reducer constructs and reads a `stringstream`; direct and serialized-LowIR objects are exact | Not a representation experiment | Frozen object changes from 4,415,480 to 4,415,448 bytes, SHA-256 `d52599359535b175519d1ce1249f2a7eafa443fa1765d1c39d7d38f93716c37f`; measured section payload changes are `.text` -17 bytes net, `.eh_frame` +12, and `.gcc_except_table` -9 | PA15/PA29/PA37 report 431/431; new PA37 roundtrip passes | Retained separately as the prerequisite correctness fix; preserving old generated-name numbering would retain unused hot-path presentation work |
| Phase 0 telemetry `635cfa13` | Adds opt-in structural counts, storage estimates, and non-overlapping timers at the PA15 adapter, LowIR-to-MIR bridge, native semantic reads/literal parses, code-buffer labels/fixups, and final ELF boundary | Frozen source records 85,104 typed name entries; 86,579 prefix and 57,301 integer renders; 72,869 presentation-map calls with 29,686 byte-remapping misses; 38,542 semantic string reads; 3 native literal parses; 4,705 named labels and 1,687 named fixups; 10,345 final ELF strings | Correction-anchored three-block A/B/B/A medians: 4.89 versus 4.93 s user, 5.365 versus 5.415 s wall, and 364,352 versus 364,244 KiB RSS; median within-block user ratio 0.996 | Frozen direct objects and emitted LowIR are exact against a fresh `6d900738` compiler; the object is 4,415,448 bytes with SHA-256 `d52599359535b175519d1ce1249f2a7eafa443fa1765d1c39d7d38f93716c37f`; self/inception binaries are exact at 16,759,240 bytes and SHA-256 `3cbe6373032f19ecf57207beffb13896b5ad5c584986830aad2cc4597b1cb6e6` | Full report 5,206/5,206; file audit has zero fatal findings and 29 pre-existing warnings; clean j32 self 18.09 s wall/247,480 KiB; fresh j32 inception 1:49.81 wall/231,148 KiB with every object and final binary exact | Telemetry is disabled by default and retained as Phase 0 evidence |
| Integral-to-floating fact correction `140e0c34` | Preserves the source integer fact when constant evaluation also records the converted floating value | PA15 reducer covers f32/f64/f80 initialization from integer literals and matches the reference `sitofp` LowIR | Final Phase 1 A/B below includes the correction | Frozen `-O0` object remains byte-identical | PA15 114/114; through PA15 1,164/1,164; focused affected report 851/851; full report 5,208/5,208 | Retained separately as the self-host correctness fix exposed while compiling `lowir_identity.cpp` |
| Phase 1 typed literal payloads `32df889e` | Decodes floating values once, carries typed raw payloads and both `i128` words through LowIR/MIR/native encoding, and omits literal presentation from normal source-object adaptation | Frozen adapter integer renders 57,301 -> 0, literal materializations 57,838 -> 0, pool calls 141,133 -> 83,295, LowIR storage 91,100,470 -> 79,289,529 bytes, and native literal parses 3 -> 0; MIR operand remains 64 bytes | Final three-block A/B/B/A medians: 5.040 vs 4.910 s user, 5.525 vs 5.395 s wall, 366,078 vs 366,912 KiB RSS; paired candidate/baseline ratios are 0.995 user, 0.995 wall, and 1.004 RSS | Frozen `-O0` object is byte-identical at 4,415,448 bytes and SHA-256 `d52599359535b175519d1ce1249f2a7eafa443fa1765d1c39d7d38f93716c37f`; emitted LowIR is exact in a same-source A/B; self and inception binaries are exact at 16,742,920 bytes and SHA-256 `c41884b234b78fb761b08bc8d3cda7ae6732a5e7345796b819f37a4b804f19a5` | PA13 100/100; through PA13 933/933; PA29 226/226; through PA29 4,118/4,118; final full report 5,208/5,208; zero-fatal audit with 29 warnings; clean j32 self 18.46 s wall/223,316 KiB; fresh j32 inception 1:51.55 wall/222,876 KiB with every object and final binary exact | Retained; closes Phase 1 at a clean self-host and inception boundary with measured work removed and no timing regression |
| Phase 2 sealed LowIR/MIR presentation store `dbd1d7f9` | Preserves LowIR `StringId` values through MIR, removes `StringIdentityMap` and its remap vector, shares one sealed immutable spelling store, and represents five backend-created frame names with bounded identities | Frozen remap calls 72,866 -> 0, misses 29,685 -> 0, mapped bytes 1,664,204 -> 0, remap-vector storage 125,936 -> 0, and bridge time 18.71 ms -> 0; incremental MIR peak storage falls 14,671,273 -> 13,290,492 bytes while the 2,850,971-byte spelling backing store is shared rather than counted twice | Three-block A/B/B/A medians: 4.875 vs 4.855 s user, 5.330 vs 5.300 s wall, 364,750 vs 365,336 KiB RSS; paired candidate/baseline deltas are -0.62% user, -0.19% wall, and +0.11% RSS | Frozen `-O0` object is byte-identical at 4,415,448 bytes and SHA-256 `d52599359535b175519d1ce1249f2a7eafa443fa1765d1c39d7d38f93716c37f`; self and inception binaries are exact at 16,777,144 bytes and SHA-256 `3f13cbf8e7965d0ac690f6ca2abaea65a7cf218a4f658e6fdbeaaacd5d8a0588` | PA29 226/226; through PA29 4,119/4,119; PA30/PA37 report 191/191; full report 5,208/5,208; zero-fatal audit with 26 warnings; clean j32 self 18.58 s wall/224,988 KiB; fresh j32 inception 1:49.59 wall/225,268 KiB with every object and final binary exact | Retained; removes the complete byte-remapping bridge without changing serialized MIR or object bytes; student-facing wording stays limited to observable requirements |
| Phase 3 compact presentation and object-only elision `d82de391`..`27973be1` | Carries parameter origins, generated-name reservations, local names, EH order, and generated ELF identities numerically; normal object compilation omits unobserved local spellings and prunes the sealed pool to referenced entries | Prefix renders 86,579 -> 0, adapter pool calls 83,295 -> 0, retained LowIR/MIR strings 31,483 -> 15,491, spelling bytes 2,850,971 -> 1,321,945, native semantic string reads 38,423 -> 9,269, named labels remain only for the 4,705 serialized names among 5,425 typed labels, and named fixups remain 1,687 among 32,050 typed fixups; adapter time falls 119.48 -> 116.65 ms and total adaptation 153.09 -> 149.50 ms | Phase-2/candidate three-block A/B/B/A medians: 4.950 vs 4.930 s user, 5.430 vs 5.415 s wall, and 364,796 vs 364,236 KiB RSS; median within-block user ratio 0.997 | Existing serialized fixtures do not change; the frozen `-O0` object remains byte-identical at 4,415,448 bytes and SHA-256 `d52599359535b175519d1ce1249f2a7eafa443fa1765d1c39d7d38f93716c37f` | PA15/PA29/PA30/PA37 531/531; through PA37 5,179/5,179 before the correction below; final corrected full report and inception gates are recorded below | Retained; removes about half the presentation entries and three quarters of native semantic string reads without a timing or RSS regression |
| Call-result store-address correction `8d69cd66` | Keeps a call result used through a folded `index` as the store address when a non-register store value needs the return register as scratch; the scalar store selects `r11` in that one conflict instead of scanning or materializing names | PA29 LowIR reducer reproduces the invalid `mov rax, 1; store.u8 [rax+40], rax` sequence at `-O0`; corrected MIR uses `r11` for the constant and retains `rax` as the address | Not a representation experiment; the corrected clean j32 self build is 18.35 s wall, 408.10 s user, and 226,892 KiB peak RSS | Frozen object remains byte-identical; corrected self and inception binaries are exact at 16,839,688 bytes with SHA-256 `6c20577d180cf51c0c38428500aa204aa952ba093161299e458b87cf3256e6ee` | PA29 227/227; through PA29 4,120/4,120; full report 5,209/5,209; zero-fatal audit with 29 warnings; fresh j32 inception passes every object and final binary check in 4:09.52 wall/1,902.37 s aggregate user/228,428 KiB peak RSS under a loaded host | Retained as the earliest-owned PA29 correction exposed by the Phase 3 self build; no existing MIR fixture changed |
| Phases 4-5 typed native/ELF identity `1d705a56`..`2df22436` | Keeps generated symbols, object names, block/local labels, fixups, function layouts, weak/COMDAT ownership, TLS wrappers, EH references, and section relocations typed through encoding and ELF layout; removes declaration compatibility maps; final name-only and typed aliases share one host-symbol record | Frozen source: zero presentation maps/bytes, zero native semantic string reads or literal parses, 15,491 pooled strings/1,321,945 spelling bytes, 5,440 typed and zero named labels, 32,064 typed and 1,673 imported named fixups, and only 11 transient ELF name entries; `LowOperation` text-compatibility audit is empty | Direct pinned Phase-0/final A/B/B/A raw medians are 4.880/4.805 s user, 5.345/5.255 s wall, and 364,722/365,162 KiB RSS; paired median deltas are -1.75% user, about -1.6% wall, and about +0.1% RSS | Frozen object remains exact at 4,415,448 bytes with SHA-256 `d52599359535b175519d1ce1249f2a7eafa443fa1765d1c39d7d38f93716c37f`; self and inception binaries are exact at 16,996,936 bytes with SHA-256 `d663e523b725fdfdbf868f1f879e46e812f6d70432b69e4f29b97391b85f0cca` | Affected report 701/701; full report 5,210/5,210; zero-fatal audit with 26 warnings; clean j32 self 18.53 s wall/407.58 s user/226,768 KiB; fresh j32 inception 2:12.91 wall/3,017.79 s user/226,680 KiB; fresh j8 inception 4:01.01 wall/1,841.51 s user/224,912 KiB; every object and final binary exact | Structural plan accepted and cleanly paused.  The measured cumulative user-time gain is real but below the 3% completion threshold, so the timing gate and plan status remain open rather than overstating completion. |

| Typed ELF section identity `ca4f6ff0` | Replaces owning encoded/host section names and per-COMDAT `.text.`/`.rela` concatenation with an 8-byte fixed-kind/pooled-ID identity; removes unused copied section names from section-symbol records; renders each spelling once into `.shstrtab` | Frozen final-ELF peak working storage falls 11,922,980 -> 11,357,435 bytes while 10,099 section names and the 720,702-byte `.shstrtab` remain exact | Direct pinned prior/candidate A/B/B/A medians: 4.840/4.830 s user, 5.330/5.315 s wall, and 364,008/363,442 KiB RSS; median within-block deltas are about -0.62% user, -0.56% wall, and -0.02% RSS | Frozen object and `readelf -SW` are exact; object remains 4,415,448 bytes with SHA-256 `d52599359535b175519d1ce1249f2a7eafa443fa1765d1c39d7d38f93716c37f` | PA30/PA31/PA32/PA37 report 363/363; full report 5,210/5,210; zero-fatal audit with 26 warnings; final self/inception proof deferred to the completed milestone | Retained; completes the explicit Phase 4 `SectionKind | StringId` requirement without a timing or memory regression. This is internal representation only, so no student README or fixture changes are appropriate. |
| Early object-only local-presentation elision `92229d24` | Stops interning parameter and slot display names in typed LowIR when the object path cannot observe them; classifies generated-name collisions once into numeric reservations; avoids rendering generated ordinal names; separates source-name collision tracking, slot/block name generation, and temporary reservations into a typed local-presentation module | Frozen typed-name entries fall 85,104 -> 53,913, typed-name bytes 2,632,617 -> 2,405,361, and typed-program storage 64,124,030 -> 63,032,957 bytes; `pa15_lowering.cpp` falls from 3,061 to 2,930 lines | Direct pinned prior/candidate A/B/B/A medians: 4.815/4.800 s user, 5.290/5.285 s wall, and 363,666/363,748 KiB RSS; median within-block user delta is about -1.03%, wall about -0.56%, and RSS is flat | Frozen object remains exact at 4,415,448 bytes with SHA-256 `d52599359535b175519d1ce1249f2a7eafa443fa1765d1c39d7d38f93716c37f` | PA15/PA18/PA28/PA29/PA30/PA37 report 611/611; full report 5,210/5,210; zero-fatal audit with 29 warnings; final self/inception proof deferred to the completed milestone | Retained; removes presentation before typed-program construction instead of dropping it in the PA30 adapter. Block spellings remain until their EH presentation rank is carried independently. |
| Compact block presentation rank `04510f9f` | Represents object-only generated block names transiently as a pooled prefix ID plus ordinal, derives the lexical EH rank after force-inlining, and then discards the transient keys; PA30 transfers the dense rank without receiving or sorting block-label text | Frozen typed-name entries fall 53,913 -> 19,476 and typed-name bytes 2,405,361 -> 1,729,814; the final dense rank offsets part of that saving, so typed-program storage falls 63,032,957 -> 62,941,899 bytes | Direct pinned prior/candidate A/B/B/A medians: 4.770/4.770 s user, 5.245/5.230 s wall, and 364,652/363,772 KiB RSS; paired ratios are timing-neutral | Frozen object remains exact at 4,415,448 bytes with SHA-256 `d52599359535b175519d1ce1249f2a7eafa443fa1765d1c39d7d38f93716c37f`; serialized PA15 fixtures remain unchanged | PA15/PA18/PA28/PA29/PA30/PA37 report 611/611; full report 5,210/5,210; zero-fatal audit with 29 warnings; final self/inception proof deferred to the completed milestone | Retained; closes the remaining local block-spelling transit without a timing or RSS regression. Exact fixed labels remain pooled once because their bytes define the compact lexical rank. |
| Dense typed values and source floating payloads `6db2a524` | Decodes typed source floating operands and static data once, omits their spelling in object-only typed LowIR, carries a dense temporary limit through lowering and force-inlining, reserves adapter value/argument storage once, and removes repeated maximum-temporary scans; the variable/array/aggregate initialization family moved intact to its existing PA16 component | Frozen typed storage is 62,941,884 bytes; prepared LowIR storage falls 76,921,064 -> 76,012,751 bytes (908,313 bytes); `pa15_lowering.cpp` falls from 2,935 to 2,510 lines while the audit warning count stays unchanged | Direct pinned prior/candidate A/B/B/A medians: 4.750/4.760 s user, 5.220/5.230 s wall, and 364,148/364,746 KiB RSS; paired comparison reports -0.42% user and values are timing-neutral under the 1% rejection threshold | Frozen object remains exact at 4,415,448 bytes with SHA-256 `d52599359535b175519d1ce1249f2a7eafa443fa1765d1c39d7d38f93716c37f` | PA15/PA16/PA18/PA28/PA29/PA30/PA37 report 906/906; full report 5,210/5,210; zero-fatal audit with 29 warnings; final self/inception proof deferred to the completed milestone | Retained; replaces dynamic adapter growth and typed literal transit with compact facts without a compile-time regression. The extraction creates about 490 source lines of headroom and is not a threshold-only split. |
| Rejected object-name substitution (uncommitted after `8f9f0378`) | Tried using an existing ABI `object_name` as the object-only compact LowIR symbol spelling | Retained LowIR strings would fall 15,491 -> 10,560 and prepared LowIR storage 76,012,751 -> 75,690,248 bytes | Stopped at the exact-output gate; timing was not run | Incorrect: constructor/destructor entry identities such as `C1`/`D1` collapsed onto `C2`/`D2`, COMDAT section names changed broadly, and the frozen object grew 4,415,448 -> 4,887,344 bytes with SHA-256 `9afc9e94e5fe4d51e19635b4f196968eb2cd7153d737220646b336c038caa984` | No report or inception run because the earliest exact-object gate failed; source change was reverted before commit | Rejected; in the current ABI model these internal spellings distinguish multiple semantic symbols that intentionally share an external object identity, so they are not removable presentation. |
| Dense typed symbol-name counters `3b28bb62` | Removes the second owning/hash table used only to count proposed symbol spellings; the existing program `StringId` indexes a dense `uint32_t` counter and the final unique spelling is interned once | Frozen typed-program storage falls 62,941,884 -> 61,687,196 bytes (1,254,688 bytes); prepared LowIR remains 76,012,751 bytes | Direct pinned prior/candidate A/B/B/A medians: 4.770/4.760 s user, 5.240/5.220 s wall, and 365,052/365,038 KiB RSS; paired user ratio is exactly neutral and wall/RSS differ by less than 0.3% | Frozen object remains exact at 4,415,448 bytes with SHA-256 `d52599359535b175519d1ce1249f2a7eafa443fa1765d1c39d7d38f93716c37f` | PA15/PA18/PA21/PA29/PA30/PA37 report 715/715; full report 5,210/5,210; zero-fatal audit with 29 warnings; final self/inception proof deferred to the completed milestone | Retained; removes duplicate string ownership and hashing from typed construction with no timing or RSS regression. Serializable symbol spellings and the distinct ABI-internal identities rejected above remain exact. |
| Semantic-ID presentation remapping `8d06b842` | Replaces the PA15 `unordered_map<string,string>` and repeated qualified-name splitting/hash probes with a dense `NameId` replacement table and reusable semantic emission path; class-template scope emission IDs are mapped explicitly | No serialized or typed spelling changes; the transient lowering map no longer owns copied strings or hash nodes | Direct pinned prior/candidate A/B/B/A medians: 4.745/4.750 s user, 5.225/5.230 s wall, and 364,330/364,516 KiB RSS; paired deltas are within 0.3% and timing-neutral | Frozen object remains exact at 4,415,448 bytes with SHA-256 `d52599359535b175519d1ce1249f2a7eafa443fa1765d1c39d7d38f93716c37f`; PA37 serialized LowIR is exact after carrying the distinct class-template scope emission ID | PA37 report 93/93; full report 5,210/5,210; zero-fatal audit with 29 warnings; final self/inception proof deferred to the completed milestone | Retained; removes text identity from a source-lowering hot path without a timing or RSS regression. The report suite caught and fixed the first incomplete semantic-path mapping before commit. |
| Direct single-source semantic-name identity `303fcecb` | Uses the semantic `NameId` directly in emission paths when the invocation has one source; the spelling-based canonical table remains only for the multi-source LowIR merge that actually crosses independent name domains | Stats-enabled frozen typed storage falls 61,785,922 -> 61,538,332 bytes (247,590 bytes); the common one-source path performs no emission-name spelling lookup, hash, or copy | Valid three-block pinned prior/candidate A/B/B/A medians: 4.725/4.720 s user, 5.205/5.180 s wall, and 364,616/364,372 KiB RSS; paired deltas are -0.11% user, -0.48% wall, and +0.16% RSS, so isolated timing is neutral | Frozen object remains exact at 4,415,448 bytes with SHA-256 `d52599359535b175519d1ce1249f2a7eafa443fa1765d1c39d7d38f93716c37f`; single- and multi-source serialized fixtures remain unchanged | Affected report 826/826; full report 5,210/5,210; zero-fatal audit with 26 warnings; final self/inception proof deferred to the completed milestone | Retained; removes the redundant string identity rather than adding a compatibility side cache. Independent source domains still canonicalize by spelling before their typed identities are compared. |
| EH-only block presentation rank `be2ff136` | Discards object-only block presentation after force-inlining without sorting or retaining a dense lexical-rank vector unless the function contains a real host EH region; non-EH native layout uses semantic `BlockId` order | Stats-enabled frozen typed and prepared-LowIR storage each fall by 38,764 bytes; only the functions whose LSDA clause order can observe lexical presentation retain the rank | Three-block pinned prior/candidate A/B/B/A medians: 4.735/4.730 s user, 5.210/5.210 s wall, and 363,950/364,540 KiB RSS; paired deltas are -0.32% user, -0.10% wall, and +0.02% RSS, so isolated timing and memory are neutral | Frozen object remains exact at 4,415,448 bytes with SHA-256 `d52599359535b175519d1ce1249f2a7eafa443fa1765d1c39d7d38f93716c37f`; serialized block labels and ranks remain unchanged | PA15/PA26/PA29/PA30/PA37 report 643/643; full report 5,210/5,210; zero-fatal audit with 26 warnings; final self/inception proof deferred to the completed milestone | Retained; narrows the compact presentation sidecar to its sole object consumer without changing the LowIR/MIR public contract. |
| Direct typed object facts and one-pass adaptation `fb52a5e4` | Publishes canonical address, call-boundary, and object facts directly from typed source data; removes generic fact erasure/reconstruction from the source-object path; adapts instructions and operands in place; folds value identity and optional telemetry into the ordered instruction walk | Two 611,985-operand canonicalization/derivation walks and 30,298 reconstructed call-boundary visits fall to zero; combined adapter/bridge time falls from 153.59 ms in Phase 0 to a 75.89 ms median, a 50.59% reduction; bridge time remains zero and exact LowIR storage remains 76,072,713 bytes | Final three-block pinned Phase-0/candidate A/B/B/A medians are 4.900/4.700 s user, 5.355/5.170 s wall, and 365,046/364,646 KiB RSS; paired deltas are -3.67% user, -3.45% wall, and -0.14% RSS | Frozen object remains exact at 4,415,448 bytes with SHA-256 `d52599359535b175519d1ce1249f2a7eafa443fa1765d1c39d7d38f93716c37f`; final self and inception binaries are exact at 17,049,584 bytes with SHA-256 `26b8fed775ef481de131359b56d663f352a0da4ccfae24d44a5c37974334b620` | PA15/PA26/PA29/PA30/PA37 report 643/643; full report 5,210/5,210; zero-fatal audit with 26 warnings; clean j32 self 19.81 s wall/410.93 s user/221,516 KiB; fresh j32 inception 1:54.03 wall/2,965.06 s user/222,124 KiB; fresh j8 inception 3:59.67 wall/1,832.78 s user/225,756 KiB; every object and final binary exact | Retained; all nine completion conditions pass, including the cumulative timing threshold, so the LowIR-to-MIR presentation-transit plan is complete. |

Rejected experiments remain in the ledger with their patch/commit identifier
and measured reason.  Do not erase negative evidence.

## 11. Completion definition

This plan is complete only when all of the following are true:

1. PA15 typed LowIR does not own repeated local/symbol presentation strings,
   and the source adapter does not prefix or reintern them.
2. Typed integer/floating facts are never recovered from spelling in LowIR
   analysis, MIR lowering, optimization, or encoding.
3. `StringIdentityMap` and the LowIR-to-MIR presentation remap are gone.
4. Normal object compilation does not materialize local LowIR/MIR names that no
   serializer or diagnostic consumes.
5. Generated symbols, labels, fixups, COMDAT ownership, TLS identity, catch
   types, and relocations stay typed until final ELF construction.
6. Every remaining backend string container is documented as an explicit
   input/output boundary and has a bounded lifetime.
7. Serialized LowIR/MIR and representation-only object bytes remain exact.
8. The full test report, fatal file audit, clean self build, and 8-way/32-way
   inception comparisons pass.
9. The frozen `-O0` A/B gate demonstrates the visible cumulative compile-time
   reduction defined above without an RSS regression.

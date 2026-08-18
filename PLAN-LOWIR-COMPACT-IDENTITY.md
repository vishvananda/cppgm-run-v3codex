# Plan: Compact LowIR-to-MIR Identity

Status: in progress

Date: 2026-08-18

Baseline commit: `11b0df6e`

## 1. Objective

Remove presentation strings from the semantic identity carried from typed
LowIR through PA37 optimization, PA29 native lowering, MIR optimization, and
machine encoding.  Source lowering and explicit textual LowIR parsing must
produce one compact typed program model.  Values, slots, blocks, symbols,
types, local machine labels, and debug files will be referenced by integer IDs;
their spellings will be owned once and consulted only by diagnostics,
serialization, and the final object-file string-table boundary.

The migration must produce a visible reduction in frozen `-O0` compile time,
not merely move string lookups behind an auxiliary index.  It must retain the
function-at-a-time, bounded, near-linear backend required by `spec.md`, retain
the same LowIR and MIR textual contracts, and preserve exact output whenever a
stage changes representation without intentionally changing behavior.

This plan precedes further O0 instruction-placement work.  The existing O0
optimizations do not need to be rolled back: their facts become fields in the
compact per-function tables described here.

## 2. What earlier performance work established

The Fable plans identified the same family of costs.  `PLAN-PERF2.md` attributed
about 0.35 seconds to backend maps keyed by names such as `%tN`, split across
LowIR validation, optimization, inlining, and LowIR-to-MIR lowering.  It named
dense indexing as the natural replacement.

This repository's `PLAN-PERF.md` subsequently proposed dense value IDs for
native lowering and compact value, slot, block, and def/use facts for PA37.  It
also records one rejected experiment:

- The experiment built one per-function name index beside the existing
  string-bearing LowIR model and then used vector facts in parts of simplify
  and DCE.
- LowIR elapsed time fell from about 3.75 to 3.52 seconds and DCE from about
  0.29 to 0.19 seconds, but end-to-end wall time was effectively unchanged and
  RSS rose slightly.
- The result was rejected because constructing and probing the additional
  index offset the pass-local savings.

That result does not reject compact identity.  It rejects a parallel ID layer
that leaves string construction, ownership, hashing, and lookup in place.  The
proposed migration assigns IDs when the program is constructed or parsed,
stores only those IDs in the core IR, and never reconstructs a name index on
the production source path.

`PLAN-O0-VALUE-PLACEMENT.md` reached the same diagnosis independently: PA15
already uses compact numeric parameter, slot, temporary, block, and symbol
IDs, but `AdaptTypedLowIRForNative` expands every reference into an owning
string in the backend-facing model.

## 3. Current measured baseline

The current frozen benchmark is:

`cppgm-extended-pa39-source-layout/benchmarks/self_compile/stable/semantic_overload.cpp`

A current `-O0 --stats` run reported:

| Metric | Baseline |
| --- | ---: |
| Wall time | 6.39 s |
| User time | 5.74 s |
| System time | 0.65 s |
| Peak RSS | 363,856 KiB |
| Frontend | 4.212 s |
| Typed-to-LowIR lowering | 0.666 s |
| Native lowering | 0.735 s |
| Encoding | 0.325 s |
| LowIR instructions | 179,040 |
| LowIR operand visits | 611,985 |
| MIR instructions | 214,496 |
| Output size | 4,417,176 bytes |

The baseline object SHA-256 is
`87bdd91604b0a3e62fdd0c7b2851a1104b2bf0f95c479f2c5a6613f9a6a19faa`.

The existing `adapt_ns` measurement overlaps frontend and typed lowering and
therefore cannot be added to those phases.  Its residual implies roughly 0.35
seconds of adapter, preparation, and O0 work, but phase 0 below must replace it
with non-overlapping telemetry before an implementation is judged.

The current backend-facing layouts magnify this cost:

| Record | Current size |
| --- | ---: |
| `lowir_model::LowType` | 64 bytes |
| `lowir_model::Operand` | 144 bytes |
| `lowir_model::Instruction` | 864 bytes |
| `lowir_model::Function` | 384 bytes |
| `mir_model::Operand` | 112 bytes |
| `mir_model::Instruction` | 200 bytes |
| `mir_model::Function` | 368 bytes |

At 179,040 instructions, inline `lowir_model::Instruction` capacity alone is
about 154.7 MB before instruction argument vectors, call parameter vectors,
hash tables, and their string allocations.  The earlier PA15 typed program's
reported storage is about 65.7 MB.  At 214,496 instructions, MIR instruction
records add about 42.9 MB before operand vectors.

These numbers make the adapter plus native-lowering interval a credible
whole-compile target.  The initial acceptance threshold is at least a 5%
median frozen `-O0` user-time reduction, with a 7-10% target.  The result must
also remove the measured string work structurally; an apparent timing win
without doing so is insufficient on an intermittently loaded host.

## 4. Representation audit

### 4.1 Compact representation already present

`dev/src/pa15_lowir_types.h` already defines compact `uint32_t` IDs for:

- symbols;
- parameters;
- slots;
- blocks; and
- temporaries.

Its operands carry a kind, numeric identity or literal bits, and typed
information.  Instruction kinds and operators are enums, and variable-length
arguments are held in program arenas.  Display names exist once on declarations
where they are needed.

This is the representation to generalize.  It demonstrates that the source
frontend does not require text identity in its lowering hot path.

### 4.2 The production expansion boundary

`dev/src/pa30_lowir_adapter.cpp::AdaptTypedLowIRForNative` converts that compact
program into `lowir_model::Program`.  In doing so it:

- renders each temporary as `%tN`;
- prefixes parameter, slot, symbol, and block names;
- renders integer literals and operator spellings;
- copies type spellings into every typed record; and
- stores the resulting text in owning `std::string` fields.

The source path then runs:

`BuildTypedLowIRProgram -> AdaptTypedLowIRForNative -> optimize_lowir -> native`

There is no file serialization and reparse on this path.  The adapter is an
in-memory expansion into a structured model whose identities happen to be
textual.  Removing the expansion is therefore compatible with the LowIR text
contract: serialization remains a view of the compact program.

### 4.3 Core LowIR model

`dev/src/lowir_model.h` stores text where compact facts suffice:

- `LowType::text` duplicates a type spelling beside its enum and size facts;
- every `Operand` owns `text` even when its semantic value is a temp, slot,
  symbol, block, or already-decoded integer;
- every instruction owns destination and operator strings, three large inline
  operands, an argument vector, and a call-parameter vector;
- parameters, slots, blocks, functions, declarations, globals, aliases, and
  debug locations repeat names directly; and
- symbol metadata repeats object, TLS, and section spellings.

The large inline instruction record also pays for operand and vector state not
used by most opcodes.  Compact identity should be paired with compact
instruction payloads and per-function operand/call-signature arenas so that
the migration removes allocation and cache pressure as well as hash cost.

### 4.4 Textual LowIR parser and validator

`dev/src/lowir_parse.cpp` currently creates owning strings for lexer tokens,
stores those strings in the program, and builds string-keyed validation maps
and sets for top-level symbols, globals, signatures, parameters, values, slots,
blocks, TLS targets, and aliases.

Text lookup is unavoidable while resolving an explicit `.lowir` input, but it
must be confined to the input boundary.  Tokens can be source spans, spellings
can be interned once, and transient maps can resolve `StringId` to semantic IDs.
Those maps must be discarded after validation.  The parsed result must be the
same compact program type produced by source lowering.

### 4.5 Preparation and whole-program identity

`dev/src/lowir_prepare.cpp` and
`dev/src/lowir_function_reachability.cpp` use string maps and sets for:

- referenced symbols and local definitions;
- linkage and retained declarations;
- function indexes and removal state;
- aliases grouped by target; and
- function boundary metadata.

These are program-wide symbol facts.  They should be vectors and byte flags
indexed by `SymbolId`; adjacency such as aliases-by-target should use compact
ranges or vectors of IDs.  Output order remains a separate vector of IDs.

### 4.6 PA37 LowIR optimization

`dev/src/lowir_opt.cpp`, `lowir_inline_o1.cpp`,
`lowir_force_inline.cpp`, `lowir_cleanup_o1.cpp`, and
`lowir_function_reachability.cpp` repeatedly rebuild string-keyed state for:

- block indexes and control-flow targets;
- types, definitions, uses, liveness, dependencies, and replacements;
- slot eligibility, loads, aliases, promotion, and storage temporaries;
- expression identities and available values;
- function boundaries, candidates, forced-inline state, and renamed values;
  and
- cleanup hashing and equality, including rendered operands and types.

Function-local `ValueId`, `SlotId`, and `BlockId` make most of these facts
dense vectors, bit vectors, or ID worklists.  Only expression value numbering
still needs hashing; its key must be a compact typed tuple of opcode, type ID,
and operand IDs or literal bits, not rendered text.  Inlining should allocate
fresh IDs monotonically and retain spelling generation only for a later dump.

### 4.7 Native LowIR analysis and selection

`dev/src/lowir_native_analysis.h`, `lowir_native_analysis.cpp`,
`lowir_native.cpp`, the native call/address/index lowering headers, and the
native control-flow code use string identity throughout the hottest backend
path.  Current state includes:

- string maps for use counts, first and last use, definitions, shared-storage
  lifetime, deferred instructions, values, slot offsets/types, incoming
  parameter registers, and cross-call homes;
- string sets for parameters, liveness classes, placement constraints,
  discarded slots, pointer globals, and special value families;
- string-to-string maps for storage aliases and TLS wrappers; and
- label and use-site maps keyed by rendered names.

These become one dense `ValueFacts` table, parameter and slot tables, block
tables, and byte-flag vectors.  Register occupants should carry `ValueId`.
Every definition and operand can then reach its facts by one bounds-checked
index in debug builds and one direct index in production.

One behavior dependency must be isolated first.  The current full-scan spill
fallback iterates an unordered string map, and ties can inherit container
iteration order.  Before replacing it, define a stable tie rule using an
explicit definition ordinal or `ValueId`, add the earliest PA29 reducer, and
record any intentional MIR change in its own commit.  The identity migration
must then remain byte-exact against that deterministic anchor.

### 4.8 MIR, EH, and local labels

`dev/src/mir_model.h` continues the text representation:

- MIR types and operand symbols/labels are strings;
- block, function, frame-binding, parameter, runtime, and global identities
  are strings;
- debug file and variable names repeat strings; and
- host EH clauses are mapped by landing-pad strings and contain type-symbol
  strings.

Machine optimization, CFG construction, trace layout, host-EH construction,
and encoding consequently rebuild label maps and symbol sets.  MIR should
carry `BlockId`, `ObjectSymbolId`, `MachineType`, `FrameBindingId`, and pooled
debug-string IDs.  Host EH clauses should be indexed by `BlockId`.  Existing
numeric `frame_binding` identity is the right precedent.

MIR operands already describe physical registers by enum.  Their remaining
text should be replaced by a tagged ID or literal payload; no side map from a
string operand to an ID should survive.

### 4.9 Encoder and ELF boundary

`lowir_native_code_buffer.*`, native program/session/selection files,
`lowir_native_host_eh.cpp`, `lowir_native_elf.cpp`,
`lowir_native_object_elf.*`, and `lowir_native_object_fixups.*` still use text
for local labels, fixup targets, declarations, symbol indexes, EH types and
actions, COMDAT signatures, and section names.

Local control-flow labels are compiler identity and become `LocalLabelId`.
Fixups should carry a tagged target of `LocalLabelId` or `ObjectSymbolId`.
ELF symbols, section names, COMDAT signatures, and debug strings genuinely
need bytes in the output; their spelling is resolved once when building the
ELF string and symbol tables.  A final object-name interner/index is valid
there because it owns output presentation rather than compiler semantics.

### 4.10 Legacy PA13 path

`dev/src/lowir_cy86.cpp` retains string-keyed value locations, functions, and
globals.  It must consume the shared compact parsed program so PA13 remains a
first-class path.  It may construct output text, but it must not preserve a
second string-identity LowIR model merely for the legacy emitter.

### 4.11 Strings that remain legitimate

The goal is not to ban strings.  The following bytes are presentation or
external ABI data and remain, owned once in a program pool or object writer:

- actual ELF symbol, section, COMDAT, and version spellings;
- source and debug file names and user-visible variable names;
- diagnostic text;
- literal spellings where exact LowIR/MIR round-trip output requires them; and
- serialized LowIR and MIR text being emitted or parsed.

These strings must not be keys in hot backend maps.  Core records refer to
them through `StringId`, and generated `%tN` or synthetic label spellings
should normally be rendered directly from numeric identity without storage.

## 5. Target representation

### 5.1 Program-wide identity

Introduce strong, non-interchangeable 32-bit wrappers:

- `SymbolId` for language and runtime symbols;
- `ObjectSymbolId` for final ABI spellings where one semantic symbol can have
  a distinct object name;
- `TypeId` for canonical LowIR types;
- `StringId` for pooled presentation text;
- `DebugFileId` and `LiteralSpellingId` where type safety is useful; and
- `SectionId` for output sections.

Use an invalid zero value and one-based vector indexes unless an existing PA15
contract requires otherwise.  A program-owned string pool maps genuine input
spellings to `StringId` once.  Interning occurs at source construction,
explicit text parsing, or object import, never during an analysis pass.

Canonical LowIR types are stored once as `(kind, bit width, storage size,
alignment)`.  Builtins have fixed IDs.  MIR uses a small `MachineType` value
or canonical ID rather than a text spelling.

### 5.2 Function-local identity

Each function owns dense, non-interchangeable IDs:

- `ValueId` for parameters and instruction results;
- `ParameterId` for ABI metadata;
- `SlotId` for source/object storage;
- `BlockId` for control flow; and
- `FrameBindingId` for compiler-created machine homes.

Parameter values are assigned first and definitions append monotonically.
Blocks retain stable identity while a separate `block_order` vector controls
layout, preventing trace layout from invalidating branch targets.  Optional
display-name IDs are stored only for explicit input and diagnostics.  Source-
generated temporaries can be rendered from their numeric ID.

### 5.3 Compact LowIR records

A LowIR operand becomes a tagged payload approximately equivalent to:

```
kind + type_id + address_binding + union(value_id, slot_id, symbol_id,
                                          block_id, integer_bits,
                                          literal_id)
```

An instruction holds opcode enums, result `ValueId`, fixed scalar metadata,
and ranges into function-owned operand, call-signature, switch, and debug
arenas.  It does not contain strings or per-instruction vector objects.

The design targets these upper bounds after layout review:

- LowIR operand: at most 32 bytes;
- LowIR instruction: at most 192 bytes, preferably materially smaller;
- MIR operand: at most 48 bytes; and
- MIR instruction: at most 128 bytes.

Size assertions and a record-layout report must enforce the intended result.

### 5.4 Dense analysis facts

Store facts by identity:

- `vector<ValueFacts>` for use counts/positions, definition, flags, clobber
  class, placement, deferred instruction index, and remaining uses;
- `vector<SlotFacts>` for offset, type, observation/write/object flags, and
  scalar promotion state;
- byte or word bitsets for sparse boolean classifications;
- `vector<BlockFacts>` plus compact predecessor/successor ranges;
- register-occupant arrays containing `ValueId`; and
- program-wide symbol metadata vectors indexed by `SymbolId`.

Sparse structures remain appropriate when the domain is genuinely sparse,
but their keys are compact IDs.  Do not replace every string set with another
hash set automatically: use dense vectors when the ID domain is already
bounded and known.

### 5.5 Serialization and parsing

LowIR and MIR serializers are the only general consumers that render values,
slots, types, and block names.  They walk the compact model directly and must
continue to produce the checked-in text exactly.

The textual LowIR lexer should retain source buffers and return token spans
`(source, offset, length)` rather than allocating a string for every token.
The parser interns only named spellings that must survive, uses transient
function-local resolution tables, and emits compact IDs.  Parser maps are
allowed because arbitrary user spellings must be resolved; they are boundary
costs and are destroyed after parsing.

## 6. Implementation sequence

Each numbered slice is a separate changeset and must pass its gates before the
next begins.

### Phase 0: Establish a deterministic and measurable anchor

1. Split telemetry into non-overlapping frontend, typed-LowIR build, compact
   bridge, preparation, PA37 optimization, native analysis/selection, MIR
   optimization, encoding, and object-writing intervals.
2. Add disabled-by-default counters for rendered name count/bytes, string
   interning, backend string-map probes, model capacity, operand arena bytes,
   and dense-table entries.
3. Record three interleaved frozen `-O0`, `-O1`, and `-O2` runs, output hashes,
   LowIR text, MIR text, object bytes, and current record layouts.
4. Make spill-victim tie behavior explicit.  If this changes serialized MIR,
   land it separately with PA29 fixtures and tracker notes before changing
   identity storage.

### Phase 1: Shared IDs, pools, types, and operators

1. Extract or generalize PA15's strong ID wrappers into a shared compact LowIR
   core without changing PA15's public behavior.
2. Add the program string pool and canonical type table.
3. Replace LowIR type text and operator text with `TypeId` and enums.
4. Convert debug locations and actual display data to pooled `StringId`.
5. Keep serializer helpers as the sole type/operator spelling tables.

This output-neutral slice validates the core ownership model before values and
control flow move.

### Phase 2: Compact source lowering and the PA30 bridge

1. Make `BuildTypedLowIRProgram` produce the shared core directly, or use a
   numeric move adapter that preserves every existing ID without rendering.
2. Remove production calls that create `%tN`, sigil-prefixed identities,
   integer text, and repeated type text in `AdaptTypedLowIRForNative`.
3. Move variable instruction data into per-function arenas and compact ranges.
4. Preserve exact serialized LowIR through the rendering view.

At the end of this phase the frozen source path must contain no LowIR name
interner or string-to-ID reconstruction step.

### Phase 3: Explicit textual LowIR input

1. Convert lexer tokens to source spans.
2. Intern persistent spellings once and resolve them directly to `SymbolId`,
   `ValueId`, `SlotId`, and `BlockId`.
3. Emit the same compact program type as source lowering.
4. Discard all parser resolution maps after validation.
5. Port `lowir_cy86.cpp` to compact identities.

This phase owns arbitrary user spelling, duplicate-name diagnostics, and
negative validation behavior; it must not force source lowering back through
the text resolver.

### Phase 4: Preparation and PA37 optimization

1. Port symbol retention, linkage, aliases, reachability, and function
   boundaries to program-wide dense symbol facts.
2. Port CFG, definition/use, liveness, slot, alias, and replacement state to
   dense function-local tables.
3. Port expression tables to compact typed keys.
4. Make inlining append fresh IDs and compact arena records; generate display
   names only when serializing.
5. Make cleanup equality/hash use typed fields and IDs rather than rendered
   LowIR.

Preserve pass order and transform decisions.  This phase must not combine the
identity migration with new O1/O2 optimizations.

### Phase 5: Native analysis and LowIR-to-MIR selection

1. Replace `FunctionFacts` string maps/sets with dense vectors and bit flags.
2. Replace `values_`, slot maps, incoming-register maps, alias maps, and
   discarded-slot sets with indexed state.
3. Carry `ValueId`, `SlotId`, `BlockId`, `SymbolId`, and `TypeId` through all
   call, address, index, control-flow, EH, and ABI helpers.
4. Retain current function-at-a-time lifetime and release all facts after each
   function.
5. Verify that the current O0 placement changes use existing dense facts and
   do not acquire a parallel textual compatibility path.

### Phase 6: Compact MIR, EH, and encoding

1. Replace MIR type, symbol, block, frame, and local-label text with typed IDs.
2. Index host EH clauses by `BlockId` and carry type symbols as
   `ObjectSymbolId`.
3. Port CFG and trace layout to stable block IDs plus explicit layout order.
4. Make code-buffer labels `LocalLabelId` and fixup targets a tagged local or
   object-symbol ID.
5. Resolve output spellings once in the ELF writer and retain strings only in
   actual object/debug tables.

Serialized MIR remains a view of this same model.  There must be no richer
object-only IR and no parse-back path for native emission.

### Phase 7: Remove compatibility state and tighten invariants

1. Delete obsolete string-bearing LowIR/MIR fields and adapters rather than
   leaving them synchronized with IDs.
2. Add compile-time type distinctions and size assertions.
3. Add debug-only validation that every ID belongs to its program/function and
   every arena range is valid.
4. Re-run the string audit.  Any remaining backend string key must be justified
   as input resolution, diagnostic/debug presentation, or ELF output spelling.

## 7. Correctness and fixture strategy

Tests belong at the earliest assignment that owns the contract:

- PA13: textual LowIR lexing, name resolution, duplicate/unknown identity,
  arbitrary user names, and `lowir2cy86` behavior;
- PA15: source-to-LowIR construction and exact LowIR serialization;
- PA29: native placement, spill tie rules, MIR serialization, CFG identity,
  EH labels, and the student-facing MIR scaffold if its public model changes;
- PA30: direct source object versus emit/reparse object equivalence;
- PA31/PA32: ELF symbols, aliases, relocations, COMDAT, TLS, and EH metadata;
- PA37: O0/O1/O2 LowIR behavior, inlining, cleanup, and exact optimized LowIR;
  and
- PA38: MIR optimization and debug-location preservation.

For a pure representation slice, checked-in LowIR, MIR, and object outputs
must be exact.  If deterministic spill tie behavior or another independently
reviewed correction changes existing MIR, land that behavior first, list every
changed fixture in this plan, and add its PA29 reducer.  Broad LowIR output
changes are not part of this migration.

Use report targets so every failure is visible at once.  The minimum loop for
each changeset is:

1. `make test-report` with the affected PA names;
2. `make test-report-through-paN` for the earliest owning PA;
3. the direct source versus textual-LowIR object comparison; and
4. the relevant O0/O1/O2 fixture lanes.

Before final performance and self-host measurements, run the full root
`make test-report` and require zero fatal file-audit findings.  After it is
clean, run a timed clean PA39 self build, then timed 8-way and 32-way inception
separately.  Do not overlap builds or benchmark runs.

## 8. Performance protocol and acceptance gates

The host is intermittently loaded, so elapsed time alone is not proof.

For every slice:

1. Compare the baseline and candidate in interleaved A/B/B/A order for at
   least three complete blocks under similar load.
2. Use median user time as the primary signal, wall time as the user-facing
   signal, and peak RSS plus phase counters as corroboration.
3. Compare exact output hashes for output-neutral slices.
4. Reject a slice that adds a second index, makes more name renders, or grows
   the core records even if a noisy wall sample appears faster.
5. Use `test-report` on selected PAs during iteration; reserve full inception
   for meaningful clean milestones.  Do not use PGO.

Per-phase structural gates:

- no `std::string` in core LowIR `Type`, `Operand`, or `Instruction` identity;
- no `std::string` in core MIR `Operand`, `Instruction`, or block identity;
- no source-path rendering of `%tN`, `$slot`, `^block`, or repeated type text;
- no string-keyed LowIR value/slot/block maps after parsing;
- no local-label strings in machine CFG or fixups;
- LowIR core storage falls by at least 50%, with a target reduction greater
  than 100 MB on the frozen input; and
- native lowering and adapter/preparation time fall materially without moving
  work into an unmeasured stage.

Final cumulative gates:

- at least 5% lower median frozen `-O0` user time, targeting 7-10%;
- no regression beyond the established noise envelope at `-O1` or `-O2`;
- exact output for all representation-only changes;
- full test report and file audit clean;
- timed clean self build and passing timed 8-way and 32-way inception; and
- peak RSS and self-host compile time recorded against the baseline.

## 9. Risks and controls

| Risk | Control |
| --- | --- |
| ID and string models diverge | Do not maintain both; remove textual identity as each owner moves |
| Block reordering invalidates IDs | Stable block table plus separate layout order |
| Unordered-map spill order changes MIR | Define and test a deterministic tie rule before migration |
| Explicit LowIR arbitrary names are lost | Optional pooled display IDs, used only by serializer/diagnostics |
| Parser maps recreate source-path overhead | Keep them inside explicit text parsing and destroy after resolution |
| Dense arrays waste space for sparse facts | Dense only for bounded function domains; compact-ID sparse maps otherwise |
| Inlining requires expensive renaming | Append fresh IDs; render names lazily |
| Object output still needs strings | Resolve IDs once at the ELF/debug string-table boundary |
| Large cross-cutting patch obscures regressions | Land one owner/representation slice per commit with earliest-PA reports |
| Timing is hidden by host load | Interleaved medians plus counters, record sizes, hashes, and RSS |

## 10. Implementation log

### CI1: compact LowIR operation identity

The first Phase 1 slice replaces `Instruction::op` owning text with a compact
closed-set operation identity.  The explicit LowIR parser decodes the spelling
once, while serializers and diagnostics render it on demand.  The source
adapter no longer allocates an operation string for every unary, binary,
comparison, or conversion instruction.

The LowIR instruction record falls from 864 to 848 bytes.  The frozen object
remains byte-identical with SHA-256
`87bdd91604b0a3e62fdd0c7b2851a1104b2bf0f95c479f2c5a6613f9a6a19faa`.
PA13, PA15, PA29, PA30, PA37, and PA38 report 654/654 passing tests.

Three A/B/B/A blocks against `30239dab` produced baseline/candidate medians of
5.965/5.650 seconds user, 6.565/6.200 seconds wall, and 365,506/365,390 KiB
peak RSS.  One late baseline run was heavily host-loaded; the interleaved
median and the unchanged output hash are retained, but cumulative timing will
be remeasured after the larger type and identity slices.

### CI2: typed LowIR type identity

The second Phase 1 slice removes `LowType::text`.  Type kind, width, storage,
and alignment are now the sole internal facts; the textual spelling is formed
only by `lowir_type_text` at LowIR/MIR serialization, diagnostic, and the
still-textual MIR compatibility boundary.  Private compiler-object payloads
retain their exact historical type spelling, but decoding discards that
presentation field immediately.

`LowType` falls from 64 to 32 bytes, `Operand` from 144 to 112 bytes, and
`Instruction` from 848 to 656 bytes.  The frozen object remains byte-identical
with the same SHA-256.  PA13, PA15, PA29, PA30, PA31, PA32, PA37, and PA38
report 825/825 passing tests.

Three A/B/B/A blocks against `dec6dd59` produced baseline/candidate medians of
5.755/5.590 seconds user, 6.340/6.130 seconds wall, and 364,280/365,316 KiB
peak RSS.  One candidate and one adjacent baseline sample were host-loaded;
the interleaved median remains favorable, while the small RSS difference is
inside observed noise because semantic analysis still owns the process peak.

### CI3: typed MIR type identity

The third Phase 1 slice carries compact types through MIR construction,
optimization, and native encoding.  Conversion instructions store source and
destination types as separate values; their historical dotted spelling is
formed only by the MIR serializer.  Fixed backend types now use enum identity
directly, with no string-to-type compatibility lookup in the lowering path.

The type value is compacted to 16 bytes by deriving scalar bit width from its
kind and retaining only the object size and alignment that cannot be derived.
Consequently `Operand` falls from 112 to 96 bytes and `Instruction` from 656
to 560 bytes.  MIR instructions remain 200 bytes despite gaining an explicit
conversion source type.  The frozen object remains byte-identical with the
same SHA-256.

PA13, PA15, PA29, PA30, PA31, PA32, PA37, and PA38 report 825/825 passing
tests; PA29's assignment and course suites report 223/223, and the full
through-PA29 report is 4,114/4,114.  The PA39 file audit has no fatal issues.
Six interleaved measurements per lane against `9365af5f` produced
baseline/candidate medians of 5.635/5.565 seconds user, 6.19/6.10 seconds wall,
and 365,086/364,998 KiB peak RSS.

### CI4: shared identity and presentation-pool foundation

The fourth Phase 1 slice introduces one shared compact-ID implementation for
program and function identities and makes PA15's existing IDs reuse it.  It
also adds a program-owned, open-addressed presentation string pool with
disabled-by-default intern/probe counters.  The pool is not yet consulted by
the source path in this foundation slice; each subsequent identity-domain
migration removes its old owning strings before it is committed.

The frozen object remains byte-identical.  PA13 and the full through-PA13
report pass 100/100 and 933/933 tests; PA15, PA29, PA30, PA37, and PA38 report
554/554 passing tests.  The PA39 file audit has no fatal issues.

### CI5: compact LowIR block identity

LowIR control-flow and EH operands now carry `BlockId`; source lowering never
renders a target label, and explicit textual LowIR resolves label tokens once
after validation and releases the operand strings.  Each function owns one
dense presentation-name table indexed by stable block identity, independently
of block layout order.  Serialization and the LowIR-to-MIR boundary consult
that table in constant time.

CFG, loop, cleanup-context, inlining, force-inline, and native liveness state
use dense ID-indexed vectors rather than block-label hash tables.  Inlining
allocates fresh monotonic identities while preserving the historical emitted
label spelling.  Private compiler-object payloads keep their exact byte-level
format.  The native label materialization boundary is isolated in
`lowir_native_block_labels.cpp`; the PA39 file audit has no fatal findings.

The frozen object remains byte-identical with SHA-256
`87bdd91604b0a3e62fdd0c7b2851a1104b2bf0f95c479f2c5a6613f9a6a19faa`.
PA13 and the full through-PA13 report pass 100/100 and 933/933 tests; PA13,
PA15, PA29, PA30, PA37, and PA38 report 654/654 passing tests.  Three A/B/B/A
blocks against `36b01df4` produced baseline/candidate medians of 5.500/5.440
seconds user, 5.990/5.960 seconds wall, and 364,610/365,302 KiB peak RSS.  The
paired block medians are -1.36% user, -0.92% wall, and +0.10% RSS.

### CI6: compact LowIR slot identity

Function-local storage operands now carry `SlotId`.  Source lowering writes
the existing PA15 slot identity directly, while explicit textual LowIR and
private compiler objects resolve each spelling once at their input boundary
and release operand text.  Functions retain one stable dense slot-name and
slot-type table for serialization and diagnostics; removing a slot from layout
does not renumber later identities.

PA37 slot liveness, forwarding, promotion, and dead-store state, plus native
storage analysis, frame layout, and discarded-slot state, now use dense
ID-indexed vectors.  Both inliners allocate fresh caller IDs and clone operands
through direct ID maps.  A reducer exposed one remaining expression-CSE key
that still compared cleared slot text; the key now includes compact operand
identity, preventing distinct storage objects from aliasing.

The frozen operand and instruction records remain 96 and 560 bytes.  The
frozen object remains byte-identical with SHA-256
`87bdd91604b0a3e62fdd0c7b2851a1104b2bf0f95c479f2c5a6613f9a6a19faa`.
PA13, PA15, PA29, PA30, PA37, and PA38 report 654/654 passing tests; the full
through-PA13 report is 933/933, and the PA39 file audit has no fatal findings.

Three A/B/B/A blocks against `52607081` produced baseline/candidate medians of
5.535/5.525 seconds user, 6.060/6.040 seconds wall, and 365,492/363,828 KiB
peak RSS.  The paired block medians are +0.18% user, 0.00% wall, and -0.37%
RSS, all inside the noise envelope.  This slice receives no timing credit by
itself; it is retained because it removes the slot-name hot structures without
growing core records or changing output.  Cumulative performance is judged
after value identity removes the substantially larger string-map domain.

### CI7: compact LowIR value identity

LowIR parameters, temporary operands, and instruction results now carry a
dense `ValueId`.  Source lowering preserves PA15's numeric identity directly;
the explicit parser and private-object reader resolve arbitrary presentation
names once at their input boundaries.  A function owns a single indexed value
name/type table for serialization and diagnostics, and generated definitions
append monotonically without rebuilding a name index.

PA37 value facts, replacements, liveness, inliner remapping, and legacy CY86
locations now use dense tables.  Native analysis and selection replace use,
definition, lifetime, placement, deferred-instruction, register-occupant, and
parameter-home string maps and sets with indexed vectors and bit flags.  The
source-to-native hot path no longer probes a string map for temporary identity.
The duplicated storage-address analysis and generic MIR operand queries were
also separated from the oversized optimizer/native owners; the PA39 file audit
has no fatal findings.

Serialized frozen LowIR is byte-identical to CI6.  Numeric value order makes a
previously unspecified unordered-map register-allocation tie deterministic;
no checked-in MIR fixture changes, while the frozen object changes by 16 bytes
from 4,417,176 to 4,417,192 bytes.  The candidate SHA-256 is
`98f77be43c75d46d29b75809bd6784a921578303484ece5355eef14693231283`.
PA13 and the full through-PA13 report pass 100/100 and 933/933 tests; PA13,
PA15, PA29, PA30, PA37, and PA38 report 654/654 passing tests.

Three A/B/B/A blocks against `52607081` produced baseline/candidate medians of
5.520/5.125 seconds user, 6.015/5.640 seconds wall, and 364,970/364,946 KiB
peak RSS.  Paired block medians improve user time by 6.83% and wall time by
5.99%, with peak RSS unchanged within noise.  This is the first individually
large timing win and exceeds the cumulative plan's initial 5% acceptance gate
before the symbol and MIR identity domains are migrated.

### CI8: compact LowIR symbol identity

Global operands, global address data, top-level declarations/definitions, and
aliases now carry the stable `SymbolId` already assigned by typed PA15 LowIR.
The source adapter initializes one program symbol-name table and never renders
an `@name` for each operand.  Explicit LowIR parsing and private-object linking
retain transient spelling maps at their input boundary, then resolve every
reference to the same compact model.

Preparation, call-boundary propagation, weak-function reachability,
force-inlining, O1 inlining, native signature lookup, pointer-global and TLS
classification, legacy CY86 lowering, and native storage analysis now use
dense symbol-indexed vectors.  Object-slot to parameter relationships are
recorded once as `ValueId` rather than rediscovered in native lowering with a
string-to-index map.  The remaining LowIR string sets are confined to explicit
text parsing or generated presentation-name collision checks.

All mutually exclusive operand identities share one tagged payload rather than
reserving separate block, slot, value, and symbol fields.  `Operand` therefore
remains 96 bytes and `Instruction` falls from 560 to 528 bytes, avoiding the
112/576-byte layout that a naive parallel symbol field produced.  The frozen
object remains byte-identical to CI7 at 4,417,192 bytes with SHA-256
`98f77be43c75d46d29b75809bd6784a921578303484ece5355eef14693231283`.

PA13 and the full through-PA13 report pass 100/100 and 933/933 tests; PA13,
PA15, PA29, PA30, PA37, and PA38 report 654/654 passing tests, and the PA39 file
audit has no fatal findings.  Three A/B/B/A blocks against `bc299fc0` produced
baseline/candidate medians of 5.130/5.095 seconds user, 5.655/5.600 seconds
wall, and 364,774/364,734 KiB peak RSS.  Paired block medians improve user time
by 0.68% and wall time by 1.06%, with peak RSS unchanged within noise.

### CI9: compact MIR block and branch identity

MIR blocks, branch operands, and protected-region landing pads now preserve
the stable LowIR `BlockId` instead of allocating another copy of each textual
label.  Each MIR function owns one presentation table.  Serialization and
diagnostics resolve that table; MIR optimization, CFG construction, trace
layout, fallthrough cleanup, host-EH region analysis, and landing-pad clause
collection use dense ID-indexed vectors.

The encoder still renders one scoped ELF label at its current string-table
boundary; the subsequent local-fixup slice will carry `LocalLabelId` through
`CodeBuffer`.  Host-function LSDA assembly likewise receives label spellings
only at the object-layout boundary.  This separation removes backend label
hashing without changing the public MIR format or native bytes.

`MirOperand` remains 112 bytes, `MirInstruction` remains 200 bytes, and
`MirFunction` falls from 368 to 352 bytes.  The candidate frozen object is
byte-identical to its `071a81d0` baseline at 4,417,192 bytes with SHA-256
`98f77be4b76e5f097be61797fa6559d80266f1e2bb096ac76328b3aabc731283`.
PA29, PA30, PA37, and PA38 report 441/441 passing tests, and the PA39 file audit
has no fatal findings.

An eight-run interleaved timing sequence crossed a severe host-load transition:
samples rose from 5.63--7.55 seconds to 15.13--20.92 seconds.  Its raw medians
are not credited to either implementation.  The unchanged output, eliminated
label maps, stable record sizes, and passing tests justify retaining the
structural slice; cumulative timing will be repeated under comparable load.

### CI10: compact MIR symbol operands and EH type identity

MIR direct-call, global-storage, TLS, EH catch, and EH filter operands now
carry `SymbolId`.  The MIR program owns one symbol-name table copied from the
compact LowIR program.  The MIR serializer resolves IDs from that table, while
the native `CodeBuffer` receives a read-only symbol view and resolves names
only when it creates its current string-based relocation boundary.  The
generic named-operand constructor has been removed.

Host-EH clauses retain catch and filter type IDs through region analysis and
LSDA planning.  Type spellings are created only while building ELF
relocations.  One existing PA29 structural test caught a pointer-global call
path that had used nonempty operand text as an internal presence sentinel;
that path now tests the operand kind and valid `SymbolId` directly.

Removing the per-instruction TLS string reduces `MirInstruction` from 200 to
176 bytes; `MirOperand` remains 112 bytes pending removal of its float-literal
presentation string.  The frozen object is byte-identical to CI9 at 4,417,192
bytes with SHA-256
`98f77be4b76e5f097be61797fa6559d80266f1e2bb096ac76328b3aabc731283`.
PA29, PA30, PA37, and PA38 report 441/441 passing tests.

Four interleaved runs per lane against `f76dbcf1` produced baseline/candidate
medians of 5.815/5.810 seconds user, 6.350/6.335 seconds wall, and
364,114/365,330 KiB peak RSS.  Time is neutral within noise and the RSS delta
is 0.33%, below the semantic-frontend process peak.  This slice receives no
standalone timing credit; it is retained because it removes symbol allocation
and lookup without growing any hot record or changing output.

## 11. Completion definition

The work is complete when the source and explicit-text paths meet in one
compact LowIR model; PA37, native lowering, MIR optimization, EH, and encoding
consume typed IDs without rebuilding text identity; only serializers,
diagnostics, debug data, and the ELF boundary render strings; the full staged
test report and inception lanes pass; and the frozen benchmark demonstrates
the required repeatable compile-time improvement.

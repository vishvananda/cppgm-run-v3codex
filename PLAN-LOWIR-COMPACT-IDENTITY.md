# Plan: Compact LowIR-to-MIR Identity

Status: complete

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
- MIR operand: 64 bytes, using a cache-line stride; and
- MIR instruction: at most 128 bytes.

The MIR operand's semantic payload naturally fits in 56 bytes after removing
its owning literal string.  Frozen-benchmark measurements showed that
56-byte operand vectors repeatedly straddled cache lines and made native
lowering about 13% slower.  One reserved 64-bit word produces a 64-byte stride
and makes the compact representation faster than the former 112-byte record.
The reserved word is therefore an intentional layout choice, not compatibility
state.

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

### CI11: compact LowIR and MIR floating-literal identity

LowIR floating operands now carry a pooled `StringId`.  Source lowering interns
the spelling while it constructs the operand; the explicit LowIR parser does
the same at its input boundary.  The private compiler-object linker
materializes spellings only while joining independently owned pools, then
interns the merged result.  LowIR serialization, legacy CY86 output, constant
folding, and typed comparisons resolve or compare the compact identity without
restoring per-operand text.

MIR uses its own dense literal identities.  A session-owned direct remap vector
translates a LowIR `StringId` on the first floating-operand use and appends that
one spelling to a shared dense MIR table.  The table is shared with the moved
program shell so function-at-a-time object emission remains valid; it does not
copy or scan the frontend's 113,563-entry general string pool.  MIR dumping and
native encoding are the only consumers that resolve the spelling.

Removing the MIR operand string gives a natural 56-byte record, but controlled
measurements found that layout slowed native lowering from about 1.03 to 1.17
seconds.  Restoring the old 112-byte stride removed the regression, identifying
cache-line straddling rather than literal mapping as the cause.  The final
64-byte layout retains nearly all of the storage reduction and improves native
lowering.  `MirInstruction` remains 176 bytes and `MirFunction` remains 352
bytes.

The frozen object is byte-identical to CI10 at 4,417,192 bytes with SHA-256
`98f77be4b76e5f097be61797fa6559d80266f1e2bb096ac76328b3aabc731283`.
PA13, PA15, PA29, PA30, PA37, and PA38 report 654/654 passing tests, and the
PA39 file audit has no fatal findings.

Three A/B/B/A blocks against `f88b1065` produced baseline/candidate medians of
5.870/5.830 seconds user, 6.415/6.350 seconds wall, and 365,126/365,998 KiB peak
RSS.  Native-lowering medians improve from 1.032 to 0.997 seconds (3.4%); total
user and wall time improve by 0.7% and 1.0%.  The 0.24% RSS difference is below
the semantic-frontend process-peak noise envelope.

### CI12: remove owning text from LowIR operands

LowIR operands no longer contain `std::string`.  Integer and floating literals
carry one pooled spelling identity plus decoded numeric facts; i128 integers
retain their low and high 64-bit words directly.  Source lowering interns each
spelling at construction.  Explicit LowIR parsing interns the token once and
temporarily uses that ID for arbitrary named operands, then replaces it with a
`ValueId`, `SlotId`, `BlockId`, or `SymbolId` during validation/resolution.

Private compiler-object input follows the same boundary rule.  Independent
object pools are remapped directly into the result pool before definitions are
moved, rather than materializing operand strings and then scanning the joined
program to intern them again.  Legacy CY86, LowIR serialization, O1/O2 cleanup
keys, native analysis, wide-integer selection, and global lowering consume the
decoded value or pooled identity without a textual compatibility path.

`LowirOperand` falls from 96 to 64 bytes and `LowirInstruction` from 528 to 432
bytes.  The frozen object is byte-identical to CI11 at 4,417,192 bytes with
SHA-256
`98f77be4b76e5f097be61797fa6559d80266f1e2bb096ac76328b3aabc731283`.
PA13, PA15, PA29, PA30, PA37, and PA38 report 654/654 passing tests.

Three A/B/B/A blocks against `6f8ff0e2` produced baseline/candidate medians of
5.870/5.825 seconds user, 6.360/6.305 seconds wall, and 365,342/365,200 KiB peak
RSS.  Adapter-build time falls from 154.5 to 128.2 ms (17.0%), the enclosing
adapt interval from 198.7 to 166.2 ms (16.4%), and native lowering from 995.5
to 982.1 ms (1.3%).  Total user and wall time improve by 0.8% and 0.9%.

### CI13: pool MIR parameter and frame presentation names

MIR parameter bindings and frame bindings now carry `StringId` rather than an
owning `std::string`.  A lowering session owns one shared MIR `StringPool` for
floating-literal spellings and metadata presentation names.  Parameter and
frame names are interned while their records are created, then resolved only
by MIR serialization.  Native analysis and encoding continue to use parameter
locations, frame offsets, and numeric frame-binding identities directly.

This generalizes the former literal-only table without copying the much larger
frontend pool.  `MirParamBinding` falls from 80 to 48 bytes and
`MirFrameBinding` from 64 to 32 bytes.  The 64-byte `MirOperand` and 176-byte
`MirInstruction` layouts are unchanged.

The frozen object remains 4,417,192 bytes.  Both implementations emitted the
same two pre-existing object variants during the interleaved run; the expected
variant has SHA-256
`98f77be4b76e5f097be61797fa6559d80266f1e2bb096ac76328b3aabc731283`.
PA13, PA15, PA29, PA30, PA37, and PA38 report 654/654 passing tests, and the
PA39 file audit has no fatal findings.

Three A/B/B/A blocks against `32c794e7` produced baseline/candidate medians of
5.715/5.705 seconds user, 6.205/6.200 seconds wall, and 364,318/364,346 KiB
peak RSS.  The paired medians are +0.09% user, 0.00% wall, and +0.18% RSS, all
inside the noise envelope.  This slice receives no standalone timing credit;
it is retained because it halves the two records without adding a hot lookup
or changing their semantic consumers.

### CI14: compact LowIR and MIR debug-file identity

LowIR and MIR debug locations now carry the source-file spelling as a pooled
`StringId`.  Source debug attachment interns the translation-unit path once;
explicit LowIR parsing and private-object reading intern at their input
boundaries.  Serializers and private-object writing resolve the spelling only
when bytes are required.  Private-object joining remaps debug IDs together
with literal spellings before moving functions into the joined pool.

O1 cleanup/resume equality and hashing compare the compact file identity
directly.  LowIR-to-MIR lowering uses the session's direct source-ID remap, so
MIR instruction selection and optimization do not construct or compare file
strings.  `LowirInstruction` falls from 432 to 400 bytes and
`MirInstruction` from 176 to 152 bytes; a native compile without debug data
therefore no longer pays for an empty `std::string` in every instruction.

A debug-bearing private compiler object is byte-identical to the CI13 output,
and linking that object produces a successful executable.  The frozen native
object remains 4,417,192 bytes.  Its `.text` bytes are identical across all
baseline and candidate runs; varying relocation-table order continues to
produce the pre-existing set of whole-object hashes, including the expected
`98f77be4b76e5f097be61797fa6559d80266f1e2bb096ac76328b3aabc731283`.

PA13, PA15, PA29, PA30, PA37, and PA38 report 654/654 passing tests, and the
PA39 file audit has no fatal findings.  Three A/B/B/A blocks against
`1be0413f` produced baseline/candidate medians of 5.745/5.730 seconds user,
6.230/6.230 seconds wall, and 364,194/365,262 KiB peak RSS.  Paired block
medians improve user time by 0.44%, with wall time and RSS neutral.  The
record-size reduction and direct-ID cleanup keys justify retaining the slice;
its small timing change is not counted as a large standalone win.

The separate PA13 debug-info lane still reports nine pre-existing fixture
mismatches for both CI13 and CI14 (O0 MIR/source-layout changes and absent
native DWARF sections).  Candidate outputs were compared directly with CI13
and are unchanged; those behavioral/fixture issues are not hidden by this
representation changeset.

### CI15: direct local-label encoding and fixups

Ordinary MIR branches no longer render `function::block` strings at the native
boundary.  Each function allocates a dense `LocalLabelId` table indexed by its
stable `BlockId`; local branches, local addresses, and encoder-created helper
labels carry that identity into a separate compact fixup stream.  The code
buffer patches these local fixups directly after per-function branch
relaxation, then releases their scope before the next function.

The existing string label map now contains only externally visible symbols and
the host-EH labels whose spelling is required by LSDA/object construction.
Host-EH block presentation labels are emitted once at block binding, while all
machine branches still use the dense local identity.  External symbol
relocations remain in their existing fixup stream for the following slice.

The frozen object is byte-identical to CI14 at 4,417,192 bytes with SHA-256
`98f77be4b76e5f097be61797fa6559d80266f1e2bb096ac76328b3aabc731283`.
PA29, PA30, PA31, PA32, PA37, and PA38 report 612/612 passing tests, and the
PA39 file audit has no fatal findings.

Three A/B/B/A blocks against `61864e08` produced baseline/candidate medians of
5.750/5.720 seconds user, 6.230/6.220 seconds wall, and 364,542/365,832 KiB
peak RSS.  Paired block medians improve user time by 0.35%, with wall and RSS
inside the noise envelope.  Three additional phase samples reduce median
native encoding time from 302.9 to 282.7 ms (6.7%), confirming that the
end-to-end signal is a real backend improvement masked by the much larger
frontend interval.

### CI16: compact symbol fixups through native object emission

Direct calls, global addresses, TLS references, and EH type references now
carry their existing `SymbolId` into `CodeBuffer` and the encoded-section
model.  The encoder no longer resolves a symbol spelling and allocates an
owning string for every fixup.  The object writer builds dense symbol-location,
presence, and external-binding arrays once, patches same-section references by
ID, and resolves a spelling only for the remaining ELF relocation records.
Fixed runtime and object-format names continue to use the named boundary.

The frozen object remains 4,417,192 bytes and deterministic runs retain SHA-256
`98f77be4b76e5f097be61797fa6559d80266f1e2bb096ac76328b3aabc731283`.
PA29, PA30, PA31, PA32, PA37, and PA38 report 612/612 passing tests, and the
PA39 file audit has no fatal findings.

Three A/B/B/A blocks against `b1085835` produced baseline/candidate medians of
5.755/5.715 seconds user, 6.255/6.185 seconds wall, and 365,452/365,108 KiB
peak RSS.  Paired block medians improve user time by 0.61% and wall time by
0.96%, while RSS is unchanged.  Alternating backend-stat samples reduce native
encoding from approximately 282.7 to 266.1 ms (5.9%), localizing the gain to
the intended boundary.  Whole-object hashing still observes the pre-existing
equivalent relocation-order variants; output size, tests, and the expected
deterministic object are unchanged.

### CI17: retain typed operation identity in consumers

PA37 expression keys and definition facts, plus O0 wide-integer and floating
instruction selection, now consume `LowOperation` directly.  They no longer
invoke the operation's presentation conversion merely to compare, reverse, or
hash an opcode.  Expression hashing uses the compact operation value, and
diagnostics remain the only path that renders its spelling.

PA29, PA37, and PA38 report 343/343 passing tests, and the frozen `.text`
section is byte-identical to CI16.  Three A/B/B/A `-O0` blocks produced
baseline/candidate medians of 5.740/5.705 seconds user, 6.230/6.200 seconds
wall, and 363,686/364,098 KiB peak RSS.  Paired medians are effectively
neutral (0.09% user and 0.40% wall improvement; 0.18% RSS increase), so this
slice receives no standalone timing credit.  It is retained because it removes
heap-backed compatibility work from both O0 and optimized paths without
changing output or a hot-record layout.

### CI18: initialize dense host-EH clause storage once

A cumulative comparison exposed a regression that CI9's load-contaminated
timing had hidden.  `collect_host_eh_clauses` cleared and resized its dense
BlockId-indexed clause table inside the outer MIR-block scan because the scan's
closing brace was misplaced.  EH-enabled functions therefore performed a
growing allocation and destruction cycle once per block.  Moving the table
initialization after the scan restores the intended one-allocation-per-function
algorithm without reverting compact block identity.

PA29, PA30, PA31, PA32, PA37, and PA38 report 612/612 passing tests.  The
frozen object's `.text` is unchanged.  Native lowering falls from approximately
970 to 238 ms on direct phase samples.  Three A/B/B/A blocks against the
pre-fix compact compiler produced baseline/candidate medians of 5.785/5.015
seconds user, 6.315/5.530 seconds wall, and 364,866/365,318 KiB peak RSS.  The
paired medians improve user time by 13.2% and wall time by 11.8%; the 0.3% RSS
difference is within the semantic-frontend process-peak envelope.  No fixture
changes are required because the defect affected allocation complexity, not
MIR or object semantics.

### CI19: clean self-host anchor and compact-ID reducers

Before residual identity work, the PA39 tree was removed and rebuilt from
scratch.  That clean build exposed two independent defects that incremental
tests had not exercised.  Both were reduced and fixed at their earliest owned
assignment before accepting a new timing anchor.

- A PA25 local array containing aggregate elements with array members rejected
  a valid copied element as though every element had to be a nested braced
  action list.  `200-local-aggregate-array-copy-and-braces` now covers the
  copy path, and already-lowered copy elements remain unchanged while actual
  braced elements still receive construction actions (`eeae0510`).
- Compact `BlockId` conversion made the O1 batch inliner index original
  landing-pad state with blocks created during inlining.  Generated blocks are
  not original landing pads, so the lookup is now bounded by the original
  table.  `380-inline-generated-block-state` covers the non-leaf-to-leaf
  transition that creates and then revisits those blocks (`cedf4191`).

The full report passes 5,203/5,203 tests and the PA39 file audit has zero fatal
findings.  Under a calm host, the clean 32-way `cppgm++-self` build takes
18.14 seconds wall, 403.43 seconds aggregate user time, and 251,600 KiB peak
RSS.  A separate clean 32-way inception build takes 1:48.99 wall, 2,890.77
seconds aggregate user time, and 240,112 KiB peak RSS.  All 161 inception
objects match; the final self and inception binaries are byte-identical at
16,781,400 bytes with SHA-256
`6645e3a9f7e204c601567a148ca514c50c0aa0c4c654cdb943c06b6dba6f2e7a`.

### CI20: pooled LowIR parameter presentation identity

Function, declaration, and indirect-call signature parameters now carry a
program `StringId` instead of an owning `std::string`.  Source lowering and
explicit LowIR parsing intern each presentation name once.  Private compiler
objects preserve their byte-level format, intern names while reading, and
remap the IDs when object programs are joined.  LowIR serialization and debug
attachment resolve the spelling only when text is required; native lowering
uses the session's direct LowIR-to-MIR string-ID map.

The parameter record falls from 72 to 40 bytes.  The frozen serialized LowIR
contains 10,698 parameter records, including 239 indirect-call signature
records, so this removes at least 342,336 bytes of inline owning-string state
before allocator metadata and avoided copies are counted.  ABI-only fallback
parameters no longer synthesize unused `%argN` presentation strings.

The migration exposed a latent typed-identity error in O0 branch selection.
The selector queried a temporary-value fact through `Operand::value` even
when the operand was an integer literal.  That union member then contained a
`StringId`, so pooling changed whether the numeric bits happened to collide
with a flagged `ValueId`.  The selector now queries value facts only for a
temporary operand.  PA29's behavioral
`literal-branch-identity-isolation` reducer makes the collision deterministic
and verifies the resulting executable, without imposing an unrelated exact
MIR layout on the assignment.

The full report passes 5,204/5,204 tests and the PA39 file audit has zero fatal
findings.  Three A/B/B/A blocks against the immutable CI19 compiler produced
baseline/candidate medians of 4.945/4.955 seconds user, 5.450/5.445 seconds
wall, and 363,252/364,016 KiB peak RSS.  Paired medians are -0.30% user,
-0.27% wall, and +0.20% RSS, all inside the noise envelope, so this slice
receives no standalone timing credit.  The frozen object remains exact at
4,417,192 bytes with SHA-256
`98f77be4b76e5f097be61797fa6559d80266f1e2bb096ac76328b3aabc731283`.

A clean 32-way self build takes 18.06 seconds wall, 401.21 seconds aggregate
user time, and 256,068 KiB peak RSS.  A separate 32-way inception build takes
1:50.52 wall, 2,906.18 seconds aggregate user time, and 237,156 KiB peak RSS.
All 160 inception objects match; the self and inception binaries are
byte-identical at 16,782,616 bytes with SHA-256
`5ff3ea4f850186a60ff8dac197607f32223354b1c2c1a03d2edf849398efd71d`.

### CI21: compact LowIR local presentation identity

Function-local LowIR presentation tables now store compact program identities
instead of owning strings.  Slot and block tables carry `StringId`.  Value
tables carry one four-byte `PresentationName`: its tagged payload is either a
pooled spelling or the ordinal of a generated `%tN` value.  One tag bit records
the explicit debug-copy preservation contract, replacing PA37's former
`%dbg_` prefix tests with a typed fact.  There is no parallel presentation
index and no generated-name side vector.

Source lowering, explicit LowIR parsing, and private compiler-object reading
intern names at their respective construction boundaries.  Private-object
joining remaps pooled IDs directly; the byte-level object format is unchanged.
Serializers render generated values only when text is requested.  Native
lowering carries `PresentationName` directly into MIR parameter and frame
bindings, so a source-generated `%tN` does not need to be constructed and
hashed merely to transit from LowIR to MIR.  Explicit text validation retains
its transient string maps because arbitrary user spellings must be resolved;
those maps are not present on the frozen source path.

The frozen LowIR contains 103,392 function-local values, 20,555 slots, 36,042
block-table entries, and 5,261 function definitions.  Relative to CI20, compact
entries remove 4,893,260 bytes of table payload and removing the generated
ordinal vector removes another 126,264 bytes of `Function` objects, for a
deterministic 5,019,524-byte reduction before allocator overhead.  The
`Function` record falls from 504 to 480 bytes; `PresentationName`, `StringId`,
MIR parameter names, and MIR frame names are each four-byte identities.

The changed lowering initially exposed sparse invalid block-label entries in
an existing PA37 private-object round-trip test.  MIR block-label presentation
is still string-owned in this slice, so the LowIR-to-MIR bridge now resolves
only valid block identities and preserves the existing sparse holes.  The
public LowIR/MIR/object contracts remain exact.  Moving frame finalization into
`lowir_native_frame_layout.cpp` keeps `lowir_native.cpp` below the PA39 file
size limit without changing the hot inline frame-binding allocator.

PA13, PA15, PA29, PA30, PA37, and PA38 report 656/656 passing tests.  The final
full report passes 5,204/5,204 tests and the PA39 file audit has zero fatal
findings.  The frozen object is byte-identical to CI20 at 4,417,192 bytes with
SHA-256
`98f77be4b76e5f097be61797fa6559d80266f1e2bb096ac76328b3aabc731283`.

Three A/B/B/A blocks against the immutable CI20 compiler produced
baseline/candidate medians of 4.970/4.950 seconds user, 5.445/5.415 seconds
wall, and 366,814/365,352 KiB peak RSS.  Paired medians improve user time by
0.30%, wall time by 0.28%, and RSS by 0.49%.  This is directionally consistent
with the structural reduction but remains within the host noise envelope, so
it is not treated as a large standalone timing win.

A clean 32-way self build takes 18.75 seconds wall, 405.09 seconds aggregate
user time, and 245,676 KiB peak RSS.  A separate 32-way inception compare takes
1:49.07 wall, 2,926.92 seconds aggregate user time, and 233,188 KiB peak RSS.
All 162 self/inception objects match byte-for-byte.  The final binaries are
byte-identical at 16,839,296 bytes with SHA-256
`271a7a5e8387cb7789b5bcf9000311ab37e535112b175115e8c938e5ddbb0c52`.

### CI22: compact LowIR top-level symbol presentation

Top-level LowIR declarations and definitions now carry only `SymbolId` as
semantic identity.  The program symbol table maps each ID to one pooled
`StringId`; structured-global address items and object aliases retain a pooled
forward-reference spelling only while explicit text or a private compiler
object is being resolved, then carry `SymbolId`.  The source adapter creates
the resolved form directly.  Serialization, diagnostics, CY86 output, MIR
shell construction, and ELF construction resolve presentation only when they
actually need bytes.

The private compiler-object byte format is unchanged.  Reading and linking no
longer materialize every symbol reference as text and then resolve it again.
Each input is remapped directly into the joined symbol domain.  Both the reader
and linker use dense `StringId`-indexed vectors after interning, rather than a
second string-keyed symbol map; linked symbol spellings are interned before
unrelated object strings so that domain remains dense.  Definition,
declaration, lifecycle, and alias coalescing then use `SymbolId`-indexed facts.

Record sizes fall as follows:

| Record | CI21 | CI22 |
| --- | ---: | ---: |
| `GlobalDeclaration` | 232 | 192 bytes |
| `GlobalDefinition` | 336 | 304 bytes |
| `GlobalDefinition::DataItem` | 160 | 128 bytes |
| `FunctionDeclaration` | 256 | 224 bytes |
| `Function` | 480 | 448 bytes |
| `ObjectAlias` | 72 | 40 bytes |

For the frozen serialized program, the 6,341-entry symbol table saves at least
177,548 bytes, the top-level and alias records save 236,192 bytes, and 31,931
structured-global data items save 1,021,792 bytes.  This is a deterministic
1,435,532-byte model-payload reduction before allocator overhead.  The native
compile prunes 683 functions before adaptation, so its corresponding live
record reduction is at least 1,413,676 bytes.

PA13, PA15, PA29, PA30, PA31, PA32, PA37, and PA38 report 827/827 passing
tests.  The full root report passes 5,204/5,204 tests and the PA39 file audit
has zero fatal findings.  The frozen object remains byte-identical to CI21 at
4,417,192 bytes with SHA-256
`98f77be4b76e5f097be61797fa6559d80266f1e2bb096ac76328b3aabc731283`.

Three A/B/B/A blocks against the immutable CI21 compiler produced
baseline/candidate medians of 4.980/5.025 seconds user, 5.475/5.500 seconds
wall, and 364,136/365,342 KiB peak RSS.  Block directions were mixed and the
1.5% user delta remains within the host noise envelope, so this slice receives
no standalone timing credit.  It removes the duplicated state structurally;
the MIR program shell still recreates a string-owned symbol-name vector and is
tracked as the next O0 boundary.

A clean 32-way self build takes 18.50 seconds wall, 402.81 seconds aggregate
user time, and 237,884 KiB peak RSS.  A separate 32-way inception compare takes
1:49.60 wall, 2,916.78 seconds aggregate user time, and 237,652 KiB peak RSS.
All 162 self/inception objects match byte-for-byte.  The final binaries are
byte-identical at 16,843,872 bytes with SHA-256
`f3a435af3c322738a19c837777479c81429cb41e680bc0237c3878a2c9881b67`.

### CI23: compact forced- and O1-inliner presentation reservations

Forced inlining no longer constructs three string sets containing every value,
slot, and block name in each caller.  It scans only callers that contain an
eligible force-inline call, recognizes exact compiler-generated collision
patterns once, and retains compact occupied ordinals by generated-name role.
Fresh semantic values, slots, and blocks continue to be allocated
monotonically by `ValueId`, `SlotId`, and `BlockId`; text is constructed and
interned once only for the exact LowIR presentation required by dumps.

The O1 inliner likewise drops its value and label string sets.  It retains the
small numeric site-ID reservation set needed when explicit LowIR input mimics
an `__o1inlN__` spelling, and tracks merge-slot collisions by pooled
`StringId`.  Generated-site recognition parses digit spans directly without a
temporary substring.  Existing PA37 fixtures already cover skipping an
occupied generated site and merge-slot collisions, and all exact fixtures are
unchanged.

PA29, PA30, PA37, and PA38 report 443/443 passing tests.  The full root report
passes 5,204/5,204 tests and the PA39 file audit has zero fatal findings.  The
frozen object remains byte-identical to CI22 at 4,417,192 bytes with SHA-256
`98f77be4b76e5f097be61797fa6559d80266f1e2bb096ac76328b3aabc731283`.

Three final A/B/B/A blocks against the immutable CI22 compiler produced
baseline/candidate medians of 4.975/4.990 seconds user, 5.485/5.475 seconds
wall, and 365,840/364,244 KiB peak RSS.  The paired candidate/baseline medians
were 1.0071 for user time, 1.0046 for wall time, and 0.9961 for RSS.  This is a
neutral result within host noise, so the change takes no standalone benchmark
credit.  The frozen O0 input measures only the forced-inliner part; PA37 tests
and the self-host lane also exercise the O1 reservation path.

A clean 32-way self build takes 18.27 seconds wall, 402.26 seconds aggregate
user time, and 254,052 KiB peak RSS.  A separate 32-way inception compare takes
1:49.02 wall, 2,917.42 seconds aggregate user time, and 234,636 KiB peak RSS.
All 162 self/inception objects match byte-for-byte.  The final binaries are
byte-identical at 16,845,168 bytes with SHA-256
`bffbff85d1c7b22a0fde2c0bd389b5ff5f1044ce6c2550a164942df0fd9e7c2a`.

### CI24: preserve semantic and pooled identity through MIR

MIR no longer copies semantic symbol names into globals, functions, aliases,
runtime records, initializer addresses, or data items.  These fields carry the
original dense `SymbolId`; the MIR symbol table carries pooled `StringId`
presentation.  Function block-label tables likewise carry pooled `StringId`
rather than one owning `std::string` per block.  The corresponding per-entry
storage falls from a typical 32-byte string object to a four-byte identity.

The native code buffer now accepts semantic labels and fixups directly and
resolves them through the bound MIR symbol/presentation tables only at the
native label, relocation, LSDA, serialization, or diagnostic boundary.  The
runtime-data lookup moved from the oversized ELF writer to the EH planning
module and returns `SymbolId`.  Host-EH diagnostics still render the original
function and block spellings through a read-only pool reference; no diagnostic
text is retained in analysis state.

PA29, PA30, PA37, and PA38 report 443/443 passing tests.  The final full root
report passes 5,204/5,204 tests and the PA39 file audit has zero fatal
findings.  All MIR fixtures are unchanged.  The frozen object remains
byte-identical to CI23 at 4,417,192 bytes with SHA-256
`98f77be4b76e5f097be61797fa6559d80266f1e2bb096ac76328b3aabc731283`.

Three final A/B/B/A blocks against the immutable CI23 compiler produced
baseline/candidate medians of 5.000/4.980 seconds user, 5.475/5.480 seconds
wall, and 363,996/365,646 KiB peak RSS.  The paired candidate/baseline medians
were 0.9960 for user time, 0.9945 for wall time, and 1.0033 for RSS.  These
sub-percent movements are neutral within host noise, so the phase takes no
standalone benchmark credit.

A clean 32-way self build takes 18.10 seconds wall, 404.05 seconds aggregate
user time, and 233,604 KiB peak RSS.  A separate 32-way inception compare takes
1:52.09 wall, 2,930.44 seconds aggregate user time, and 235,560 KiB peak RSS.
All 162 self/inception objects match byte-for-byte.  The final binaries are
byte-identical at 16,843,400 bytes with SHA-256
`3e55a11e491c27e87d2782ddb15c8f18613ddca80050651984da2c4188e136df`.

### CI25: pool MIR ABI and global-data presentation

MIR object names, section names, alias names, and the literal spellings needed
for floating-point and 128-bit global data now carry pooled `StringId` rather
than owning strings.  Ordinary integer global data retains only its parsed
value because neither MIR serialization nor encoding needs the original
spelling.  Fixed floating and wide-zero spellings are interned once.

Global-data encoding now owns the final identity-to-bytes transition in the
new `lowir_native_global_encoding` module.  This keeps the ELF writer below its
fatal file-size threshold and leaves pooled presentation intact until labels,
relocations, serialized MIR, or actual data bytes require text.  No new name
map or string set was added to analysis or lowering.

PA29, PA30, PA37, and PA38 report 443/443 passing tests.  The full root report
passes 5,204/5,204 tests and the PA39 file audit has zero fatal findings.  All
MIR fixtures are unchanged.  The frozen object remains byte-identical to CI24
at 4,417,192 bytes with SHA-256
`98f77be4b76e5f097be61797fa6559d80266f1e2bb096ac76328b3aabc731283`.

Three A/B/B/A blocks against the immutable CI24 compiler produced
baseline/candidate medians of 5.020/4.960 seconds user, 5.535/5.430 seconds
wall, and 364,846/364,464 KiB peak RSS.  The paired candidate/baseline medians
were 0.9920 for user time, 0.9918 for wall time, and 0.9982 for RSS.  One
candidate run encountered a visible host-load spike; the robust paired median
still trends better, but the sub-percent result takes no standalone timing
credit.

A clean 32-way self build takes 18.41 seconds wall, 407.20 seconds aggregate
user time, and 246,684 KiB peak RSS.  A separate 32-way inception compare takes
1:50.49 wall, 2,946.65 seconds aggregate user time, and 231,844 KiB peak RSS.
All 163 self/inception objects match byte-for-byte.  The final binaries are
byte-identical at 16,852,472 bytes with SHA-256
`8ca0082229994f91b5df089f117e8e7ebc0c23ac7164acb7e947c500d5b4d35c`.

### CI26: keep hosted-EH planning compact through LSDA encoding

Hosted object emission no longer converts landing `BlockId` values into block
names, constructs `function::block` labels, or stores clauses and call-site
handlers in string-keyed maps.  Call sites now retain `BlockId` and
`LocalLabelId`; branch relaxation publishes the final landing offset directly
to LSDA construction.  Clause storage is dense by `BlockId`, with a compact
sorted ID list preserving the previous presentation-name order and therefore
the exact object layout.  Suppressed host-global state is likewise a dense
symbol bitmap rather than a set of rendered symbol names.

The last unused MIR debug-variable spelling is now a pooled `StringId`, and
the fixed native target is an enum instead of an owning string.  Human-facing
EH errors continue to render function and block spellings only on the error
path.  Final ELF symbol, relocation, section, COMDAT, and string-table names
remain text because those bytes are the required object-file boundary.

PA29, PA30, PA37, and PA38 report 443/443 passing tests.  The full root report
passes 5,204/5,204 tests and the PA39 file audit has zero fatal findings.  All
MIR and host-object fixtures are unchanged.  The frozen object remains
byte-identical to CI25 at 4,417,192 bytes with SHA-256
`98f77be4b76e5f097be61797fa6559d80266f1e2bb096ac76328b3aabc731283`.

Three A/B/B/A blocks against the immutable CI25 compiler produced
baseline/candidate medians of 5.045/4.960 seconds user, 5.545/5.440 seconds
wall, and 365,476/365,116 KiB peak RSS.  Every paired block improved: the
candidate/baseline medians were 0.9821 for user time, 0.9810 for wall time,
and 0.9991 for RSS.  The consistent 1.8--1.9% compile-time reduction is
credited to removing the hosted-EH string round trip.

A clean 32-way self build takes 18.15 seconds wall, 403.09 seconds aggregate
user time, and 242,848 KiB peak RSS.  A separate 32-way inception compare takes
1:52.67 wall, 2,943.85 seconds aggregate user time, and 233,176 KiB peak RSS.
All 163 self/inception objects match byte-for-byte.  The final binaries are
byte-identical at 16,829,104 bytes with SHA-256
`a9d4d455c7737eff04a2c828cec97ba69169aabd1b5ba43d8e00d8aaebee3959`.

### CI27: pool the remaining LowIR ABI presentation

The remaining ABI-facing fields in the LowIR model no longer own strings.
Object-symbol, section-segment, section-name, and object-alias presentation
use the program `StringPool`; TLS wrapper metadata carries its target
`SymbolId` directly.  Explicit textual input temporarily retains a pooled TLS
spelling only until whole-program resolution and then invalidates it.  The
source adapter never creates that spelling because the typed program already
has the target identity.

LowIR-to-MIR lowering maps these pooled identities through the session's
existing direct `StringId` remap rather than reinterning bytes.  Call boundary
facts use a narrow adapter which avoids constructing ignored ABI metadata for
every call.  Private-object serialization is byte-compatible: the writer
renders at its output boundary and the reader interns at its input boundary.
Alias joining now uses a dense vector indexed by pooled identity instead of a
string hash map.  `SymbolMetadata` is 40 bytes and `ObjectAlias` is 12 bytes
after the change, down from string-bearing layouts of 152 and 40 bytes.

The affected PA13, PA15, PA29--PA32, PA37, and PA38 report passes 827/827; the
through-PA13 report passes 933/933; the full root report passes 5,204/5,204;
and the PA39 file audit has zero fatal findings.  The frozen object is
byte-identical to CI26 at 4,417,192 bytes with SHA-256
`98f77be4b76e5f097be61797fa6559d80266f1e2bb096ac76328b3aabc731283`.

Three A/B/B/A blocks against the immutable CI26 compiler produced
baseline/candidate medians of 4.950/4.970 seconds user, 5.430/5.465 seconds
wall, and 364,452/365,428 KiB peak RSS.  Paired medians are +0.20% user,
+0.83% wall, and +0.04% RSS, all within the noise envelope.  This slice takes
no timing credit; it is retained because it removes the last owning ABI text
from the LowIR-to-MIR model without adding an index or changing output.

A clean 32-way self build takes 18.52 seconds wall, 405.59 seconds aggregate
user time, and 235,440 KiB peak RSS.  The timed 8-way inception compare takes
3:56.08 wall, 1,806.65 seconds aggregate user time, and 238,996 KiB peak RSS;
the separately cleaned 32-way compare takes 1:49.71 wall, 2,929.98 seconds
aggregate user time, and 232,428 KiB peak RSS.  All 163 inception objects
match.  The self and inception binaries are byte-identical at 16,797,056 bytes
with SHA-256
`0a608ffc47ea0f99a2603fcec7d18ef0858ab64663af519dfcba2cefd51b4a1e`.

## 11. Current residual audit and next slices

The cumulative compact-identity milestone passes the performance gate.  Three
A/B/B/A blocks compare the deterministic plan-start compiler at `30239dab`
with the completed compiler.  Baseline/candidate medians are 5.670/4.910
seconds user, 6.265/5.410 seconds wall, and 364,680/363,770 KiB peak RSS.  The
paired candidate/baseline medians improve user time by 13.60% and wall time by
14.08%, with peak RSS effectively unchanged.  The earlier resumed anchor was
not used for the authoritative result because repeated compiles exposed its
pre-CI7 unordered spill-tie nondeterminism.  Each lane in this final comparison
is internally deterministic.

The plan-start object is 4,417,176 bytes with SHA-256
`87bdd91604b0a3e62fdd0c7b2851a1104b2bf0f95c479f2c5a6613f9a6a19faa`;
the completed object is 4,417,192 bytes with SHA-256
`98f77be4b76e5f097be61797fa6559d80266f1e2bb096ac76328b3aabc731283`.
The 16-byte behavioral difference is the isolated CI7 deterministic spill-tie
correction.  Every later representation-only phase preserved the CI7 bytes.
The latest full root report passes 5,204/5,204 tests and the PA39 file audit
has zero fatal findings.

The current source path has no string-keyed value, slot, block, symbol, or
fixup identity in native analysis and selection.  LowIR types, operations, and
operands are compact; MIR operands, instructions, blocks, debug files, and
fixups carry typed identities.  Explicit LowIR parsing still uses transient
string maps to resolve arbitrary user spellings, and the serializers and ELF
writer still own output text.  Those are intentional boundaries.

The remaining avoidable text is lower-volume than the value/slot/block maps
already removed.  A source-wide field and string-key audit classifies it as
follows:

| Owner | Avoidable representation | Efficient replacement | Benchmark priority |
| --- | --- | --- | --- |
| LowIR top-level model | Completed in CI22: declarations, definitions, global address data, and alias targets carry `SymbolId`; the symbol table carries pooled `StringId` presentation | Keep rendering at serialization, diagnostic, and object-output boundaries; explicit-input forward spellings are transient pooled IDs | Complete |
| LowIR function presentation | Completed in CI21: slots and blocks use pooled IDs; values use one tagged pooled spelling or generated ordinal, with explicit behavior bits | Keep rendering confined to serialization and diagnostics; do not add a parallel name index | Complete for LowIR; MIR block-label presentation remains below |
| LowIR ABI metadata | Completed in CI27: object symbols, section names, and aliases use pooled `StringId`; TLS wrapper relationships use `SymbolId` | Map pooled identities directly into MIR and render only at input, serialization, diagnostic, or object-output boundaries | Complete |
| PA37 inliners | Completed in CI23: semantic allocation is monotonic by compact ID; exact-name collision state uses numeric generated ordinals and pooled slot IDs | Keep the sparse numeric reservation for arbitrary explicit input; do not rebuild full caller string sets | Complete |
| MIR program shell | CI24 completes semantic symbols and pooled block labels; CI25 pools object names, sections, aliases, and required global literals | Keep presentation rendering at MIR/ELF boundaries and parsed ordinary integer values text-free | Complete |
| MIR debug metadata | CI26 pools the remaining debug-variable spelling and replaces the fixed target string with an enum | Render only for future debug output or diagnostics | Complete |
| Native encoding before ELF | CI26 removes host-EH and suppressed-symbol string identity; named `CodeBuffer` fixups and declaration/COMDAT/section maps remain | Keep arbitrary imported-object and final ELF names textual; separately compact fixed compiler-runtime labels only if measurement justifies the added tag domain | Output-boundary residual |
| Explicit `.lowir` parsing and private-object joining | arbitrary input spellings require string maps to diagnose duplicates and unite independently owned programs | Keep transient boundary resolvers, preferably keyed by interned IDs/spans, and destroy them after producing compact identity | Not on frozen source hot path |
| ELF construction | final symbol, COMDAT, section, relocation, and string-table indexes require byte identity | Keep string lookup at this output boundary; do not force it back into MIR or native analysis | Required output work |

String-keyed maps in `lowir_parse.cpp`, the resolution portion of
`lowir_identity.cpp`, and the ELF symbol/string-table builder remain valid.
They resolve input or construct required output bytes and are destroyed with
that boundary.  `lowir_cy86.cpp` may also construct output opcode and address
text, but it must continue consuming compact LowIR identities internally.

The remaining string owners are deliberate boundaries, not another internal
identity layer.  `RelocatableObject`, declaration/COMDAT/section maps, and ELF
symbol indexes join arbitrary independently-produced object spellings or emit
required output bytes.  `CodeBuffer` named labels represent imported/final
object names; semantic symbols and local control-flow labels already use typed
IDs.  Fixed compiler-runtime labels are a bounded residual, but adding another
tag domain is deferred unless measurement shows that output-boundary pocket is
material.  `ir_model::ExportedSymbol` likewise carries final object
presentation prepared for serialization rather than identity that transits
through LowIR analysis or MIR selection.

## 12. Completion definition

The work is complete when the source and explicit-text paths meet in one
compact LowIR model; PA37, native lowering, MIR optimization, EH, and encoding
consume typed IDs without rebuilding text identity; only serializers,
diagnostics, debug data, and the ELF boundary render strings; the full staged
test report and inception lanes pass; and the frozen benchmark demonstrates
the required repeatable compile-time improvement.

# Optimization Path Unification Plan

## Objective and ownership

The ordinary `cppgm++` compile and link driver uses the highest implemented
optimization level when no `-O` option is present. The base unification landed
with level 2; the current PA37 extension raises it to a distinct level 3.
Explicit `-O0` remains the PA29 baseline, but it must travel through the same
LowIR preparation and native pipeline as every other level.

PA37 is the primary owner because it introduced optimization-level plumbing,
the source/LowIR object-equivalence contract, and the shared LowIR optimizer.
The work deliberately places narrower regressions at their earlier owners:
PA30 for compiler-object serialization and linking, and PA38 for machine-level
selection.  PA39 remains the determinism and inception gate rather than the
home for earlier bugs.

This plan applies the default-level change to the ordinary `cppgm++` compile
and link interface used by the frozen compile benchmark.  It does not change
the PA37 rule that standalone `lowiropt` requires an explicit level.  The
legacy no-level `--emit-lowir` view remains a PA15 tool surface; the PA37
object-capable view continues to be selected explicitly with `-O*`/`-g*`.
Neither view is allowed to select a different in-process object compiler path.

## Current-state findings

The current driver has two materially different source-object paths:

| Input and flags | LowIR preparation | LowIR optimization | native optimization |
| --- | --- | --- | --- |
| source, no `-O` | raw PA30 adapter result | skipped | level 0 |
| source, explicit `-O0` | frontend canonicalization plus object normalization | level-0 no-op | level 0 |
| source, `-O1`/`-O2`/`-O3` | frontend canonicalization plus object normalization | requested level | requested level |
| textual `.lowir` | parser normalizes, then driver normalizes again | requested level | requested level |
| compiler `.o` at link | binary payload is read without the source/text preparation path | not rerun | link invocation level |

The split is controlled by `has_optimization_level`, not by the effective
level.  This is why omitted `-O` is a hidden fast path rather than another
spelling of an optimization policy.

`canonicalize_frontend_lowir` currently does four unrelated jobs:

1. Scan every global and instruction operand to discover referenced symbols,
   then prune and deduplicate declarations.
2. Compute first-use order for generated weak functions and corresponding
   alias order.
3. Deep-copy every `Function` into a reordered vector.
4. Convert frontend linkage/object-symbol presentation to the textual LowIR
   convention.

`normalize_lowir_object_model` also combines unrelated jobs:

1. Erase frontend/parser transient operand types and unused instruction fields.
2. Convert EH selector side facts to the serialized form.
3. Derive global-address binding by scanning definitions and then every
   operand.
4. Propagate direct-call boundaries by another full instruction scan.
5. Clear and rebuild the export list.

The canonicalization is not needed because native code generation consumes
LowIR text; it does not.  Source compilation passes an in-memory
`lowir_model::LowirProgram` directly to PA37 and PA38, and `.cppgm_object`
contains a binary serialization of that model.  Canonicalization exists to
satisfy the PA37 boundary: direct source compilation must be reconstructible
from serialized LowIR without private frontend facts, and PA39 requires stable
object bytes.

The current binary boundary is not identical to either the textual boundary or
the live model.  For example, it serializes `Operand::literal_type` even after
canonical normalization has cleared it, but does not serialize
`Operand::address_binding`.  `ReadCompilerObject` does not currently rederive
that omitted binding.  This is evidence that persistent, serialized, and
derived facts need explicit ownership rather than a single catch-all normalize
pass.

There is a second tool-only fork in `run_emit_lowir_mode`: no debug flag at
level zero uses the PA15 typed renderer, while debug or nonzero optimization
uses the PA30 adapter and PA37 model.  This is not on the production
source-to-object path, but tests must ensure it never leaks a second production
implementation back into compile mode.

## Fresh performance evidence

These single-run measurements used the current committed compiler and the
frozen `benchmarks/self_compile/stable/semantic_overload.cpp` in the extended
checkout, with `CPPGM_DRIVER_STATS=1`, `CPPGM_LOWIR_OPT_STATS=1`, and one
compiler process at a time:

| mode | real | user | max RSS | adapter/prep | PA37 optimizer | native lower | PA38 optimizer | encode | object bytes |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| omitted `-O` | 29.82 s | 27.42 s | 1,229,156 KiB | 14.45 s | skipped | 12.47 s | skipped | 1.44 s | 158,883,024 |
| `-O0` | 29.14 s | 26.74 s | 1,263,364 KiB | 14.09 s | 0.001 s | 12.21 s | skipped | 1.38 s | 154,767,624 |
| `-O2` | 33.76 s | 31.59 s | 1,147,944 KiB | 22.09 s | 7.93 s | 9.41 s | 0.21 s | 1.14 s | 123,601,256 |

The earlier 64.75-second explicit-O0 result is not reproducible with the
current binary.  It remains a useful regression witness, but the current data
does not support attributing roughly 35 seconds to canonicalization.  The O0
optimizer is a one-millisecond no-op, and the combined residual for adaptation,
canonicalization, and normalization suggests hundreds of milliseconds of
incremental work.  Phase-specific instrumentation is required before changing
algorithms on that assumption.

The present default-level performance problem is instead PA37 O2.  It visits
3,278,613 instructions for 282,097 input instructions, performs 221,852
rewrites and 18,292 inlines, and takes 7.93 seconds.  Its smaller result saves
about 2.85 seconds in native lowering and encoding, leaving the full O2 compile
about 4.6 seconds slower than O0.  Making O2 the default without improving this
schedule would knowingly make the main benchmark slower.

The checked-in extended frozen LowIR snapshot is not currently usable with
this compiler: validation fails with `call arity mismatch`.  Optimizer-only
profiles must use a version-matched O0 snapshot generated from the same source,
headers, and compiler revision.  The frozen C++ source remains the end-to-end
benchmark authority; an incompatible LowIR snapshot must not be weakened to
make it parse.

## Target architecture

The target production flow is:

```text
source
  -> BuildTypedLowIRProgram exactly once
  -> adapt to canonical, serializable LowIR by construction
  -> optimize LowIR at effective level
  -> finalize only optimizer-dirtied serialized facts
  -> build read-only derived symbol/call indexes
  -> per-function MIR optimization at the same effective level
  -> direct ELF plus canonical binary compiler-object payload
```

Textual LowIR uses the same middle representation but remains an adapter:

```text
LowIR text -> parse and validate -> canonical LowIR -> optimize -> object/native
```

A binary compiler object follows:

```text
binary payload -> decode canonical LowIR -> rehydrate derived indexes -> link/native
```

There is no text render/parse in the source path, no second semantic run, and
no whole-program validator for a program produced in memory.

The key type distinction is conceptual even if introduced incrementally in
C++11:

- **Persistent canonical facts** affect text/object equivalence: top-level
  entries, instruction fields represented by LowIR, stable order, linkage,
  object spelling, EH selector form, and debug locations.
- **Derived facts** are functions of persistent LowIR: definition indexes,
  address binding, direct-call signature/boundary lookup, exports, CFG, and
  def/use indexes.  They are built once at their consumer and are not stamped
  redundantly onto every operand/instruction.
- **Transient facts** belong to a producer or pass: frontend literal types,
  temporary optimizer maps, source byte/token counters, and dirty worklists.
  They are not serialized as if they were semantic LowIR.

Canonicalization must be idempotent, but the ordinary source path should not
need a generic repair pass.  The source adapter emits canonical facts, the text
parser emits canonical facts after validation, the binary reader decodes
canonical facts, and the optimizer preserves those invariants for unchanged
nodes.

## Implementation phases

### Phase 0: reducers and observability before behavior changes

Add low-overhead timers and counters to the existing opt-in telemetry:

- typed-to-native adaptation time;
- referenced-symbol collection and declaration-filter visits;
- function/alias ordering visits, moves, and deep copies;
- transient-field cleanup operand visits;
- address/call/export derivation visits;
- compiler-object serialization bytes, allocations/copies, and elapsed time;
- per-PA37-pass input/output sizes, visits, rewrites, and elapsed time.

Record three warmed runs of omitted `-O`, `-O0`, and `-O2`, including user
time, wall time, RSS, phase counters, output size, and output hash.  Keep the
64.75-second observation in the record as non-reproduced evidence rather than
silently replacing it.

Create minimal failures before modifying their owners:

| Failure/invariant | Earliest owner | Reducer shape |
| --- | --- | --- |
| binary object loses a derived global-address fact | PA30 | compile to `.o`, relink the payload, and inspect/run an imported address use |
| source object differs from text-reconstructed object | PA37 | one source-owned root, weak helpers in first-use order, unused declarations, and an ABI alias |
| omitted `-O` is not the maximum policy | PA37 | byte-compare no flag and `-O3`; also compare direct source with O0-text-to-default object |
| LowIR optimization behavior depends on frontend-only facts | PA37 | run the same small program from source and handwritten/serialized LowIR at each level |
| native level selection differs between driver paths | PA38 | compare structural MIR/behavior for explicit maximum and the chosen default surface |
| a frozen C++ file exposes a language/backend failure | its first language/backend PA | reduce the syntax and semantics there; do not add the first regression only to PA39 |

The test files go under `cppgm.tests/course/paN/`; harness extensions may live
with the owning PA.  Reference files are not regenerated to conceal changed
behavior.

### Phase 1: replace the catch-all normalizer with explicit boundaries

Move production preparation out of `dev/cppgm++.cpp` and parser implementation
details into responsibility-named shared modules under `dev/src/` (and update
the relevant source sets).

Split the API into operations with explicit contracts:

1. Source adaptation produces persistent canonical LowIR.
2. Text parsing validates external input and produces the same persistent
   canonical LowIR.
3. Binary decoding checks the payload version and produces that canonical
   LowIR.
4. A derived-fact/index builder supplies address, call-boundary, export, and
   native symbol queries without changing serialized meaning.
5. The optimizer reports the functions/blocks whose persistent form changed;
   only those nodes receive post-pass finalization.

Remove `has_optimization_level` from pipeline selection.  Explicitness may
remain in option parsing for duplicate-option diagnostics, but only the
effective numeric level may select optimization work.

At the end of this phase, omitted `-O` can temporarily resolve to effective
level zero while the paths are unified.  Omitted `-O` and explicit `-O0` must
then have identical preparation, binary payload, and native output.  This
creates a clean semantic baseline before changing the default.

### Phase 2: make canonical LowIR cheap by construction

Move source-only cleanup to the PA30 adapter instead of repairing its output:

- use typed `Symbol::referenced`, declaration-emitted, and definition-emitted
  facts to retain only required declarations rather than rediscovering every
  reference from string operands;
- emit canonical linkage and redundant object spelling directly in
  `AdaptSymbolFacts`;
- assign stable source/emission ordinals and build the generated-helper
  first-use order while adapting call edges;
- reorder with indices and moves, or emit directly in final order; never
  deep-copy a `Function` graph merely to canonicalize presentation;
- emit aliases in the target's stable order without copying their payloads;
- derive exports only for actual declarations, definitions, and aliases rather
  than first exporting every frontend symbol and then rebuilding the list.

Make optimizer-created instructions canonical at construction.  In particular,
do not persist operand literal types or EH side fields that textual LowIR cannot
represent.  If a pass needs such a fact, retain it in a function-local analysis
indexed by compact value ID.

Replace per-operand address-binding mutation with a program symbol-definition
index queried during selection/relocation formation.  Replace propagated
direct-call boundary copies with a function-signature/boundary index queried by
PA37 and PA38.  These changes both avoid full scans and guarantee that source,
text, and decoded binary inputs see the same facts.

Require work counters to show O(symbols + globals + functions + instructions)
preparation, zero function deep copies, one stable ordering operation, and no
duplicate derivation for already-canonical input.

### Phase 3: reduce the PA37 O2 cost before making it the default

Use the new per-pass telemetry to retain only measured, high-value work.  The
current bounded schedule repeatedly runs simplification, DCE, CFG cleanup, and
slot cleanup over whole functions.  The first optimization candidates are:

- build compact function/block/value/slot IDs once per function instead of
  repeatedly hashing presentation strings;
- build the call graph, no-unwind dependencies, and recursive SCCs once with
  function IDs, updating only callers changed by inlining;
- replace rendered expression keys and owning string maps with structural keys
  over opcode, type ID, and operand ID;
- fuse simplification with def/use worklist DCE so a rewrite enqueues only its
  users and newly dead definitions;
- make CFG cleanup a block worklist and reuse its predecessor/successor index;
- run slot analyses only on functions with eligible non-escaped slots, and
  reuse the address/escape classification across promotion and dead-store
  removal;
- preserve the explicit inline budget, but batch caller rewrites and avoid
  rebuilding name/index state after every inline site;
- release all per-function analysis storage before advancing to the next large
  function.

Every removed or fused scan needs a handcrafted PA37 LowIR reducer proving the
same optimized shape and behavior.  O0 remains a deterministic no-transform
level; PA38 O0 remains the PA29 machine baseline.

The concrete performance target follows from the current measurements.  To
make maximum optimization no slower than today's O0 end-to-end compile, PA37
O2 must fall from 7.93 seconds to approximately 3.3 seconds or less while
retaining its roughly 2.85-second backend saving.  Wall time is a confirmation
metric; instruction visits/count, allocations, and RSS are the primary gates.
If a pass cannot meet its complexity budget, narrow or defer that pass rather
than making an expensive global transform an implicit production prerequisite.

### Phase 4: remove avoidable binary-payload work

The current `.cppgm_object` writer builds an `ostringstream`, copies it to a
`std::string`, then copies it to `vector<unsigned char>` before embedding it in
ELF.  Replace this with a bounds-checked vector/buffer writer that reserves from
measured size and writes the final payload once.

Version the payload if its schema changes.  In that version:

- omit transient operand types and counters from the semantic payload;
- encode only fields meaningful for an operand/instruction kind;
- use a deterministic string/type table where measurements show repeated
  spelling dominates payload size;
- rebuild derived indexes after read rather than serializing duplicated facts;
- keep all behavior-affecting persistent facts that textual LowIR can express
  or derive.

This phase must preserve PA30 same-version compile/link behavior and PA37
direct/text object equality.  Compatibility with an older payload should be
either implemented explicitly or rejected by its version; it must never be
guessed.

### Phase 5: select one effective optimization policy

Introduce one shared driver policy with two independent fields:

- `effective_level`, defaulting to the maximum implemented level (3);
- `was_explicit`, used only for diagnostics/telemetry.

`-O0`, `-O1`, `-O2`, and `-O3` select their matching levels. The ordinary
compile and link paths pass the same effective level to PA37 and PA38. No
production branch may test `was_explicit`.

The default flip lands only after the explicit-O2 performance gate is met.
Then all of these must hold:

```text
cppgm++ -c source.cpp        == cppgm++ -c -O3 source.cpp
emit O0 text -> compile default == direct source compile default
direct default link behavior == separate default compile/link behavior
```

The equality above is byte equality where PA37 already requires it and
behavior/ABI equality where filenames or link layout are legitimately part of
the output.  Linking an object previously compiled at explicit O0 does not
retroactively promise LowIR LTO; the link invocation selects PA38 native work,
while the payload retains the LowIR level chosen when that object was built.

PA39 already supplies explicit `-O3` through `INCEPTION_CC_FLAGS`, so the
default flip does not silently change the inception build recipe.  It does make
the frozen no-flag benchmark exercise the same maximum path that production
users receive.

## Validation and audit gates

Run focused gates after each owner changes:

```sh
make test-pa30
make test-report-through-pa30
make test-pa37
make test-report-through-pa37
make test-pa38
make test-report-through-pa38
```

Run root `make inception` after the complete path and binary-format changes.
The final audit also requires:

- PA37 primary, debuginfo, driver, and object-roundtrip buckets;
- repeated direct/text/binary canonicalization and byte reproducibility;
- PA29 O0 backend replay and PA38 O1/O2 structural/behavior tests;
- PA30 separate, direct-source, and mixed source/object link paths;
- file audit and the full cumulative report through PA38;
- frozen `semantic_overload.cpp` three-run omitted/O0/O2 comparison;
- version-matched optimizer-only LowIR profile;
- PA39 self/inception object and final compiler byte comparison.

The architecture audit should be able to answer yes to each item:

- Is `BuildTypedLowIRProgram` called once per source translation unit?
- Is there exactly one source-to-object LowIR model and no text transport?
- Can every native behavior fact be obtained from persistent LowIR or a
  deterministic derived index?
- Is source-only presentation canonicalization performed by the adapter rather
  than a repair scan?
- Does binary read rehydrate every omitted derived fact?
- Are canonical ordering operations stable and free of deep IR copies?
- Does omitted `-O` have the same effective level and output as explicit
  maximum optimization?
- Does explicit O0 retain the baseline semantics without a slower alternate
  frontend?
- Are ordinary preparation, optimization, machine lowering, and object writing
  O(n) or O(n log n) with counters accounting for the work?
- Are new correctness failures reduced at their earliest assignment owner?

## Completion criteria

This effort is complete when there is no `has_optimization_level`-selected
production path, omitted `-O` is byte-equivalent to explicit maximum
optimization for PA37 object cases, explicit O0 shares the same preparation
pipeline, text and binary inputs reconstruct all required facts, the frozen
default compile is not slower than the current O0 baseline within controlled
measurement noise, RSS does not regress from the current O2 baseline, all
owner-level/cumulative tests pass, and PA39 inception remains byte-identical.

# Plan: Minimize the Public LowIR Contract

Status: complete (L0-L7 landed, audited, gated, timed, and pushed)

Date: 2026-08-27

## Objective

Reduce LowIR to facts and operations that the compiler can justify at its
durable text boundary.  A public LowIR feature stays only when the repository
can name its producer, the downstream obligation or optimization that consumes
it, its earliest student-facing assignment owner, and a structural or
behavioral test for that relationship.

This is not a plan to make LowIR spell LLVM IR.  The LLVM comparison is useful
because it distinguishes semantic facts, lowered graph shape, ABI facts, and
optimizer promises.  LLVM having an equivalent construct is not, by itself, a
reason to add or keep a LowIR construct.  LLVM omitting a source-level label is
also not, by itself, a reason to remove it.

The desired boundary remains:

```text
semantic graph -> typed lowering -> serialized LowIR -> optimizer -> MIR/backend
```

The in-process compiler may avoid a text round trip, but no object-relevant
fact may depend on hidden state that a serialized LowIR consumer cannot
recover.

## Evidence reviewed

The peer investigation is in the `lowir-investigation` worktree at
`5755d549b248227d05ae96df1a7047d8f9493594`.  The relevant uncommitted evidence
files are:

- `REPORT-LOWIR-LLVM-VALIDATION.md`;
- `REPORT-LOWIR-RECENT-ADDITIONS-AUDIT.md`;
- `REPORT-LOWIR-SECTION-PLACEMENT-AUDIT.md`; and
- `doc/lowir-llvm-crosswalk.md`.

That investigation independently lowered the semantic graph to verifier-read
LLVM 21.1 IR, compared 1,090 experimental and 2,664 Clang modules, and found:

- one correctness gap: volatile accesses were not durable in LowIR;
- one durability gap: section placement existed only in the in-memory path;
- no reason to import most Clang attributes, module flags, or metadata;
- a redundant `trivial_lifecycle` source classification;
- an indirect `role=unreachable` call encoding that should be a terminator; and
- a runtime-role family that needs producer/consumer classification rather
  than blanket retention.

The analysis snapshot of `v3opt` is `4060618a`.  Relative to the peer baseline,
the public LowIR changes at that snapshot are exactly:

1. `load volatile` and `store volatile`;
2. `select`; and
3. a clarification that `copyobj` is non-overlapping.

The later optimizer/backend work did not add another public field or
instruction.  It did strengthen the justification for several existing facts:

- `storage=readonly` now drives string-literal and readonly-table folding;
- `projection=array_element` now drives scaled-address and string-table
  transforms;
- `inline_hint=yes` now gates lifetime-bounded O1 load reuse as well as inline
  profitability; and
- call `effects` continue to determine memory barriers in load reuse.

It did not wire `convert_select_diamonds`.  `select` still has no source or
optimizer producer; only its two handwritten feature fixtures produce it.

## Retention rule

A public LowIR fact is retained only when at least one of these obligations is
demonstrated:

1. **Language behavior:** removing the fact loses observable source semantics,
   as with a volatile access.
2. **Durable ABI or object behavior:** a later backend needs a non-derivable
   fact after LowIR serialization, as with object binding, TLS identity, or an
   explicit section name.
3. **Active optimization:** a current producer and current optimizer consumer
   use the fact for a measured or contractually required transform, and the
   consumer cannot safely and cheaply derive it from simpler LowIR.
4. **Committed target portability:** an already-supported target requires the
   distinction.  A hypothetical future target is not enough.

Every retained fact must also have:

- an earliest owning `paN/README.md` description at the level a student can
  implement;
- a structural or behavioral property check in that assignment or the first
  downstream assignment that consumes it; and
- a direct-versus-serialized check when the fact reaches object emission.

The following are not retention arguments:

- Clang emits it;
- LLVM has an opcode or attribute for it;
- a handwritten fixture can spell it;
- it may be useful later;
- it preserves source terminology after the relevant behavior is already
  lowered; or
- removing it changes an exact reference file without changing the documented
  property.

Implementation-only identity, pools, presentation policy, telemetry, and MIR
layout facts are outside this public-contract rule unless they leak into
serialized LowIR.

## Current disposition

### Retain: demonstrated obligations

| Surface | Evidence | Disposition |
| --- | --- | --- |
| `phi` | Slot promotion produces it; CFG validation, inlining, optimization, and native parallel-edge lowering consume it. It is not reconstructible after memory promotion. | Keep. |
| `load/store volatile` | Volatile access is observable behavior. The frontend produces it and DCE, GVN, slot promotion, frame forwarding, MIR, and native lowering honor it. | Keep and finish any separately proven volatile-access coverage gaps; do not broaden bulk operations speculatively. |
| `force_inline`, `inline_hint`, `no_inline` | Distinct source policies with active inliner and O1 load-reuse consumers. Linkage does not recover them. | Keep independently. |
| `storage=readonly` and call `effects` | Active readonly folding and memory-barrier consumers; declaration facts cannot be inferred from bodies that are absent. | Keep. |
| `object_root` | Active reachability/pruning consumer; language-required emission is not use-def reachability. | Keep the fact. |
| `alias=noalias` | Produced for eligible value boundaries and consumed by staged-copy disjointness proof. | Keep with its current scoped meaning. |
| `pass=indirect_result`, `pass=by_address` | Active call ABI/address-materialization consumers; pointer type alone cannot distinguish caller-owned result storage or an addressable-storage boundary. `by_address` is shared by indirect objects, values, and already-lowered source references. | Keep. |
| `projection=array_element`, `projection=field` | Produced by lowering and consumed by current scalar/address/table transforms. | Keep. |
| entry/init/fini, active EH/runtime imports, RTTI roles | Produced by source lowering and consumed by startup, standalone runtime, host object, or RTTI lowering. | Keep only the active values listed in the final role ledger. |
| `section_name` in memory | Produced by the supported GNU section attribute and consumed by ELF placement. | Keep and make durable as `section=`; it is a real current boundary gap. |
| `copyobj` non-overlap rule | It is a semantic precondition on an existing operation and permits the current scalar/vector/native lowering. | Keep the clarification; this added no opcode or field. |
| `!dbg` locations | Produced by source lowering and consumed by line-table object emission and object roundtrip. | Keep. |

### Remove: high-confidence redundancy

| Surface | Current evidence | Planned replacement |
| --- | --- | --- |
| `select` / `IK_SELECT` / `MI_CMOV` vertical slice | The only possible optimizer producer is unwired. The attempted if-conversion regressed the frozen workload by 1.1--1.6%. Only the two select-specific handwritten fixtures exercise it. Branch plus `phi` already expresses the behavior. | Remove the public instruction, dormant converter, dedicated MIR opcode/encoding, stats, docs, grammar, and select-only fixtures. Keep ordinary branch/phi conditional behavior tests. |
| `trivial_lifecycle` | It has 224 checked-in reference occurrences across assignment-local and shared copies, but its only production consumer is `force_inline || (trivial_lifecycle && !no_inline)`. Retention is already independent in `object_root`. | At the semantic-to-LowIR boundary set `force_inline=yes` when the lifecycle helper is trivial and not `no_inline`; preserve `object_root` independently; then remove the field and spelling. |
| `arity=prototype_relaxed` | No source producer, one handwritten smoke input, and no native variadic treatment. The documentation says it exists for a future producer. | Remove the enum, parser/serializer spelling, docs, and smoke fixture. Keep only `fixed` and `variadic`. |
| `section_segment` | No producer and no Linux/ELF consumer. It is merely copied through LowIR, binary object, and MIR records. | Remove the field from every model and internal serialization. Do not add `section_segment=` without a supported target. |
| `pass=reference` | No consumer distinguished the source-reference origin from another `by_address` boundary. Replacing it with an ordinary direct pointer lost required address materialization and regressed same-source O1 throughput, but mapping it to the existing `by_address` fact preserves the required semantics and backend policy. | Render and adapt source-reference boundaries as `pass=by_address`; remove the separate public/model value and reject its old spelling. |
| Explicit conservative/default spellings | `arity=fixed`, `effects=readwrite`, `unwind=may`, `return=returns`, `pass=direct`, `capture=maycapture`, `access=readwrite`, `linkage=cpp`, and `key=no` mean the same thing as omission. Several are never emitted; the rest only exercise transport. | Canonicalize them to omission and remove public spellings or enum states that no active consumer needs. Retain non-default facts such as `readonly`, `noreturn`, `nounwind`, and `nocapture` only when independently justified. |
| `role=unreachable` and synthetic call | The semantic fact is real, but the symbol role and call are indirect. `return=noreturn` cannot distinguish undefined continuation from an observable terminating call. | Add a first-class `unreachable` terminator, lower the builtin directly to it, and remove `SR_UNREACHABLE`, the synthetic declaration, and role-based CFG indexing. |
| `eh_type`, `eh_call_unexpected`, `eh_current_exception_type` roles | No source producer and no behavioral backend consumer. | Remove from the public role set unless the baseline ledger finds a missed assignment-owned producer and property test. |
| `eh_top`, `eh_value`, `eh_unhandled` roles | No source producer or handwritten role input. Only the PA13 CY86 adapter's optional override path reads them; its tested default runtime needs no role. | Remove after a PA13 behavior ablation proves the default EH path. Do not retain an untested customization hook as course syntax. |

### Presumptive removal: ablate one fact at a time

These facts are widely printed, so fixture frequency is high, but static use is
not a justification.  They should be removed unless a focused ablation finds a
non-derivable current obligation.

| Candidate | Static finding | Required keep evidence |
| --- | --- | --- |
| `unary decay ptr` | Produced frequently, but it is an identity operation; O1 removes it and native lowering treats it as a copy. | A current behavior or optimizer consumer that cannot use `copy ptr` or the original address value. |
| `pass=decay` | Two generated-reference families and no decay-specific consumer; generic `passing != direct` tests are not evidence that decay origin is required. | A call-boundary behavior or measured backend decision that cannot be derived from the already-lowered `ptr` type. |
| `capture=nocapture` and parameter `access=*` | Produced for a small builtin boundary set, transported and compared for signature identity, but not consumed by production optimization or native lowering. `maycapture` and `access=readwrite` duplicate conservative defaults. | An active transform or ABI/object consumer. If one is added, retain only the minimal proven values and make the transform's guard property-tested. |
| `projection=base_subobject`, `projection=reference_field` | Produced in many source refs, but no current pass has kind-specific behavior for either; generic expression keys merely preserve the label. | A current alias/layout/optimization decision that cannot use byte offset, type, `field`, or existing object facts. |

The ablation must distinguish a real consumer from an implementation accident.
For example, if removing `pass=reference` changes register placement because a
backend condition tests `passing != direct`, first determine whether that
condition can test the actual pointer/object property.  An avoidable blanket
condition does not justify permanent public syntax.

### Explicitly reject additions from the LLVM census

Do not add target triples, data layout, PIC/PIE policy, unwind-table policy,
compiler identity, module flags, O0 `optnone`, TBAA, loop/profile/sanitizer
metadata, `noundef`, `nonnull`, `dereferenceable`, ranges, `mustprogress`,
`willreturn`, `norecurse`, `nofree`, `nosync`, or `speculatable` to LowIR
without a new producer/precondition/consumer/test case that satisfies the
retention rule.

## Implementation program

Each numbered increment is independently reviewable, tested, committed, and
pushed.  Do not combine an uncertain ablation with a high-confidence removal.

### L0. Freeze the contract ledger and baseline

1. Create a machine-checkable public-surface ledger covering every LowIR type,
   instruction, metadata key/value family, role, and debug field.
2. For each row record:
   - semantic source or handwritten-only status;
   - typed producer;
   - parser and serializer location;
   - optimizer/backend/object consumers;
   - earliest README owner;
   - focused property test; and
   - disposition: `keep`, `remove`, `replace`, or `ablate`.
3. Treat parser/serializer/model references as transport, not consumption.
   Signature equality/hash code also does not count unless the distinction
   changes a documented behavior.
4. Capture baseline results for PA13, PA15, PA29, PA37, PA38, through PA38, the
   PA38 file audit, and 32-way O1 inception.
5. Record direct-versus-serialized object hashes for lifecycle, EH/unreachable,
   section placement, reference/by-address boundaries, and debug locations.
6. Record the current public token census.  The known starting facts include:
   - no source/optimizer-produced `select`;
   - 224 checked-in reference occurrences of `trivial_lifecycle` across
     assignment-local and shared copies, but only one policy consumer;
   - no generated `prototype_relaxed`;
   - no producer or target consumer for `section_segment`;
   - among explicit conservative spellings, only `effects=readwrite`,
     `unwind=may`, and `access=readwrite` occur in checked-in references; the
     other redundant defaults are not compiler-emitted, and none has a
     distinct consumer; and
   - no generated use of the six legacy/unproduced EH role values above.

Exit when every current surface has a ledger row and the baseline is
reproducible.  The ledger is a gate for the following phases, not a reason to
delay obvious focused tests.

### L1. Remove the rejected `select` experiment

1. Remove `IK_SELECT` from `lowir_model` and all parser, serializer, validator,
   optimizer, inliner, DCE, analysis, and native switches.
2. Remove `lowir_select_conversion.{h,cpp}` and its source-set entry.
3. Remove `MI_CMOV`, its text form, use/def description, native encoder, and
   select-only lowering if no other MIR producer exists at implementation time.
4. Remove `select_diamonds_converted` telemetry and documentation.
5. Remove `select` from PA13 grammar, generated grammar pages, README, and
   `lowir.md`.
6. Delete only the select-specific PA37/PA38 fixtures.  Keep or strengthen
   branch-plus-phi behavioral coverage so scalar choice remains implementable
   without exact MIR or complete LowIR matching.
7. Add a PA13 negative syntax check for the removed instruction and positive
   branch/phi controls.

Expected public-output change: none for compiler-produced LowIR, because no
production path emits `select`.

### L2. Remove anticipatory and redundant enum surface

Perform these as separate commits:

1. Remove `prototype_relaxed`; retain fixed-arity validation and variadic
   minimum-prefix validation as positive controls.
2. Remove `section_segment` from typed LowIR, compact LowIR, string remapping,
   MIR, prepared globals, and the internal PA30 object encoder/decoder.  Rebuild
   all test objects; PA30's encoding is internal, not a cross-version artifact.
3. Canonicalize explicit conservative/default metadata to omission one family
   at a time.  Remove redundant parser spellings and enum states only after
   confirming that the omitted form already has the same validation and
   downstream behavior.  Do not combine this with removal of a non-default
   fact from the same family.
4. Remove `eh_type`, `eh_call_unexpected`, and
   `eh_current_exception_type` roles after the ledger confirms zero producer,
   consumer, and assignment test.
5. Ablate the PA13 CY86 override path for `eh_top`, `eh_value`, and
   `eh_unhandled`.  If all EH behavior fixtures continue to use the adapter's
   private defaults, remove those roles too.

Do not replace any removed item with generic “reserved” metadata.  A later
feature may add the minimal fact when its first real producer and consumer
land together.

### L3. Normalize lifecycle policy before LowIR

1. Update the PA16 lifecycle contract to describe the behavior rather than the
   old metadata spelling:
   - eligible trivial lifecycle calls are mandatory inline candidates;
   - `no_inline` takes precedence;
   - object retention is independent.
2. In typed lowering, set `force_inline` for an eligible trivial lifecycle
   helper.  Set `object_root` only for the existing independent emission
   reasons.
3. Remove `trivial_lifecycle` from:
   - PA15 typed symbol rendering;
   - the PA30 adapter and binary object model;
   - compact LowIR metadata;
   - parser and serializer;
   - force-inline candidate discovery;
   - identity/string accounting;
   - PA13 grammar/docs; and
   - optimizer docs that call it a separate root.
4. Do not keep a legacy text alias.  This public spelling is recent, checked-in
   inputs do not use it, and normalization has a complete existing
   replacement.
5. Add property checks that compile source and establish:
   - a trivial helper receives mandatory inline policy;
   - a `noinline` trivial helper does not;
   - an independently required base/complete entry remains an object root;
   - ordinary unreferenced weak helpers remain prunable; and
   - direct and serialized object paths agree at O0--O3.

Expected source-to-LowIR fixture churn: removal of `trivial_lifecycle=yes` and,
where policy is active, appearance of `force_inline=yes`.  Regenerate affected
fixtures through documented reference targets only after the structural and
behavioral properties pass.

### L4. Replace the unreachable role/call with a terminator

1. Add an `unreachable` terminator to PA13 grammar, parser, serializer, typed
   models, and structural validation.
2. Require it to end a block and reject operands or following instructions.
3. Lower `__builtin_unreachable()` directly to that terminator.  Do not emit a
   synthetic declaration or call.
4. Replace the `UnreachableRoleIndex` edge recognizer with ordinary CFG
   reasoning over successor blocks whose terminator is `unreachable`.
5. Let noreturn-call continuation cleanup remain based on `return=noreturn`;
   do not conflate an observable noreturn call with undefined control flow.
6. Teach CY86, native lowering, MIR construction, inlining, CFG cleanup, and EH
   validation the terminator semantics.  The release/native policy may emit no
   bytes for the impossible continuation; a diagnostic trap is a separate
   mode, not LowIR semantics.
7. Remove `SR_UNREACHABLE`, runtime-role mapping, stats names tied to the role,
   synthetic builtin identity, and role-specific tests/docs.
8. Add student-facing property tests:
   - source builtin -> terminator with no synthetic call/declaration;
   - handwritten terminator parse/O0 roundtrip;
   - invalid instruction after terminator;
   - conditional edge cleanup with a normal guarded twin;
   - inlining/phi repair around an impossible edge; and
   - direct versus serialized native/object behavior.

This is the only phase that replaces one removed public encoding with a new
public operation.  The new operation is justified by non-derivable control-flow
semantics and has direct producers and consumers.

### L5. Run source-origin ablations

Use one detached candidate worktree and one commit per candidate family.  Never
edit source fixtures to make an ablation pass.

For each candidate:

1. Normalize the fact at the typed-to-LowIR boundary using existing LowIR:
   - `unary decay ptr` -> the original pointer or `copy ptr`;
   - `pass=decay` -> direct `ptr`;
   - `pass=reference` -> direct `ptr` only if no addressable-storage behavior
     is required, otherwise the already-retained `pass=by_address` fact;
   - unused capture/access values -> omission/conservative defaults; and
   - unconsumed projection kinds -> unannotated `index` or the minimal active
     projection.
2. Replace downstream blanket enum tests with the actual property they need,
   such as object value versus pointer value, indirect result, by-address
   boundary, noalias, pointer type, or known layout.
3. Run the earliest source-to-LowIR property lane, PA29 native behavior, PA37
   optimization, PA37 object roundtrip, PA38 behavior, and restored-self
   inception.
4. Inspect differences by relationship:
   - behavior and exit status;
   - symbol/relocation/section/debug facts;
   - LowIR well-formedness and required operation relationships;
   - MIR/native instruction class when a backend claim exists; and
   - end-to-end O1 timing only when performance is offered as retention
     evidence.
5. Remove the fact if behavior and required object properties remain correct.
6. Keep it only after recording the exact non-derivable consumer in the ledger,
   documenting it in the owning README, and adding a focused property test.

Run candidates in this order, from least to most entangled:

1. any conservative/default states not removed in L2, then `access=none`;
2. `unary decay ptr` and `pass=decay`;
3. the remaining capture/access family;
4. `projection=base_subobject` and `projection=reference_field`; and
5. `pass=reference`, first against ordinary `ptr` and then against the retained
   `by_address` boundary if direct passing loses address semantics.

Do not use fixture count as a keep criterion.  Conversely, do not remove
`projection=array_element`, `projection=field`, `alias=noalias`,
`pass=by_address`, or `pass=indirect_result` in this phase: they already have
active, distinct consumers.

The final `pass=reference` ablation resolved the distinction. Replacing it
with ordinary direct passing remained correct across the full 5,460-test gate
and inception, but an adjacent same-host all-32 rotation worsened aggregate
self/GCC CPU ratio from `915.29 / 585.88 = 1.562x` to
`920.12 / 583.93 = 1.576x` (+0.87%). More importantly, that form required a
special native layout exception to recover the address of a scalar/register
temporary. Mapping source references to the existing public
`pass=by_address` boundary preserves that addressable-storage obligation
without a separate public source-origin enum or spelling. Keep the private
typed-source `CALL_PASS_REFERENCE` tag: collapsing that tag as well perturbed
self-generated code enough to worsen the repeated aggregate self/GCC ratio
from `1.5585x` to `1.5711x` (+0.81%), despite a small GCC improvement. With
the private tag retained and normalized through explicit boundary cases, two
adjacent self samples averaged 922.25 CPU-seconds versus 921.09 for the parent
(+0.13%, within noise); GCC candidate samples repeated at 587.08 versus the
587.54 established parent mean (-0.08%). PA29's focused control checks the
storage, address, call-use, and execution relationships without fixing a
register, frame offset, or complete MIR dump.

### L6. Make the justified section fact durable

This phase prevents minimization from confusing “not public yet” with
“unnecessary.”

1. Add one global metadata key, `section=<name>`, backed by the existing
   `section_name` field.
2. Limit the first contract to the token-safe ELF section names supported and
   tested by the Linux x86_64 course target.  Document that limit.  Do not add
   generic quoted metadata, a segment key, or function-section support in this
   cleanup program.
3. Parse, serialize, validate declaration/definition consistency, and preserve
   the key through O0--O3.
4. Add the existing PA32 GNU section reducer to PA37 object roundtrip at all
   optimization levels.  Check section and relocation relationships with
   object inspection in addition to direct/replayed object equality.
5. Reject the key on unsupported top-level kinds.

If the current source frontend accepts a section spelling outside the chosen
token-safe LowIR subset, either reject that spelling at its owning GNU
extension boundary with a documented test or separately justify a robust
string encoding.  Do not silently lose it during serialization.

The retained contract uses nonempty ASCII alphanumeric/underscore/dot names.
PA13 checks global-only parsing, rejection, and O0--O3 preservation; PA32
checks the valid source producer plus unsafe-name and conflicting-redeclaration
rejection. PA37 reuses the GNU section reducer at every optimization level and
checks that both direct and replayed objects place `section_alias` in
`cppgmsec`, with the custom section's relocation targeting that symbol, before
checking byte equality. No quoted metadata, function-section fact, target
segment, or `section_segment` state was added.

### L7. Close the contract and prevent regrowth

1. Resolve every `ablate` ledger row to `keep` or `remove` with evidence.
2. Add a lightweight audit that reports:
   - public model enum values absent from parser or serializer;
   - parser/serializer fields absent from `pa13/lowir.md`;
   - public fields with no non-transport producer or consumer; and
   - retained rows without an owning property test.
3. The audit may require a human-reviewed allowlist for genuinely semantic
   facts.  It must not infer legitimacy from identifier occurrence counts.
4. Update PA13, the earliest producer README, and the first consumer README in
   the same commit as each public change.
5. Mark this plan complete only when the model, parser, serializer, grammar,
   docs, ledger, and tests agree exactly on the supported surface.

The closing audit is `make audit-lowir-contract`, also run by PA13. It checks
the eight-column ledger schema and unique surfaces, requires every disposition
to be resolved to `keep` or `remove`, and requires all 99 retained rows to name
a semantic producer, a non-transport consumer, existing README ownership, and
existing property tests. Its human-reviewed public-enum census checks 140
textual model choices against parsing and serialization, with narrow exceptions
for canonical omitted states and two existing exhaustive serializer fallbacks.
It also checks all 20 parsed/serialized metadata keys and all 21 roles against
the PA13 LowIR document and retained ledger ownership. The final ledger has 124
rows and deliberately describes only explicit public spellings: internal
default enum states are not mislabeled as `key=default` syntax.

## Testing requirements

Tests describe implementable properties; they do not require a student's
complete LowIR program or MIR to match the course solution exactly.

| Change | Earliest property owner | Downstream proof |
| --- | --- | --- |
| Remove `select` | PA13 rejects the removed spelling; README teaches branch + `phi` | PA29/PA37 branch-and-merge behavior and phi integrity |
| Normalize lifecycle | PA16 describes mandatory inline eligibility and independent retention | PA37 inlining/pruning plus PA37 object roundtrip |
| Add `unreachable` terminator | PA13 syntax/structure; PA16 builtin lowering | PA37 CFG/phi property; PA29/PA38 native behavior |
| Remove `prototype_relaxed` | PA13 fixed/variadic positive and removed-spelling negative tests | No downstream surface |
| Remove explicit defaults | PA13 verifies omission is canonical and removed spellings are rejected | Existing PA29/PA37 behavior remains the control for retained non-default facts |
| Remove unused roles | PA13 role validation and EH behavior | PA29/PA31 runtime/object behavior for retained roles |
| Remove origin facts | Earliest source producer checks the equivalent operational LowIR | PA29 behavior, PA37 optimization, object roundtrip |
| Retain `section` | PA32 GNU section behavior | PA37 direct-versus-serialized object inspection |

For every public increment:

1. Write the high-level README requirement before or with the property test.
2. Use a small positive reducer and a guarded negative/control case.
3. Inspect only the relevant relationship: presence/absence of an operation,
   predecessor completeness, metadata relationship, object section/symbol
   fact, or runtime result.
4. Do not compare whole compiler-produced LowIR or MIR as the definition of the
   feature.  Existing complete references remain compatibility information.
5. Regenerate a checked-in reference only through its documented `ref-test`
   target and only after the changed contract is independently established.
6. Never rewrite source to avoid exposing a compiler defect.

## Verification and push cadence

Use fast verification for each increment, periodic cumulative gates, and
frequent recoverable pushes.

### Per increment

Run the narrowest relevant commands, for example:

```sh
make -j32 build
make -C pa13 check TEST='tests/path/to/focused.t'
make -C pa29 check TEST='tests/path/to/focused.t'
make -C pa37 check TEST='tests/path/to/focused.t'
make -C pa38 check TEST='tests/path/to/focused.t'
git diff --check
```

Then run `make test-paN` for every assignment whose code or contract changed.
Commit and push each accepted removal/replacement independently.  A rejected
ablation is not mixed into the next candidate; record its evidence and restore
the candidate worktree cleanly.

### Periodic cumulative gate

After at most three accepted increments, and before pushing any batch that
contains multiple public LowIR increments, run:

```sh
make test-report-through-pa13
make test-report-through-pa29
make test-report-through-pa37
make test-report-through-pa38
perl scripts/cppgm_file_audit.pl --stage pa38 --paths dev
```

The narrower through gates may be skipped only when the later through-PA38 gate
was run from the same exact tree and its report makes the earlier totals
visible.

### Final gate

```sh
make test-pa13
make test-pa15
make test-pa29
make test-pa37
make test-pa38
make test-report-through-pa38
perl scripts/cppgm_file_audit.pl --stage pa38 --paths dev
make -j32 inception INCEPTION_BUILD_JOBS=32 INCEPTION_OBJECT_BUILD_JOBS=32
```

Completion record, 2026-08-27:

- PA13 passed 122/122, PA15 121/121, PA29 51/51, PA37 21/21, and PA38
  161/161.
- `make test-report-through-pa38` passed 5,465/5,465.
- The PA38 file audit passed with the established 36 nonfatal warnings, and
  the contract audit passed with 124 rows and 99 retained facts.
- Inception was run with the outer make, inner inception build, and object
  build all explicitly set to 32 workers. The inception `cppgm++` matched.
- Two reverse-order, fresh-output all-32 O1 rounds produced byte-identical
  compiler binaries in every lane. Aggregate user+system CPU means were
  920.84 seconds for self, 586.87 for GCC 15.2, and 607.78 for Clang 21.1.8:
  self/GCC = **1.569x** and self/Clang = **1.515x**. Mean wall times were
  32.57, 21.23, and 21.65 seconds respectively. The recent pre-section
  confirmation was about 1.572x against GCC, so the retained contract program
  did not regress the honest same-tree ratio.

The apparently surprising absolute-time movement during this program does not
show that removing LowIR facts added execution work. First, this was not a
pure-deletion phase: the justified PA32 `section` fact added validation,
serialization, and object-placement code. Relative to the last pre-section O1
preparations, the final compiler text grew by 1,960 bytes when built by self
and 1,404 bytes when built by GCC; the audit itself is not compiled into the
compiler. Second, the all-source oracle changes its workload when compiler
source changes, so both host denominators and the self numerator can move.
Third, enum/field deletion and nearby source edits perturb struct alignment,
switch lowering, inlining thresholds, function order, and instruction/cache
layout. Those discontinuities can outweigh the dynamic cost of the deleted
field without representing surviving LowIR work. Finally, parallel absolute
wall and CPU times remain sensitive to frequency, memory-bandwidth contention,
and cache warmth; a movement shared by self, GCC, and Clang but absent from the
interleaved ratio is environmental evidence, not an optimization verdict.
This is why the same-tree ratio and byte-identical output, not an isolated
self or frozen-source wall time, are the final performance gate.

Use the 32-way inception path for all future inception runs.  If an ablation is
retained for performance, compare the self-built O1 all-32 time against the
same-tree GCC- and Clang-built compiler times; an isolated self-time change is
not the metric.  Timing never overrides a semantic or object-roundtrip failure.

Before profiling or timing, verify that no stale `cachegrind`, Valgrind, perf,
or detached benchmark process remains from an earlier candidate.

## Commit sequence

Use small semantic commits in this order:

1. `plan: define LowIR contract minimization`;
2. `pa13: remove unused select contract`;
3. `pa13: remove anticipatory arity mode`;
4. `lowir: remove unused section segment state`;
5. `pa13: canonicalize redundant metadata defaults`;
6. `lowir: trim unproduced runtime roles`;
7. `pa16: normalize trivial lifecycle policy`;
8. `pa13: represent unreachable control flow directly`;
9. one commit per accepted source-origin ablation;
10. `pa37: preserve explicit sections through LowIR`; and
11. `plan: close LowIR contract minimization`.

Push after each high-confidence semantic commit or after a maximum of three
small, already cumulative-gated commits.  Do not wait until the entire program
is complete to publish recoverable checkpoints.

## Rollback rule

Each removal must be independently revertible.  If a candidate fails:

1. identify the first semantic, ABI, object, optimization, or target boundary
   that differs;
2. decide whether the difference is a real LowIR obligation or an avoidable
   consumer implementation detail;
3. if real, restore the minimal fact, document the exact consumer, and add the
   missing property test before classifying it `keep`;
4. if avoidable, repair the consumer to use existing LowIR and continue the
   removal; and
5. never retain a field merely to recover an old exact dump.

## Completion criteria

The program is complete when:

- `select`, its dormant converter, and its select-only MIR/native surface are
  gone;
- `trivial_lifecycle` is normalized into existing policy and removed;
- `prototype_relaxed` and `section_segment` are gone;
- explicit conservative/default metadata has one canonical omitted form;
- unreachable control flow is a terminator, not a runtime-role call;
- every unproduced runtime role is removed or has newly demonstrated ownership;
- every source-origin ablation is resolved with recorded evidence;
- section placement survives serialized LowIR without adding an unjustified
  segment or generic metadata family;
- every retained public fact has producer, consumer, README, and property-test
  ownership in the ledger;
- direct and serialized objects agree for all affected object facts;
- through-PA38, the PA38 file audit, and 32-way inception pass; and
- all accepted commits are pushed with a clean, synchronized `v3opt` branch.

# PA11 Mainline Backbone Plan: Replace the Parallel Type Analyzer

Status: complete. Phase 0 established the divergence census, Phase 1 landed as
`fca6c977`, and Phases 2-5 landed as `61868b70`. The public `--emit-types`
path now uses the mainline semantic graph, the standalone analyzer is deleted,
all 52 successful inputs match exactly, and all 18 failure inputs retain their
expected status.

Date: 2026-08-19

## 1. Objective

Convert the standalone PA11 `--emit-types` implementation from a parallel
analyzer into a thin output view over the mainline semantic backbone.  The
target shape is:

```
source bytes -> PA10 syntax -> mainline SemanticAnalyzer -> pa11::Program
             -> Program::Render (the existing PA11 type/scope writer)
```

`dev/src/pa11_semantic.cpp` (1,384 lines) duplicates declaration, specifier,
declarator, class/enum, constant-expression, and `decltype` analysis that the
mainline analyzer also implements.  Both populate the same `pa11::Program`
model, and the PA11 output writer is already the shared
`Program::Render` in `dev/src/pa11_model.cpp`, which nothing else calls.
The parallel analyzer is therefore a maintenance treadmill: T2y spent a full
slice restoring its typed-name parity, and its operator and literal handling
is queued for the same duplicated treatment under the typed-boundary plan's
T6/T7 designs.  Deleting it removes the treadmill and makes the PA11 suite
exercise production analysis.

This plan does not change the PA11 student-facing assignment contract.  If
the maintainers decide the standalone analyzer has pedagogical value as a
model solution, this plan should be closed as rejected with that decision
recorded; the census results remain useful either way because Phase 1 fixes
real mainline gaps.

## 2. Feasibility census (measured)

The initial census ran every reference-passing PA11 assignment and course
input through the mainline analyzer (`--emit-semantics` acceptance as the
proxy):

- The mainline analyzer accepts 48 of 52 inputs.
- It rejects 4 inputs in 3 root-cause classes:

| Class | Inputs | Error | Diagnosis |
| --- | --- | --- | --- |
| Qualified out-of-class scoped-enum member definition | `cppgm.tests/course/pa11/200-qualified-scoped-enum-definition-lookup.t`, `pa11/tests/general/200-qualified-member-scoped-enum-definition.t` | `unknown enum type` | Mainline gap: `enum class writer::state : int { ... }` defining a member enum at namespace scope is not resolved.  These are the same two fixtures whose PA10 semantic-only structured-name child T2y retained. |
| Namespace-scope anonymous union member injection | `pa11/tests/spec/200-namespace-anonymous-union-injected-members.t` | `unknown expression name: t` | Mixed diagnosis corrected in Phase 1: this fixture is ill-formed because a namespace-scope anonymous union must be `static`, but a new standard-valid `static union { ... };` probe also exposed a real mainline storage/member-injection gap. |
| Scoped-enum comparison strictness | `pa11/tests/spec/200-enum-scoped.t` | `invalid comparison operands` | Contract conflict: `FY::Y == 3` on a scoped enum is ill-formed by the standard; the mainline analyzer correctly rejects it while the pinned PA11 reference accepts it. |

Structural facts confirmed by the census:

- `run_emit_types_mode` and `run_emit_semantics_mode` in `dev/cppgm++.cpp`
  have the same driver shape (per-TU header lines, writer call, stats line),
  so the wrapper is a driver-level substitution plus a render call.
- `Program::Render` (`dev/src/pa11_model.cpp:2491`) is called only by the
  PA11 `TypeAnalyzer`; the mainline analyzer renders the dump tree instead.
  The types/scopes view writer therefore already exists, unowned by PA11.
- The two analyzers already agree on delicate generated-name conventions in
  at least one pinned case (`__anonymous_union_type__0_10` in the PA11
  namespace-anonymous-union reference).

The hidden `--emit-mainline-types` prototype then rendered the actual
mainline `Program`.  Before projection, none of the 50 accepted inputs is
byte-identical.  The dominant measured additions are deterministic rather
than unexplained semantic drift: every input contains one builtin
`nullptr_t` alias and four allocation/deallocation functions (250 lines),
and class inputs contain injected self bindings and roughly 108 implicit
constructor, assignment, and destructor entries.  The remaining families
are blank/deferred function-scope presentation, source-scope nesting,
qualified member-enum presentation, and anonymous-type typedef presentation.
These are the Phase 2/3 worklist; they are not reasons to fork analysis.

## 3. Success criteria

1. `--emit-types` output is byte-identical to the current PA11 output for
   every input where the pinned reference and the standard agree, across the
   full PA11 assignment and course suites.
2. Divergent cases each have an earliest-owned disposition: a mainline fix
   with an active course reducer, or removal when no standard-observable
   requirement can be expressed.  An adopted reference disagreement requires
   an authoritative reference update before activation.
3. `dev/src/pa11_semantic.cpp` and its private analysis machinery are
   deleted; `frontend_source_sets.mk` and the `pa11_stats` driver plumbing
   are updated; no include-as-code splits.
4. The production source-to-object path is unaffected: exact frozen object,
   timing neutral within the screened band, no common-record growth.
5. Root `make test-report` passes and the PA39 file audit has zero fatal
   findings.

## 4. Phases

### Phase 0: prototype renderer and full divergence census

Add a driver-internal prototype that runs the mainline analyzer and calls
`Program::Render`, then diff against the current PA11 output for all 52
inputs.  The acceptance census above bounds rejection divergence; this phase
bounds *presentation* divergence: extra bindings and scopes the mainline
model contains (implicit special members, lifecycle entries, function body
scopes, `this` and parameter bindings, hidden storage), generated-name
ordinal drift, and binding order in scope chains.  Classify every diff line
by producing family before changing anything.  This phase is measurement
only and lands behind an unadvertised flag or in a scratch harness.

### Phase 1: close the mainline gaps (independent value)

1. Qualified out-of-class scoped-enum member definitions, owned by the
   mainline declaration/specifier handoff, with the two existing PA11
   fixtures plus a new earliest-owned PA12 reducer.  A declaratorless member
   declaration now declares its enum instead of requesting elaborated lookup.
2. Namespace-scope anonymous union member injection, owned by the mainline
   anonymous-union path.  A standard-valid `static union` PA12 reducer covers
   generated internal storage, injected member lookup, semantic lowering, and
   source-to-object compilation.  The shared helper also retains the existing
   block-scope behavior.  The old non-`static` PA11 fixture is a Phase 4
   contract conflict, not an implementation target.

These are real language gaps in the production analyzer and should land
regardless of the wrapper decision.  Each follows the standard commit
sequence: reducer, fix, owner and through reports, full report, audit,
frozen-object exactness.

### Phase 2: model-pollution gating

Make the types/scopes view faithful when rendered from the richer mainline
model.  Prefer filtering in `Program::Render` by existing typed facts
(binding kinds, lifecycle flags, `ordinary_visible`, unindexed placement)
over analysis-mode gates, so the analyzer itself stays one dialect.  An
analysis-mode gate is acceptable only for work that is both expensive and
invisible in the types view (for example function-body dump construction),
and must not change any `--emit-semantics` or object-path behavior.

### Phase 3: presentation parity

Reconcile generated-name ordinals and any binding-order differences found in
Phase 0.  The known typedef-linkage naming divergence (the reference
presents typedef-named anonymous structs by linkage name; the mainline uses
`__local_typeN`) is already documented by the PA12 normalization record; if the PA11
suite contains such a shape, its disposition extends that record rather than
forking the convention.

### Phase 4: contract conflicts

`200-enum-scoped.t` and the non-`static`
`200-namespace-anonymous-union-injected-members.t` are the two current
conflicts: both GCC and Clang reject the latter under C++11, and the standard
supports the mainline scoped-enum comparison rejection.  Rewrite each active
fixture to preserve its standard-valid coverage, retain the conflicting form
as an active `-bad` course test, and use the documented reference-regeneration path if
any oracle changes.  Do not weaken the mainline analyzer to match a lax
oracle.

### Phase 5: switchover and deletion

Point `run_emit_types_mode` at the mainline analyzer plus `Program::Render`,
map the `pa11_stats` fields onto the mainline stats (documenting removed
fields), delete `dev/src/pa11_semantic.cpp` and its header entries, and
update `frontend_source_sets.mk`.  Close with PA11 and through-PA11 reports,
the full report, the PA39 audit, frozen-object exactness, and a screened
timing check on the production path.

## 5. Risks and open decisions

- Pedagogical: resolved in favor of a reusable semantic backbone. The PA11
  handout describes the required output and recommends a shared graph in its
  non-normative Design Notes without prescribing repository internals.
- Strictness asymmetry: the mainline analyzer evaluates more deeply and may
  reject other lax-oracle patterns that only appear in future PA11 inputs;
  Phase 4's policy covers them.
- Stats contract: `pa11_stats` currently exposes analyzer-private counters
  (lookup probes, name-path parse families).  Downstream tooling that parses
  them must be surveyed before the fields change.

## 6. Results ledger

| ID | Change | Result | Output effect | Tests | Commit / disposition |
| --- | --- | --- | --- | --- | --- |
| B0 | Hidden mainline renderer and presentation census | Initial mainline acceptance was 48/52; after B1 it was 50/52. All 50 accepted cases differed before projection. Builtins contributed exactly 250 extra lines; implicit/self class facts, deferred scope names, qualified member-enum placement, and anonymous typedef naming accounted for the remaining families. | Hidden `--emit-mainline-types` only; public `--emit-types` and object compilation unchanged | Full PA11 successful-input census; full report 5,220/5,220; audit zero fatal | Phase 0 complete |
| B1 | Standard-valid mainline semantic gaps | Declaratorless class-member enum declarations now reach `AnalyzeEnum` as declarations; a responsibility-named anonymous-union component creates typed private storage, registers static/local lifetime facts, and injects member bindings. | Two new PA12 semantic fixtures generated by the pinned reference; existing block anonymous-union output unchanged; frozen `-O0` object exact at 4,415,448 bytes and baseline SHA | PA12 184/184; selected report 864/864; full report 5,220/5,220; audit zero fatal | `fca6c977` |
| B2 | Typed source-view projection | Typed binding provenance filters builtins, implicit special members, injected self names, hidden anonymous-union storage, and production template proxy scopes. Source-declared parameter types use dense view-only binding/type overrides; named types retain entity identity while rendering terminal or owner-qualified presentation as required. | Hidden census improved from 0/50 exact to 50/50 exact before fixture correction; production records did not grow (`BindingRecord` remained 136 bytes and `EntityRecord` 208 bytes). | Full successful-input byte census and failure-status census | `61868b70` |
| B3 | Qualified and anonymous presentation | Qualified member-enum definitions retain their semantic member scope while typed output-only bindings/scopes reproduce source placement. Anonymous typedef/object types reuse their entity with a source presentation terminal; compatible class-key redeclarations append an unindexed output fact. | No qualified spelling is stored as semantic identity; the renderer derives qualification from owner/entity IDs. | Covered by existing PA11 fixtures plus the PA12 qualified-enum reducer | `61868b70` |
| B4 | Standard-correct fixture disposition | The scoped-enum assertions now compare values of the same enum type; the namespace anonymous union is `static`. The rejected historical forms are active PA11 `-bad` course tests. GCC and Clang accept both corrected active fixtures under `-std=c++11 -pedantic-errors`. | Anonymous-union generated token identity changed from `0_10` to `1_10`; its reference was regenerated through `make ref-test-pa11`. | PA11 70/70; full PA11 census 52 success + 18 failure with zero mismatches | `61868b70`; negative tests activated by normalization commit `ac1c39a2` |
| B5 | Public switchover and deletion | `--emit-types` calls the shared analyzer/view directly; the hidden prototype flag and header are removed; the 1,384-line standalone analyzer is deleted. Legacy PA11 telemetry is populated from mainline counters where the categories correspond. | Frozen `-O0` object remains byte-identical: 4,415,448 bytes, SHA-256 `d52599359535b175519d1ce1249f2a7eafa443fa1765d1c39d7d38f93716c37f`; screened run 4.66 s wall, 361,176 KiB RSS. | Through PA11 653/653; full report 5,220/5,220; PA39 audit zero fatal | `61868b70`; complete |

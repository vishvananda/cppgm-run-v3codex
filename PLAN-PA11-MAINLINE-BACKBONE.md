# PA11 Mainline Backbone Plan: Replace the Parallel Type Analyzer

Status: proposed; not started.  The feasibility census below was measured
against the working tree at the T6 operation-transport checkpoint; re-run it
at the anchor commit before beginning Phase 0.

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

The census ran every reference-passing PA11 assignment and course input
through the mainline analyzer (`--emit-semantics` acceptance as the proxy):

- The mainline analyzer accepts 48 of 52 inputs.
- It rejects 4 inputs in 3 root-cause classes:

| Class | Inputs | Error | Diagnosis |
| --- | --- | --- | --- |
| Qualified out-of-class scoped-enum member definition | `cppgm.tests/course/pa11/200-qualified-scoped-enum-definition-lookup.t`, `pa11/tests/general/200-qualified-member-scoped-enum-definition.t` | `unknown enum type` | Mainline gap: `enum class writer::state : int { ... }` defining a member enum at namespace scope is not resolved.  These are the same two fixtures whose PA10 semantic-only structured-name child T2y retained. |
| Namespace-scope anonymous union member injection | `pa11/tests/spec/200-namespace-anonymous-union-injected-members.t` | `unknown expression name: t` | Mainline gap: member injection for anonymous unions is implemented for block scope (`local` gated) but not namespace scope. |
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

## 3. Success criteria

1. `--emit-types` output is byte-identical to the current PA11 output for
   every input where the pinned reference and the standard agree, across the
   full PA11 assignment and course suites.
2. Divergent cases each have an earliest-owned disposition: a mainline fix
   with a reducer, or a documented `proposed/` entry where the reference
   disagrees with the standard.
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
   mainline enum declaration path (`AnalyzeEnum`), with the two existing
   PA11 fixtures plus a new earliest-owned mainline reducer.
2. Namespace-scope anonymous union member injection, owned by the mainline
   anonymous-union path, generalizing the block-scope injection; reuse the
   existing PA11 spec fixture as the oracle and add a mainline reducer.

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
`__local_typeN`) is already documented under `proposed/pa12/`; if the PA11
suite contains such a shape, its disposition extends that entry rather than
forking the convention.

### Phase 4: contract conflicts

`200-enum-scoped.t` is the template: the standard supports the mainline
rejection, the pinned reference accepts.  Per the repository fixture policy,
the standard-correct behavior wins; move the fixture's conflicting assertion
to a `proposed/pa11/` entry documenting the reference disagreement, or amend
the fixture through the reference-regeneration targets if the maintainers
prefer.  Do not weaken the mainline analyzer to match a lax oracle.

### Phase 5: switchover and deletion

Point `run_emit_types_mode` at the mainline analyzer plus `Program::Render`,
map the `pa11_stats` fields onto the mainline stats (documenting removed
fields), delete `dev/src/pa11_semantic.cpp` and its header entries, and
update `frontend_source_sets.mk`.  Close with PA11 and through-PA11 reports,
the full report, the PA39 audit, frozen-object exactness, and a screened
timing check on the production path.

## 5. Risks and open decisions

- Pedagogical: the standalone analyzer may be a deliberate model solution
  for the PA11 assignment.  This is a maintainer decision to record before
  Phase 5; Phases 0-1 are worthwhile regardless.
- Strictness asymmetry: the mainline analyzer evaluates more deeply and may
  reject other lax-oracle patterns that only appear in future PA11 inputs;
  Phase 4's policy covers them.
- Stats contract: `pa11_stats` currently exposes analyzer-private counters
  (lookup probes, name-path parse families).  Downstream tooling that parses
  them must be surveyed before the fields change.

## 6. Results ledger

| ID | Change | Result | Output effect | Tests | Commit / disposition |
| --- | --- | --- | --- | --- | --- |
| B0 | Feasibility census | Mainline accepts 48/52 reference-passing PA11 inputs; 4 rejects in 3 classes (2 qualified scoped-enum member definitions, 1 namespace anonymous-union injection, 1 standard-versus-oracle strictness conflict); shared `Program::Render` writer confirmed PA11-only | None; measurement only | Census script over the PA11 suites | Recorded in section 2; plan proposed |

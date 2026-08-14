# PA35 Plan

## Stage Design and Spec Alignment

PA35 extends the existing source -> streaming preprocessing/post-tokenization ->
integrated syntax/semantics -> typed LowIR -> native ELF path; it adds no
hosted-only compiler route.  The relevant `spec.md` requirements are linear
preprocessing/parsing (§1, §9), one canonical typed fact flow into lowering
(§2, §6), demand-scoped template work (§4), and observable work counters for
heavy-header scaling (§9).  The stage owner remains the shared front end and
its existing translation-unit/per-phase storage.

## Current Failure Map

Turn-start baseline: 6/104 overall (the PA-local compile report is 6/103).
The first checkpoint raised the PA-local report to 15/103 and dependent lookup
plus call/type disambiguation raised it to 18/103.  Explicit-instantiation
routing then removed 36 barriers without changing the pass count because those
tests reached later checks.  The final report is 18/103, leaving 85 failures:
retained declaration/specialization identity 19; retained type identity 14;
constexpr/static evaluation 16; dependent lookup and access 21; unresolved
expression/call/parser cases 9; allocator corruption 2; exception rules 1; and
lowering/type-completion singletons 3.

## Active Checkpoint

**Retained declaration canonical identity.**  Per `spec.md` §§2 and 4, repeated
template declarations merge into one canonical pattern while incompatible
parameter, result, owner, or definition facts remain conflicts.  Data flow is
parsed declaration -> canonical owner/name and normalized template/function
shape -> indexed retained pattern -> merged defaults/definition -> existing
specialization cache.  The function-template registry owns the merge; expected
work is average O(1) indexed lookup plus O(parameter/signature size) comparison.
Validate legal repeated declarations and definitions, incompatible negatives,
the 16 affected hosted cases, full PA35, PA1-34, audit, and doubled declaration
set work/time/storage.

## Performance Evidence

Wrapped-probe stress at 2,000/4,000 directives produced 18,000/36,000 macro
lookups, 4,000/8,000 builtin probes, 27.1/52.4 ms preprocessing time, and
4,852/4,860 KiB peak RSS.  Doubling variable input doubled counted work, took
1.94x time, and left peak line/rescan storage fixed at 18/12 tokens.
Attributed/explicit-template/decltype parser stress at 1,000/2,000 units used
81,012/162,012 tokens, 3,000/6,000 bounded template scans, 7,000/14,000 scanned
tokens (maximum 3), 31.3/60.9 ms parse time, and 2.29/4.58 MB parser storage.
Canonical using-import stress at 1,000/2,000 scopes used 7,008/14,008 signature
lookups, 51.3/104.3 ms semantic time, and 6.45/12.89 MB semantic storage.
Attributed member-partial registration at 100/200 template families used
500/1,000 shape materializations, 34.8/67.2 ms semantic time, and 4.60/9.20 MB
semantic storage.
Private member-template defaults at 256/512 cases used 768/1,536 template
requests, 5,636/11,268 lookups, 33.1/68.5 ms semantic time, and 7.30/14.59 MB
peak semantic storage; declaration access context adds constant work per replay.

## Completed Checkpoints

| Checkpoint | Result | Validation |
|---|---|---|
| Hosted front-end ingress | PA35 6 -> 15/103 | focused utility/probes pass; PA1-34 4756/4756; audit pass |
| Hosted attributed/template syntax | shared syntax barriers removed; PA35 15/103 | focused decltype-shift passes; parser work/storage doubles linearly |
| Canonical using-function merge | 35 duplicate-import barriers removed; PA35 15/103 | repeated/distinct imports correct; semantic work/storage linear |
| Hosted class-template registration | 49 registration barriers removed; PA35 15/103 | attributed adapters/partial identity correct; registration scales linearly |
| Dependent nested-type lookup | prior 55-case barrier removed; PA35 15 -> 17/103 | focused shape replay and hosted allocator cases advance; audit pass |
| Dependent call/type disambiguation | dependent calls remain calls; complete scalar cast set | PA35 17 -> 18/103; qualified-base regressions restored |
| Explicit-instantiation operator identity | 36 routing barriers removed; PA35 remains 18/103 | `operator<<`, `operator<`, and genuine template-id targets route distinctly |
| Constexpr declaration/completion boundary | 16 owner-literal barriers removed; PA35 remains 18/103 | `error_category` advances; PA21 member/base/object negatives remain rejected |
| Retained template-default access context | 19 access barriers removed; PA35 remains 18/103 | member/friend defaults pass; external private alias rejected; scaling linear |

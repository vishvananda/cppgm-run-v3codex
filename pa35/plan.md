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

Turn-start baseline: 6/104 overall (6/103 in the PA-local report); current PA35
is 19/103.  The 84 remaining failures group by shared owner and first stop:
dependent type/owner identity 42; constexpr/static evaluation 16; qualified
nested-class member replay 11; unresolved expression/call/parser behavior 9;
explicit class-specialization identity 3; allocator corruption 2; and lowering
type completion 1.

## Active Checkpoint

**Qualified nested-class member replay.**  Per `spec.md` §§2 and 4, an
out-of-class member definition under dependent outer-template arguments must
resolve to the canonical member declared by the retained nested class, without
manufacturing a namespace function.  Data flow is parsed qualified owner path
-> outer specialization -> nested class entity/member scope -> normalized
function signature -> canonical member binding -> definition demand.  The
class-specialization and function-declaration registries own the boundary;
expected work is average O(1) indexed identity lookup plus O(path + signature)
normalization.  Validate focused owner/signature positives and mismatches, the
11 stream cases, full PA35, PA1-34, audit, and doubled nested-member families.

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
Retained tag/control-scope replay at 256/512 template families used
14,337/28,673 tokens, 2,305/4,609 scopes, 260/516 lookups, 25.1/49.6 ms semantic
time, and 1.42/2.84 MB semantic storage (2.69/5.38 MB stage peak); all measured
work and storage remained linear under doubling.

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
| Canonical retained declaration replay | tag/control/function/exception facts merge canonically; PA35 18 -> 19/103 | 16 barriers advance; allocation-spec ± pass; PA1-34 4756/4756; audit/scaling pass |

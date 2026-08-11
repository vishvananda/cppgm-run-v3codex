# PA24 Implementation Plan

## Stage Design and Spec Alignment

PA24 composes the PA19-PA23 template engine through the canonical semantic
graph and typed LowIR path. Retained syntax is parsed once; template,
specialization, argument, scope, declaration, and emission identities own
replay and demand facts. No textual transport or alternate lowering path is
introduced.

Relevant `spec.md` requirements are O(1)-average canonical identity and
owner-indexed lookup (sections 2-3), complete specialization keys, monotonic
facts, parent-linked environments, dependent-only replay, and demanded bodies
(section 4), deduplicated work (section 5), lowering from selected declaration
and emission identities (section 6), translation-unit-owned storage (section
8), and work proportional to owner-local candidates, arguments, and newly
demanded nodes (section 9).

## Current Failure Map

Current state: **421/422** pa24 tests (checkpoint start: 419/422; goal start:
368/422); all **3,049/3,049** tests through pa23 pass.

| Shared behavior and owner | Count | Complete failing set (test basename) |
| --- | ---: | --- |
| Reference-temporary slot planning order (`pa16`/`pa17` typed lowering) | 1 | `constructor-template-const-ref-enable-if-conversion` |

## Active Checkpoint

**Evaluation-ordered reference-temporary slots.** The typed slot planner owns
one append-only order for automatic objects and expression materializations.
For a declaration with a reference-binding initializer, its initializer's
materialized argument slot must precede the later declaration object while
preserving canonical semantic identities. Data flows as `selected declaration
-> initializer evaluation/materialization -> reference-transfer action -> slot
plan -> LowIR slots`. This applies `spec.md` sections 6, 8, and 9: lowering
consumes typed semantic actions in evaluation order, stores compact IDs in
translation-unit arenas, and visits each action once. Expected work is O(D + E)
for D declarations and E initializer actions, with O(1)-average slot lookup.
Validate the remaining constructor-template fixture, declaration/reference
controls, PA16-PA23 reports, full reports, audit, and a declaration-width probe.

## Performance Evidence

Explicit-specialization definition/redeclaration scale 1/2/4/8/16/32 produced
template requests 4/7/13/25/49/97, cache hits 3/5/9/17/33/65, lookup-scope
visits 9/10/12/16/24/40, and semantic times 0.326/0.373/0.550/0.800/1.314/
2.306 ms; demand pushes stayed at one.

Qualified member-variable-template NTTP scale 1/2/4/8/16/32 produced template
requests 4/8/16/32/64/128, cache hits 1/2/4/8/16/32, canonical argument-list
requests 9/18/36/72/144/288 with hits 7/14/28/56/112/224, lookup-scope visits
23/41/77/149/293/581, demand pushes 1/2/4/8/16/32, and semantic times
0.504/0.657/1.107/1.627/2.855/5.368 ms. Counts and time remain linear in the
number of distinct requested specializations.

Recursive base-constructor depth 1/2/4/8/16/32/64 emitted 3/4/6/10/18/34/66
functions with median compile times 3.787/4.046/4.147/4.664/5.397/7.362/
10.825 ms (nine runs each after warm-up). Emission and time remain linear in
the number of selected constructor entries.

Explicit-id ADL width 2/3/5/9/17/33/65 associated scopes emitted two functions
throughout with median compile times 4.145/4.165/4.274/4.603/5.091/6.286/
8.597 ms (nine runs each after warm-up). Lookup time remains linear in the
deduplicated associated-scope count.

Retained current-specialization reference width 1/2/4/8/16/32/64 compiled in
median 11.535/11.672/11.657/11.779/12.386/13.077/14.761 ms (nine runs each
after warm-up). Validation time remains linear in the number of retained
member signatures; each class/qualified owner publishes one scope fact.

Qualified default-expression candidate width 1/2/4/8/16/32 produced lookup
scope visits 55/60/70/90/130/210, while viable overload candidates stayed at
three, specialization requests at 12, and default materializations at one.
Median semantic times were 1.509/1.544/1.602/1.660/1.923/2.318 ms (nine runs);
owner lookup and time remain linear and rejected arities do not enter ordering.

Dependent qualified-constraint width 1/2/4/8/16/32 produced 92/99/113/141/
197/309 lookup-scope visits and 13/18/28/48/88/168 specialization requests,
with 7/12/22/42/82/162 cache hits. Deduction visits stayed at six; median
semantic times were 1.127/1.299/1.481/1.967/2.997/4.923 ms over nine runs,
showing linear retained-syntax work and cache reuse of repeated concrete work.

Trivial default-initialized array extents 1/2/4/8/16/32/64/128 emitted 12
instructions, one function, zero default-constructor definitions, and 16
materialized-demand visits throughout. Median lowering times were 0.062/0.058/
0.053/0.056/0.051/0.055/0.050/0.055 ms over nine runs; no-op construction work
is independent of extent, while nontrivial arrays retain the linear loop path.

Undefined function-specialization widths 1/2/4/8/16/32/64 kept demand pushes
and declaration emissions at one. Semantic nodes were 8/9/11/15/23/39/71,
deduction visits 2/4/8/16/32/64/128, output bytes 249/268/306/382/540/860/
1500, and median semantic times 0.223/0.239/0.250/0.270/0.314/0.410/0.567 ms
over nine runs. Typed declaration work remains linear in canonical parameter
width without replaying a declaration body environment.

Same-type rvalue-reference casts with 1/2/4/8/16/32/64 visible conversion
operators kept overload candidates at four and demand pushes at one. Semantic
nodes were 33/34/36/40/48/64/96, template requests 1/2/4/8/16/32/64, and
median semantic times 0.415/0.436/0.511/0.585/0.694/0.997/1.573 ms over nine
runs. Total declaration work is linear, while direct-reference classification
avoids a conversion-candidate-width scan.

Repeated empty reference-transfer and namespace-object widths
1/2/4/8/16/32/64 produced functions 4/6/10/18/34/66/130, globals and
empty-chain visits 1/2/4/8/16/32/64, instructions 12/21/39/75/147/291/579,
and demand pushes 5/9/17/33/65/129/257. Seven-run median semantic/lowering
times were 1.016/0.235, 1.401/0.266, 2.149/0.369, 3.465/0.543, 6.112/0.938,
11.710/1.719, and 24.724/3.347 ms; all owner-local work remains linear.

Nested empty construction-transfer depths 1/2/4/8/16/32/64 produced semantic
nodes 40/60/100/180/340/660/1300, demand visits 25/34/52/88/160/304/592,
and instructions 18/22/30/46/78/142/270. Seven-run median semantic/lowering
times were 0.579/0.210, 0.701/0.234, 0.958/0.251, 1.478/0.349, 2.378/0.504,
4.349/0.815, and 8.819/1.453 ms; recipe traversal and demand remain linear.

Materialized default-suffix widths 1/2/4/8/16/32/64 produced deduction visits
12/15/21/33/57/105/201 while semantic nodes stayed at 26 and canonical
argument-list requests at 11. Seven-run median semantic times were 0.610/0.637/
0.656/0.718/0.811/1.066/1.390 ms; the suffix anchor is one linear reverse pass.

Retained non-static receiver depths 1/2/4/8/16/32/64 produced 32/40/56/88/
152/280/536 demand visits and 28/32/40/56/88/152/280 semantic nodes; demand
pushes stayed at three. Seven-run median semantic times were 0.251/0.261/0.268/
0.298/0.365/0.523/0.810 ms, showing one scan per newly retained call node.

Dependent trailing-result failure widths 1/2/4/8/16/32/64 produced 9/18/36/
72/144/288/576 deduction visits, 54/100/192/376/744/1,480/2,952 specialization
requests, and 362/592/1,052/1,972/3,812/7,492/14,852 lookup-scope visits.
Seven-run median semantic times were 2.237/3.037/4.776/8.146/14.873/28.775/
57.541 ms; candidate-local failure formation remains linear in request width.

Shared ABI-recipe specialization widths 1/2/4/8/16/32/64 produced 29/53/101/
197/389/773/1,541 semantic nodes, 6/12/24/48/96/192/384 specialization
requests, and 2/4/8/16/32/64/128 globals. Five-run median semantic times were
0.443/0.575/0.817/1.246/2.156/3.931/7.830 ms; pattern storage is shared and
specialization-local identity work remains linear.

## Completed Checkpoints

| Checkpoint | Commit | Disposition |
| --- | --- | --- |
| Explicit specialization identity and definition publication | `62f37ef1` | Eight failures removed; 376/422 pa24, 3,049/3,049 through pa23, audit pass. |
| Canonical non-type arguments, designators, static-value demand, and variable-template lowering identity | `6b06e092` | Fifteen failures removed; 391/422 pa24, 3,049/3,049 through pa23, audit pass. |
| Typed construction-entry demand and nontrivial empty-result ABI | `6a1a66c7` | Four failures removed; 395/422 pa24, 3,049/3,049 through pa23, linear depth probe. |
| Explicit-id ADL and candidate-local deleted/dependent invalidity | `4c694582` | Four failures removed; 399/422 pa24, 3,049/3,049 through pa23, linear ADL-width probe. |
| Retained current-specialization dependency | `739eab0f` | Two failures removed; 401/422 pa24, 3,049/3,049 through pa23, linear reference-width probe. |
| Default-expression candidate completion and partial ordering | `b555a4eb` | Two failures removed; 403/422 pa24, 3,049/3,049 through pa23, linear candidate-width probe. |
| Nonterminal dependent template-id and prior-parameter retention | `9ab41503` | One failure removed; 404/422 pa24, 3,049/3,049 through pa23, linear constraint-width probe. |
| Destination-consistent default/value construction lowering | `5be465c9` | Two failures removed; 406/422 pa24, 3,049/3,049 through pa23, constant trivial-array extent probe. |
| Identity-only typed declaration emission | `6a107535` | Two failures removed; 408/422 pa24, 3,049/3,049 through pa23, linear declaration-width probe. |
| Direct-reference classification and runtime conversion-object materialization | `f3724c7e` | Two failures removed; 410/422 pa24, 3,049/3,049 through pa23, constant candidate count across conversion width. |
| Empty-value lifecycle demand and destination-owned lowering | `6a238d0f` | Three failures removed; 413/422 pa24, 3,049/3,049 through pa23, linear empty-transfer/object scaling. |
| Type-preserving class-argument staging and nested empty-recipe consumption | `dfe38634` | Two failures removed; 415/422 pa24, 3,049/3,049 through pa23, linear nested-construction scaling. |
| Declaration-ordered retained arguments and fixed-primary pack spans | `30fe0986` | Two failures removed; 417/422 pa24, 3,049/3,049 through pa23, linear default-suffix scaling. |
| Candidate-local member-call formation and retained receiver demand | `f18a8293` | Two failures removed; 419/422 pa24, 3,049/3,049 through pa23, audit pass, linear failure-width and receiver-depth scaling. |
| Retained function-template ABI recipes and local-static owner identity | this commit | Two failures removed; 421/422 pa24, focused PA21-PA22 controls pass, linear specialization-width scaling. |

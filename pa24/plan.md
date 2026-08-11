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

Current state: **408/422** pa24 tests (turn start: 406/422); all **3,049/3,049**
tests through pa23 pass.

| Shared behavior and owner | Count | Complete failing set (test basename) |
| --- | ---: | --- |
| Deduction, candidate SFINAE, overload ordering, and conversion/initialization selection (`pa19` deduction/instantiation + ordinary call/initialization) | 7 | `member-operator-template-reference-pattern-partial-order`, `constructor-template-const-ref-enable-if-conversion`, `static-cast-rvalue-ref-skips-conversion-operator`, `defaulted-class-template-argument-pack-prefix-deduction`, `trailing-return-expression-sfinae-default-param`, `conversion-function-template-top-cv-sequence`, `out-of-class-conversion-operator-definition` |
| Dependent owner/current-specialization, alias, function-type, and pack replay through demand/lowering (`pa19`/`pa22`/`pa23`) | 7 | `alias-template-partial-specialization-default-dependent-arg`, `member-template-result-pack-preserves-nested-function-pointer-owner`, `out-of-class-partial-member-template-owner-parameter-alias`, `forwarding-pack-function-type-enable-if`, `source-namespace-base-sfinae-chain`, `defaulted-nested-class-argument-partial-specialization`, `defaulted-template-arg-partial-base-completion` |

## Active Checkpoint

**Identity-only declaration emission — completed.** A demanded callable's
canonical `FunctionInfo` and binding own declaration identity and function
type. An undefined declaration has no body environment to replay: semantic
graph emission for typed lowering publishes only its declaration node, while
PA15 enumerates the canonical function type and synthesizes stable `argN`
presentation names. Source parameter bindings, lifetime obligations, and
function scopes are created only for definitions that consume them. The PA12
human-readable semantic view retains its source parameter children as part of
that stage's output contract.

This applies `spec.md` sections 4-6, 8, and 9: declaration demand remains a
monotonic identity fact, no alternate body path is introduced, and lowering
consumes the selected typed declaration. Semantic declaration-shell work is
O(1) after selection instead of O(P) scope/binding replay; unavoidable typed
boundary lowering and rendering remain O(P) in parameter count. Validation
removed `decorated-template-id-type-argument-pack-replay` and
`partial-member-template-trailing-result-scope`; the PA12 declaration-view
regression case, neighboring definitions, a declaration-width probe, the full
PA24 and through-PA23 reports, and the file audit cover the boundary.

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
| Identity-only typed declaration emission | this commit | Two failures removed; 408/422 pa24, 3,049/3,049 through pa23, linear declaration-width probe. |

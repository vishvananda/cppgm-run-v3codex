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

Current state: **399/422** pa24 tests (turn start: 395/422); all **3,049/3,049**
tests through pa23 pass.

| Shared behavior and owner | Count | Complete failing set (test basename) |
| --- | ---: | --- |
| Deduction, candidate SFINAE, overload ordering, and conversion/initialization selection (`pa19` deduction/instantiation + ordinary call/initialization) | 12 | `member-operator-template-reference-pattern-partial-order`, `constructor-template-const-ref-enable-if-conversion`, `defaulted-enable-if-template-parameter-selection`, `static-cast-rvalue-ref-skips-conversion-operator`, `concrete-enable-if-nontype-parameter-type-sfinae`, `constructor-template-default-constraint-previous-param`, `dependent-typename-member-enable-if-return`, `defaulted-class-template-argument-pack-prefix-deduction`, `constructor-default-pack-partial-ordering`, `trailing-return-expression-sfinae-default-param`, `conversion-function-template-top-cv-sequence`, `out-of-class-conversion-operator-definition` |
| Dependent owner/current-specialization, alias, function-type, and pack replay through demand/lowering (`pa19`/`pa22`/`pa23`) | 11 | `alias-template-partial-specialization-default-dependent-arg`, `common-type-current-specialization-member-return`, `decorated-template-id-type-argument-pack-replay`, `dependent-function-type-member-specialization`, `member-template-result-pack-preserves-nested-function-pointer-owner`, `out-of-class-partial-member-template-owner-parameter-alias`, `forwarding-pack-function-type-enable-if`, `partial-member-template-trailing-result-scope`, `source-namespace-base-sfinae-chain`, `defaulted-nested-class-argument-partial-specialization`, `defaulted-template-arg-partial-base-completion` |

## Active Checkpoint

**Explicit template-id ADL and candidate-local invalidity — completed.** The retained call
owns its explicit template-argument syntax and ADL eligibility; associated
namespace and hidden-friend lookup must deduce with that same canonical prefix.
The selected `FunctionInfo` owns a durable deleted declaration fact, and a
deleted selection becomes candidate-local substitution failure before any call
node or emission demand is published. Dependent non-type partial arguments use
a nondeduced declaration shape and replay once with concrete bindings, keeping
invalid `sizeof` expressions inside that same failure boundary.

This applies `spec.md` sections 3-6 and 9: associated lookup is owner-indexed,
explicit arguments participate in the specialization key, failures remain in
the candidate frame, duplicate declarations/candidates are canonicalized, and
lowering only sees a viable selected declaration. Expected work is O(A + S +
C), for argument-associated entities A, associated scopes S, and owner-local
candidates C, with each canonical candidate deduplicated once. Validation
removes `adl-explicit-template-id-call`, both
`deleted-function-template-*-sfinae` tests, and
`incomplete-sizeof-partial-specialization-sfinae`; neighboring explicit-id,
hidden-friend, deleted-call, and partial-replay behavior, the full through-PA23
gate, PA24 report, audit, and associated-scope width scaling cover the boundary.

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

## Completed Checkpoints

| Checkpoint | Commit | Disposition |
| --- | --- | --- |
| Explicit specialization identity and definition publication | `62f37ef1` | Eight failures removed; 376/422 pa24, 3,049/3,049 through pa23, audit pass. |
| Canonical non-type arguments, designators, static-value demand, and variable-template lowering identity | `6b06e092` | Fifteen failures removed; 391/422 pa24, 3,049/3,049 through pa23, audit pass. |
| Typed construction-entry demand and nontrivial empty-result ABI | `6a1a66c7` | Four failures removed; 395/422 pa24, 3,049/3,049 through pa23, linear depth probe. |
| Explicit-id ADL and candidate-local deleted/dependent invalidity | this commit | Four failures removed; 399/422 pa24, 3,049/3,049 through pa23, linear ADL-width probe. |

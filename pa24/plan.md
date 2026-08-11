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

Current state: **404/422** pa24 tests (turn start: 403/422); all **3,049/3,049**
tests through pa23 pass.

| Shared behavior and owner | Count | Complete failing set (test basename) |
| --- | ---: | --- |
| Deduction, candidate SFINAE, overload ordering, and conversion/initialization selection (`pa19` deduction/instantiation + ordinary call/initialization) | 8 | `member-operator-template-reference-pattern-partial-order`, `constructor-template-const-ref-enable-if-conversion`, `static-cast-rvalue-ref-skips-conversion-operator`, `dependent-typename-member-enable-if-return`, `defaulted-class-template-argument-pack-prefix-deduction`, `trailing-return-expression-sfinae-default-param`, `conversion-function-template-top-cv-sequence`, `out-of-class-conversion-operator-definition` |
| Dependent owner/current-specialization, alias, function-type, and pack replay through demand/lowering (`pa19`/`pa22`/`pa23`) | 10 | `alias-template-partial-specialization-default-dependent-arg`, `decorated-template-id-type-argument-pack-replay`, `dependent-function-type-member-specialization`, `member-template-result-pack-preserves-nested-function-pointer-owner`, `out-of-class-partial-member-template-owner-parameter-alias`, `forwarding-pack-function-type-enable-if`, `partial-member-template-trailing-result-scope`, `source-namespace-base-sfinae-chain`, `defaulted-nested-class-argument-partial-specialization`, `defaulted-template-arg-partial-base-completion` |

## Active Checkpoint

**Nonterminal dependent template-id retention — completed.** Function-template
shape formation classifies a parameter or result type before building it.
`HasDependentQualifiedType` must inspect template arguments on every component
of a qualified name: in `trait<dependent...>::value`, the dependent template-id
is a nonterminal scope carrier. `BuildParameters` also carries forward names of
earlier function parameters whose types are dependent, so a later
`decltype(parameter.member(...))` joins the same dependency closure. The
declaration owns that syntax and publishes the shared nondeduced type shape;
candidate substitution later replays it in the completed parameter environment,
where template-template proxies and dependent member types resolve to canonical
concrete owners.

This applies `spec.md` sections 2-5, 8, and 9: retained syntax remains attached
to its declaration, only dependent work is replayed in a parent-linked
environment, and concrete lookup/demand stays on existing owner-local paths.
Classification visits each syntax node and template argument once, O(S), with
alias expansion bounded by alias depth A; candidate replay complexity is
unchanged. Validation removes
`constructor-template-default-constraint-previous-param`; the related dependent
return test now reaches its separately owned default-constructor emission path.
A qualified-component width probe, the full PA24 and through-PA23 reports, and
the file audit cover the boundary.

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

## Completed Checkpoints

| Checkpoint | Commit | Disposition |
| --- | --- | --- |
| Explicit specialization identity and definition publication | `62f37ef1` | Eight failures removed; 376/422 pa24, 3,049/3,049 through pa23, audit pass. |
| Canonical non-type arguments, designators, static-value demand, and variable-template lowering identity | `6b06e092` | Fifteen failures removed; 391/422 pa24, 3,049/3,049 through pa23, audit pass. |
| Typed construction-entry demand and nontrivial empty-result ABI | `6a1a66c7` | Four failures removed; 395/422 pa24, 3,049/3,049 through pa23, linear depth probe. |
| Explicit-id ADL and candidate-local deleted/dependent invalidity | `4c694582` | Four failures removed; 399/422 pa24, 3,049/3,049 through pa23, linear ADL-width probe. |
| Retained current-specialization dependency | `739eab0f` | Two failures removed; 401/422 pa24, 3,049/3,049 through pa23, linear reference-width probe. |
| Default-expression candidate completion and partial ordering | `b555a4eb` | Two failures removed; 403/422 pa24, 3,049/3,049 through pa23, linear candidate-width probe. |
| Nonterminal dependent template-id and prior-parameter retention | this commit | One failure removed; 404/422 pa24, 3,049/3,049 through pa23, linear constraint-width probe. |

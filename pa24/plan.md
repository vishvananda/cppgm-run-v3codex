# PA24 Implementation Plan

## Stage Design and Spec Alignment

PA24 composes the PA19-PA23 template engine through the existing typed semantic
graph and LowIR path. Retained template syntax is parsed once; canonical class,
function, argument, specialization, scope, and binding identities own replay,
candidate failure, demand, and emission facts. This stage adds no textual
transport or alternate lowering path.

Relevant `spec.md` requirements are canonical O(1)-average identity and
owner-indexed lookup (sections 2-3), complete specialization keys, monotonic
fact states, immutable parent-linked environments, dependent-only replay, and
demanded bodies (section 4), deduplicated fact work (section 5), and lowering
from selected declarations and emission identities without relookup (section
6). Work must remain proportional to owner-local candidates, demanded
specializations, and dependent nodes (section 9); storage remains translation-
unit-owned semantic facts plus phase-local replay scratch (section 8).

## Current Failure Map

Current checkpoint baseline: **376/422** pa24 tests (turn start: 368/422);
all 3,049 tests through pa23 pass.

| Shared behavior and owner | Count | Complete failing set (test basename) |
| --- | ---: | --- |
| NTTP type/value/designator/default and static-value identity (`pa20` arguments + `pa21` constants/storage) | 14 | `dependent-qualified-nontype-base-argument`, `intermediate-type-transform-value-nontype`, `nontype-function-parameter-adjustment`, `nontype-integral-value-binding-conversion`, `structured-bool-boost-convertible-mpl-overload`, `qualified-member-variable-template-class-value`, `dependent-qualified-sizeof-static-member`, `dependent-sizeof-template-default`, `extern-template-static-data-declaration`, both `function-template-nontype-function-pointer-*`, `nontype-function-pointer-argument`, `partial-specialization-member-primary-param-name`, `function-deduction-dependent-defaulted-nontype` |
| Deduction, candidate SFINAE, overload ordering, and conversion/initialization selection (`pa19` deduction/instantiation + ordinary call/initialization) | 17 | `adl-explicit-template-id-call`, `member-operator-template-reference-pattern-partial-order`, `constructor-template-const-ref-enable-if-conversion`, `defaulted-class-template-exact-overload-rank`, `defaulted-enable-if-template-parameter-selection`, `incomplete-sizeof-partial-specialization-sfinae`, `static-cast-rvalue-ref-skips-conversion-operator`, `concrete-enable-if-nontype-parameter-type-sfinae`, `constructor-template-default-constraint-previous-param`, `dependent-typename-member-enable-if-return`, `defaulted-class-template-argument-pack-prefix-deduction`, `constructor-default-pack-partial-ordering`, both `deleted-function-template-*-sfinae`, `trailing-return-expression-sfinae-default-param`, `conversion-function-template-top-cv-sequence`, `out-of-class-conversion-operator-definition` |
| Dependent owner/current-specialization, alias, function-type, and pack replay through demand/lowering (`pa19`/`pa22`/`pa23`) | 15 | `constructor-template-parameter-shadows-instantiated-type`, `dependent-bool-base-trait-type-argument`, `function-template-empty-pack-trailing-default`, `alias-template-partial-specialization-default-dependent-arg`, `common-type-current-specialization-member-return`, `decorated-template-id-type-argument-pack-replay`, `dependent-function-type-member-specialization`, `member-template-result-pack-preserves-nested-function-pointer-owner`, `out-of-class-partial-member-template-owner-parameter-alias`, `dependent-function-type-pack-expansion-ctor-init`, `forwarding-pack-function-type-enable-if`, `partial-member-template-trailing-result-scope`, `source-namespace-base-sfinae-chain`, `defaulted-nested-class-argument-partial-specialization`, `defaulted-template-arg-partial-base-completion` |

## Active Checkpoint

**Canonical non-type arguments and designators.** Preserve adjusted NTTP types,
converted integral constants, function/variable declaration identity,
dependent defaults, and qualified static-value ownership from `pa20` argument
construction through `pa21` constant/address facts and specialization demand.
Lowering must consume the recorded value or function designator rather than
turning it back into a direct name lookup.

The owner is the canonical template-argument/value fact keyed by template,
argument list, and environment; data flows through parameter binding and
constant/address evaluation into selected declarations, storage demand, and
typed LowIR. Expected work is O(A + T + D), for argument count A, dependent
type/expression size T, and newly demanded facts D, with O(1)-average canonical
key lookup. Validate all 14 mapped fixtures, earlier template reports, the
through-pa23 gate, file audit, and value/function-designator scale probes.

## Performance Evidence

Explicit-specialization definition/redeclaration scale 1/2/4/8/16/32 produced
template requests 4/7/13/25/49/97, cache hits 3/5/9/17/33/65, lookup-scope
visits 9/10/12/16/24/40, and semantic times 0.326/0.373/0.550/0.800/1.314/
2.306 ms. Demand pushes stayed at one. This supports owner-local linear work
and shows that cache hits no longer replay primary class member definitions.

## Completed Checkpoints

| Checkpoint | Commit | Disposition |
| --- | --- | --- |
| Explicit specialization identity and definition publication | pending | Eight failures removed; 376/422 pa24, 3,049/3,049 through pa23, and file audit pass. |

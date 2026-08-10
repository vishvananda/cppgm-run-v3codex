# PA23 Plan

## Stage Design and Spec Alignment

PA23 completes C++11 function-template deduction, partial ordering, and
substitution failure on the retained PA19-PA22 semantic graph and PA15 typed
LowIR path. `spec.md` sections 1-6 require one parsed branch, canonical
specialization identity, declaration-owned retained syntax, candidate-local
failure, demand-driven completion, and lowering that consumes selected facts;
section 9 requires work proportional to participating shapes and candidates.
`TypeTable` owns canonical structure, template patterns own source syntax and
lexical context, deduction/overload resolution own candidate frames and
selection, instantiation owns request state and completion, and lowering does
not rediscover template semantics.

## Current Failure Map

Current result is 269/401, up from the turn baseline of 266 and stage baseline
of 223 with no regressions; all `200-*` tests pass. The remaining 132 failures
group by primary owner and observed kind: call/address/constructor deduction
(`100-*`: 14 exits, 2 LowIR), immediate substitution and demand (`300-*`: 66
exits, 9 LowIR), canonical non-type/conversion arguments (`400-*`: 18 exits,
1 LowIR), and composed lookup/alias/class paths (`500-*`: 13 exits, 9 LowIR).

## Active Checkpoint

Three remaining `100-*` failures share the function-designator boundary:
explicit template-ids and overload sets used as call arguments must remain
candidate sets until a parameter target supplies a function type. N3485 13.4,
14.8.1, and 14.8.2.1 require explicit arguments to form viable template
specializations, a unique target-compatible function to participate in
deduction, and an unresolved set to remain non-deduced. Parsed id-expressions
own explicit syntax; target-aware function lookup owns specialization and
ordinary-overload candidates; deduction owns uniqueness; call conversion
records the selected canonical binding for lowering. Expected work is
O(overloads * deduction shape), cached per `(syntax,target)`. Validate the
explicit-address, user-conversion, and unique-overload failures, free/member
function-pointer guards, all `100-*`, PA1-PA22, and audit; measure doubled
overload sets.

## Performance Evidence

Earlier default, pack, overload, retained-partial, and inherited-constructor
probes scale linearly in their participating requests/candidates. For braced
overload sets with both candidates and list elements doubled through
32/64/128/256, three runs each took 0.01/0.04/0.15/0.59-0.61 s, matching the
expected O(candidates * elements) traversal without superlinear cache churn.
Final gates are PA1-PA22 2,639/2,639, PA23 269/401, zero regressions, and audit
pass with 13 inherited warnings.

## Completed Checkpoints

| Checkpoint | Result |
| --- | --- |
| Array-extent NTTP deduction and candidate ownership | Ordinary/repeated/hidden-friend/constructor probes pass; PA23 169 -> 177, exact baseline retained, linear scaling. |
| Non-deduced qualified types and compound array bounds | Qualified replay, ordering, rebind, and compact array lowering pass; 177 -> 186, no regressions, linear scaling. |
| Defaulted function-template materialization | Declaration-owned defaults and explicit request states pass; 186 -> 211 overall with linear success/failure scaling. |
| Explicit-call expression substitution and variadic class boundary | Candidate failure spans defaults through expressions and lowering consumes ellipsis storage facts; 211 -> 223, no regressions, linear scaling. |
| Concrete replay of retained class-partial type arguments | Dependent alias/`void_t`/array/`decltype` patterns replay after deduction; builtin invoke preserves call result facts, hard body errors escape SFINAE, and candidate result formation is demand-driven; 223 -> 248, no regressions, linear replay scaling. |
| Typed function-pack deduction and partial ordering | Inner/trailing/empty/repeated packs, active defaulted tails, and direct `T&`/forwarding-`T&&` ordering pass; 248 -> 255, seven gains, no regressions, linear pack/candidate scaling. |
| Dependent class defaults and lazy type identity | Symbolic defaults retain canonical non-deduced facts, aliases avoid eager layout, explicit uses demand completion, re-entrant layout retains stable owners (ASan-clean), and typed ordering selects correctly; 255 -> 263, eight gains, no regressions, linear scaling. |
| Inherited member-template ordering and virtual emission | Constructor-template using edges synthesize derived wrappers, exact reference ranks expose typed partial ordering, and canonical RTTI/vtable symbols emit once; all 46 `200-*` pass, 263 -> 266, no regressions, linear candidate scaling. |
| Contextual braced-call deduction and materialization | Lists stay untyped through deduction, rank per selected parameter, materialize scalar/class/array arguments once, and preserve constexpr temporary lifetime; three gains, 266 -> 269, no regressions, O(candidates * elements). |

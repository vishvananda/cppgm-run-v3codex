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

Current result is 289/401, up from this turn's 284 and stage baseline of 223
with no regressions; all `200-*` tests pass. The remaining 112 failures group
by primary owner and observed kind: call/address/constructor deduction
(`100-*`: 1 exit, 1 LowIR), immediate substitution and demand (`300-*`: 61
exits, 10 LowIR), canonical non-type/conversion arguments (`400-*`: 17 exits,
1 LowIR), and composed lookup/alias/class paths (`500-*`: 12 exits, 9 LowIR).

## Active Checkpoint

The next checkpoint closes the two remaining `100-*` boundaries: deduction
through a namespace-qualified alias parameter on an inherited member template,
and preservation of reference-to-array cv/default facts through LowIR. N3485
8.3.5 and 14.8.2.1 require deduction against the canonical parameter before
function adjustment while retaining the selected declaration's exact boundary
type. Alias/template lookup owns the source pattern and inherited using edge;
deduction owns canonical arguments; initialization/lowering consume the chosen
array-reference parameter without reconstructing it. Expected work is O(C*S+E)
for C candidates, retained shape S, and E array elements, with specialization
keys on pattern plus canonical arguments. Validate both direct targets, related
inherited-alias and array-cv/default guards, all `100-*`, PA1-PA22, and audit;
measure doubled candidate counts and array extents.

## Performance Evidence

Earlier default, pack, overload, retained-partial, and inherited-constructor
probes scale linearly in their participating requests/candidates. For braced
overload sets with both candidates and list elements doubled through
32/64/128/256, three runs each took 0.01/0.04/0.15/0.59-0.61 s, matching the
expected O(candidates * elements) traversal without superlinear cache churn.
For 64/128/256/512 ordinary overloads, function-template deduction visits were
256/512/1,024/2,048, peak semantic storage was 0.78/1.55/3.10/6.19 MB, and wall
time was 0.00/0.01/0.02/0.04 s. For 8/16/32/64/128 positional member-initializer
pack elements, semantic nodes were 68/108/188/348/668, candidate-index visits
9/17/33/65/129, peak stage storage 0.11/0.15/0.25/0.43/0.85 MB, and all three
wall runs were below 0.01 s, confirming linear produced-element work. Final
candidate-prefix probes at 16/32/64/128 candidates took
0.00/0.00/0.01/0.01-0.02 s with peak RSS 5.9/6.6/6.9/8.4 MB; nested-array
depths 16/32/64/128 all took under 0.01 s and at most 6.6 MB. Final gates are
PA1-PA22 2,639/2,639, PA23 289/401, zero regressions, and audit pass with 13
inherited warnings. For 16/32/64/128 omitted-argument explicit specializations,
three runs took 0.00/0.00/0.01/0.01 s and at most 7.5 MB; retained trailing
result depths 16/32/64/128 took 0.00/0.00/0.00/0.01 s and at most 7.2 MB.

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
| Target-aware function-designator deduction | Explicit-fixed parameters defer to conversion, ordinary overloads trial-deduce in isolated state, address conversion applies normal non-template preference, and selected syntax replays once; four gains, 269 -> 273, no regressions, linear overload scaling. |
| Typed pack-expansion initialization and constexpr union state | Scalar/member init consumes one typed positional sequence, empty constructor packs participate, special-member template specifiers persist, and constexpr unions retain only the active member; six gains, 273 -> 279, no regressions, linear scaling. |
| Canonical explicit-prefix, array-cv, and reference-result flow | Incomplete prefixes leave later packs unbound, cv subtraction preserves array shape, and class lvalue conditionals lower through reference addresses; five gains, 279 -> 284, no regressions, linear scaling. |
| Explicit template identity and specialization result replay | Type/function explicit-ids retain syntax, complete types deduce omitted arguments, inherited defaults and selected bodies replay, synthesized constructors stay out of ordinary lookup, and aggregate returns keep one lowering identity; 284 -> 289, no regressions, linear specialization/depth scaling. |

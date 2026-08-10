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

Current result is 295/401, up from this checkpoint's 292 and stage baseline of
223 with no regressions; all `100-*` and `200-*` tests pass. The remaining 106
failures group by primary owner and observed kind: immediate substitution and
demand (`300-*`: 58 exits, 9 LowIR), canonical non-type/conversion arguments
(`400-*`: 17 exits, 1 LowIR), and composed lookup/alias/class paths (`500-*`:
12 exits, 9 LowIR).

## Active Checkpoint

The next checkpoint owns concrete candidate-result replay in the `300-*`
group, beginning with dependent `enable_if` values, invalid alias formation,
member-call probes, and detector fallbacks. `spec.md` sections 3-6 require
retained syntax and lexical/use contexts, canonical argument substitution in
an isolated candidate frame, immediate-context failure without body demand,
and selected facts reused by instantiation. Result formation owns the flow from
pattern plus canonical arguments through alias/class lookup to candidate
viability; the request cache owns the resulting success/failure state.
Expected work is O(C*(S+L+A)) for C candidates, retained shape S, lookup L, and
participating alias edges A, memoized by pattern, canonical arguments, and
lookup context. Validate focused enable-if, invalid-alias, member-call, and
detector probes, then all `300-*`, PA1-PA22, and audit; measure doubled
candidate counts and retained-result/alias depths.

## Performance Evidence

Earlier default, pack, overload, retained-partial, and inherited-constructor
probes scale linearly in their participating requests/candidates. For braced
overload sets with both candidates and list elements doubled through
32/64/128/256, three runs each took 0.01/0.04/0.15/0.59-0.61 s, matching the
expected O(candidates * elements) traversal without superlinear cache churn.
For 64/128/256/512 ordinary overloads, function-template deduction visits were
256/512/1,024/2,048, peak semantic storage was 0.78/1.55/3.10/6.19 MB, and wall
time was 0.00/0.01/0.02/0.04 s. For 8/16/32/64/128 positional member-initializer
pack elements, semantic nodes were 68/108/188/348/668 and all three wall runs
were below 0.01 s. Candidate-prefix and retained-result depth probes through
128 took at most 0.02 s and 8.4 MB. For 16/32/64/128 simultaneously viable
inherited-using candidates, three runs took 0.00/0.01/0.01/0.02-0.03 s with
peak RSS 6.6/7.1/7.5/8.7 MB; reference-to-array extents 64/128/256/512 all took
under 0.01 s and at most 6.4 MB. For 16/32/64/128 simultaneously formed
dependent-result candidates, three runs took 0.00/0.00/0.01/0.02 s with peak
RSS 6.6/7.3/8.0/9.0 MB; retained-result depths 64/128/256/512 all took under
0.01 s and at most 6.7 MB. Final gates are PA1-PA22 2,639/2,639, PA23 295/401,
zero regressions, and audit pass with 13 inherited warnings.

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
| Inherited-using identity and reference/base lowering | Access-owned specialization aliases, local-signature preference, array-reference stores, and nonempty-base value initialization pass; all `100-*` pass, 289 -> 292, no regressions, linear/flat scaling. |
| Declaration-time result lookup and canonical identity | Deferred results validate nondependent calls/templates once in their retained scope and equivalent global/unqualified roots merge by resolved entity; 292 -> 295, no regressions, linear result/candidate scaling. |

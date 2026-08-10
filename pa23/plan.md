# PA23 Plan

## Stage Design and Spec Alignment

PA23 completes C++11 function-template deduction, partial ordering, and
substitution failure on the retained PA19-PA22 semantic graph and PA15 typed
LowIR path. `spec.md` sections 1-6 require one parsed branch, canonical
specialization identity, declaration-owned retained syntax, candidate-local
failure, demand-driven completion, and lowering that consumes selected facts;
section 9 requires work proportional to participating shapes and candidates.
`TypeTable` owns canonical structure, template patterns own source syntax and
lexical context plus first-declaration result lookup facts, deduction and
overload resolution own candidate frames and selection, instantiation owns
request state and completion, and lowering does not rediscover template
semantics. Equivalent result declarations remap retained facts once by syntax
and compare canonical declaration or namespace identity thereafter.

## Current Failure Map

Current result is 298/403 including two audit guards, or 296/401 on the original
suite, up from this checkpoint's 292 and stage baseline of 223 with no
regressions; all `100-*` and `200-*` tests pass. The remaining 105
failures group by primary owner and observed kind: immediate substitution and
demand (`300-*`: 57 exits, 9 LowIR), canonical non-type/conversion arguments
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
candidate counts and retained-result/alias depths. First-declaration lookup is
now an input fact to this checkpoint rather than work result formation repeats.

## Performance Evidence

Earlier deduction, pack, overload, and inherited-member probes remain linear in
participating requests and produced elements. The audited first-lookup path was
measured directly: for 32/64/128/256 paired retained call/type declarations,
lookup queries are 954/1,882/3,738/7,450, deduction visits are
128/256/512/1,024, peak semantic bytes are
1,586,723/3,158,951/6,038,783/12,061,031, and three-run semantic medians are
6.87/13.12/25.84/51.59 ms. At nested result depths 16/32/64/128, lookup queries
are 76/124/220/412, peak bytes are 90,279/133,607/308,466/694,579, and medians
are 0.79/1.20/2.12/4.78 ms. Final gates are PA1-PA22 2,639/2,639, original
PA23 296/401 and full PA23 298/403, zero regressions, and file-audit pass with
13 inherited warnings.

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
| Declaration-time result lookup and canonical identity | Deferred results retain canonical type/namespace roots and first-declaration call candidates through redeclaration and substitution; fixed qualifiers validate immediately, recursive calls stay bounded, original PA23 292 -> 296 (298/403 with audit guards), no regressions, linear candidate/depth scaling. |

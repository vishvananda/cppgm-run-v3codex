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
and compare canonical declaration or namespace identity thereafter. Canonical
type construction exposes typed no-throw validity to candidate frames, alias
requests retain distinct expected- and hard-failure states, and retained
`decltype` bases are selected by syntax identity rather than payload text.
Expression validity records canonical virtual-base and exceptional-action
facts; one complete-object pointer-arithmetic owner supplies binary, increment,
subscript, and compound forms before a selected assignment publishes its typed
lowering fact.

## Current Failure Map

The report is 343/405, with every `100-*` and `200-*` test passing. The 62
remaining failures group by owner: retained result/default/member lookup and
candidate-local substitution (`300-*`: 31 exits), template pack/default
partition and constructor participation (`400-*`: 4 exits), and composed
owner/pack replay (`500-*`: 4 exits). The other 23 are selected-fact lowering
or ABI-shape mismatches (`300/400/500`: 10/2/11). Canonical object, reference,
function, static-member, and null pointer NTTP identity has left the map.

## Active Checkpoint

Template pack/default partition and constructor candidate participation is the
next stable boundary. The six remaining `400-*` failures cover packs before
defaulted NTTPs, mismatched expansion sizes, unnamed non-type packs, inherited
constructor templates, invocability lowering, and template-template default
arity. Per `spec.md` sections 3-5, declaration-owned parameter shape feeds one
candidate-local argument partition; deduction, default substitution, arity
validation, and partial ordering consume that partition without mutating the
template pattern. Selected facts alone reach lowering (`spec.md` section 6).
The template argument builder and function-template deduction owner carry the
data into constructor participation and overload resolution. Expected work is
O(C*(A+P)) for C candidates, A arguments, and P parameters, with indexed
specialization lookup. Validate all remaining `400-*`, nearby `300-*`/`500-*`
pack and constructor guards, PA23, PA1-PA22, and file audit; measure doubled
pack widths and candidate counts.

## Performance Evidence

For 8/16/32/64 independent abstract-expression units, candidates are
16/32/64/128, specialization requests 80/160/320/640, deduction visits
32/64/128/256, and lookups 336/672/1,344/2,688; three-run semantic medians are
1.97/3.65/6.89/13.32 ms and per-unit peak storage stays 57,488 bytes. Virtual
base chains of depth 16/32/64/128 take 34/66/130/258 typed path visits,
164/292/548/1,060 lookups, 165,575/297,811/585,639/1,147,815 peak bytes, and
0.88/1.26/2.10/3.88 ms medians. Complete/incomplete-pointer probe groups at
8/16/32/64 units take 256/512/1,024/2,048 candidates,
832/1,664/3,328/6,656 specialization requests,
1,341,484/2,668,404/5,142,620/10,271,148 peak bytes, and
7.87/15.91/30.42/61.87 ms medians. For 8/16/32/64 independent callable units,
candidates are 96/192/384/768, specialization requests 112/224/448/896,
deduction visits 96/192/384/768, lookups 1,221/2,437/4,869/9,733, peak bytes
1,316,475/2,571,681/5,129,121/10,246,209, and medians
6.94/13.92/27.31/56.60 ms. Detector base depths 8/16/32/64 visit
16/32/64/128 lookup edges and 177/241/369/625 lookups with five candidates and
11 requests flat; peak bytes are 243,267/302,575/446,765/779,629 and medians
1.51/1.63/2.17/3.01 ms. For 8/16/32/64 constructor/conversion-template owners,
candidates are 32/64/128/256, requests and deduction visits are
16/32/64/128, lookups are 340/676/1,348/2,692, peak stage bytes are
363,555/722,780/1,441,196/2,878,156, and three-run semantic medians are
2.02/3.73/7.04/14.07 ms. For 8/16/32/64 retained owner-alias default units,
candidates are 16/32/64/128, requests 105/209/417/833, lookups
428/788/1,508/2,948, peak bytes 604,038/1,040,877/2,056,813/3,795,309, and
three-run semantic medians 3.26/5.70/10.70/21.00 ms. Work, storage, and time are
linear. Pointer-like NTTP groups of 8/16/32/64 entities make 24/48/96/192
specialization requests, 40/80/160/320 canonical-list requests, and
246/486/966/1,926 lookups; peak bytes are
241,231/457,028/906,506/1,806,076 and three-run semantic medians are
1.13/1.89/3.54/6.93 ms. Canonical hash/cache work, storage, and time remain
linear; repaired owner/result and pointer/reference/function/null NTTP probes,
the 64-entity case, and distinct `decltype`/`noexcept` contexts are
ASan/UBSan-clean. Gates are PA1-PA22 2,639/2,639, PA23 343/405, and file-audit
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
| Target-aware function-designator deduction | Explicit-fixed parameters defer to conversion, ordinary overloads trial-deduce in isolated state, address conversion applies normal non-template preference, and selected syntax replays once; four gains, 269 -> 273, no regressions, linear overload scaling. |
| Typed pack-expansion initialization and constexpr union state | Scalar/member init consumes one typed positional sequence, empty constructor packs participate, special-member template specifiers persist, and constexpr unions retain only the active member; six gains, 273 -> 279, no regressions, linear scaling. |
| Canonical explicit-prefix, array-cv, and reference-result flow | Incomplete prefixes leave later packs unbound, cv subtraction preserves array shape, and class lvalue conditionals lower through reference addresses; five gains, 279 -> 284, no regressions, linear scaling. |
| Explicit template identity and specialization result replay | Type/function explicit-ids retain syntax, complete types deduce omitted arguments, inherited defaults and selected bodies replay, synthesized constructors stay out of ordinary lookup, and aggregate returns keep one lowering identity; 284 -> 289, no regressions, linear specialization/depth scaling. |
| Inherited-using identity and reference/base lowering | Access-owned specialization aliases, local-signature preference, array-reference stores, and nonempty-base value initialization pass; all `100-*` pass, 289 -> 292, no regressions, linear/flat scaling. |
| Declaration-time result lookup and canonical identity | Deferred results retain canonical type/namespace roots and first-declaration call candidates through redeclaration and substitution; fixed qualifiers validate immediately, recursive calls stay bounded, original PA23 292 -> 296 (298/403 with audit guards), no regressions, linear candidate/depth scaling. |
| Candidate-local alias, call-surrogate, and detector replay | Nested aliases defer without body demand, explicit invalid types use typed candidate failure and monotonic alias states without exception control flow, retained `decltype` bases replay by syntax identity, and ellipsis fallbacks participate; landed 298 -> 307, audited full report 308/404, no regressions, linear failure scaling. |
| Candidate-local expression validity and typed exceptional facts | Abstract construction/allocation, narrowing, virtual-base casts, scalar pseudo-destruction, and `void()` dispatch use scoped typed facts; the audit unified complete-object pointer arithmetic and typed comparison/arithmetic failure before lowering; original 308 -> 315, audit guard 316/405, no regressions, linear candidate/path scaling. |
| Dependent callable replay and re-entrant detector demand | Lexical value/type depth selects callable objects correctly; canonical class requests expose scoped in-progress state; invalid callees stay candidate-local; ellipsis conversions publish typed recipes; 316 -> 320, no regressions, sanitizer-clean, linear call/depth scaling. |
| Constructor and conversion-function template participation | Entity-owned conversion patterns feed target deduction; constructor templates join conversion ranking; dependent `decltype` and template-template targets retain shape identity; selected class results materialize without disturbing constexpr objects; 320 -> 330, no regressions, sanitizer-clean, linear owner-local scaling. |
| Dependent owner-qualified alias/member/default replay | Alias results retain non-deduced shapes, target matching validates deferred positions after deduction, equivalent leading/trailing results remap first-declaration facts by canonical identity, and `decltype`/`noexcept` keep distinct temporary semantics; 330 -> 334, no regressions, sanitizer-clean, linear owner scaling. |
| Canonical pointer/reference/function NTTP identity | Canonical source bindings and null identity survive hashed specialization keys, constexpr replay, LowIR identity, and entity/reference ABI encoding; local statics fail and selected function addresses create demand; 334 -> 343, no regressions, sanitizer-clean, linear identity scaling. |

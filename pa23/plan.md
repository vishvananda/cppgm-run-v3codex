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

The original suite remains at its 315/404 checkpoint baseline; the audit guard
makes the combined report 316/405, with no regression and all `100-*` and
`200-*` tests passing. The complete remaining set groups by shared behavior and
primary owner: candidate result/default substitution, dependent lookup, and
demand timing (`300-*`: 45 exits, 8 LowIR); canonical non-type arguments,
conversion templates, and overload ownership (`400-*`: 16 exits, 2 LowIR);
and composed alias/class/member lookup, pack replay, and selected-fact lowering
(`500-*`: 8 exits, 10 LowIR). These groups account for all 89 failures.

## Active Checkpoint

Dependent callable replay and re-entrant detector demand are the next stable
boundary. Retained call syntax and canonical argument/object types flow through
indexed ordinary/member/ADL lookup into one overload candidate sequence;
candidate frames own failure while specialization request states own recursive
observation. `spec.md` sections 2-5 require canonical object cv identity,
explicit lookup edges, complete candidate keys, in-progress observation, and
no global retry; section 6 requires the selected call/conversion facts to reach
lowering unchanged; section 9 bounds work to O(C*(A+L)) for participating
candidates C, argument count A, and visited lookup edges L. Validate mutable
versus const call operators, recursive streamable/swappable detectors, call
surrogates, positive/fallback/hard-error pairs, all remaining `300-*`, PA23,
PA1-PA22, and file audit; measure doubled call sets and recursion depth.

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
7.87/15.91/30.42/61.87 ms medians. Candidate work, graph work, storage, and
time are linear. Gates are PA1-PA22 2,639/2,639, original PA23 315/404 and
combined 316/405 with no regressions, and file-audit pass with 13 inherited
warnings.

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

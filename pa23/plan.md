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

## Current Failure Map

Current result is 308/404 including three audit guards, or 305/401 on the
original suite. The landed checkpoint's 307/403 baseline is preserved with no
regression; all `100-*` and `200-*` tests pass. The complete remaining set
groups by shared behavior and primary owner: candidate result/default
substitution, dependent lookup, and demand timing (`300-*`: 52 exits, 8
LowIR); canonical non-type arguments, conversion templates, and overload
ownership (`400-*`: 16 exits, 2 LowIR); and composed alias/class/member lookup,
pack replay, and selected-fact lowering (`500-*`: 8 exits, 10 LowIR). These
groups account for all 96 failures.

## Active Checkpoint

The next checkpoint owns candidate-local expression validity in the remaining
`300-*` set: abstract construction, narrowing, cast legality, overloaded
operators, pseudo-destructors, and `noexcept` probes. `spec.md` sections 2-5
require canonical expression facts, isolated expected failure, and cached
demand; section 6 requires lowering to consume only the selected facts, and
section 9 requires O(C*(E+L)) work for C candidates, dependent expression size
E, and indexed lookup L. Expression analysis owns typed validity, overload
resolution owns candidate discard, and instantiation/lowering consume the
winner without reanalysis. Validate focused positive/fallback/hard-error pairs,
all `300-*`, PA1-PA22, and audit; measure doubled expression and candidate sets.

## Performance Evidence

Independent invalid-alias sets of 16/32/64/128 produce 16/32/64/128 overload
visits, 176/352/704/1,408 specialization requests, 96/192/384/768 cache hits,
32/64/128/256 deduction visits, 844/1,660/3,292/6,556 lookups, and
727,475/1,446,883/2,887,131/5,766,323 peak semantic bytes. Three-run semantic
medians are 4.08/7.89/15.35/30.90 ms. Work, storage, and time are linear in
participating failures, and representative failed paths have no C++ unwind.
Final gates are PA1-PA22 2,639/2,639, PA23 308/404 with zero regressions, and
file-audit pass with 13 inherited warnings.

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

# PA23 Plan

## Stage Design and Spec Alignment

PA23 completes C++11 function-template deduction, ordering, and substitution
failure on the retained PA19-PA22 graph and PA15 typed LowIR path. Per
`spec.md` sections 1-6 and 9, template patterns own syntax and lexical context,
canonical tables own identity, candidate frames own recoverable failure,
instantiation owns demand/completion state, and lowering consumes selected
facts. Lookup and specialization remain indexed, with work proportional to the
participating candidates, arguments, parameters, and subobject edges.

## Current Failure Map

The report is 351/405, with all `100-*`, `200-*`, and `400-*` tests passing.
The 54 remaining failures group into retained result/default/member lookup and
candidate-local substitution (`300-*`: 28 exits, 11 LowIR) and composed
owner/pack replay (`500-*`: 4 exits, 11 LowIR). Template pack/default
partition, constructor participation, inherited constructor lookup, and
constant-leaf lowering have left the map.

## Active Checkpoint

Retained nested-owner result/default replay is the next stable boundary. The
related `300-*` failures cover nested aliases, declaring-scope defaults,
inherited member templates, hidden friends, and lazy class completion. Per
`spec.md` sections 3-5, the pattern owns immutable lookup/context facts;
deduction binds one candidate-local environment; alias/member lookup consumes
that environment without eager body demand; failed formation drops only that
candidate. The semantic result replayer owns the flow from retained syntax to
canonical lookup identity and specialization demand. Expected work is
O(C*(A+P+L)) for C candidates, A arguments, P parameters, and L indexed lookup
edges. Validate the grouped `300-*` owner/alias cases, neighboring guards,
PA23, PA1-PA22, sanitizer stress, and file audit; measure doubled owner depth
and candidate counts.

## Performance Evidence

Prior doubled-size probes for expression validity, virtual bases, pointer
types, callable deduction, detector depth, constructor/conversion templates,
owner aliases, and pointer NTTP identity all showed linear work and storage.
For this checkpoint, 128 explicit calls with pack widths 8/16/32/64 took
three-run wall medians 0.01/0.02/0.03/0.05 s and peak RSS
7,276/7,580/8,256/9,648 KiB. With fixed width and 8/16/32/64 overload
candidates, medians were 0.02/0.03/0.06/0.12 s and peak RSS
8,304/9,100/11,380/15,284 KiB. Both 64-scale cases and all eight repaired
fixtures are ASan/UBSan-clean. Gates are PA1-PA22 2,639/2,639, PA23 351/405,
and file-audit pass with 13 inherited warnings.

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
| Template pack/default partition and constructor participation | Per-parameter offsets preserve non-terminal packs and defaults; unnamed NTTP packs, inherited constructors, empty subobject chains, qualified `sizeof` calls, and constant leaves retain typed ownership; all `400-*` pass, 343 -> 351, no regressions, sanitizer-clean, linear scaling. |

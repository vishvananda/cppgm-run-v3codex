# PA23 Plan

## Stage Design and Spec Alignment

PA23 completes C++11 function-template deduction, ordering, and substitution
failure on the retained PA19-PA22 graph and PA15 typed LowIR path. Per
`spec.md` sections 1-6 and 9, template patterns own syntax and lexical context,
canonical tables own identity, candidate frames own recoverable failure,
specializations own separate result, exception, body, and emission demand
states, and lowering consumes selected facts. Dependent exception
specifications use monotonic per-specialization completion and canonical
post-reentry publication. Lookup and specialization remain indexed, with work
proportional to participating candidates, arguments, parameters, demand edges,
and subobjects.

## Current Failure Map

The report is 375/406, with all `100-*`, `200-*`, and `400-*` tests passing.
The 31 failures are 12 exits and 19 LowIR mismatches (`300-*`: 16; `500-*`:
15). They group by owner into retained alias/default/pack/member-result replay
(16), deduction/partial-order/non-deduced and array conversion (7), and
constructor or conversion-function participation/lowering (8).

## Active Checkpoint

Defaulted value/pack replay through aliases and dependent result formation is
the next substantial stable boundary and owns the largest remaining cluster
(16 failures). Per `spec.md` sections 3-6, declarations own default syntax and
lexical scope, explicit/deduced argument partitions own pack offsets,
candidate frames own recoverable substitution failure, and lowering consumes
only the selected typed result. The flow is canonical pattern plus argument
key -> pack partition -> scoped defaults -> alias/result substitution ->
selected candidate fact. Expected work is O(C*(A+P+R)) for C candidates, A
arguments, P parameters, and R replayed syntax edges, with specialization and
default requests cached by canonical key. Validate empty/nonempty packs,
dependent bool/NTTP defaults, nested aliases, short-circuit failure, LowIR
identity, unaffected deduction/constructor guards, scaling, sanitizers, PA23,
PA1-PA22, and audit.

## Performance Evidence

For the landed 1/2/4/8 dependent-result elements and hidden-friend owners,
deduction visits were 10/20/40/80 and 8/16/32/64, while associated declaration
visits were 2/4/8/16 and hidden-friend typed storage stayed 1,735 bytes. The
audit's twice-queried 1/2/4/8 dependent exception specializations performed
exactly 1/2/4/8 evaluations and 3/6/12/24 cache hits; lookups were
17/29/53/101, typed LowIR storage stayed 1,735 bytes, and semantic medians were
0.317/0.370/0.464/0.689 ms. Participating-edge work is linear and repeated
exception queries are memoized. The landed gains and audit re-entry guards are
ASan/UBSan-clean. Gates are PA1-PA22 2,639/2,639, PA23 375/406 with the same 31
failures, and file-audit pass with 13 inherited warnings.

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
| Demand-owned class-template shells and wrapped result identity | Typedef/using aliases retain shells without eager body replay; concrete layout and qualified lookup own demand; definition upgrades retry only pending shells and refresh late partial selection; wrapped deferred results preserve overload identity; 351 -> 356, no regressions, sanitizer-clean, linear scaling. |
| Retained explicit-kind frames and alias-expanded result identity | Known type syntax drops non-type candidates without poisoning enclosing replay; alias defaults and packs expand to declaration-bound canonical keys; lookup facts remap across alias/direct redeclarations while distinct overloads remain separate; 356 -> 359, no regressions, sanitizer-clean, bounded scaling. |
| Nested-owner return materialization and alias-expanded member-definition identity | Concrete empty specialization returns demand one implicit constructor; owner aliases normalize to canonical types for out-of-class redeclaration matching; four gains, 359 -> 363, no regressions, sanitizer-clean, flat alias-depth and linear call scaling. |
| Retained member publication and nested demand boundary | Function declarations validate in parameter scopes, nested definitions remain demand-owned, and inherited variable templates use cached base lookup; five gains, 363 -> 368, no regressions, sanitizer-clean, linear scaling. |
| Typed designator publication and specialization-owned hidden friends | Outer shapes publish in retained scopes, compound NTTPs defer, deferred results substitute after target deduction, and hidden definitions keep owner-local identity/indexing; three gains, 368 -> 371, no regressions, sanitizer-clean, linear candidate/owner scaling. |
| Dependent call/result replay and ADL specialization demand | Parameter-dependent results and aliases retain indexed call facts; ADL completes supplied owners; the audit separated exception demand, memoized its state, and repaired canonical post-reentry publication and qualified-name detection; original 371 -> 374, combined 375/406, no regressions, sanitizer-clean, linear scaling. |

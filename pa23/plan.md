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
and subobjects. Zero-cardinality pack initialization publishes one typed
storage contract for declaration, lifetime, and lowering consumers. Retained
static-member definitions publish their value-use storage policy once, while
address/reference formation owns explicit emission demand. Class-key
declarations parse their class specifier once, route directly around bit-field
speculation, and publish restored name facts once.
Nested template parameter lists share a scoped local-name overlay; outer
dependent non-type parameter types resolve in the canonical parameter scope
after preceding bindings, while nested-local dependencies remain symbolic.
Alias-template substitution restores its declaration-owned class privilege;
dead constant arms retain semantic checking without publishing runtime demand.
Typed scalar facts canonicalize null-pointer immediates and comparison widths
before LowIR. Canonical constructor identity owns monotonic empty-chain results:
user constructors require known empty bodies, success retains compact member
and base-entry demand edges, and failure publishes no ABI state. Reusable
generation marks restrict each uncached query to its participating subobjects.

## Current Failure Map

The report is 390/409, with all `100-*`, `200-*`, and `400-*` tests passing.
The 19 failures are 9 exits and 10 LowIR mismatches (`300-*`: 13; `500-*`: 6).
By shared behavior and owner they are deduction/non-deduced/partial ordering
(7), constructor or conversion-function participation (7), and retained
result/default/candidate replay (5). The denominator includes the passing
declaration-only constructor-demand audit guard; the landed 389/408 baseline
and its failure set are unchanged.

## Active Checkpoint

Canonical non-deduced matching and partial ordering is the next substantial
stable boundary and owns seven failures spanning abstract/array parameter
SFINAE, transitive-base deduction, dependent NTTP packs, recursive partial
completion, direct template-id matching, and fixed/variadic template-template
ordering. Per `spec.md` sections 2-5 and 9, canonical parameter shapes and the
candidate deduction frame own bindings; class-partial selection owns recursive
completion; ordering consumes complete typed deductions without rendered-name
fallbacks. The flow is candidate parameter shape -> indexed deduction facts ->
complete canonical arguments -> candidate-local substitution -> typed ordering
result. Expected work is O(C*(P+A+E)) for participating candidates C,
parameters P, arguments A, and expanded elements E, with O(1)-average identity
lookup and one completion per specialization key. Validate all seven grouped
failures, 1/2/4/8 candidate/pack scaling, sanitizers, PA23, PA1-PA22, and audit.

## Performance Evidence

Before the audit repair, nested-class depths 8 and 16 caused 6,272 and
1,605,632 parser checkpoints, and depth 32 exceeded ten seconds. For depths
8/16/32/64/128/256 after parse-once routing, tokens were
53/93/173/333/653/1,293, checkpoints 119/199/359/679/1,319/2,599, rollbacks
79/135/247/471/919/1,815, and fact changes 19/35/67/131/259/515. For 1/2/4/8
dependent function-pointer and template-template parameter pairs, semantic
nodes were 18/21/27/39, specialization requests 2/3/5/9, argument-list
requests and index probes 6/9/15/27, candidate visits 1/2/4/8, and typed storage
4,196/4,204/4,220/4,252 bytes. For the completed alias/default/demand boundary,
1/2/4/8 member-alias specializations produced semantic nodes 27/46/84/160,
specialization requests 14/28/56/112, default materializations 1/2/4/8, access
checks 31/60/118/234, emissions 1/2/4/8, and 4,597/6,720/12,214/23,522 typed
bytes. Argument-list probes were 22/41/80/274 (1.0-1.8 per request, reflecting
bounded small-table collisions); semantic time was 1.03/1.22/1.66/2.43 ms.
For 1/2/4/8 repeated proven-empty and declaration-only constructor pairs amid
16 unrelated classes, empty-chain requests were 2/4/8/16, cache hits
0/2/6/14, entity visits 3/3/3/3, dependency
edges 2/2/2/2, worklist pushes 3/3/3/3, and emissions 3/3/3/3; semantic nodes
were 21/27/39/63 and typed storage 6,413/7,085/8,429/11,117 bytes. The audit
guard raises the combined report to 390/409 without changing the 19 prior
failures, and all nine affected probes are ASan/UBSan-clean. PA1-PA22 pass
2,639/2,639; file audit passes with 13 inherited header-division advisories.

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
| Zero-cardinality expansion lowering and static-member demand | Typed size/alignment drives automatic, global, and local-static storage without element lifetime actions; retained value-use policy and explicit address/reference demand replace syntax reopening; original 375 -> 377, audit guard 378/407, no regressions, sanitizer-clean, linear scaling. |
| Retained enclosing-pack dependency and dependent braced construction | Nested parameter types defer to specialization replay; qualified/template calls avoid speculative type formation; the audit replaced class-body lookahead with parse-once routing and completed declarator/local/template-template dependency ownership in canonical parameter scopes; original 378 -> 380, audit guard 381/408, no regressions, sanitizer-clean, linear scaling. |
| Declaration-owned alias access, dead-arm demand, and typed scalar/ABI finalization | Alias bodies substitute with lexical privilege; short-circuited calls stay undemanded; null and boolean facts lower canonically; empty-chain elision now requires known bodies, memoizes canonical dependency facts, and publishes C2 state only after success; 381 -> 389, audit guard 390/409, no regressions, sanitizer-clean, bounded scaling. |

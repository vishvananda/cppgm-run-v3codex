# PA35 Plan

## Stage Design and Spec Alignment

PA35 keeps the source -> streaming preprocessing/post-tokenization -> integrated
syntax/semantics -> typed LowIR -> native ELF architecture. Relevant `spec.md`
requirements are canonical typed identity and phase flow (§2, §6), demand-owned
template specialization (§4), and bounded, observable heavy-header work (§9).
The shared front end owns all changes; no hosted-only route is introduced.

Static assertions are independent required-evaluation roots: they preserve but
do not inherit a caller's discarded/unevaluated suppression state. Their syntax
nodes own compact source ranges, and the compile-driver telemetry exposes the
existing constexpr request/cache/step counters. Work remains proportional to
the demanded assertion expression and specialization facts; completed calls
continue to use the specialization-owned constexpr cache.

## Current Failure Map

Current PA35 is 53/104 tracked: the required compile report is 53/103 with 50
failures, while the single PA35 run test is outside that compile-only target.
The complete compile failure set groups by owner: completed-type trait/access
facts 13; explicit specialization/instantiation identity 7; inherited alias
identity 10; expression/call/body demand 11; stream/heap stability 5; native
object/register lowering 3; and retained special-member exception identity 1.

## Active Checkpoint

**Canonical completed-type trait demand.** Per `spec.md` §§2-5, class completion
and retained alias lookup must consume one canonical specialization identity
with monotonic definition/layout/member states. Data flows from retained type
identity -> specialization-owned completion -> indexed member/base lookup ->
trait or overload result. Semantic class/template ownership supplies the facts;
lookup and constexpr evaluation consume them without global retry. Expected
work is O(demanded specialization facts + visited base/member edges), with each
completed fact memoized. Validate the 13-case completion/access cluster, PA35,
PA1-34, audit, and doubled independent completed-type families.

## Performance Evidence

For 16/32 independent `std::pair` trait families, constexpr calls were 160/320,
cache hits 64/128, and evaluator steps 305/609. Template requests were
4,676/9,028; semantic time 201/393 ms, peak semantic storage 30.8/58.5 MB,
wall time 0.32/0.53 s, and peak RSS 32.1/57.0 MB. The newly unsuppressed work
therefore scales linearly with demanded families and reuses the existing cache.

## Completed Checkpoints

| Checkpoint | Result | Validation |
|---|---|---|
| Hosted front-end ingress | PA35 6 -> 15/103 | focused utility/probes; PA1-34 and audit pass |
| Hosted attributed/template syntax | shared syntax barriers removed; PA35 15/103 | decltype-shift; parser scaling linear |
| Canonical using-function merge | 35 duplicate-import barriers removed; PA35 15/103 | repeated/distinct imports; scaling linear |
| Hosted class-template registration | 49 registration barriers removed; PA35 15/103 | attributed partial identity; scaling linear |
| Dependent nested-type lookup | prior 55-case barrier removed; PA35 15 -> 17/103 | focused shape replay and allocator cases advance |
| Dependent call/type disambiguation | dependent calls remain calls; scalar cast set completed | PA35 17 -> 18/103; qualified-base regressions pass |
| Explicit-instantiation operator identity | 36 routing barriers removed; PA35 18/103 | operator and genuine template-id targets distinct |
| Constexpr declaration/completion | 16 owner-literal barriers removed; PA35 18/103 | member/base/object negatives remain rejected |
| Retained template-default access | 19 access barriers removed; PA35 18/103 | member/friend defaults; external private alias rejected |
| Canonical retained declarations | tag/control/function/exception facts merge; PA35 18 -> 19/103 | 16 barriers advance; PA1-34, audit, scaling pass |
| Qualified nested-member replay | canonical current specialization, definition parameters, retained operator call, constexpr string array, and local-class access | PA35 19 -> 21/103; PA1-34 4756/4756; audit/focused/scaling pass |
| Demand-safe declarations and function-local lowering | incomplete parameter ABI deferred; nested cleanup bounded; shared node/binding slots reset per function; direct class calls retained as objects | PA35 21 -> 36/103; PA1-34 4756/4756; focused/scaling/audit pass |
| Target-typed casts and bounded hosted shapes | braced casts, specialization-local constexpr, symbolic packs, runtime atomic order, and cv-overloaded special members | PA35 36 -> 41/103; PA1-34 4756/4756; behavior/scaling/audit pass |
| Retained current-class ownership | canonical owner/member view, enclosing packs, and member-template result calls; audited pack facts use one direct/per-scope index | PA35 41 -> 48/103 compile (48/104 tracked); PA1-34 4756/4756; retained-pack probes linear; file audit pass |
| Mandatory static-assert evaluation roots | assertion-local suppression scope, source ranges, and compile-path constexpr counters | PA35 48 -> 53/103; five pass and 16 advance; PA1-34 4756/4756; focused/scaling/audit pass |

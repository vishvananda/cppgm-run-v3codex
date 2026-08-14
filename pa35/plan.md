# PA35 Plan

## Stage Design and Spec Alignment

PA35 keeps the source -> streaming preprocessing/post-tokenization -> integrated
syntax/semantics -> typed LowIR -> native ELF architecture. Relevant `spec.md`
requirements are canonical typed identity and phase flow (§2, §6), demand-owned
template specialization (§4), and bounded, observable heavy-header work (§9).
The shared front end owns all changes; no hosted-only route is introduced.

Retained enclosing packs are published once under their canonical `(scope,
name)` key. The binding table provides both direct lookup and a dense-scope
secondary index; result-identity requests walk lexical owners once and use a
flat request-local binding map. Immutable parent overlays remain reserved for
alias/default substitution. This keeps work proportional to result syntax,
visible pack facts, and bound values, and the result-identity counters include
both discovery and substitution work.

## Current Failure Map

Current PA35 is 48/104 tracked: the required compile report is 48/103 with 55
failures, while the single PA35 run test is outside that compile-only target.
The compile failures group by first diagnostic and owning boundary:
constexpr/static evaluation 24; explicit specialization or
instantiation identity 7; inherited partial-specialization aliases 6;
expression/call/access lookup 9; stream-path stability 5; native object/register
lowering 3; and retained special-member exception identity 1.

## Next Substantial Checkpoint

**Constant-expression trait closure.** Per `spec.md` §§2, 4, and 6, deferred
trait expressions must retain canonical declaration/specialization identity
and evaluate only after demanded types are complete. Data flows from retained
expression syntax -> specialization-owned bindings -> typed constexpr
operations -> static-assert result. Semantic analysis owns binding and demand;
the constant evaluator owns typed execution. Expected work is O(visited
expression + demanded specializations), with memoized specialization lookup.
Validate all 24 constant-expression failures, representative trait/tuple/pair
families, PA35, PA1-34, audit, and doubled independent trait families.

## Performance Evidence

A nested retained-owner probe referenced every independent enclosing pack in
one member-template result. At depths 32/64, result environment probes changed
from 695/2,472 before audit to 162/322 after audit. The corrected syntax counter,
which now includes the formerly uncounted discovery walk, was 202/394. Median
semantic time was 18.85/67.93 ms, wall time 0.02/0.08 s, peak RSS 8.7/10.6 MiB,
and semantic peak storage 1.17/3.80 MB. The affected lookup work is linear in
pack depth; total work still follows the probe's 1,329/4,689 generated scopes.

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

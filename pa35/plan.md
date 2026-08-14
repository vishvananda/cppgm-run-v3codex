# PA35 Plan

## Stage Design and Spec Alignment

PA35 keeps the source -> streaming preprocessing/post-tokenization -> integrated
syntax/semantics -> typed LowIR -> native ELF architecture. Relevant `spec.md`
requirements are canonical typed identity and phase flow (§2, §6), demand-owned
template specialization (§4), and bounded, observable heavy-header work (§9).
The shared front end owns all changes; no hosted-only route is introduced.

Canonical source identity distinguishes ISO `_Float128` from GNU `__float128`
through type analysis and ABI naming. Explicit class targets collect syntax,
resolve one primary owner, build all type/value/template arguments through the
shared builder, and publish state under the canonical specialization key.

## Current Failure Map

PA35 is 77/111 with 34 failures. The complete set groups by first owner:
retained declaration/class lookup and access 6; expression/call/template demand
16; parser/local semantics 2; stream/heap stability 6; and native
object/register lowering 4.

## Active Checkpoint

**Retained class/declaration convergence.** Per `spec.md` §§2, 4, and 6,
retained template declarations must converge on structural owner and signature
identity before access, completion, or demand. Data flows retained syntax ->
normalized owner/parameter ordinals -> canonical declaration index -> merge and
access lookup -> concrete replay. PA19/PA20 template semantics and PA24 retained
integration own the boundary. Expected work is O(owner depth + signature shape)
per new declaration followed by O(1) average indexed reuse. Validate the six
current duplicate/access/lookup failures, true duplicate and private-access
negatives, full PA35, PA1-34, audit, and 8/16 declaration families.

## Performance Evidence

For 8/16 explicit value-argument families, semantic nodes were 17/33, lookup
queries stayed 25/25, template requests stayed 2/2, and peak semantic storage
was 49,412/60,972 bytes. Five-run median semantic time was 0.295/0.311 ms.
Emitted `_Float128`/`__float128` symbols matched GCC's `DF128_`/`g` ABI codes.

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
| Mandatory static-assert evaluation roots | assertion-local suppression, compact provenance with error-only rendering, and compile-path constexpr counters | PA35 48 -> 53/103; five pass and 16 advance; PA1-34 4756/4756; focused/scaling/audit pass |
| Canonical completed-type trait demand | complete canonical type-edge readiness; specialization-owned member access; lazy defaulted-destructor nonthrowing/boundary facts | PA35 handout 53 -> 60/103 plus composite course regression; all 13 barriers advance; PA1-34 4756/4756; scaling/file audit pass |
| Canonical qualified/inherited type identity | selected partial names, enclosing packs, identity-only friends, and function references keep canonical ownership | PA35 handout 61 -> 64/104 (65/105 total); 13 barriers removed/advanced; PA1-34 4756/4756; scaling/audit pass |
| Canonical retained exception equivalence | special-member kind, structural overload shape, ordinal parameter mapping, and deferred exception state select one retained declaration | PA35 65/105 -> 68/107; eight barriers removed, one handout plus two regressions pass; PA1-34 4756/4756; scaling/audit pass |
| Parameter-owned exception evaluation | declarators carry parameter/`this` scope into ordinary and deferred `noexcept` evaluation | five handout cases advance from unknown names to incomplete construction; direct positive/negative probes and 8/16 scaling pass |
| Demand-safe unevaluated construction | member typing skips constexpr address/layout work in unevaluated operands; dependent function-template specs rebuild canonical parameter scopes | PA35 68/107 -> 70/108 (one handout plus one regression); four cases advance; PA1-34 4756/4756; scaling/audit pass |
| Canonical specialization completion re-entry | synthetic initializer-list layout and declaration replay remain distinct; only canonical in-progress replay crosses the duplicate guard | PA35 70/108 -> 73/109 (two handout plus one regression); PA1-34 4756/4756; 8/16 scaling and audit pass |
| Canonical explicit class target routing | distinct `_Float128`/`__float128` identities and generic type/value/template argument routing remove all seven barriers | PA35 73/109 -> 77/111 (75/109 existing); two handouts pass, five advance, two regressions pass; PA1-34 4756/4756; scaling/audit pass |

# PA35 Plan

## Stage Design and Spec Alignment

PA35 keeps the source -> streaming preprocessing/post-tokenization -> integrated
syntax/semantics -> typed LowIR -> native ELF architecture. Relevant `spec.md`
requirements are canonical identity and phase flow (§2, §6), demand-owned
template specialization (§4), and bounded, observable heavy-header work (§9).
Retained syntax is shared, but concrete declaration, construction, lookup, and
access facts belong to the canonical specialization owner. The shared front end
owns all changes; no hosted-only route is introduced.

## Current Failure Map

PA35 is 99/121 with 22 failures. The complete set groups by first owner:
retained template-parameter classification 2; retained declaration/class lookup
and access 3; expression/call/template demand 14; parser/local semantics 2; and
native object/register lowering 1.

## Active Checkpoint

**Retained parameter-context classification.** Per `spec.md` §§2-5, retained
dependent expressions must classify names against the canonical owning template
parameter scope before ordinary type/value lookup. Data flows retained
id-expression -> reconstructed lexical parameter scope -> parameter kind ->
dependent type/value use -> specialization demand. PA19
`RetainedTemplateValidator::VisitIdExpression` and specialization parameter
scope reconstruction own the boundary. Expected work is O(name-path length +
lexical parameter depth), with O(1) average indexed parameter lookup. Validate
both regex `_CharT` failures, direct dependent construction and genuine
type-as-value rejection, 8/16 parameter-depth probes, PA35, PA1-34, and audit.

## Performance Evidence

For 8/16 fixed-only variadic invocations, preprocessing tokens were 3,591/3,641,
post-tokens 23/39, macro lookups 10/18, and expansions 8/16; peak rescan stayed
20 tokens. Five-run median preprocessing time was 1.714/1.720 ms, so the empty
tail adds constant storage and invocation work remains linear.

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
| Retained class/declaration convergence | injected class tags merge through the class-tag index; stale specialization-owned call facts rebuild in the active scope | PA35 77/111 -> 82/113 (80/111 existing); map/codecvt/wide-string and two regressions pass; PA1-34 4756/4756; scaling/audit pass |
| Specialization-local construction convergence | class references remain references; static downcasts complete concrete targets; fixed cv-reference patterns order over forwarding packs; direct-member calls expand packs | PA35 82/113 -> 86/117; six construction and three pack-call barriers advance, four regressions pass; PA1-34 4756/4756; scaling/audit pass |
| Explicit-id pack partition convergence | explicit function-template prefixes wait for argument-aware deduction, preserving canonical trailing-pack partitions | PA35 86/117 -> 91/119; three handout and two regressions pass; direct piecewise construction advances to native transport; PA1-34 4756/4756; scaling/audit pass |
| Native scalar aggregate transport | object copy/load/store values stay address-backed until existing ABI call/return chunking; MIR uses bounded byte copies | PA35 91/119 -> 98/120; six handout plus one regression pass; 1/16-byte executable probe, PA1-34 4756/4756, linear MIR scaling, and audit pass |
| Empty variadic-tail ownership | fixed-only variadic invocations bind a zero-length slice at the raw-token end instead of indexing an absent parsed range | PA35 98/120 -> 99/121; two regex crashes advance under ASan to retained `_CharT`; direct regression and 8/16 scaling pass; PA1-34/audit pass |

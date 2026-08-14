# PA35 Plan

## Stage Design and Spec Alignment

PA35 keeps the source -> streaming preprocessing/post-tokenization -> integrated
syntax/semantics -> typed LowIR -> native ELF architecture. Relevant `spec.md`
requirements are canonical typed identity and phase flow (§2, §6), demand-owned
template specialization (§4), and bounded, observable heavy-header work (§9).
The shared front end owns all changes; no hosted-only route is introduced.

Retained member identity is specialization-owned: the selected pattern supplies
syntax, owner template parameters are normalized by ordinal, structural
signature identity selects an overload, and known/deferred exception state is
finalized during concrete replay. Indexed canonical facts feed overload and
lowering; there is no rendered-name or hosted-only fallback.

## Current Failure Map

PA35 is 68/107 with 39 failures. The complete set groups by first owner:
explicit specialization/instantiation identity 7; retained name/declaration
lookup 10; expression/call/body demand and overload selection 14; stream/heap
stability 5; and native object/register lowering 3.

## Active Checkpoint

**Parameter-owned retained exception evaluation.** Per `spec.md` §§2-5,
function parameters must be published in the declaration's function scope
before a retained `noexcept` expression is evaluated. Data should flow from
selected retained declarator -> immutable owner substitution -> parameter
bindings -> contextual constant evaluation -> canonical exception fact ->
function-entity merge. PA12 declarator/function scopes own parameter bindings;
PA19 replay owns demand. Expected work is O(parameters + exception-expression
nodes + newly demanded facts), once per declaration, with repeat demand O(1)
average. Validate the five `__umap`/`__uset` cases, direct renamed-parameter and
negative-scope probes, full PA35, PA1-34, audit, and 8/16 exception families.

## Performance Evidence

For 8/16 retained exception families, template requests/cache hits were 16/32
and 8/16, lookup queries were 190/350, and overload candidates were 160/320.
Median semantic time was 1.88/3.25 ms and peak semantic storage was
0.392/0.712 MB. Doubling families kept work and storage at or below 2x.

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

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

PA35 is 70/108 with 38 failures. The complete set groups by first owner:
explicit specialization/instantiation routing 7; retained declaration/class
lookup and completion 6; expression/call/template demand 14; parser/local
semantics 2; stream/heap stability 5; and native object/register lowering 4.

## Active Checkpoint

**Canonical specialization completion re-entry.** Per `spec.md` §4 and §9,
repeated or recursive class-specialization demand must converge on one
canonical entity and one monotone completion state. Data flows unevaluated
member/call typing -> canonical template request key -> pending/in-progress/
succeeded state -> one declaration replay -> cached member lookup. PA12 lookup
requests completion and PA19 owns the state machine. Expected work is O(new
specialization declarations + member edges) once, with repeat/re-entrant demand
O(1) average. Validate the two cases now reporting duplicate class definition,
direct recursive/repeated-completion probes, full PA35, PA1-34, audit, and 8/16
re-entry families.

## Performance Evidence

For 8/16 retained exception families after parameter-scope publication,
template requests/cache hits were 16/32 and 8/16, lookup queries were 190/350,
and overload candidates were 160/320. Median semantic time was 1.93/3.35 ms
and peak semantic storage was 0.396/0.720 MB. Doubling families kept work and
storage at or below 2x.

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

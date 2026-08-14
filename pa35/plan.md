# PA35 Plan

## Stage Design and Spec Alignment

PA35 keeps the source -> streaming preprocessing/post-tokenization -> integrated
syntax/semantics -> typed LowIR -> native ELF architecture. Relevant `spec.md`
requirements are canonical typed identity and phase flow (§2, §6), demand-owned
template specialization (§4), and bounded, observable heavy-header work (§9).
The shared front end owns all changes; no hosted-only route is introduced.

## Current Failure Map

Turn-start PA-local baseline was 21/103; current PA35 is 36/103. The 67
failures group by first shared owner: dependent type/owner/access identity 44;
constexpr/static evaluation 9; native object/register emission 5; unresolved
expression/call/parser behavior 4; explicit class-specialization identity 3;
and allocator corruption 2.

## Active Checkpoint

**Canonical dependent type and owner resolution.** Per `spec.md` §§2 and 4,
retained names must resolve through the active specialization environment to
one canonical type/entity and must not inherit access or owner context from an
unrelated replay. Data flows from retained name/type -> specialization bindings
-> qualified owner lookup -> canonical type/entity -> demand/access/lowering.
The retained type resolver and specialization registry own the boundary.
Expected work is O(name components + newly completed members), with average
O(1) canonical request lookup. Validate the 44 alias/owner/access failures,
full PA35, PA1-34, audit, and doubled dependent-owner families.

## Performance Evidence

For 256/512 combined incomplete-boundary, nested-demand cleanup, and shared-node
slot families, semantic nodes were 8,710/17,414; lexical/unwind cleanup visits
were 512/1,024 and 256/512; functions 769/1,537; instructions 6,658/13,314;
and typed storage 2.56/5.12 MB. Semantic/lowering time was 53.3/110.5 ms and
14.4/30.4 ms (2.07x/2.11x); wall time 0.08/0.17 s and peak RSS 16.7/26.3 MB.

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

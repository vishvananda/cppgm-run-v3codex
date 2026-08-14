# PA35 Plan

## Stage Design and Spec Alignment

PA35 keeps the source -> streaming preprocessing/post-tokenization -> integrated
syntax/semantics -> typed LowIR -> native ELF architecture. Relevant `spec.md`
requirements are canonical typed identity and phase flow (§2, §6), demand-owned
template specialization (§4), and bounded, observable heavy-header work (§9).
The shared front end owns all changes; no hosted-only route is introduced.

## Current Failure Map

Checkpoint-start PA35 was 36/103; current PA35 is 41/103. The 62 remaining
failures group by first shared owner: dependent type/owner/access identity 31;
constexpr/static evaluation 15; native object/register emission 7; unresolved
expression/call/exception behavior 5; and explicit class-specialization
identity 4.

## Active Checkpoint

**Canonical dependent owner/type lookup.** Per `spec.md` §§2 and 4, a retained
qualified name must resolve through the active specialization environment to
one canonical owner/type without inheriting access state from another replay.
Data flows from retained name -> specialization bindings -> qualified owner
lookup -> canonical type/entity -> access and demand. The retained type resolver
and specialization registry own the boundary. Expected work is O(name
components + newly completed members), with average O(1) request lookup.
Validate the 31 type/owner/access failures, full PA35, PA1-34, audit, and doubled
dependent-owner families.

## Performance Evidence

For 256/512 combined retained-cast, two-pack, constexpr-shape, runtime-atomic,
and cv-special-member families, semantic nodes were 2,574/5,134, instructions
1,030/2,054, and typed storage 0.437/0.871 MB. Semantic/lowering time was
79.9/163.2 ms and 2.36/4.41 ms (2.04x/1.87x); wall time was 0.13/0.26 s and
peak RSS 19.3/30.9 MB.

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

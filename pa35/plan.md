# PA35 Plan

## Stage Design and Spec Alignment

PA35 keeps the source -> streaming preprocessing/post-tokenization -> integrated
syntax/semantics -> typed LowIR -> native ELF architecture. Relevant `spec.md`
requirements are canonical typed identity and phase flow (§2, §6), demand-owned
template specialization (§4), and bounded, observable heavy-header work (§9).
The shared front end owns all changes; no hosted-only route is introduced.

## Current Failure Map

Turn-start PA-local baseline was 19/103; current PA35 is 21/103. The 82 failures
group by first shared owner: dependent type/owner identity 40; constexpr/static
evaluation 16; class-specialization completion/reentry 11; unresolved
expression/call/parser behavior 9; explicit class-specialization identity 3;
allocator corruption 2; and lowering type completion 1.

## Active Checkpoint

**Class-specialization completion and recursive demand.** Per `spec.md` §§2 and
4, a fixed specialization must have one canonical entity and one demand-owned
completion; recursive lookup must reuse that identity without exposing a
half-complete unrelated type. Data flow is structured lookup -> canonical
specialization request -> entity/member-scope completion -> member/function
demand. The specialization request registry and class entity own the boundary;
expected work is average O(1) request lookup plus O(newly completed members).
Validate nested-constructor reentry, the 11 stream cases, full PA35, PA1-34,
audit, and doubled specialization families.

## Performance Evidence

Earlier checkpoints established linear preprocessing, parser, using-merge,
class registration, defaults, and retained-scope replay under doubled inputs.
For this checkpoint, 256/512 nested specialization families produced
8,471/16,919 lookups, 256/512 specialization requests, 57.6/98.2 ms semantic
time, 10.55/21.08 MB semantic storage, and 16.9/27.2 MB peak RSS. Counted work
and storage doubled; measured time grew 1.70x.

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

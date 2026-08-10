# PA23 Plan

## Stage Design and Spec Alignment

PA23 completes C++11 function-template deduction, partial ordering, and
substitution failure on the retained PA19-PA22 semantic graph and PA15 typed
LowIR path. `spec.md` sections 1-6 require one parsed branch, canonical
specialization identity, declaration-owned retained syntax, candidate-local
failure, demand-driven completion, and lowering that consumes selected facts;
section 9 requires work proportional to participating shapes and candidates.
`TypeTable` owns canonical structure, template patterns own source syntax and
lexical context, deduction/overload resolution own candidate frames and
selection, instantiation owns request state and completion, and lowering does
not rediscover template semantics.

## Current Failure Map

Current result is 263/401, up from the turn baseline of 255/401 and stage
baseline of 223/401 with no regressions. The remaining 138 failures group by
primary owner and observed kind: call/address/constructor deduction (`100-*`:
17 exits, 2 LowIR), typed partial ordering (`200-*`: 2 exits, 1 LowIR),
immediate substitution and demand (`300-*`: 66 exits, 9 LowIR), canonical
non-type/conversion arguments (`400-*`: 18 exits, 1 LowIR), and composed
lookup/alias/class paths (`500-*`:
13 exits, 9 LowIR).

## Active Checkpoint

The final three `200-*` failures share the member-template candidate lifecycle:
inherited-constructor publication, member-operator ordering through dependent
aliases, and canonical virtual-member emission. N3485 10.3, 12.9, 14.7.1,
14.8.2.4, and 14.8.3 require inherited declarations to retain their template
owner and forwarding shape, typed aliases to constrain partial ordering, and
selected virtual definitions to demand one canonical specialization. Class
specializations own member publication and base/using edges; deduction owns
candidate-local bindings; overload resolution records the winner; demand owns
definition and vtable emission. Expected work is O(inherited edges plus
candidate shape nodes), O(1)-average canonical lookup, and one emission per
selected binding. Validate all three failures, the complete passing `200-*`
set, PA1-PA22, and audit; measure doubled inherited/member candidate sets.

## Performance Evidence

For 64/128/256 repeated concrete default requests, specialization requests are
384/768/1,536 with 382/766/1,534 cache hits; peak semantic memory is
0.54/1.06/2.09 MB and time is 2.69/5.17/9.74 ms. For dependent-default chains
of 16/32/64, deduction visits are 51/99/195, requests 53/101/197, peak memory
0.19/0.31/0.54 MB, and time 0.96/1.31/2.04 ms. Incremental dependency tracking,
identity caches, work, and storage scale linearly. Earlier pack, overload, and
retained-partial probes also scaled linearly. Final gates are PA1-PA22
2,639/2,639, PA23 263/401, zero regressions, and audit pass with 13 inherited
warnings.

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

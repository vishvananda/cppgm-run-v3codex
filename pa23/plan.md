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

Current result is 255/401, up from the turn baseline of 248/401 and stage
baseline of 223/401 with no regressions. The remaining 146 failures group by
primary owner and observed kind: call/address/constructor deduction (`100-*`:
17 exits, 2 LowIR), typed partial ordering (`200-*`: 6 exits, 1 LowIR),
immediate substitution and demand (`300-*`: 70 exits, 9 LowIR), canonical
non-type/conversion arguments (`400-*`: 18 exits, 1 LowIR), and composed
lookup/alias/class paths (`500-*`:
13 exits, 9 LowIR).

## Active Checkpoint

The seven remaining `200-*` failures split into class/default typed formation
(4), inherited/member candidate publication (2), and emission identity (1).
The next checkpoint owns the first group at retained class-template arguments
-> specialization completion -> function-pattern ordering. N3485 14.5.7,
14.7.1, 14.7.3, 14.8.2.4, and 14.8.2.5 require defaults to materialize in the
declaration context, dependent aliases to preserve specialization identity,
and template-template shapes to participate in bidirectional ordering. Class
patterns own defaults and retained syntax; specialization requests own
canonical argument partitions and completion state; qualified lookup exposes
formed aliases; function ordering consumes typed results. Expected work is
O(arguments plus qualified lookup) per cached request and O(pattern nodes) per
ordering comparison. Validate the four class/default `200-*` failures, the
seven newly passing ordering cases, PA1-PA22, and audit; measure repeated
defaulted requests and doubled template-template argument lists.

## Performance Evidence

For trailing packs of 32/64/128 elements, deduction visits are 132/260/516,
ordering comparisons remain 2, peak semantic memory is 0.35/0.66/1.31 MB, and
semantic time is 1.22/1.89/3.37 ms. For 17/33/65 viable overload candidates,
ordering comparisons are 32/64/128, deduction visits 32/64/128, peak memory
0.39/0.75/1.48 MB, and semantic time 2.01/3.68/6.86 ms. Counts and storage are
linear with no expansion-product search. The prior retained-partial probe also
scaled linearly through 512 patterns. Final gates are PA1-PA22 2,639/2,639,
PA23 255/401, zero regressions, and audit pass with 13 inherited warnings.

## Completed Checkpoints

| Checkpoint | Result |
| --- | --- |
| Array-extent NTTP deduction and candidate ownership | Ordinary/repeated/hidden-friend/constructor probes pass; PA23 169 -> 177, exact baseline retained, linear scaling. |
| Non-deduced qualified types and compound array bounds | Qualified replay, ordering, rebind, and compact array lowering pass; 177 -> 186, no regressions, linear scaling. |
| Defaulted function-template materialization | Declaration-owned defaults and explicit request states pass; 186 -> 211 overall with linear success/failure scaling. |
| Explicit-call expression substitution and variadic class boundary | Candidate failure spans defaults through expressions and lowering consumes ellipsis storage facts; 211 -> 223, no regressions, linear scaling. |
| Concrete replay of retained class-partial type arguments | Dependent alias/`void_t`/array/`decltype` patterns replay after deduction; builtin invoke preserves call result facts, hard body errors escape SFINAE, and candidate result formation is demand-driven; 223 -> 248, no regressions, linear replay scaling. |
| Typed function-pack deduction and partial ordering | Inner/trailing/empty/repeated packs, active defaulted tails, and direct `T&`/forwarding-`T&&` ordering pass; 248 -> 255, seven gains, no regressions, linear pack/candidate scaling. |

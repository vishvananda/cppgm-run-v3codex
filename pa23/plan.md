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

Current result is 248/401, up from the turn baseline of 223/401 with no
regressions. The remaining 153 failures group by primary owner and observed
kind: call/address/constructor deduction (`100-*`: 18 exits, 2 LowIR), typed
partial ordering (`200-*`: 11 exits, 2 LowIR), immediate substitution and
demand (`300-*`: 70 exits, 9 LowIR), canonical non-type/conversion arguments
(`400-*`: 18 exits, 1 LowIR), and composed lookup/alias/class paths (`500-*`:
13 exits, 9 LowIR).

## Active Checkpoint

Next is the retained function-pattern -> typed partial-order boundary. N3485
13.3.3, 14.8.2.4, and 14.8.2.5 require transformed function types,
reference/cv adjustments, pack participation, and forwarding-reference rules
to rank only viable specializations. `FunctionTemplatePattern` owns normalized
parameter syntax; deduction produces canonical bindings and conversion facts;
`DeduceFunctionTemplatePatterns` owns pairwise ordering; overload resolution
consumes the winner. Expected work is O(pattern nodes plus participating
candidates squared) for pairwise ordering, with O(1)-average specialization
lookup. Validate the complete `200-*` set plus constructor, member assignment,
fixed/default tail, inner-pack, and earlier-PA guards; measure doubling
candidate/shape sets before the full PA23, through-PA22, and audit gates.

## Performance Evidence

For 128/256/512 unique retained `void_t` partial probes, valid replay performs
128/256/512 candidate checks, 768/1,536/3,072 deduction visits, and one shape
materialization with 128/256/512 shape-cache hits; semantic storage is
6.61/13.20/26.40 MB and analysis time is 31.1/63.6/129.3 ms. Failed replay
performs 128/256/512 checks, 384/768/1,536 deduction visits, the same one
materialization and cache-hit counts, uses 6.01/12.02/24.03 MB, and takes
32.8/68.6/135.7 ms. Requests, visits, memory, and time scale linearly with no
repeated shape materialization. Final uncontended gates are PA1-PA22
2,639/2,639 and a passing file audit with 13 inherited warnings.

## Completed Checkpoints

| Checkpoint | Result |
| --- | --- |
| Array-extent NTTP deduction and candidate ownership | Ordinary/repeated/hidden-friend/constructor probes pass; PA23 169 -> 177, exact baseline retained, linear scaling. |
| Non-deduced qualified types and compound array bounds | Qualified replay, ordering, rebind, and compact array lowering pass; 177 -> 186, no regressions, linear scaling. |
| Defaulted function-template materialization | Declaration-owned defaults and explicit request states pass; 186 -> 211 overall with linear success/failure scaling. |
| Explicit-call expression substitution and variadic class boundary | Candidate failure spans defaults through expressions and lowering consumes ellipsis storage facts; 211 -> 223, no regressions, linear scaling. |
| Concrete replay of retained class-partial type arguments | Dependent alias/`void_t`/array/`decltype` patterns replay after deduction; builtin invoke preserves call result facts, hard body errors escape SFINAE, and candidate result formation is demand-driven; 223 -> 248, no regressions, linear replay scaling. |

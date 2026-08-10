# PA23 Plan

## Stage Design and Spec Alignment

PA23 completes C++11 function-template deduction, partial ordering, and
substitution failure while retaining the PA19-PA22 typed semantic graph and
PA15 typed LowIR path. The relevant `spec.md` constraints are canonical type
and declaration identity (section 2), indexed overload candidates with retained
selection facts (section 3), specialization keys and narrow demand states
(section 4), precise cache state (section 5), direct typed lowering (section 6),
and semantic work proportional to participating type shapes and candidates
(section 9). Ownership remains: `TypeTable` for canonical type structure,
function-template deduction for candidate-local bindings, declaration
publication for indexed candidates, declaration-owned contexts for default
syntax and dependent-result identity, instantiation for complete canonical
specialization and request states, overload resolution for selection, and
lowering for consuming typed conversion and selected-binding facts without
replay. Default contexts retain their declaring lexical scope and template head;
request keys contain canonical arguments and required pack partitions.

## Current Failure Map

Current result: 223/401 pass: 222/400 handout tests plus the 1/1 course audit;
178 handout tests fail (156 false rejections, 1 false acceptance, 21 LowIR
mismatches), with 19 gains and no regressions from the 203/400 audit baseline.
The remaining failures group by primary owner: core call/address/constructor
deduction (`100-*`: 21), partial ordering over typed patterns (`200-*`: 13),
SFINAE/dependent replay and demand (`300-*`: 97), canonical non-type arguments
and conversion deduction (`400-*`: 20), and composed lookup/alias/class paths
(`500-*`: 27).

## Active Checkpoint

Complete candidate-local dependent type formation for class-template partial
selection. N3485 14.5.5 and 14.8.2 require substituted dependent qualified
types, alias/`void_t` arguments, and invalid immediate `decltype` calls to
select or discard each partial without escaping as a hard error. Retained class
partial patterns own syntax; canonical argument binding overlays own concrete
types; partial selection owns success/failure state; completion and lowering
consume only the selected specialization. This follows `spec.md` sections 2-6
and 9: canonical keys, narrow monotonic request states, candidate-local expected
failure, no eager body demand, and O(pattern nodes plus indexed partial
candidates) work with O(1)-average cached lookup. Validate first with
`300-void-t-detector`, `300-decltype-call-substitution-failure-partial-specialization`,
and invalid qualified-member fallbacks, then the dependent alias and array-bound
partial groups; run pa23 and through-pa22 reports and measure repeated valid and
failed partial probes.

## Performance Evidence

For 1,024/2,048/4,096 repeated successful explicit default-construction probes,
specialization requests are 4,096/8,192/16,384, deduction visits are
2,048/4,096/8,192, peak bytes are 7,697,241/15,378,649/30,741,465, and semantic
medians are 38.7/77.3/157.6 ms. Failed probes materialize the invalid default
once at every size, record 2,047/4,095/8,191 failure-cache hits, use
7,698,761/15,381,961/30,748,361 peak bytes, and take 59.4/117.3/238.1 ms.
Requests, candidate/deduction visits, memory, and time scale linearly. PA1-PA22
are 2,639/2,639 and file audit passes with 13 inherited advisories.

## Completed Checkpoints

| Checkpoint | Result |
| --- | --- |
| Direct array-extent NTTP deduction and current-call candidate ownership | Ordinary, repeated, hidden-friend, constructor, range, and guarded deduction pass; PA23 169 -> 177. Audit moved filtering to the nontemplate source index and replaced template-aware lowering with a semantic conversion fact; exact baseline preserved and scaling is linear. |
| Retained non-deduced qualified types and compound array bounds | Qualified-id deduction, conversion replay, partial ordering, dependent rebind, explicit specialization, and compact nested-array lowering pass; PA23 177 -> 186 with no regressions. Required `typename`, normalized redeclaration identity, exact candidate-local skipping, and linear scaling are retained facts. |
| Defaulted function-template materialization and substitution failure | Defaults bind in their declaration-owned contexts before complete canonical identity; normalized dependent results preserve overloads, duplicate defaults are rejected, and explicit request states memoize success, failure, and recursion. PA23 186 -> 210 handout passes plus one course regression, with no audit-baseline regressions and linear success/failure scaling. |
| Explicit-call immediate expression substitution and variadic class boundary | Ambiguous `sizeof(f<T>())` parses once as an expression, invalid default `decltype` construction/operators drop only their candidate, `void*` arithmetic is rejected, and selected ellipsis class arguments carry materialization facts into lowering. PA23 211 -> 223 overall with 12 gains, no regressions, and linear valid/failed scaling. |

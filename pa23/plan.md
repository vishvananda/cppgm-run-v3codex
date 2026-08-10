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

Current handout result: 210/400 pass; 190 fail (169 false rejections, 1 false
acceptance, 20 LowIR mismatches), with seven gains and no regressions from the
203/400 audit baseline. The course audit regression is 1/1.
The remaining failures group by primary owner: core call/address/constructor
deduction (`100-*`: 22), partial ordering over typed patterns (`200-*`: 13),
SFINAE/dependent replay and demand (`300-*`: 107), canonical non-type arguments
and conversion deduction (`400-*`: 20), and composed lookup/alias/class paths
(`500-*`: 28).

## Active Checkpoint

Represent dependent `decltype` and unevaluated call validity as typed
candidate-local substitution. N3485 14.8.2 paragraphs 7-8 require lexical
substitution through general expressions and make only invalid immediate
function-type expressions deduction failures. Expression analysis owns the
typed result or invalid-substitution status; indexed overload lookup owns call
candidates; function-template replay consumes the result without demanding an
unselected body. Retain expression syntax only until its concrete binding
overlay exists, then cache by canonical specialization plus expression node.
Work must be O(retained expression nodes plus participating indexed candidates).
Start with `300-decltype-default-type-arg-construction-sfinae`, the invalid
operator probes, and dependent call/return replay groups. The still-failing
`100-defaulted-nontype-deduction-overrides-default` belongs here because
deduction establishes both values but the dependent result expression `A + B`
is not yet replayed; it is not a default-selection failure.

## Performance Evidence

For 1,024/2,048/4,096 repeated successful defaulted calls, specialization
requests are 2,049/4,097/8,193 with 2,047/4,095/8,191 cache hits and semantic
medians of 9.9/18.6/36.8 ms. For equally sized failed-default probes, default
materializations remain 1/1/1 while failure-cache hits are
2,047/4,095/8,191; candidate visits are 1,024/2,048/4,096, deduction visits are
2,048/4,096/8,192, peak bytes are 2,331,968/4,625,728/9,213,248, and semantic
medians are 13.0/25.2/49.5 ms. PA1-PA22 are 2,639/2,639 and file audit passes
with 13 inherited advisories.

## Completed Checkpoints

| Checkpoint | Result |
| --- | --- |
| Direct array-extent NTTP deduction and current-call candidate ownership | Ordinary, repeated, hidden-friend, constructor, range, and guarded deduction pass; PA23 169 -> 177. Audit moved filtering to the nontemplate source index and replaced template-aware lowering with a semantic conversion fact; exact baseline preserved and scaling is linear. |
| Retained non-deduced qualified types and compound array bounds | Qualified-id deduction, conversion replay, partial ordering, dependent rebind, explicit specialization, and compact nested-array lowering pass; PA23 177 -> 186 with no regressions. Required `typename`, normalized redeclaration identity, exact candidate-local skipping, and linear scaling are retained facts. |
| Defaulted function-template materialization and substitution failure | Defaults bind in their declaration-owned contexts before complete canonical identity; normalized dependent results preserve overloads, duplicate defaults are rejected, and explicit request states memoize success, failure, and recursion. PA23 186 -> 210 handout passes plus one course regression, with no audit-baseline regressions and linear success/failure scaling. |

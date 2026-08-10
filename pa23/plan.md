# PA23 Plan

## Stage Design and Spec Alignment

PA23 completes C++11 function-template deduction, partial ordering, and
substitution failure while retaining the PA19-PA22 typed semantic graph and
PA15 typed LowIR path. The relevant `spec.md` constraints are canonical type
and declaration identity (section 2), indexed overload candidates with retained
selection facts (section 3), specialization keys and narrow demand states
(section 4), direct typed lowering (section 6), and semantic work proportional
to participating type shapes and candidates (section 9). Ownership remains:
`TypeTable` for canonical type structure, function-template deduction for
candidate-local bindings, declaration publication for separate all-function
and nontemplate indexes, instantiation for canonical specialization facts,
overload resolution for selection, and lowering for consuming typed conversion
and selected-binding facts without replay. A direct dependent array extent is
normalized into the candidate binding environment; call completion merges only
specializations deduced for that call by canonical declaration identity.

## Current Failure Map

Current result: 203/400 pass; 197 fail (175 false rejections, 2 false
acceptances, 20 LowIR mismatches), with no regressions and 17 gains from the
186/400 checkpoint baseline. The report preserves the exact pass/fail set.
The remaining failures group by primary owner: core call/address/constructor
deduction (`100-*`: 22), partial ordering over typed patterns (`200-*`: 13),
SFINAE/dependent replay and demand (`300-*`: 113), canonical non-type arguments
and conversion deduction (`400-*`: 20), and composed lookup/alias/class paths
(`500-*`: 29).

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
operator probes, and dependent call/return replay groups.

## Performance Evidence

On generated 1,024/2,048/4,096 repeated defaulted-call probes,
`template_specialization_requests` is 2,049/4,097/8,193 with
2,047/4,095/8,191 cache hits, `function_template_deduction_visits` is
2,048/4,096/8,192, semantic peak bytes are
1,781,873/3,526,769/7,016,561, and three-run semantic medians are
9.9/18.6/36.8 ms. PA23 is 203/400, PA1-PA22 are 2,639/2,639, and file audit
passes with 13 inherited advisory warnings.

## Completed Checkpoints

| Checkpoint | Result |
| --- | --- |
| Direct array-extent NTTP deduction and current-call candidate ownership | Ordinary, repeated, hidden-friend, constructor, range, and guarded deduction pass; PA23 169 -> 177. Audit moved filtering to the nontemplate source index and replaced template-aware lowering with a semantic conversion fact; exact baseline preserved and scaling is linear. |
| Retained non-deduced qualified types and compound array bounds | Qualified-id deduction, conversion replay, partial ordering, dependent rebind, explicit specialization, and compact nested-array lowering pass; PA23 177 -> 186 with no regressions. Required `typename`, normalized redeclaration identity, exact candidate-local skipping, and linear scaling are retained facts. |
| Defaulted function-template materialization and substitution failure | Defaults bind in lexical order before canonical specialization identity; invalid immediate function types drop only their candidate, while a separate request alias prevents repeated default replay. SFINAE, imported/qualified lookup, dependent result, and alias-return groups pass; PA23 186 -> 203 with no regressions and linear scaling. |

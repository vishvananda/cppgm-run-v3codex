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

Current result: 186/400 pass; 214 fail (197 false rejections, 2 false
acceptances, 15 LowIR mismatches), with no regressions and nine gains from the
177/400 turn baseline. The audit preserves the exact checkpoint pass/fail set.
The remaining failures group by primary owner: core call/address/constructor
deduction (`100-*`: 22), partial ordering over typed patterns (`200-*`: 13),
SFINAE/dependent replay and demand (`300-*`: 127), canonical non-type arguments
and conversion deduction (`400-*`: 21), and composed lookup/alias/class paths
(`500-*`: 31).

## Active Checkpoint

Materialize defaulted function-template arguments after deduction and before
specialization-key construction. N3485 14.8.2 requires every parameter to be
deduced, explicit, or defaulted, while 14.8.3 makes immediate-context default
substitution failure discard only that candidate. Candidate deduction owns a
parent-linked binding overlay populated in parameter order; instantiation sees
only a complete canonical argument list and typed offsets. Work must be
O(template parameters plus demanded default syntax) once per candidate, with
success/failure cached by the complete specialization key. Start with
`100-defaulted-nontype-deduction-overrides-default`, then the defaulted
`enable_if` call/constructor and owner-scope groups.

## Performance Evidence

On generated 1,024/2,048/4,096-call qualified-parameter probes,
`overload_candidates` is 1,024/2,048/4,096,
`function_template_deduction_visits` is 2,048/4,096/8,192, semantic peak bytes
are 2,430,329/4,823,297/9,609,239, and three-run semantic medians are
14.1/26.4/52.9 ms. Constant multidimensional initialization now emits direct
byte offsets (13 rather than 33 temporaries in the checkpoint probe). PA23 is
186/400, the nine gained cases are 9/9, PA1-PA22 are 2,639/2,639, and file audit
passes with 13 inherited advisory warnings.

## Completed Checkpoints

| Checkpoint | Result |
| --- | --- |
| Direct array-extent NTTP deduction and current-call candidate ownership | Ordinary, repeated, hidden-friend, constructor, range, and guarded deduction pass; PA23 169 -> 177. Audit moved filtering to the nontemplate source index and replaced template-aware lowering with a semantic conversion fact; exact baseline preserved and scaling is linear. |
| Retained non-deduced qualified types and compound array bounds | Qualified-id deduction, conversion replay, partial ordering, dependent rebind, explicit specialization, and compact nested-array lowering pass; PA23 177 -> 186 with no regressions. Required `typename`, normalized redeclaration identity, exact candidate-local skipping, and linear scaling are retained facts. |

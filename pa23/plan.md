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

Current result: 177/400 pass; 223 fail (205 false rejections, 3 false
acceptances, 15 LowIR mismatches). The audit preserves the exact checkpoint
pass/fail set and the improvement from the 169/400 checkpoint-start baseline.
The remaining failures group by primary owner: core call/address/constructor
deduction (`100-*`: 24), partial ordering over typed patterns (`200-*`: 17),
SFINAE/dependent replay and demand (`300-*`: 129), canonical non-type arguments
and conversion deduction (`400-*`: 22), and composed lookup/alias/class paths
(`500-*`: 31).

## Active Checkpoint

Next, represent qualified-id and compound-expression non-deduced contexts as
typed retained substitution facts. N3485 14.8.2.5 requires a qualified type
such as `typename Wrap<T>::type` not to deduce `T`, while an expression bound
such as `N - 1` is checked only after another parameter has established `N`.
The declarator/type builder owns the canonical non-deduced shape and the
function-template candidate owns replay against its completed binding overlay.
Work must be O(retained shape nodes) once per participating candidate without
completing unrelated class bodies. Start with
`200-nondeduced-context-only-bad` and
`400-array-bound-expression-is-nondeduced`, then cover the related qualified
type and substitution-failure groups.

## Performance Evidence

On generated 1,024/2,048/4,096-call unique-bound probes,
`function_candidate_index_visits` is 0/0/0, `overload_candidates` is
1,024/2,048/4,096, `function_template_deduction_visits` is
4,096/8,192/16,384, and semantic peak bytes are
20,109,886/40,244,801/80,547,396. Three-run semantic medians are
85.2/177.5/369.0 ms; before selecting the nontemplate source index, the 4,096
case took 1,141.8 ms because every call copied prior implicit specializations.
Checkpoint tests are 7/7, the exact PA23 pass set is 177/400, PA1-PA22 are
2,639/2,639, and the PA23 `dev/src` file audit passes with 13 inherited
advisory warnings.

## Completed Checkpoints

| Checkpoint | Result |
| --- | --- |
| Direct array-extent NTTP deduction and current-call candidate ownership | Ordinary, repeated, hidden-friend, constructor, range, and guarded deduction pass; PA23 169 -> 177. Audit moved filtering to the nontemplate source index and replaced template-aware lowering with a semantic conversion fact; exact baseline preserved and scaling is linear. |

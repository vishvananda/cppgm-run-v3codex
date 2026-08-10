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
candidate-local bindings, instantiation for canonical specialization facts,
overload resolution for selection, and lowering for consuming the selected
binding without replay.

## Current Failure Map

Current result: 177/400 pass; 223 fail (205 false rejections, 3 false
acceptances, 15 LowIR mismatches), improved from the 169/400 turn-start
baseline. The complete remaining set groups by staged behavior and primary
owner: core call/address/constructor deduction (`100-*`, deduction and
instantiation: 24), function-template partial ordering (`200-*`, ordering over
typed patterns: 17), SFINAE/dependent replay and demand (`300-*`, substitution
and candidate state: 129), non-type/conversion edge cases (`400-*`, canonical
arguments and conversion deduction: 22), and composed cross-owner behavior
(`500-*`, deduction through lookup/aliases/classes: 31).

## Active Checkpoint

Next, represent qualified-id and compound-expression non-deduced contexts as
typed retained substitution facts. N3485 14.8.2.5 requires qualified types such
as `typename Wrap<T>::type` not to deduce `T`, while a bound such as `N - 1`
must be checked after `N` is deduced elsewhere. Ownership is the declarator/type
builder plus function-template candidate substitution; the data flow is parsed
dependent syntax to a canonical non-deduced shape, then candidate-local replay
against the completed binding environment. Expected work is O(retained shape
nodes) once per candidate and must not instantiate unrelated class bodies.
Validation starts with `200-nondeduced-context-only-bad` and
`400-array-bound-expression-is-nondeduced`, then the related qualified-type and
SFINAE groups.

## Performance Evidence

Generated unique-bound call probes show linear checkpoint work. For 32/64/128
calls, `function_template_deduction_visits` is 128/256/512,
`overload_candidates` is 32/64/128, semantic peak bytes are
378766/749094/1489189, and semantic time is 1.94/3.60/7.20 ms. Before filtering
previous implicit specializations from each new call, candidate visits were
528/2080/8256; current-call candidate ownership removes that quadratic scan.
Required validation: checkpoint tests 7/7, pa23 177/400, prior stages
2639/2639, and the pa23 `dev/src` file audit pass.

## Completed Checkpoints

| Checkpoint | Result |
| --- | --- |
| Direct array-extent NTTP deduction and current-call candidate ownership | Ordinary, repeated, hidden-friend, constructor, range, and guarded deduction pass; pa23 169 -> 177; scaling linear. |

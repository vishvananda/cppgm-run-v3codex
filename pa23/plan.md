# PA23 Plan

## Stage Design and Spec Alignment

PA23 completes C++11 function-template deduction, partial ordering, and
substitution failure while retaining the PA19-PA22 typed semantic graph and
PA15 typed LowIR path. `spec.md` section 1 requires a single parser branch per
source region; sections 2-5 require canonical specialization identity, compact
candidate-local failure, and precise request states; section 6 requires
lowering to consume selected declarations, conversions, and ABI facts; section
9 requires work proportional to participating shapes and candidates. Ownership
is therefore: cached token lookahead for grammar disambiguation, `TypeTable`
for canonical type structure, deduction/explicit binding for candidate frames,
declaration-owned contexts for default syntax, instantiation for complete
specialization and request states, overload resolution for selection facts,
and lowering for direct consumption only. Default contexts retain declaring
scope and template head; request keys retain canonical arguments and required
pack partitions; failed immediate substitution cannot publish an invalid type
or declaration.

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

The next substantial checkpoint is completing candidate-local dependent type
formation for class-template partial selection. N3485 14.5.5 and 14.8.2
require substituted dependent qualified
types, alias/`void_t` arguments, and invalid immediate `decltype` calls to
select or discard each partial without escaping as a hard error. Retained class
partial patterns own syntax; canonical argument binding overlays own concrete
types; partial selection owns success/failure state; completion and lowering
consume only the selected specialization. This follows `spec.md` sections 2-6
and 9: canonical keys, narrow monotonic request states, candidate-local expected
failure, no eager body demand, and O(pattern nodes plus indexed partial
candidates) work with O(1)-average cached lookup. Validate first with
`300-void-t-detector`,
`300-decltype-call-substitution-failure-partial-specialization`, and invalid
qualified-member fallbacks, then the dependent alias and array-bound partial
groups; run PA23 and through-PA22 reports and measure repeated valid and failed
partial probes.

## Performance Evidence

For 1,024/2,048/4,096 repeated valid default-construction probes,
specialization requests are 4,096/8,192/16,384, overload-candidate visits are
5,123/10,243/20,483, deduction visits are 2,048/4,096/8,192, peak bytes are
7,762,775/15,509,719/31,003,607, and three-run semantic medians are
38.8/79.0/159.1 ms. Failed probes issue 3,072/6,144/12,288 requests, visit
4,099/8,195/16,387 candidates, materialize the invalid default once, record
2,047/4,095/8,191 failure-cache hits, use
7,764,295/15,513,031/31,010,503 bytes, and take 38.4/78.6/155.2 ms. Requests,
visits, memory, and time scale linearly; the failed path no longer pays C++
exception-unwinding cost. PA1-PA22 are 2,639/2,639 and file audit passes with
13 inherited advisories.

## Completed Checkpoints

| Checkpoint | Result |
| --- | --- |
| Direct array-extent NTTP deduction and current-call candidate ownership | Ordinary, repeated, hidden-friend, constructor, range, and guarded deduction pass; PA23 169 -> 177. Audit moved filtering to the nontemplate source index and replaced template-aware lowering with a semantic conversion fact; exact baseline preserved and scaling is linear. |
| Retained non-deduced qualified types and compound array bounds | Qualified-id deduction, conversion replay, partial ordering, dependent rebind, explicit specialization, and compact nested-array lowering pass; PA23 177 -> 186 with no regressions. Required `typename`, normalized redeclaration identity, exact candidate-local skipping, and linear scaling are retained facts. |
| Defaulted function-template materialization and substitution failure | Defaults bind in their declaration-owned contexts before complete canonical identity; normalized dependent results preserve overloads, duplicate defaults are rejected, and explicit request states memoize success, failure, and recursion. PA23 186 -> 210 handout passes plus one course regression, with no audit-baseline regressions and linear success/failure scaling. |
| Explicit-call immediate expression substitution and variadic class boundary | Cached token lookahead selects one parse; explicit and deduced candidate frames carry compact failure through defaults, declarators, lookup, construction, and expression checks; selected ellipsis class arguments carry a semantic storage fact into lowering. PA23 211 -> 223 overall with 12 gains, the 223 checkpoint baseline preserved, zero throws across representative candidate probes, and linear valid/failed scaling. |

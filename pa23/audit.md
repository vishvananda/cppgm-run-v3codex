# PA23 Checkpoint Audit

## Current Checkpoint Review

The landed `ef0fa8c5` increment correctly moved function-template defaults
between deduction and canonical specialization creation, but its request alias
represented successful bindings only. Repeated invalid defaults were rebuilt,
recursive demand had no in-progress state, and the merged declaration record
could accept a duplicate default or discard a default when an alpha-renamed
definition replaced the template head. The same coarse declaration identity
also merged complementary dependent result types. Those were correctness and
repeated-work defects in the checkpoint's `spec.md` sections 2--5 ownership
path, not later expression-SFINAE work.

Declaration insertion now gives each default a retained context containing its
declaring lexical scope and parameter head. Redeclarations map parameters to
those contexts, reject a second default, and let a definition adopt its own
names without changing earlier default lookup. Dependent result syntax is
normalized by template-parameter ordinal at insertion, so alpha-equivalent
declarations merge while complementary result constraints remain separate
overloads. The audit regression composes defaults added by two differently
named declarations and forms one canonical `int, int` specialization.

Candidate replay uses a flat canonical request table keyed by template
identity, incomplete canonical arguments, and required pack partitions. Its
monotonic states are in-progress, success, and expected failure; only requests
whose missing parameters already own defaults are cached, so a later default
declaration cannot stale an earlier incomplete result. Successful requests
alias the complete specialization key, failed and recursive requests return a
candidate discard, and lowering continues to consume the selected typed
binding without lookup or template-specific recovery.

On 1,024/2,048/4,096 repeated failed-default calls, default materializations
remain 1/1/1, failure-cache hits are 2,047/4,095/8,191, candidate-index visits
are 1,024/2,048/4,096, deduction visits are 2,048/4,096/8,192, and semantic
peak bytes are 2,331,968/4,625,728/9,213,248. Three-run semantic medians are
13.0/25.2/49.5 ms. Together with the landed successful-request probe, this
shows linear participating-candidate work and no repeated default substitution
for either disposition.

The handout suite is 210/400: seven gains and no regression from the 203/400
turn baseline. The declaration-context course regression is 1/1, PA1--PA22 are
2,639/2,639, and the PA23 file audit passes with the same 13 inherited
header-division advisories. `git diff --check` also passes.

## Checkpoint Audit Ledger

| Checkpoint | Evidence and disposition |
| --- | --- |
| Direct array-extent NTTP deduction (`b6d38290`, this audit) | Canonical bound deduction and ordinary typed lowering pass; candidate ownership was repaired at the source index and scales linearly; baseline preserved. |
| Defaulted function-template substitution (`ef0fa8c5`, this audit) | Declaration-owned default contexts, normalized dependent-result identity, and complete request states repair redeclaration correctness and repeated failed work; PA23 203 -> 210 with no regressions and linear success/failure scaling. |

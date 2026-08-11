# PA23 Checkpoint Audit

## Current Checkpoint Review

The landed `e218b8dc` increment moved PA23 from 371/405 to 374/405. Its three
gains retain parameter-dependent calls in trailing results, replay dependent
`noexcept` in a function-parameter scope, publish calls inside aliases, avoid
constexpr execution in unevaluated operands, and complete supplied class
specialization owners before indexed ADL finds hidden friends. The path is
retained syntax and lexical scope -> canonical specialization and parameter
facts -> lexical/ADL candidate indexes -> selected call fact -> ordinary
constexpr or LowIR consumers.

The audit found three connected defects. Function-specialization formation
eagerly substituted dependent exception specifications and converted an
invalid specification into candidate rejection, although N3485 14.8.2 requires
that substitution only when the function is instantiated. This merged
declaration and exception demand, repeated work without a completion state,
and rejected a valid dormant specialization. The new result-dependence probe
also used spelling alone, so a qualified value could become dependent merely
by matching a function-parameter name. Finally, direct publication of an
`IsNonthrowing` result could retain a binding-vector reference across re-entrant
class completion and write through invalidated storage. These violate
`spec.md` sections 2-5, 8-10.

Each specialization now owns a monotonic fixed/deferred/in-progress/succeeded/
failed exception-specification state and a lazily created parameter scope.
Only `noexcept` inspection, constexpr definition use, runtime/emission demand,
or an inherited constructor dependency requests the fact; subsequent users
read the canonical binding through the completed state. Re-entrant analysis
computes into a local value before identity-based publication, and action
walkers copy transient dump records before requesting nested facts. Result
dependence recognizes only unqualified parameter id-expressions. No lowering
fallback, text round trip, global retry, exception-as-expected-control-flow,
or source/test recognition remains on the affected path.

For 1/2/4/8 demanded NTTP specializations queried twice, exception requests
were 4/8/16/32, cache hits 3/6/12/24, and evaluations exactly 1/2/4/8. Scopes
were 16/25/43/79, lookups 17/29/53/101, overload candidates 2/4/8/16,
specialization requests 6/12/24/48, and specialization hits 5/10/20/40;
typed LowIR storage stayed 1,735 bytes. Three-run semantic medians were
0.317/0.370/0.464/0.689 ms. Work and retained lowering storage therefore track
the demanded specialization/call edges, while repeated queries consume the
memoized fact.

The original 374/405 checkpoint remains intact and the audit guard makes the
combined report 375/406, with the same 31 prior failures and no timeout.
The landed three gains and audit demand/re-entrancy guards are ASan/UBSan-clean;
PA1--PA22 pass 2,639/2,639, file audit passes with the same 13 inherited
header-division advisories, and `git diff --check` passes.

## Checkpoint Audit Ledger

| Checkpoint | Evidence and disposition |
| --- | --- |
| Direct array-extent NTTP deduction (`b6d38290`, this audit) | Canonical bound deduction and ordinary typed lowering pass; candidate ownership was repaired at the source index and scales linearly; baseline preserved. |
| Defaulted function-template substitution (`ef0fa8c5`, this audit) | Declaration-owned default contexts, normalized dependent-result identity, and complete request states repair redeclaration correctness and repeated failed work; PA23 203 -> 210 with no regressions and linear success/failure scaling. |
| Immediate expression substitution and variadic class calls (`63596fc7`, this audit) | Single-branch parsing, compact explicit/deduced candidate failure, and a semantic variadic-class fact replace reparsing, exception control flow, and lowering reconstruction; PA23 stays 223/401 with linear scaling. |
| Declaration-time result lookup (`bf4b7a83`, this audit) | Pattern-owned canonical type/call facts preserve first-declaration lookup through redeclaration and substitution; original PA23 292 -> 296 with no regressions and linear candidate/depth evidence. |
| Candidate-local alias and detector replay (`94a33b59`, this audit) | Typed no-throw formation and monotonic alias failure states replace broad exception handling; retained base syntax replaces spelling dispatch; 307/403 is preserved and failure scaling is linear. |
| Candidate-local expression validity (`84a3f7c5`, this audit) | Shared complete-object pointer arithmetic repairs the reverse compound path; typed comparison/arithmetic failure preserves candidate ownership; original 315/404 is intact, the audit guard passes, and candidate/path scaling is linear. |
| Dependent call/result replay (`e218b8dc`, this audit) | Demand-owned exception states, canonical post-reentry publication, and qualified value identity repair the landed call/ADL increment; original 374/405 is intact, the audit guard passes, and demanded work scales linearly. |

# PA22 Checkpoint Audit

## Current Checkpoint Review

The bounded review covers landed checkpoint `da807b9f`: nested template-head
retention, canonical class-owner attachment, function-template registration,
member/static/constructor call deduction, late definition upgrade, and ordinary
LowIR demand. The source is parsed once into the translation-unit `SyntaxArena`;
member patterns retain compact node IDs in stable deques, class owners and arguments
use canonical IDs, and specialization caches use pattern identity plus canonical
arguments and pack partitions. Indexed owner/name lookup feeds the selected binding
through the existing conversion, demand, and typed lowering path. There is no token
reparse, rendered semantic key, global template scan, external compiler, or alternate
lowering route. The parser's added call-shaped template probe uses a bounded rollback
and retains no abandoned tree.

The audit found one identity/candidate leak across that complete path. Registration
treated equal function types and equal template-parameter counts as one declaration,
so distinct type and non-type member-template heads merged. After one explicit call,
its materialized specialization also remained in the ordinary member set and could
compete in a later call with different explicit arguments. Registration now compares
parameter kind, pack shape, canonical fixed value type, and normalized retained syntax
for dependent value-parameter types; names normalize to parameter ordinals, so renamed
redeclarations still attach. Direct calls discard cached template specializations from
ordinary candidates and reconstruct only the specializations valid for the current
template-id and arguments. Stable pattern IDs remain the specialization owner, while
the normalized syntax walk is short-lived declaration work rather than a hot lookup
or textual key.

Representative 16/32/64-specialization families sharing one retained out-of-class
member template produced five-run median semantic times of 3.02/5.55/10.78 ms and
peak semantic storage of 0.53/1.05/2.10 MiB. Requests were 80/160/320, cache hits
48/96/192, overload candidates 32/64/128, and both demand pushes and emitted
functions 16/32/64. The exact doubling supports linear related-owner attachment and
call filtering; no unrelated-template growth or repeated emission appeared.

Validation preserves every landed result: the original PA22 failure set is unchanged,
the audit regression passes, and the report is 145/310 versus the 144/309 turn-start
baseline. The ten focused attachment/identity cases pass, PA1–PA21 pass 2329/2329,
and the PA22 file audit passes with only the same 13 header-division advisories. The
remaining similarly named failures fall at later owners such as explicit-specialization
replacement, nested class/alias graphs, dependent-call timing/deduction, or LowIR
presentation and do not reopen this attachment identity path.

## Checkpoint Audit Ledger

| Checkpoint | Audit disposition | Evidence |
|---|---|---|
| `5d70a120` canonical partial-specialization selection | Pass after checkpoint repair | Selected owner/revision/substitution survives shell completion; canonical typed identity and pack shape are retained; PA22 103 -> 111 with no regressions, prior 2329/2329, file audit pass. |
| `da807b9f` member-template attachment | Pass after checkpoint repair | Distinct template heads retain identity and each explicit call rebuilds its current specialization set; focused 10/10, PA22 145/310 with the original failures unchanged, prior 2329/2329, linear 16/32/64 evidence, file audit pass. |

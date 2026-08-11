# PA23 Checkpoint Audit

## Current Checkpoint Review

The landed `1d55437f` increment moved PA23 from 375/406 to 377/406. Its two
gains lower an empty pack-expanded unknown-bound array as one aligned byte with
no elements and keep qualified pack-expanded `constexpr` member values on the
constant path without emitting backing globals. Earlier assignments remained
clean at the checkpoint boundary.

The audit found two ownership violations in that increment. Slot planning and
array lowering independently inferred the zero-cardinality storage layout from
initializer syntax, so the same ABI fact had competing owners and global,
automatic, and local-static paths could diverge. Static-member value demand
also reopened retained declaration syntax and compared `constexpr` spelling at
each use. This bypassed the retained semantic graph, repeated AST work, and
briefly regressed PA21 compile-time-only demand and PA22 non-`constexpr`
value-use behavior. Both violate `spec.md` sections 2-5, 8-10.

Pack semantic analysis now publishes size/alignment on the typed braced result,
declaration analysis transfers that fact to the variable, and shared slot,
global, local-static, initializer, and lifetime consumers use it directly.
Zero-cardinality objects reserve storage but create no element construction or
destruction actions. Separately, retained member definitions capture a compact
value-use storage policy when first classified and preserve it through nested
owner routing. `EnsureStaticMemberStorage` reads only that indexed fact;
runtime address/reference formation supplies explicit storage demand, while
compile-time-only evaluation does not manufacture emission demand. No lowering
syntax inspection, retained-syntax reopening, filename recognition, global
retry, or exception control flow remains on either affected path.

The landed 1/2/4/8 array scaling remains linear: semantic nodes
32/39/53/81, lowered nodes 15/19/27/43, temporary-dependency visits
16/24/40/72, materialized-demand visits 26/30/38/54, and instructions
10/12/16/24. A current zero-cardinality probe used 25 semantic nodes, 11
lowered nodes, 22 demand visits, 9 instructions, 5,618 typed-storage bytes, and
no globals. The static-demand guard used 59 semantic nodes and emitted exactly
the two address/reference-demanded globals despite two value-only `constexpr`
specializations; the landed qualified-pack probe emitted none. Existing
`DumpNode` storage fields were generalized, so the storage handoff adds no node
size. Work remains proportional to expanded elements and indexed retained
definitions, without an AST-depth factor at use sites.

The original 377/406 checkpoint remains intact; the audit guard makes the
combined report 378/407 with the same 29 prior failures. PA1--PA22 pass
2,639/2,639, the five affected-path probes are ASan/UBSan-clean, file audit
passes with the same 13 inherited header-division advisories, and
`git diff --check` passes.

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
| Zero-cardinality expansion and static-member demand (`1d55437f`, this audit) | Typed size/alignment now owns all storage and lifetime paths; retained definition policy and explicit address/reference demand replace syntax reopening; 377/406 is preserved, the audit guard raises the report to 378/407, and representative work is linear. |

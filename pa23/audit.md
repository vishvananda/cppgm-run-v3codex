# PA23 Checkpoint Audit

## Current Checkpoint Review

The landed `8da6b98e` increment moved PA23 from 381/408 to 389/408. It restores
declaration-owned access while substituting alias templates, keeps calls in
discarded constant arms out of runtime demand, retains typed null/comparison
facts through LowIR, and delays constructor C2 publication until an entire
speculative empty chain succeeds. The eight gains cover constructor/default
pack replay, dependent current-specialization defaults, member-template alias
and result SFINAE, and unqualified member-template multi-deduction. Earlier
assignments remained clean at the checkpoint boundary.

The audit traced constructor selection through `BuildConstructorAction`,
`EmptyDefaultConstructorChain`, C1/C2 demand, and typed LowIR emission. The
elision query treated a merely declared user constructor as an empty body,
which removed both its containing implicit constructor and the required member
call. Each query also allocated and cleared an entity-sized visited array, and
identical successful or failed chains were recomputed. This violated the
semantic-fact ownership, complete cache-key, explicit demand-edge, and
proportional-work requirements in `spec.md` sections 2, 4, 5, 6, and 9.

Canonical constructor identity now owns a monotonic unknown/conservative-
failure/proven-empty fact. A user constructor is elidable only when its
definition body is known and empty; implicit and defaulted constructors retain
their language-owned status. A successful fact stores the flattened member and
base-entry dependency IDs, while reusable generation marks and scratch vectors
visit only the participating subobject graph. ABI base entries and dependency
facts are still published only after complete success. Lowering consumes those
typed IDs through the existing demand path, and the scalar path has no
competing fallback. There is no source-text dispatch, whole-program retry,
global invalidation, or exception-based candidate control flow on the repaired
path.

For 1/2/4/8 repeated proven-empty and declaration-only object pairs amid 16
unrelated classes, requests were 2/4/8/16 and cache hits 0/2/6/14; entity visits
were 3/3/3/3, dependency-edge visits 2/2/2/2, worklist pushes 3/3/3/3, and
emissions 3/3/3/3. Tokens grew 131/137/149/173, semantic nodes
21/27/39/63, and typed storage 6,413/7,085/8,429/11,117 bytes. Thus repeated
queries replay compact dependency IDs, and unrelated declarations do not enter
the elision work.

The original 389/408 checkpoint remains intact; the declaration-only guard
makes the combined report 390/409 with the same 19 prior failures. The nine
affected-path probes pass under ASan/UBSan. PA1--PA22 pass 2,639/2,639, file
audit passes with the same 13 inherited header-division advisories, and
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
| Enclosing dependency and dependent construction (`c510b4af`, this audit) | Parse-once class routing and canonical parameter-scope replay remove exponential nested-class retries and complete dependent NTTP/template-template ownership; 380/407 is preserved, the guard raises the report to 381/408, and representative work is linear. |
| Substitution and demand boundaries (`8da6b98e`, this audit) | Declaration-owned alias substitution, typed scalar facts, and dead-arm demand are preserved; constructor-chain elision now requires a known body and memoizes canonical dependency facts; 389/408 is preserved, the guard raises the report to 390/409, and repeated work is flat. |

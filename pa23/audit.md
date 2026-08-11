# PA23 Checkpoint Audit

## Current Checkpoint Review

The landed `c510b4af` increment moved PA23 from 378/407 to 380/407. It retains
enclosing-pack dependency through nested template parameter types and parses
dependent qualified/braced constructions needed by the two new passing tests.
Earlier assignments remained clean at the checkpoint boundary.

The audit found two ownership violations in the increment's affected paths.
Parser disambiguation scanned an entire class body before parsing it, while a
class-key member could first be parsed speculatively as a bit-field and then as
a declaration. Nested classes therefore replayed complete subtrees at every
level. Template parameter dependency detection separately considered an
enclosing-name set for specifiers but omitted declarators, preceding local
parameters, and their nested template-template ownership; matching then
compared an outer-dependent symbolic type directly with a concrete argument.
These behaviors violated the parse-once, retained-context, canonical-scope, and
bounded-work requirements in `spec.md` sections 2-5 and 8-10.

Class-key declarations and members now enter `ParseClass` once and continue
through a shared declarator tail only when the parsed class specifier is not a
standalone declaration. Class publication restores the speculative fact frame
and commits the resulting facts directly, without propagating change history
through every enclosing class. Template parameter parsing uses one mutable
local-name overlay plus the enclosing set, checks both specifiers and
declarators, and carries that overlay through nested template-template lists.
Template-template matching resolves outer-dependent non-type parameter types
in the canonical parameter scope after preceding bindings are installed while
leaving nested-local dependencies symbolic. Argument formation remains a
single bounded owner helper. No body prescan, subtree replay, source spelling
dispatch, global retry, or exception-based candidate control flow remains on
the repaired paths.

Before repair, nested-class depths 8 and 16 caused 6,272 and 1,605,632 parser
checkpoints, and depth 32 exceeded ten seconds. At depths
8/16/32/64/128/256 after repair, tokens were 53/93/173/333/653/1,293 and
checkpoints were 119/199/359/679/1,319/2,599; rollbacks and fact changes were
also linear. For 1/2/4/8 dependent function-pointer and template-template
parameter pairs, semantic nodes were 18/21/27/39, specialization requests
2/3/5/9, argument-list requests 6/9/15/27, candidate visits 1/2/4/8, and typed
storage 4,196/4,204/4,220/4,252 bytes. Thus work is proportional to represented
syntax, parameters, and indexed requests rather than nesting products.

The original 380/407 checkpoint remains intact; the ownership guard makes the
combined report 381/408 with the same 27 prior failures. PA1--PA22 pass
2,639/2,639, PA10 passes 157/157 under ASan/UBSan together with the five
affected-path probes, file audit passes with the same 13 inherited
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
| Zero-cardinality expansion and static-member demand (`1d55437f`, this audit) | Typed size/alignment now owns all storage and lifetime paths; retained definition policy and explicit address/reference demand replace syntax reopening; 377/406 is preserved, the audit guard raises the report to 378/407, and representative work is linear. |
| Enclosing dependency and dependent construction (`c510b4af`, this audit) | Parse-once class routing and canonical parameter-scope replay remove exponential nested-class retries and complete dependent NTTP/template-template ownership; 380/407 is preserved, the guard raises the report to 381/408, and representative work is linear. |

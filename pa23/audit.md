# PA23 Checkpoint Audit

## Current Checkpoint Review

The landed `3531871f` increment moved PA23 from 398/409 to 408/409. It carries
placeholder deduction through variable initialization, completes constructor
and conversion-template participation, records specialization result ABI and
materialization facts, and extends conditional/reference and slot lowering.
The ten gains cover defaulted constructor SFINAE, copy/move and non-template
preference, conversion-object copy initialization, conditional reference
results, template-template conversion targets, and adjacent alias/default
replay. PA1--PA22 were clean at the checkpoint boundary.

The audit traced class contextual conversion from overload selection through
constant evaluation, specialization-member demand, selected call publication,
and LowIR. `TryFoldConstantClassConversion` interpreted a retained return body
and bypassed the generic evaluator without requiring a `constexpr` function;
it therefore accepted a non-`constexpr` conversion in a constant expression.
Constant-expression conversion now uses the canonical constexpr call engine,
which demands a missing class-template member definition at its specialization
owner and enforces the declaration's `constexpr` fact. The safe ordinary O0
constant-return canonicalization is separate and memoized by canonical function
identity. A compile-fail guard proves the timing boundary; no candidate failure
is converted into a hard error or exception-based control path.

The lowering audit traced expression-statement class arguments through member
object conversion, temporary materialization, slot planning, and call lowering.
Slot planning had rediscovered whether the implicit object contained a
temporary by walking its semantic subtree. Semantic construction now propagates
one typed containment bit per edge and publishes the selected call's implicit-
object fact; lowering performs one O(1) fact read, exposed by a release counter.
The path has no source/test spelling dispatch, whole-program retry, global
invalidation, or textual reconstruction.

For 1/2/4/8 ordinary and temporary-object call pairs, fact reads were
2/4/8/16, semantic nodes 52/68/100/164, lowered nodes 26/33/47/75,
instructions 38/48/68/108, and typed storage
12,703/15,124/19,966/29,650 bytes. For 1/2/4/8 repeated safe conversion
canonicalizations, requests were 1/2/4/8, cache hits 0/1/3/7, semantic nodes
29/36/50/78, and instructions 8/11/17/29. The counters and output sizes support
linear produced-work scaling and one owner computation followed by O(1) hits.

The original 408/409 checkpoint remains intact and the audit guard raises the
combined baseline to 409/410. The final fixture-contract checkpoint names and
returns the member-pack predicate in
`500-tcc-member-constructible-pack-sfinae`, making its positive source agree
with the checked constrained-overload LowIR without a compiler semantic
override. For 1/2/4/8 pack arguments, deduction visits are 1/2/4/8, semantic
nodes are 22/24/28/36, and instructions are 5/6/8/12, while overload candidates
stay 3 and specialization requests stay 7. PA23 passes 410/410, PA1--PA22 pass
2,639/2,639, and file audit passes with the unchanged 13 inherited
header-division advisories.

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
| Constructor/conversion target and materialization (`3531871f`, this audit) | Canonical constexpr evaluation now owns constant-expression conversion and rejects non-`constexpr` calls; selected calls publish temporary-object slot facts consumed in O(1); 408/409 is preserved, the guard raises the report to 409/410, and representative work is linear. |
| Member-pack predicate fixture reconciliation (this checkpoint) | The positive probe now returns its equal-cardinality template predicate while neighboring negative paths retain candidate-local failure; PA23 409 -> 410 with linear pack work and fixed candidate/specialization counts. |

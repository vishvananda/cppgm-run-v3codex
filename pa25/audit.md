# PA25 Checkpoint Audit

## Current Checkpoint Review

Checkpoint `2e7bf454` (function-template placeholder results and deduced
class-value locals) passes after audit repair. The landed increment publishes a
specialization before analyzing its retained placeholder body, uses a canonical
four-state body fact, republishes the deduced function type and result ABI, and
retains the selected same-class transfer needed by `auto` locals.

The audit found one correctness and architecture defect on the completed-result
path. Semantic return-slot finalization and PA17 lowering independently rebuilt
class-result ABI policy from layout and lifecycle properties. In addition, the
callable override used `deferred_result_formation`, which identifies results
that cannot initially be formed but not an already-formed dependent result such
as `pair_like<T, U>`. The caller and callee therefore selected a direct
`obj<16x8>` boundary for the dependent-owner lifecycle case instead of its
required indirect result. Class special-member completion now publishes the
generic result-boundary fact once; each function-template pattern retains one
dependent-result bit; and only the canonical function binding owns a required
callable-specific override. Return-slot planning and lowering consume those
facts by identity. The previously failing dependent-owner case now matches its
checked LowIR.

The demanded trace is source bytes -> one retained template pattern and body ->
canonical specialization key -> cache publication -> one in-progress body
transition -> canonical placeholder result `TypeId` -> updated function/binding
type and result ABI -> retained typed semantic edges -> demand worklist -> direct
typed LowIR. Cache hits observe success without reanalyzing the body; recursive
deduction observes in-progress and fails immediately. The added regressions
cover repeated cache hits, `auto*`, `auto&`, collapsing `auto&&`, and recursive
deduction. The positive regression also executes through LowIR/CY86 with status
zero.

For 16/64/256 distinct `constexpr auto` specializations and their trivial token
types, tokens were 197/677/2,597 and semantic nodes 260/1,028/4,100.
Specialization requests were 81/321/1,281 with 48/192/768 cache hits; demand
pushes and emissions were 32/128/512, functions 33/129/513, instructions
112/448/1,792, and typed storage 55,413/219,573/877,149 bytes. Five-run median
semantic time was 2.045/6.782/27.001 ms and lowering time
0.850/2.662/10.529 ms. Work, storage, and time track demanded specializations
and emitted IR without a translation-unit scan or quadratic trend.

No relevant source/test shortcut, whole-program retry, lowering-time semantic
search, text transport, timeout behavior, duplicate ABI reconstruction, or
unresolved checkpoint-owned correctness, performance, or file-audit issue
remains. The one non-lambda failure is a pre-existing local-declaration/template
parse ambiguity and does not enter this ownership path. Shipped PA25 improves
from 87/130 to 88/130; the two audit regressions produce 90/132. PA1-24 pass
3,471/3,471, and file audit passes with 15 inherited nonfatal division
advisories.

## Checkpoint Audit Ledger

| Checkpoint | Audit disposition |
|---|---|
| Ordinary placeholder results (`583b174a`) | Pass after cv-reference, runtime-demand, direct-ownership, and retained-body copy repairs; shipped baseline and all earlier stages preserved; linear scaling and file audit verified. |
| Range-for statements (`b985f854`) | Pass after single-parse dispatch, category-correct one-time range binding, and condition/iteration cleanup repairs; 67/128 PA25 and 3,471/3,471 earlier tests preserved; linear scaling and file audit verified. |
| Selected class conversions (`cec97359`) | Pass after single-parse canonical conversion targets, semantic-owned parameter ABI, and modifiable-reference filtering; 85/130 PA25 and 3,471/3,471 earlier tests preserved; linear scaling and file audit verified. |
| Function-template placeholder results (`2e7bf454`) | Pass after canonical dependent-result identity and semantic-owned class-result ABI repair; shipped PA25 is 88/130, audit regressions 2/2, PA1-24 3,471/3,471; linear scaling and file audit verified. |

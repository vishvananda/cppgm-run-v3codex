# PA22 Implementation Plan

## Stage Design and Spec Alignment

PA22 extends the existing typed path
`SyntaxArena -> Program/SemanticAnalyzer -> SemanticGraphView -> GraphLowerer -> LowIR`.
Template primaries own indexed partial-pattern sequences, each retained pattern owns
one memoized canonical typed shape, and the canonical specialization table owns
identity. A specialization binding stores the selected declaration index, declaration
revision, and fixed/pack substitution overlay until completion; a later equivalent
definition refreshes only that overlay. Pack expansion is part of canonical argument
identity. Semantic lookup uses an internal specialization slot while source-oriented
emission remains a separate policy, including local-type context.

Function-template patterns have stable deque/index identity. Declaration equivalence
includes template-parameter kind and pack shape, canonical non-dependent value type,
and declaration-time structural comparison of dependent value-parameter syntax with
parameter names normalized to ordinals. A call derives template specializations from
the indexed visible patterns and its current arguments; previously materialized
specializations are cache results, not independent lookup candidates.

This stage applies `spec.md` §§2–6, 8–10: canonical specialization identity,
indexed lookup, separate retained-pattern/selection/completion state, memoized demand,
typed lowering, bounded temporary ownership, explicit work counters, and no textual
semantic keys or external compiler fallback. Partial selection may inspect ordinary
incomplete class arguments, but layout and completion remain deferred. PA23
deduction/SFINAE and §7 object-backend work remain outside this LowIR stage.

## Current Failure Map

Current result: **145/310**: the landed checkpoint remains 144/309 and the audit
identity regression adds one passing test. The remaining 165 failures are unchanged
and group by exclusive primary owner: partial matching/ordering/replay 40;
member/friend ownership, lookup, and access 44; alias/template-template/pack graph
38; explicit instantiation/specialization ownership 27; dependent lookup,
deduction, conversion, and lowering integration 16.

## Active Checkpoint

**Alias and template-template binding across retained scopes.** Owner: the canonical
alias/template declaration and its lexical parameter scope; uses retain their own
lookup scope and canonical argument overlay. Data flow: retained declaration ->
use-scope argument binding -> canonical substitution -> dependent member lookup ->
ordinary demand/completion/lowering. Apply `spec.md` §§2–6 and 8–9: stable entity
identity, typed substitution keys, immutable retained syntax, indexed lookup, and
memoized demand. Expected work is O(alias-chain depth + bound arguments + related
overloads), with O(1)-average specialization lookup and cycle detection. Validate
forward aliases, template-template packs/defaults, qualified identities, lexical
use-scope separation, and dependent result reconstruction; then run PA22 and the
through-PA21 report.

## Performance Evidence

Five-run medians on the audited path for 16/32/64 concrete class specializations
sharing one retained out-of-class member template are 3.02/5.55/10.78 ms semantic
time and 0.53/1.05/2.10 MiB peak semantic storage. Specialization requests are
80/160/320, cache hits 48/96/192, overload candidates 32/64/128, and demand pushes
and emitted functions are each 16/32/64. Time, storage, and every work counter scale
linearly; call filtering visits the selected member set and does not scan unrelated
templates. Earlier partial-family evidence remains 1.269/2.272/4.313 ms for
8/16/32 patterns with the expected O(k*s) related-candidate work.

## Completed Checkpoints

| Checkpoint | Result |
|---|---|
| `5d70a120` + `26eafabe` canonical partial selection and ownership audit | Retained selected owner/revision/substitution, canonical pack identity, deterministic replay/emission separation, and work counters; PA22 82 -> 112/309, prior 2329/2329. |
| `da807b9f` member-template attachment plus checkpoint audit | Stable owner-pattern replay; indexed member calls; static/constructor and late definitions; canonical template-head identity; no stale specialization candidates. Landed PA22 112 -> 144/309, audit corpus 145/310, prior 2329/2329. |

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

This stage applies `spec.md` §§2–6, 8–10: canonical specialization identity,
indexed lookup, separate retained-pattern/selection/completion state, memoized demand,
typed lowering, bounded temporary ownership, explicit work counters, and no textual
semantic keys or external compiler fallback. Partial selection may inspect ordinary
incomplete class arguments, but layout and completion remain deferred. PA23
deduction/SFINAE and §7 object-backend work remain outside this LowIR stage.

## Current Failure Map

Checkpoint result: **144/309**, up 32 from the 112/309 turn baseline. The remaining
165 failures group by exclusive primary owner: partial matching/ordering/replay 40;
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

Five-run medians for 16/32/64 concrete class specializations sharing one retained
out-of-class member template are 3.61/6.75/13.29 ms semantic time and
0.65/1.29/2.57 MiB peak semantic storage. Specialization requests are 80/160/320,
cache hits 48/96/192, and overload candidates, demand pushes, and emitted functions
are each 16/32/64. Time, storage, and work counters scale linearly; attachment does
not scan unrelated templates. Earlier partial-family evidence remains
1.269/2.272/4.313 ms for 8/16/32 patterns with the expected O(k*s) candidate work.

## Completed Checkpoints

| Checkpoint | Result |
|---|---|
| `5d70a120` + `26eafabe` canonical partial selection and ownership audit | Retained selected owner/revision/substitution, canonical pack identity, deterministic replay/emission separation, and work counters; PA22 82 -> 112/309, prior 2329/2329. |
| Nested template-head retention and member-template attachment | Canonical owner-pattern matching, nested replay, member call lookup, static/constructor templates, and late definitions; PA22 112 -> 144/309, prior 2329/2329. |

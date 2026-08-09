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

Current result: **111/308**, up from the checkpoint-audit baseline of 103/308 with
no PA22 regressions. The remaining 197 failures group by exclusive primary owner as
follows: partial matching/ordering/replay 40; member/friend ownership, lookup, and access 76;
alias/template-template/pack graph 38; explicit instantiation/specialization
ownership 27; dependent lookup, deduction, conversion, and lowering integration 16.

## Active Checkpoint

**Member-template declaration identity and out-of-class attachment.** Owner: the
canonical class specialization plus retained member-pattern sequence. Data flow:
qualified owner path -> selected class/partial identity -> indexed member-template
declaration -> attached definition/demand state -> ordinary function completion and
lowering. Apply PA22 member-template ownership/replacement rules and `spec.md` §§2–5,
8–9. Expected lookup is O(path depth + declarations in the selected member set),
with O(1)-average specialization-state lookup and no scan of unrelated templates.
Validate renamed owner parameters, nested owners, overload sets, definitions after
first demand, explicit replacement, and through-stage regressions.

## Performance Evidence

For synthetic families with 8/16/32 partial patterns, the same number of unique keys,
and repeated uses, five-run medians are 1.269/2.272/4.313 ms with peak semantic
storage 194/422/813 KiB. Counters report 64/256/1024 candidate visits,
24/48/96 cache hits, and exactly 8/16/32 shape materializations; the increasing shape
cache hits are declaration-equivalence and selection reuse, not rematerialization.
Representative checked-in ordering tests report 2, 4, and 2 comparisons. This is
the required O(k*s) candidate work for k requested keys and s related partials, with
ordering confined to matches and near-linear retained storage.

## Completed Checkpoints

| Checkpoint | Result |
|---|---|
| `5d70a120` canonical class/variable partial matching and ordering, audited | Retained selected owner/revision/substitution, canonical pack-expansion identity, incomplete-argument selection, deterministic replay and emission separation, indexed nested matching, and work counters; PA22 103 -> 111 during audit (82 -> 111 checkpoint total), prior 2329/2329. |

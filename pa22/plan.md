# PA22 Implementation Plan

## Stage Design and Spec Alignment

PA22 extends the existing typed path
`SyntaxArena -> Program/SemanticAnalyzer -> SemanticGraphView -> GraphLowerer -> LowIR`.
Template primaries own indexed partial-pattern sequences; retained patterns own
canonical typed argument shapes, and specialization keys continue to use canonical
template identity plus typed arguments. Dependent array bounds are canonical typed
components. Selection records one declaration owner before ordinary class
completion and LowIR demand.

This stage applies `spec.md` §§2–6, 8–10: canonical specialization identity,
indexed lookup, separate retained-pattern and specialization state, memoized demand,
typed lowering, bounded temporary ownership, work counters, and no textual keys or
external compiler fallback. PA23 deduction/SFINAE and §7 object-backend work remain
outside this LowIR stage.

## Current Failure Map

Current result: **103/308**, up from the 82/308 turn-start baseline with no PA22
regressions. The remaining 205 failures group by exclusive primary owner as follows:
partial matching/ordering/replay 48; member/friend ownership, lookup, and access 76;
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

For synthetic families with 8/16/32 partial patterns and the same number of unique
specialization keys, counters report exactly 64/256/1024 candidate visits and
8/16/32 specialization-cache hits. Median semantic time was 1.31/2.33/4.38 ms and
peak semantic storage 221/435/865 KiB. Focused two-candidate ordering cases report
two comparisons. This matches O(k*s) required indexed-candidate work plus ordering
only among matches; canonical pattern shapes are materialized once per declaration.

## Completed Checkpoints

| Checkpoint | Result |
|---|---|
| Canonical class/variable partial matching and ordering | Memoized typed shapes, strict cv/function/pack/non-type/array deduction, deterministic bidirectional ordering, equivalent redeclaration merge, and counters; PA22 82 -> 103, prior 2329/2329. |

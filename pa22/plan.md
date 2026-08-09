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

Alias templates now own retained lexical patterns and typed specialization caches.
Template-template arguments carry canonical template-marker identity; dependent
partial patterns use non-completing proxy specializations only as typed deduction
shapes, then bind the selected concrete template entity for ordinary completion.
Explicit function and class instantiations share canonical specialization bindings.
Binding-level suppression controls extern-template emission without changing the
owned definition, while indexed declaration/definition states reject duplicate
ownership and allow a later definition to monotonically release suppression.

This stage applies `spec.md` §§2–6, 8–10: canonical specialization identity,
indexed lookup, separate retained-pattern/selection/completion state, memoized demand,
typed lowering, bounded temporary ownership, explicit work counters, and no textual
semantic keys or external compiler fallback. Partial selection may inspect ordinary
incomplete class arguments, but layout and completion remain deferred. PA23
deduction/SFINAE and §7 object-backend work remain outside this LowIR stage.

## Current Failure Map

Current result: **208/310**. The remaining 102 failures group by exclusive primary
owner: member/friend ownership, lookup, and access 60; alias/template-template/pack
integration 20; explicit specialization and instantiation integration 10;
dependent lookup, deduction, conversion, and lowering 10; residual partial
ordering/replay 2.

## Active Checkpoint

**Nested member/friend template owner graph and access provenance.** Owner: each
retained nested/member/friend declaration's canonical namespace or concrete class
scope, plus the naming-class access path that selected it. Data flow: retained
declaration -> typed owner-path replay -> indexed member/friend publication ->
access-aware lookup/ADL -> ordinary demand and lowering. Apply `spec.md` §§2–6 and
8–9: stable entity identity, lexical versus semantic scope separation, indexed
lookup, monotonic replay state, and one canonical emission owner. Expected work is
O(owner-path depth + related members/friends), with O(1)-average owner indexes and
no scan of unrelated specializations. Validate nested member-template definitions,
friend access/ADL, explicit member-template replacement, inherited/private paths,
and active-owner overloads; then run PA22, through PA21, audit, and owner-depth and
member-count probes.

## Performance Evidence

Five-run medians on the audited path for 16/32/64 concrete class specializations
sharing one retained out-of-class member template are 3.02/5.55/10.78 ms semantic
time and 0.53/1.05/2.10 MiB peak semantic storage. Specialization requests are
80/160/320, cache hits 48/96/192, overload candidates 32/64/128, and demand pushes
and emitted functions are each 16/32/64. Time, storage, and every work counter scale
linearly; call filtering visits the selected member set and does not scan unrelated
templates. Earlier partial-family evidence remains 1.269/2.272/4.313 ms for
8/16/32 patterns with the expected O(k*s) related-candidate work.

Five-run medians for 16/32/64 recursively demanded class uses of one alias pattern
are 2.218/3.867/7.453 ms semantic time and 0.503/0.834/1.636 MB peak stage storage.
Specialization requests are 66/130/258, cache hits 17/33/65, layouts 33/65/129,
and lookup queries 523/1019/2011. Every work counter and storage scale linearly;
the typed alias cache adds no unrelated-template scan.

Five-run medians for 16/32/64 explicit function-instantiation definitions are
0.577/0.986/1.658 ms semantic time and 0.108/0.209/0.411 MB peak stage storage.
Specialization requests and demand pushes/emissions are exactly 16/32/64; lookup
queries are 117/229/453 and conversion checks 33/65/129. Time, storage, and all
representative work counters scale linearly with the related definition set.

## Completed Checkpoints

| Checkpoint | Result |
|---|---|
| `5d70a120` + `26eafabe` canonical partial selection and ownership audit | Retained selected owner/revision/substitution, canonical pack identity, deterministic replay/emission separation, and work counters; PA22 82 -> 112/309, prior 2329/2329. |
| `da807b9f` member-template attachment plus checkpoint audit | Stable owner-pattern replay; indexed member calls; static/constructor and late definitions; canonical template-head identity; no stale specialization candidates. Landed PA22 112 -> 144/309, audit corpus 145/310, prior 2329/2329. |
| `b0f34797` canonical alias/template entity graph | Retained alias patterns, typed cached substitution, template-template shape/identity matching, proxy partial deduction, defaults/packs, ADL ownership, and nested parameter parsing; PA22 145 -> 192/310 with no regressions, prior 2329/2329, audit pass. |
| Explicit specialization/instantiation ownership | Canonical function target selection, extern suppression, definition demand/root state, class redeclaration transitions, and primary-body replacement; PA22 192 -> 208/310 with 16 gains and no regressions, prior 2329/2329, audit pass; linear scaling evidence above. |

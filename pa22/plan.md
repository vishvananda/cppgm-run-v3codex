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
Friend templates retain lexical class scope separately from their canonical
namespace/member-template owner. Indexed entity/name ADL edges and monotonic
class/function grant sets connect each declaring specialization to cached and future
friend specializations without publishing hidden names to ordinary lookup.
Retained class-member definitions also record whether their canonical owner is the
primary or one exact partial-pattern ordinal. Replay compares that stable identity
with the specialization's selected pattern before substitution. Qualified type and
template carriers preserve their naming-class binding and access provenance.
Dependent nested owner components retain indexed names plus argument syntax until
their enclosing specialization is concrete; replay then materializes canonical
arguments and transfers the remaining pattern to the indexed nested template owner.
Semantic owner scope and source/substitution overlays remain separate, and terminal
function-template IDs resolve through the concrete structured carrier.

This stage applies `spec.md` §§2–6, 8–10: canonical specialization identity,
indexed lookup, separate retained-pattern/selection/completion state, memoized demand,
typed lowering, bounded temporary ownership, explicit work counters, and no textual
semantic keys or external compiler fallback. Partial selection may inspect ordinary
incomplete class arguments, but layout and completion remain deferred. PA23
deduction/SFINAE and §7 object-backend work remain outside this LowIR stage.

## Current Failure Map

Current result: **234/310**. The remaining 76 failures group by exclusive primary
owner: member/friend ownership, lookup, and access 36; alias/template-template/pack
integration 20; explicit specialization and instantiation integration 8;
dependent lookup, deduction, conversion, and lowering 10; residual partial
ordering/replay 2.

## Active Checkpoint

**Concrete explicit-member replacement and revision propagation.** Owner: one
canonical class/member specialization binding owns its selected explicit definition;
an earlier primary-instantiated body is replaceable only by that binding's later
explicit definition. Data flow: explicit declaration -> canonical class arguments and
member signature -> indexed specialization binding -> monotonic definition revision
and stale-body replacement -> demand queue -> one typed emission. Apply `spec.md`
§§2–6 and 9: complete specialization keys, separate definition/demand state, precise
invalidation, and lowering from the chosen declaration. Expected work is O(owner
depth + indexed target overloads + affected specialization), with no unrelated
specialization scan. Validate multiple owners, forward use, stale primary refresh,
member templates, converting constructors, duplicate rejection, then PA22, through
PA21, audit, and unrelated-owner/replacement-count probes.

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

Five-run medians with 16/32/64 differently named hidden friend templates and one
selected ADL call are 1.178/1.779/3.329 ms semantic time and 0.161/0.295/0.564 MB
peak stage storage. Associated scope/declaration visits and specialization requests
remain exactly 1/1/1 at every size; demand pushes/emissions remain 3/1, while lookup
queries grow only 76/108/172 with source declarations. The entity/name edge excludes
all unrelated friend patterns and total construction cost scales linearly.

Five-run medians for exact out-of-class ownership over 16/32/64 partial patterns are
0.967/1.601/2.989 ms semantic time and 0.136/0.223/0.432 MB peak stage storage.
Related candidates are 16/32/64. Deduction visits are 160/568/2152: the new exact
owner scan is O(P * owner arity), while the complete probe exposes the pre-existing
all-pairs partial-registration cost. Storage and observed time remain near-linear at
these sizes; the quadratic deduction counter is retained as a later optimization
target rather than hidden by the checkpoint.

Five-run medians for one late nested definition routed into 16/32/64 pre-existing
outer/inner specialization pairs are 17.73/36.78/74.19 ms semantic time and
1.260/2.485/4.945 MB peak stage storage. Specialization requests are 198/390/774,
cache hits 132/260/516, lookup queries 1919/3791/7535, and demand pushes/emissions
33/65/129. Every counter, time, and storage scales linearly; each transfer visits only
the concrete outer pattern and its directly owned nested specialization sequence.

## Completed Checkpoints

| Checkpoint | Result |
|---|---|
| `5d70a120` + `26eafabe` canonical partial selection and ownership audit | Retained selected owner/revision/substitution, canonical pack identity, deterministic replay/emission separation, and work counters; PA22 82 -> 112/309, prior 2329/2329. |
| `da807b9f` member-template attachment plus checkpoint audit | Stable owner-pattern replay; indexed member calls; static/constructor and late definitions; canonical template-head identity; no stale specialization candidates. Landed PA22 112 -> 144/309, audit corpus 145/310, prior 2329/2329. |
| `b0f34797` canonical alias/template entity graph | Retained alias patterns, typed cached substitution, template-template shape/identity matching, proxy partial deduction, defaults/packs, ADL ownership, and nested parameter parsing; PA22 145 -> 192/310 with no regressions, prior 2329/2329, audit pass. |
| `88ab9ab1` explicit specialization/instantiation ownership | Canonical function target selection, extern suppression, definition demand/root state, class redeclaration transitions, and primary-body replacement; PA22 192 -> 208/310 with 16 gains and no regressions, prior 2329/2329, audit pass; linear scaling evidence above. |
| `03b7bf00` friend-template ownership and grant propagation | Canonical namespace/member owners, indexed hidden ADL edges, cached/future specialization grants, dependent friend type-ids, and protected access context; PA22 208 -> 221/310 with 13 gains and no regressions, prior 2329/2329, audit pass; constant unrelated-friend candidate work above. |
| `403c1ff5` partial-member owner identity and naming-class access | Exact primary/partial owner validation and replay, member-template access provenance, and intermediate qualified-carrier checks; PA22 221 -> 229/310 with eight gains and no regressions, prior 2329/2329, audit pass; scaling evidence above. |
| Nested template-id owner routing | Staged canonical transfer through concrete nested patterns, owner-aware lexical overlays, retained-local validation, and structured terminal function-template lookup; PA22 229 -> 234/310 with five gains and no regressions, prior 2329/2329, audit pass; linear scaling evidence above. |

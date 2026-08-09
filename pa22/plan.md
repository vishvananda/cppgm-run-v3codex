# PA22 Implementation Plan

## Stage Design and Spec Alignment

PA22 extends the existing typed path
`SyntaxArena -> Program/SemanticAnalyzer -> SemanticGraphView -> GraphLowerer -> LowIR`.
Canonical class/function/alias specialization tables own identity; retained patterns
own lexical syntax and indexed declaration state; selection, completion, semantic
use, demand, and source-oriented emission remain separate monotonic states. Member,
friend, nested, primary, partial, and explicit definitions route through stable owner
and pattern ordinals rather than textual keys.

Function-template patterns now retain ordinary, constructor, and conversion roles
explicitly. Declaration-side function-parameter names/defaults survive an
out-of-class definition and map through trailing parameter-pack expansion before a
specialization is declared. Class-valued call results and conversion targets are
completed only when overload/conversion use demands their members; canonical shells
remain memoized and incomplete until that boundary.

This follows `spec.md` §§2–6, 8–10: canonical typed identity, indexed/provenance-aware
lookup, retained substitution scope, demand-driven completion, typed lowering,
bounded side storage, and observable work counters. It adds no PA23 SFINAE behavior
and does not pull in the §7 object backend.

## Current Failure Map

Current result: **245/310**. The remaining 65 failures have one primary owner each:
member/friend callable ownership, body replay, lookup, and access 29;
alias/template-template/pack integration 20; dependent deduction, conversion, and
lowering 10; explicit specialization/instantiation integration 4; residual partial
ordering/replay 2.

## Active Checkpoint

**Dependent callable replay and lookup provenance.** Owner: each instantiated
function owns one lexical template-argument overlay, while its concrete member owner
and naming class own access and indexed candidate lookup. Data flow: retained body ->
specialization lexical scope -> dependent member/unqualified name replay -> indexed
member/base/using and ADL edges -> overload selection -> demand and typed emission.
Apply `spec.md` §§3–6 and 9: preserve naming-class and ordinary-vs-ADL provenance,
defer bodies past open classes, and cache replay per specialization. Expected work is
O(body nodes + owner depth + visited name/ADL edges + viable candidates), with no
unrelated-template scan. Validate open-class body deferral, local-using plus ADL,
dependent base/super calls, using-imported members, nested member templates, PA22,
through PA21, audit, and 16/32/64 indexed-owner probes.

## Performance Evidence

Five-run medians for 16/32/64 independent calls whose result specialization is
initially blocked by its open class owner are 3.818/7.596/14.498 ms semantic time and
0.904/1.799/3.588 MB peak stage storage. Layouts are 32/64/128, specialization
requests 144/288/576, cache hits 112/224/448, overload candidates 272/544/1088, and
demand pushes/emissions 48/96/192. Time, storage, and every representative counter
scale linearly; result/target completion revisits only the demanded canonical shell.

Earlier audited 16/32/64 probes likewise showed linear retained-member, alias,
explicit-instantiation, hidden-friend, exact-owner, and nested-owner construction.
Exact explicit-member replacement kept selected candidates, demand, and emission
constant as unrelated concrete owners grew; the known all-pairs partial-registration
deduction counter remains a later optimization target.

## Completed Checkpoints

| Checkpoint | Result |
|---|---|
| `5d70a120` + `26eafabe` canonical partial selection | Stable owner/revision/substitution and pack identity; PA22 82 -> 112, prior 2329/2329. |
| `da807b9f` member-template attachment | Indexed member calls, retained late definitions, and template-head identity; PA22 112 -> 145. |
| `b0f34797` alias/template entity graph | Typed alias cache, template-template identity, proxy deduction, defaults/packs; PA22 145 -> 192. |
| `88ab9ab1` explicit specialization/instantiation | Canonical targets, extern suppression, definition demand, and class state transitions; PA22 192 -> 208. |
| `03b7bf00` friend-template ownership | Namespace/member owners, hidden ADL edges, grant propagation, and protected access; PA22 208 -> 221. |
| `403c1ff5` partial-member ownership | Exact primary/partial routing and naming-class access provenance; PA22 221 -> 229. |
| `c52d6734` nested owner routing | Staged nested transfer, lexical overlays, and structured terminal lookup; PA22 229 -> 234. |
| `98e953dd` explicit-member replacement | In-place primary replacement, semantic-use reset guard, and explicit shell publication; PA22 234 -> 240. |
| Callable role, metadata, and demand boundary | Constructor/conversion role split, declaration metadata inheritance with pack mapping, and demanded result/target completion; PA22 240 -> 245, prior 2329/2329, audit pass. |

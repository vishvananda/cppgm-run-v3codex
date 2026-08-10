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

Retained callable replay now combines lexical nonmember candidates with only the
active concrete member owner's inheritance lineage. Qualified inherited calls carry
their naming-class projection separately from the selected function owner's object
conversion, preserving lookup provenance through typed lowering.

Shape-keyed class specializations that encounter an unresolved dependent base enter
a terminal deferred state; concrete substitutions use distinct canonical keys.
Derived-to-base reference casts preserve xvalue storage identity, and empty-class
copy elision is limited to aggregate-member and base-subobject initialization.

Explicit nested class specializations resolve their terminal template marker in the
completed enclosing specialization's member scope. Nested template class values
carry special-member ABI facts through named-return and call boundaries; nontrivial
temporary destruction enters the existing staged full-expression cleanup flow.

Dependent `typename` primaries retain their structured qualified-name syntax until
semantic replay. An unparenthesized member access in `decltype` resolves to the
selected member's declared type; category-based reference formation remains limited
to parenthesized and other expression forms.

This follows `spec.md` §§2–6, 8–10: canonical typed identity, indexed/provenance-aware
lookup, retained substitution scope, demand-driven completion, typed lowering,
bounded side storage, and observable work counters. It adds no PA23 SFINAE behavior
and does not pull in the §7 object backend.

## Current Failure Map

Current result: **255/310**. The remaining 55 failures have one primary owner each:
member/friend callable ownership, body replay, lookup, and access 23;
alias/template-template/pack integration 18; dependent deduction, conversion, and
lowering 9; explicit specialization/instantiation integration 3; residual partial
ordering/replay 2.

## Active Checkpoint

**Retained local qualified-id and class-value replay.** Owner: the retained function
body owns source-ordered local declarators; its canonical specialization owns typed
local bindings and demand state. Data flow: local declaration -> substitution overlay
-> dependent template argument/`decltype` -> initialization recipe -> demand and
lowering. Apply `spec.md` §§2–6 and 8–9: defer lookup until locals exist, preserve
declared type separately from value category, and demand only runtime-visible
actions. Expected work is O(retained body nodes + local bindings + emitted actions),
with indexed lookup and memoized specialization. Validate explicit member-`decltype`
arguments, nested pack functional casts, local qualified replay, PA22, through PA21,
audit, and 16/32/64 scaling.

## Performance Evidence

Five-run 16/32/64 independent active-owner replay medians initially exposed sibling
specialization accumulation: semantic time 6.415/13.171/30.211 ms, overload
candidates 320/1152/4352, and specialization requests 288/1088/4224. Filtering
retained member patterns/candidates to the active inheritance lineage reduced these
to 3.719/6.850/13.823 ms, 80/160/320 candidates, and 48/96/192 requests. Peak stage
storage is 0.724/1.442/2.877 MB; lookup queries are 670/1326/2638, scope visits
1079/2135/4247, demand pushes 48/96/192, and emissions 32/64/128. Representative
work and storage now scale linearly; lexical nonmember candidates remain available
for using declarations and ADL.

Five-run 16/32/64 qualified dependent-base replay medians are
3.472/6.228/11.842 ms semantic time and 0.593/1.128/2.254 MB peak stage storage.
Lookup queries are 608/1184/2336, edge visits 16/32/64, specialization requests
112/224/448, overload candidates and demand emissions 16/32/64. Partial-shape
materialization and deduction stay constant at 1 and 15: terminal shape deferral
prevents repeated completion while concrete specialization work scales linearly.

Five-run 16/32/64 independent nested explicit-specialization medians are
1.447/2.753/5.341 ms semantic time and 0.371/0.737/1.471 MB peak stage storage.
Lookup queries are 341/677/1349, scope visits 32/64/128, specialization requests
48/96/192 with 16/32/64 cache hits, and layouts 32/64/128. Enclosing-owner
resolution and terminal member-scope lookup therefore scale linearly.

Five-run 16/32/64 combined dependent-primary and member-`decltype` replay medians
are 6.682/13.086/26.165 ms semantic time and 1.263/2.426/4.817 MB peak stage storage.
Lookup queries are 1354/2650/5242, scope visits 890/1738/3434, specialization
requests 162/322/642 with 63/127/255 cache hits, partial deduction visits
160/320/640, and demand emissions 48/96/192. Work and storage remain linear.

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
| Dependent callable replay and lookup provenance | Active-owner/base lookup with lexical using/ADL retention, open-class deferral, qualified projection provenance, and linear owner filtering; PA22 245 -> 248, prior 2329/2329, audit pass. |
| Qualified and nested callable owner replay | Terminal dependent-base shape deferral, current-owner lookup, base-reference xvalues, and scoped empty-subobject elision; PA22 248 -> 251, prior 2329/2329, audit pass. |
| Nested explicit specialization and prvalue lifecycle | Enclosing member-scope target lookup, nested nontrivial value ABI, and staged temporary cleanup; PA22 251 -> 253, prior 2329/2329, audit pass. |
| Dependent primary and member-`decltype` typing | Structured leading-`typename` primaries and declared-type member `decltype`; PA22 253 -> 255, prior 2329/2329, audit pass; linear 16/32/64 replay. |

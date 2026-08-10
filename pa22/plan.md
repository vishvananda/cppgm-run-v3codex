# PA22 Implementation Plan

## Stage Design and Spec Alignment

PA22 extends the existing typed pipeline
`SyntaxArena -> Program/SemanticAnalyzer -> SemanticGraphView -> GraphLowerer -> LowIR`.
Canonical class, function, and alias tables own specialization identity; retained
patterns own lexical syntax and source scope; lookup, completion, semantic demand,
and emission remain separate monotonic states. Concrete member replay uses indexed
owner/pattern identities rather than textual specialization keys.

Pack expansion is element-scoped: an outer expansion collects only packs it owns,
while nested declarator packs establish a new boundary. Function-template explicit
arguments bind to the first reachable pack and later parameters remain available
for call deduction. A retained member-template call may reconnect to indexed
patterns owned by the active concrete class, but does not reopen namespace lookup
from the instantiation context. Array list-casts become typed temporary-object
actions and lower through the existing temporary-storage boundary.

This applies `spec.md` §§2–6 and 8–10: canonical typed identity, provenance-aware
indexed lookup, retained substitution scopes, demand-driven completion, typed
lowering, bounded side storage, and observable work counters. It adds no PA23
SFINAE behavior and does not pull in the §7 object backend.

## Current Failure Map

Current result: **262/310**. The remaining 48 failures group by primary owner:
member/friend/function-template ownership, replay, access, overload, and demand 28;
alias/template-template/non-type-pack construction and qualified type lookup 18;
partial-specialization selection/order replay 2.

## Active Checkpoint

**Qualified alias and template-id pack replay.** Requirements: preserve canonical
type/integral/template argument kind and lexical value scope through alias expansion;
resolve structured `typename` paths through inline-namespace using edges and concrete
class owners (`spec.md` §§2–6, 8–9). Owner and flow: retained alias/template-id syntax
-> expansion element scope -> canonical alias arguments -> indexed namespace/class
lookup -> demanded member type -> typed use. Expected work is
O(pattern nodes + expanded elements + visited lookup edges + specialization
requests), with canonical specialization memoization. Validate inline-namespace
qualified packs, alias non-type packs and expressions, template-template arity,
PA22, through PA21, audit, and direct 16/32/64 scaling.

## Performance Evidence

All entries are five-run 16/32/64 medians; times are semantic milliseconds and
storage is peak-stage MB.

| Representative path | Time | Storage | Work evidence |
|---|---:|---:|---|
| Active-owner callable replay | 3.719/6.850/13.823 | 0.724/1.442/2.877 | candidates 80/160/320; requests 48/96/192 |
| Qualified dependent-base replay | 3.472/6.228/11.842 | 0.593/1.128/2.254 | lookups 608/1184/2336; requests 112/224/448 |
| Nested explicit specialization | 1.447/2.753/5.341 | 0.371/0.737/1.471 | lookups 341/677/1349; layouts 32/64/128 |
| Dependent primary/member `decltype` | 6.682/13.086/26.165 | 1.263/2.426/4.817 | lookups 1354/2650/5242; deduction 160/320/640 |
| Retained locals/concrete packs | 7.464/14.329/28.251 | 1.428/2.844/5.649 | lookups 1807/3567/7087; requests 241/481/961 |
| Direct retained deduction + array pack | 2.613/6.618/7.578 | 0.573/0.917/1.823 | lookups 443/827/1595; requests 52/100/196; instructions 55/103/199; candidates fixed at 2 |

The current direct-pack benchmark excludes recursive index-sequence construction;
lookup, specialization, storage, and emitted-instruction counters scale linearly.

## Completed Checkpoints

| Checkpoint | Result |
|---|---|
| Canonical partial selection (`5d70a120`, `26eafabe`) | Stable owner/revision/substitution and pack identity; PA22 82 -> 112. |
| Member-template attachment (`da807b9f`) | Indexed member calls, retained definitions, and template-head identity; 112 -> 145. |
| Alias/template entity graph (`b0f34797`) | Typed alias cache, template-template identity, defaults, packs, and proxy deduction; 145 -> 192. |
| Explicit specialization/instantiation (`88ab9ab1`) | Canonical targets, extern suppression, demand, and class transitions; 192 -> 208. |
| Friend-template ownership (`03b7bf00`) | Namespace/member owners, hidden ADL, grants, and protected access; 208 -> 221. |
| Partial-member ownership (`403c1ff5`) | Primary/partial routing and naming-class provenance; 221 -> 229. |
| Nested owner routing (`c52d6734`) | Nested transfer, lexical overlays, and terminal structured lookup; 229 -> 234. |
| Explicit-member replacement (`98e953dd`) | In-place replacement, reset guard, and explicit shell publication; 234 -> 240. |
| Callable metadata and demand | Callable roles, declaration metadata inheritance, parameter-pack mapping, and result completion; 240 -> 245. |
| Dependent callable provenance | Active owner/base filtering with lexical using/ADL retention and qualified projection; 245 -> 248. |
| Qualified/nested callable replay | Terminal base-shape deferral, base-reference xvalues, and scoped empty-subobject elision; 248 -> 251. |
| Nested specialization/prvalue lifecycle | Enclosing member lookup, nested value ABI, and staged temporary cleanup; 251 -> 253. |
| Dependent primary/member `decltype` | Structured leading `typename` and declared-type member `decltype`; 253 -> 255. |
| Retained locals/concrete packs (`4edd7339`) | Local-value deferral, aggregate transfer, provisional demand, pack scopes, and delegation replay; 255 -> 258. |
| Nested packs/function-template replay | Nested expansion boundaries, direct kind validation, explicit pack allocation, active-class retained lookup, and array-temporary lowering; 258 -> 262, prior 2329/2329, audit pass. |

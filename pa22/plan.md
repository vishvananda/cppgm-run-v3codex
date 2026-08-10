# PA22 Implementation Plan

## Stage Design and Spec Alignment

PA22 extends the existing typed pipeline
`SyntaxArena -> Program/SemanticAnalyzer -> SemanticGraphView -> GraphLowerer -> LowIR`.
Canonical class, function, and alias tables own specialization identity; retained
patterns own lexical syntax and source scope; lookup, completion, semantic demand,
and emission remain separate monotonic states. Concrete member replay uses indexed
owner/pattern identities rather than textual specialization keys.

Pack expansion is element-scoped: an outer expansion collects only packs it owns,
while nested declarator packs establish a new boundary. Compound non-type expansion
operands recursively discover their packs, then replay once in each ordered element
scope. A symbolic pack exemplar keeps its expansion marker when an alias forwards
it into another canonical template-id. Function-template explicit arguments bind
to the first reachable pack and later parameters remain available for call
deduction. Their syntax is interpreted
against each candidate's canonical type/value/template kinds instead of a type-only
probe; explicit calls with a pack defer specialization until deduction supplies the
remaining elements. Parser facts are
block scoped, so an unqualified local value can shadow a template without
corrupting qualified lookup. Dependent qualified result shapes stay retained until
substitution; partial patterns mark dependent owner qualifications as non-deduced,
then replay them exactly after other arguments bind concrete parameters. Direct
template-ids still resolve to canonical concrete types.
Static initializers demanded while type-checking a skipped logical arm evaluate as
independent constant roots. Array list-casts and scalar value initialization lower
through typed boundaries. Partial deduction records pack participation even when
zero elements bind, and matches concrete incomplete specializations through their
canonical template heads without demanding layout. Qualified C-style cast type-ids
enter that same typed path.
Concrete specializations retain the fixed/pack ABI boundary; static member globals
use structured owner identity and weak ODR emission. Initializer argument packs are
expanded before constructor selection, function ids decay under pointer targets,
and runtime constant use demands only indexed retained definitions while explicit
member specializations suppress primary replay.
Dependent qualified declarations retain rooted template-ids, conversion-name
boundaries, and member qualifications. Shape construction gives unknown dependent
member templates unique non-deduced markers; concrete replay performs exact owner
lookup, derived-to-base deduction, reference operand layout, and runtime declaration
demand before typed lowering.

This applies `spec.md` §§2–6 and 8–10: canonical typed identity, provenance-aware
indexed lookup, retained substitution scopes, demand-driven completion, typed
lowering, bounded side storage, and observable work counters. It adds no PA23
SFINAE behavior and does not pull in the §7 object backend.

## Current Failure Map

Current result: **291/310**. The 19 remaining failures group by actual phase owner:
specialization-local-static initialization presentation 1;
dependent declaration/lookup/overload rejection 7; typed materialization,
conversion, empty-object, and lifetime lowering 11.

## Active Checkpoint

**Dependent declaration and owner-lookup replay.** Requirements: dependent member
typedefs, aliases, qualified declarations, and overload candidates retain canonical
owner identity plus lexical source scope; concrete substitution resolves each owner
path once and rejects invalid candidates before lowering (`spec.md` §§2–6, 8–10).
Owner and flow: retained declaration/path -> canonical argument tuple -> indexed
owner/member lookup -> typed declaration or candidate set -> demand -> lowering.
Expected work is O(replayed declarations + owner-path edges + viable candidates),
with no textual specialization scan or unrelated shell completion. Validate the
seven declaration/lookup/overload failures, adjacent nested-owner/partial/friend
tests, PA22, through PA21, audit, and 16/32/64 independent-owner scaling.

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
| Alias non-type expression replay | 2.682/5.871/18.499 | 0.518/1.339/4.875 | lookups 486/1118/3150; requests 70/134/262; constexpr calls 17/33/65; steps 34/66/130 |
| Compound non-type pack replay | 13.216/24.053/47.324 | 1.516/2.908/5.708 | lookups 1348/2372/4420; scopes 677/1189/2213; nodes 2118/4166/8262; requests fixed at 96 |
| Template-proxy call replay | 9.827/13.611/20.620 | 1.139/1.573/2.512 | lookups 1398/2166/3702; scopes 685/941/1453; access 1008/1776/3312; requests fixed at 96 |
| Alias-partial canonical forwarding | 5.449/8.808/15.663 | 1.564/2.492/4.750 | lookups 1849/3337/6313; requests 307/563/1075; deduction 672/1184/2208; candidates fixed at 16 |
| Short-circuit static demand | 12.335/25.270/63.085 | 3.349/6.762/17.015 | expressions 945/1761/3393; lookups 3040/6104/14536; requests 957/1773/3405; deduction 849/2161/6321 |
| Dependent qualified-type replay | 4.170/7.961/15.067 | 0.851/1.713/3.501 | scopes 340/660/1300; lookups 880/1728/3424; requests 82/162/322; candidates 16/32/64; deduction 96/192/384 |
| Repeated-pack/incomplete-head ordering | 12.134/23.043/46.103 | 2.109/4.331/8.592 | candidates 96/192/384; comparisons 32/64/128; deduction 1333/2645/5269; requests 730/1450/2890 |
| Dependent call replay/constructor demand | 3.052/4.072/6.903 | 0.506/0.696/1.038 | candidates 133/245/469; requests 156/300/588; cache hits 144/288/576; lookups 775/1335/2455; demand pushes fixed at 13 |
| Dependent qualified declaration replay | 9.819/19.353/38.468 | 1.662/3.025/6.022 | lookups 1894/3702/7318; requests 311/615/1223; candidates 401/801/1601; demand pushes 66/130/258 |
| Canonical callable declarations | 3.314/6.275/12.513 | 0.647/1.288/2.570 | lookups 526/1038/2062; candidates 80/160/320; requests 64/128/256; demand pushes 32/64/128 |
| Specialized static definition demand | 1.469/2.523/4.784 | 0.305/0.602/1.196 | lookups 367/719/1423; requests 48/96/192; demand/static visits 16/32/64; globals 17/33/65 |

The alias benchmark confirms linear specialization requests and constexpr call/step
counts. Its recursive variadic `cx_plus` evaluator retains 440/1648/6368 local-index
probes, explaining the quadratic storage/time component; alias replay itself does
not multiply specialization work. The compound-pack benchmark uses 16 independent
specializations; its element scopes, nodes, lookups, time, and storage scale linearly.
The template-proxy benchmark likewise keeps specialization and signature work fixed
while argument-scope lookup, time, and storage grow linearly.
The alias-partial benchmark uses 16 independent outer specializations. Candidate
count stays fixed while canonical requests, deduction visits, lookup, time, and
storage track the forwarded 16/32/64-element pack without replay multiplication.
The short-circuit benchmark uses 16 independently prefixed recursive folds and one
shared suffix family. Canonical completion has no retry cascade; the superlinear
time/storage follows the existing trailing-pack partial matcher, whose suffix
deduction visits grow quadratically (849/2161/6321).
The qualified-type benchmark instantiates 16/32/64 independent concrete owners.
Candidate replay, deduction, lookup, time, and storage all scale linearly, showing
one exact post-substitution validation per canonical partial request.
The ordering benchmark combines equal/unequal repeated packs with incomplete
template-template heads; work, elapsed time, and storage remain linear.
The call benchmark repeats incomplete-pointee calls, later member-template address
binding, and evaluated braced call objects. Candidate visits, requests, lookups,
time, and storage scale linearly; canonical demand is constant after first use.
The qualified-declaration benchmark recursively demands 16/32/64 distinct owner
specializations with one dependent qualified-base assignment each. Lookup,
specialization, candidate, demand, elapsed-time, and storage growth are linear.
The callable benchmark constructs 16/32/64 distinct owners, each with one unused
ill-formed return-class shell and one selected one-argument member template. Every
work counter, semantic time, and peak storage scales linearly; partial deduction is
zero because overload arity rejects the shell before template deduction.
The static-definition benchmark takes the address of 16/32/64 specialized integral
members with retained out-of-class definitions. Demand, initializer visits, globals,
lookup, specialization requests, time, and storage all scale linearly. The remaining
local-static fixture differs only because its oracle stages function-address
constants dynamically; the compiler keeps C++11 constant initialization.

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
| Qualified alias/template-id replay | Pack calls defer to deduction, qualified result shapes defer lookup, block-local parser shadows stay scoped, and floating value-init stays typed; 262 -> 265, prior 2329/2329, audit pass. |
| Compound non-type pack replay | Recursive pack discovery and ordered element scopes for comma/void/qualified-base expressions; 265 -> 268, prior 2329/2329, audit pass. |
| Template-proxy call replay | Pattern-directed canonical explicit arguments preserve template proxies and non-trailing pack allocation; 268 -> 269, prior 2329/2329, audit pass. |
| Alias-partial specialization replay | Symbolic alias-pack forwarding preserves canonical expansion identity for non-type partial deduction and base/member replay; 269 -> 271, prior 2329/2329, audit pass. |
| Short-circuit constant-demand isolation | Skipped-arm suppression no longer contaminates independently demanded static initializers; direct and alias recursive folds pass; 271 -> 272, prior 2329/2329, audit pass. |
| Dependent qualified member-type replay | Top-level dependent owner qualifications are non-deduced during shape construction and exactly replayed after concrete binding; 272 -> 273, prior 2329/2329, audit pass. |
| Partial ordering and incomplete template heads | Empty repeated packs retain deduction identity; concrete incomplete heads match without layout demand; 273 -> 275, prior 2329/2329, audit pass. |
| Dependent function-template call replay | Incomplete pointee shells avoid layout demand, class validation predeclares later member-template heads, and evaluated braced temporaries demand their selected constructors; 275 -> 278, prior 2329/2329, audit pass. |
| Dependent qualified declaration replay | Rooted/template-id retention, qualified member lookup, derived-base deduction, reference layout, dependent template markers, and external-callee demand; 278 -> 285, prior 2329/2329, audit pass. |
| Canonical callable declaration reconciliation | Qualified/direct-init disambiguation, return-class shells, retained non-deduced partial paths, and explicit template-id friend grants; 285 -> 288, prior 2329/2329, audit pass. |
| Specialization-owned static initialization | Pack-aware ABI identity, initializer-pack replay, structured constexpr pointers, exact retained-definition demand, and explicit-specialization suppression; 288 -> 291, prior 2329/2329, audit pass. |

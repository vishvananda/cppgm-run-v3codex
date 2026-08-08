# PA19 Full-Stage Plan

## Stage Design and Spec Alignment

PA19 extends the PA11/PA12 canonical semantic graph and PA15-PA18 typed LowIR
path with retained template patterns and demanded specializations. Patterns own
one PA10 syntax body; specializations use canonical `(pattern, TypeId...)` keys,
parent-linked argument scopes, indexed lexical/using/associated relations, and
the ordinary class/function, lifetime, and lowering owners. Supported explicit
type arguments remain ordinary PA10 `type-id` trees below interned qualified-name
components; `BuildTypeId` supplies canonical argument identity, so rendered
names never drive specialization, while semantic-only serialization preserves
the PA10 public dump contract. Class-specialization entities and
function-specialization bindings own slices in one canonical `TypeId` argument
pool. Function-local named types retain their enclosing canonical function
binding for specialization, emission, and ABI identity. Canonical operator-ids
survive split close-angle tokens; member-function patterns retain class owner
and access; dependent function results use one non-deduced shape until replay.
Enum-only operator candidates use a compact index keyed by `(ScopeId,
NameId, enum TypeId, operand)`; visibility and associated-scope edges choose
index owners before exact candidates are ranked. Selected enum conversions and
empty class-value constructors are semantic facts consumed by call staging and
typed lowering. Scalar functional value-initialization retains provenance;
typed lowering applies selected immediate-conversion, template-layout, and
constant-control facts without lookup. Immediate policy is keyed only by the
local source/target and parent expression facts, never by a translation-unit
template registry; pointer value-initialization is owned by generic literal
lowering rather than a call-site exception. Explicit-instantiation demand,
weak ODR linkage, object roots, and structured ABI identity remain
binding/entity-owned. This follows
`spec.md` sections 1-6 and 9: source regions are parsed once, hot identity/cache
access is O(1) average, completion is monotonic, retained syntax is shared,
unrelated candidates are not materialized, and lowering performs neither lookup
replay nor presentation-name reconstruction. Native IR and ELF remain later
stages.

## Current Failure Map

Canonical default/base replay raises PA19 to 297/300 while preserving
PA1-PA18. The exact remaining three-test set groups as follows:

| Failures | Shared behavior | Owner |
|---:|---|---|
| 2 extension declarations | Dependent GNU alignment and variable-template partial syntax. | PA10/PA19 declaration patterns |
| 1 allocation lowering | Inherited class `operator new` reaches an unlowered semantic node. | PA12 selection and PA16 allocation lowering |

## Active Checkpoint

Retain the remaining extension declaration shapes: dependent GNU `__alignof`
inside class-template alignment and variable-template declarations with a
defaulted parameter and partial specialization. PA10 owns lossless syntax;
PA19 owns retained declaration patterns and specialization-time type/alignment
facts. The flow is `one syntax region -> declaration classification -> retained
pattern/default frame -> demanded semantic fact`, aligned with `spec.md`
sections 1-4: parsing remains iterative, source is not reparsed, and lookup is
canonical rather than spelling-driven. Expected work is O(declaration syntax
plus demanded specialization facts). Validate both failures, PA19, PA1-PA18,
file audit, and repeated extension-declaration scaling.

## Performance Evidence

| Probe | Result |
|---|---|
| 128/256/512 paired local-relational and qualified-shadow uses | Tokens 3,911/7,751/15,431; syntax nodes 4,942/9,806/19,534; scans 128/256/512 at exactly two tokens with none failed; parser storage 33,640/66,664/132,712 bytes; median parse time 1.176/2.336/4.716 ms. |
| 128/256/512 structural `result_traits<F, F (*)()>::type` uses | Requests 128/256/512 with 127/255/511 hits; canonical types stay at 36, layouts at two, member visits at one, and lookup misses at three; semantic nodes 133/261/517, storage 125,486/236,974/464,046 bytes, and median semantic time 1.246/2.329/4.531 ms. |
| 64/128/256 same-spelling function-local type specializations | Requests and demand pushes 64/128/256 with no duplicate completions; semantic nodes 837/1,669/3,333, typed storage 217,243/434,323/868,627 bytes, and semantic time 4.484/10.485/17.659 ms. |
| 32/64/128 indexed member-template candidates | Candidate visits are exactly 32/64/128 with one viable overload and four conversions; specialization requests 35/67/131, peak semantic storage 336,615/659,683/1,305,887 bytes, and median semantic time 1.425/4.274/8.103 ms. |
| 128/256/512 mixed scalar/control replay operations, audit rerun | Semantic nodes 2,069/4,117/8,213; lowered nodes 1,421/2,829/5,645; conversion checks 1,288/2,568/5,128; peak storage 1,165,394/2,318,866/4,625,810 bytes; five-run median semantic-plus-lowering time 6.515/12.769/25.157 ms. |
| 16/32/64 polymorphic specializations, static objects, and unused inline definitions | Requests 16/32/64; demand pushes 33/65/129; semantic nodes 279/551/1,095; lowered nodes 83/163/323; emitted functions 50/98/194 (the 16/32/64 unused inline bodies stay absent); globals 64/128/256; typed storage 165,813/331,285/662,229 bytes; five-run median semantic-plus-lowering time 2.469/4.452/8.442 ms. |
| 32/64/128 nested class-default/base-constructor levels | Requests and base-action visits are 32/64/128; demand pushes are 33/65/129; semantic nodes 335/655/1,295 and instructions 265/521/1,033. Output grows 26,223/77,599/257,151 bytes with required nested ABI spellings; five-run median semantic/lowering time is 2.634/4.473, 5.868/14.832, and 15.068/54.327 ms, while canonical replay work remains linear. |

## Completed Checkpoints

| Checkpoint | Final result | Principal evidence |
|---|---|---|
| PA18 handoff | Pass | PA1-PA18 through report clean |
| Demanded function-template definitions | Pass | Canonical merge/deduction/cache; PA19 14 to 32, prior 1713/1713 |
| Canonical class-template identity/layout | Pass | Retained patterns, defaults, forward upgrade, canonical specialization shells and one-time layout; PA19 32 to 129, 13/13 focused, prior 1713/1713, audit pass |
| Current-instantiation and retained member owners | Pass | Canonical injected-name bridge, structured nested owner paths, renamed definition scopes, deferred functions/special members, static/nested definitions; PA19 129 to 166, prior 1713/1713, audit pass |
| Indexed dependent ADL and using replay | Pass | Canonical class arguments, structural class-template deduction, direct associated template candidates and using aliases; PA19 166 to 177, linear candidate probe, prior 1713/1713, audit pass |
| Explicit and target-context specialization | Pass | Partial explicit completion, deferred overload-set arguments, target-shape deduction, canonical template ABI/symbol identity, parameter-scoped unevaluated trailing returns; PA19 177 to 183, linear target probe, prior 1713/1713, audit pass |
| Structured qualified specialization replay | Pass | Indexed using/member lookup, separate template-owner and use-site argument scopes, canonical replay through dependent bases and namespace owners; PA19 183 to 190, linear qualified probe, prior 1713/1713, audit pass |
| Namespace-visible function-template replay | Pass | Template-owner provenance in ordinary lookup, name-specific using/inline traversal and hiding, owner-local deduction aliases, parenthesized call reclassification, scalar reference-return slot; PA19 190 to 195, linear relation probe, prior 1713/1713, audit pass |
| Indexed retained-syntax reclassification | Pass | Lexical type/callable hiding, retained direct calls and constructor initialization, parenthesized value operators and parameter names; PA19 195 to 202, six focused cases, linear shape probe, prior 1713/1713, audit pass |
| Structural dependent-type replay | Pass | Qualified `decltype`, parameter-visible array references, deferred template shape returns, member trailing-return object context, elaborated template-ids and keyword boundaries; PA19 202 to 208, linear type/path probe, prior 1713/1713, audit pass |
| Indexed specialization completion demand | Pass | Canonical incomplete shells, O(1) entity-to-pattern/argument recovery, layout/member demand, incomplete-cycle nested retention, and rebound out-of-class nested definitions; PA19 208 to 214, six focused cases, linear demand probe, prior 1713/1713, audit pass |
| One-pass retained-definition validation | Pass | Indexed lexical scopes, nondependent value/type classification, parameter and member redeclaration checks, dependent-name deferral, and retained special-member exception matching; PA19 214 to 225, all 11 accepted-invalid cases, linear body probe, prior 1713/1713, audit pass |
| Canonical function-template deduction and result shapes | Pass | Memoized dependent-pattern classification, nondependent conversion deferral, preserved cv/ref deduction, postfix-cv recovery, target-driven operator operands, and nested reference/array declarators; PA19 225 to 233, prior 1713/1713, audit pass |
| Canonical static-member constant/storage demand | Pass | PA15 consumes canonical constant `id-expression` facts; PA12 suppresses synthetic globals for non-odr constant uses while explicit out-of-class definitions remain ordinary storage; PA19 233 to 241 across eight cases, prior 1713/1713, audit pass |
| Canonical class-value call and construction boundaries | Pass | Direct, indirect, and converting calls share selected argument staging; typed call-passing facts, aggregate helpers, reference-move actions, and retained defaulted definitions reuse ordinary lowering; PA19 241 to 247 across six cases, linear argument/member probes, prior 1713/1713, audit pass |
| Definition-time non-template call provenance | Pass | Node-indexed candidate/naming-class facts bypass concrete dependent bases while fixed-base replay remains intact; PA19 247 to 250, constant call-candidate probe across base depth, prior 1713/1713, audit pass |
| Complete retained call provenance and demand | Pass | Node-indexed function/template/empty sets, naming class and ADL bit replay; local using sets, cv-reference ordering, value-aware `sizeof` ambiguity, and selected demand in template units with PA18-compatible ordinary anchors; PA19 250 to 255, linear pattern-set probe, prior 1713/1713, audit pass |
| Explicit class-instantiation completion and member demand | Pass after audit fix | Entity-owned canonical argument slices, indexed source-member demand, N3485 namespace/key/order controls, typed owner/argument symbol identity, structured nested-template ABI names, weak linkage and parser-preserved object roots; PA19 handout 255 to 258 plus 3 audit regressions, linear member probe, prior 1713/1713 |
| Canonical enum builtin competition and class default arguments | Pass after audit fix | Exact enum-parameter index and filtering, comma fallback, typed conversion and source-selected constructor facts; PA19 261 to 264 plus 2 audit regressions, constant unrelated-candidate work, linear required work, prior 1713/1713 |
| Declaration-owned local and qualified type replay | Pass after audit fix | Scoped parser facts, retained type-argument trees, interned qualified/current-class identity, canonical function-pointer specialization, contextual bool conversion, and alias-inherited constructors; PA19 266 to 272 across six cases, bounded parser scans, one-completion specialization probe, prior 1713/1713 |
| Canonical local-type specialization and emission identity | Pass | Collision-free compact specialization scope keys, function-owned local type identity, typed emission keys and structured local-type ABI facts; PA19 272 to 274, two focused cases, linear specialization probe, prior 1713/1713, audit pass |
| Canonical retained callable/member replay | Pass | Canonical `>>`/qualified `[]` ids, indexed member-template owner/access replay, adjusted parameter bindings, static callable lowering, deferred dependent results, forwarding-reference deduction and constrained tie-breaking; PA19 274 to 280, six focused cases, linear candidate probe, prior 1713/1713, audit pass |
| Selected scalar/control replay | Pass after audit fix | Local source/target, constant, and template-layout conversion keys; literal-owned pointer null provenance; bool/narrowing and direct constant branches; PA19 280 to 288 plus two audit controls, linear mixed replay evidence, prior 1713/1713 |
| Demand-owned inline and polymorphic static/ABI lowering | Pass | Explicit inline emission demand, deferred-completion fact, canonical RTTI type encoding, and typed static vptr data; PA19 290 to 294 across four cases, linear demand/ABI probe, prior 1713/1713, audit pass |
| Canonical default and base-constructor replay | Pass | Pattern-owned required arity, structured template-id base resolution, direct derived/base reference casts, selected constructor ABI-pair demand, and empty-base copy lowering; PA19 294 to 297 across three cases, nested replay probe, prior 1713/1713, audit pass |

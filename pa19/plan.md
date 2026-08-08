# PA19 Full-Stage Plan

## Stage Design and Spec Alignment

PA19 extends the PA11/PA12 canonical semantic graph and PA15-PA18 typed LowIR
path with retained template patterns and demanded specializations. Patterns own
one PA10 syntax body; specializations use canonical `(pattern, TypeId...)` keys,
parent-linked argument scopes, indexed lexical/using/associated relations, and
the ordinary class/function, lifetime, and lowering owners. Class-specialization
entities and function-specialization bindings own slices in one canonical
`TypeId` argument pool. Enum-only operator candidates use a compact index keyed
by `(ScopeId, NameId, enum TypeId, operand)`; visibility and associated-scope
edges choose index owners before exact candidates are ranked. Selected enum
conversions and empty class-value constructors are semantic facts consumed by
call staging and typed lowering. Explicit-instantiation demand, weak ODR
linkage, object roots, and structured ABI identity remain binding/entity-owned.
This follows `spec.md` sections 2-6 and 9: hot identity/cache access is O(1)
average, completion is monotonic, retained syntax is shared, unrelated
candidates are not materialized, and lowering performs neither lookup replay
nor presentation-name reconstruction. Native IR and ELF remain later stages.

## Current Failure Map

The landed enum/default checkpoint baseline of 264/296 is preserved. Two audit
regressions pass for 266/298 combined, while the same complete 32-test handout
failure set remains:

| Failures | Shared behavior | Owner |
|---:|---|---|
| 8 lookup/call failures | Member/operator overloads, target function references, ADL, and logical template operators. | PA19 candidate materialization and PA12/PA16 overload selection |
| 15 replay/completion failures | Dependent/local names, nested and base completion, aliases, shadowing, and retained scopes. | PA12 retained provenance and PA19 specialization completion |
| 4 declaration/default failures | Function aliases, alignment, variable-template syntax, and default-argument demand. | PA19 declaration patterns and demand owners |
| 5 emission/lifetime mismatches | Reference casts, local enum identity, virtual destruction, unevaluated construction, and unused inline emission. | PA12 selected facts and PA15-PA19 typed lowering/demand |

## Next Substantial Checkpoint

Complete declaration-owned local and qualified type replay across retained
template bodies. The retained validator and PA19 specialization owner will
carry canonical lexical-scope, template-argument, current-specialization, and
qualified-name facts through for-init scopes, postfix-cv aliases, nested member
definitions, shadowed template-ids, concrete class arguments, and constructor
parameter aliases. This applies `spec.md` sections 1-5 and 9: each body remains
parsed once, environments remain parent-linked, lookup is by interned name and
scope identity, and one restored fact cannot trigger broad retry. Validate the
affected replay/completion group, PA1-PA18, file audit, and 128/256/512 retained
local-declaration and qualified-owner probes.

## Performance Evidence

| Probe | Result |
|---|---|
| One demanded function specialization across 128/256/512 calls | Requests 128/256/512, hits 127/255/511, one demand push; semantic nodes and instructions grow linearly at 783/1,551/3,087 and 519/1,031/2,055. |
| One demanded class specialization across 128/256/512 object uses | Requests 128/256/512, hits 127/255/511, exactly one layout/member visit; typed bytes 132,519/263,463/525,351 and semantic time 1.82/3.62/7.16 ms. |
| Retained call with 64/128/256 visible template patterns | Two overload candidates, five conversion checks, two specialization requests, three functions, and 11 instructions stay constant; lookup visits 364/684/1,324 and semantic time 2.34/4.62/8.95 ms scale with the published lookup set. |
| Explicit class definition with 64/128/256 members | One specialization request; 64/128/256 demand pushes/emissions, 129/257/513 instructions, and 1.10/2.12/3.96 ms semantic time. |
| 128/256/512 unrelated same-name enum operators | Audit index keeps declaration visits, overload visits, conversion checks, and conversion-cache misses constant at 2/2/9/1; typed bytes 132,259/263,203/525,091 and semantic time 2.88/4.59/9.56 ms scale with source publication. |
| 128/256/512 language-required exact enum operators | Declaration visits 129/257/513, overload visits 128/256/512, conversion checks 263/519/1,031, typed bytes 132,542/263,358/524,990, and semantic time 2.37/4.48/9.06 ms scale linearly with required candidates. |

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

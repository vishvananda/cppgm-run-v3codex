# PA19 Full-Stage Plan

## Stage Design and Spec Alignment

PA19 extends the PA11/PA12 canonical semantic graph and PA15-PA18 typed LowIR
path with retained template patterns and demanded specializations. Patterns own
one PA10 syntax body; specializations use canonical `(pattern, TypeId...)` keys,
parent-linked argument scopes, indexed lexical/using/associated relations, and
the ordinary class/function, lifetime, and lowering owners. Class-specialization
entities and function-specialization bindings own slices in one canonical
`TypeId` argument pool. Explicit-instantiation state records member demand,
weak ODR linkage, and object roots; typed lowering uses the owner path and
argument IDs for symbol/ABI identity, and the textual LowIR adapter preserves
the root fact. This follows `spec.md` sections 2-6 and 9: identity/cache access
is O(1) average, completion is monotonic, retained syntax is shared, and
lowering performs neither lookup replay nor presentation-name reconstruction.
Native IR and ELF remain later-stage boundaries.

## Current Failure Map

Explicit class-instantiation completion and member demand plus its audit left
the turn baseline at 261/296. Canonical enum competition and empty class-value
default arguments now pass three more cases, leaving this complete 32-test set:

| Failures | Shared behavior | Owner |
|---:|---|---|
| 8 lookup/call failures | Member/operator overloads, target function references, ADL, and logical template operators. | PA19 candidate materialization and PA12/PA16 overload selection |
| 15 replay/completion failures | Dependent/local names, nested and base completion, aliases, shadowing, and retained scopes. | PA12 retained provenance and PA19 specialization completion |
| 4 declaration/default failures | Function aliases, alignment, variable-template syntax, and default-argument demand. | PA19 declaration patterns and demand owners |
| 5 emission/lifetime mismatches | Reference casts, local enum identity, virtual destruction, unevaluated construction, and unused inline emission. | PA12 selected facts and PA15-PA19 typed lowering/demand |

## Active Checkpoint

Complete: canonical enum builtin competition and empty class-value default
arguments. PA16 owns candidate construction from canonical operand `TypeId`s:
builtin targets/ranks and indexed ordinary/template/ADL candidates produce one
selected builtin or `BindingId`; PA12 rejects implicit integral-to-enum flow,
and PA15 consumes the selected enum conversion without reconstructing it.
PA12/PA16 also preserve an empty aggregate functional cast as a demanded
constructor action when it crosses a by-value default-argument boundary. This
applies `spec.md` sections 2-4, 6, and 9. Candidate work is O(actual candidates
and associated edges), deduplication is O(1) average, and class construction adds
one demand edge. Validate builtin preference/fallback, definition-point enum
comparison with a class default argument, PA1-PA18, file audit, and the
128/256/512 indexed candidate probe.

## Performance Evidence

| Probe | Result |
|---|---|
| One demanded function-template specialization, 128/256/512 calls | Requests 128/256/512, hits 127/255/511, one demand push; semantic nodes 783/1,551/3,087 and instructions 519/1,031/2,055. |
| One class-template specialization, 128/256/512 object uses | Requests 128/256/512, hits 127/255/511, exactly one class layout/member visit; semantic nodes 1,544/3,080/6,152, typed bytes 132,519/263,463/525,351, semantic time 1.82/3.62/7.16 ms. |
| One renamed out-of-class member specialization, 128/256/512 calls | Requests 128/256/512, hits 127/255/511, one class layout and one demanded body emission; semantic nodes 2,064/4,112/8,208, typed bytes 144,482/285,794/568,418, semantic time 2.41/4.78/9.45 ms. |
| Indexed class-argument ADL of one function template, 128/256/512 calls | Scope visits 128/256/512 and declaration visits 256/512/1,024; one demanded body, semantic nodes 797/1,565/3,101, typed bytes 131,240/258,344/512,552, semantic time 1.22/2.08/3.95 ms. |
| Target-deduced function-template address, 128/256/512 uses | Overload visits 128/256/512, requests 256/512/1,024 with 255/511/1,023 cache hits, one demanded body; semantic nodes 922/1,818/3,610, typed bytes 145,770/285,546/565,098, semantic time 1.41/2.67/5.08 ms. |
| Namespace-qualified class template with use-site argument, 128/256/512 uses | Scope visits 139/267/523, zero using-edge visits, requests 128/256/512 with 127/255/511 cache hits; semantic nodes 911/1,807/3,599, typed bytes 47,487/92,415/182,271, semantic time 1.19/2.01/3.79 ms. |
| One function template through 128/256/512 nested using-directives | Scope visits 781/1,549/3,085 and relation visits 512/1,024/2,048; exactly one specialization request, semantic nodes 141/269/525, constant 3,284 typed bytes, semantic time 1.68/3.07/3.74 ms. |
| 128/256/512 retained direct initializers with one qualified template call each | Semantic nodes 795/1,563/3,099, lookup scope visits 690/1,330/2,610, requests 515/1,027/2,051 with 513/1,025/2,049 cache hits, typed bytes 160,143/314,127/622,095, semantic time 6.51/12.41/26.73 ms. |
| 128/256/512 qualified `decltype` class-template arguments | Semantic nodes 914/1,810/3,602, lookup scope visits 278/534/1,046, requests 128/256/512 with 127/255/511 cache hits and one specialization; typed bytes 46,518/91,446/181,302. |
| 128/256/512 incomplete shells followed by object-definition demand | Requests 256/512/1,024 with 128/256/512 cache hits, layout member visits 128/256/512, semantic nodes 1,285/2,565/5,125, typed bytes 153,074/304,626/607,730, semantic time 19.5/40.6/74.9 ms. |
| One retained function body with 128/256/512 local declarations | Constant six lookup queries and no specialization requests; semantic peak bytes 126,695/236,775/458,983 and semantic time 1.64/3.15/5.79 ms. |
| Template-operator stress after deduction normalization | 122 tokens; 16 specialization requests with 15 cache hits, 22 candidate visits, 40 conversion checks, one demanded body, and 0.53 ms semantic time. Dependency classification is memoized per canonical `TypeId`. |
| Instantiated static constants with/without explicit storage | Constant-only case: 9 requests/6 hits, zero globals, 0.32 ms semantic time; out-of-class-definition case: one request, exactly one global, 0.35 ms. Each value use adds only canonical binding/fact probes. |
| Reference-move specialization with 128/256/512 scalar-prefix members | Layout visits 129/257/513, special-member fact lookups 129/257/513, and subobject visits 388/772/1,540; three functions and 32 instructions stay constant through one prefix transfer plus one reference action; semantic time 3.16/5.80/11.80 ms. |
| Indirect class-value call with 32/64/128 arguments | Conversion checks 136/264/520, instructions 190/350/670, requests 35/67/131 with 33/65/129 hits, and constant three demand pushes; semantic time 2.40/3.80/6.66 ms and lowering 1.19/1.49/2.35 ms. |
| Retained global call over substituted base depth 64/128/256 | Overload candidates and conversion checks stay constant at 5, with 3 functions and 8 instructions; total lookup scope visits 158/286/542 and semantic time 1.88/3.48/6.90 ms grow linearly with declarations/layout, not a replayed call lookup. |
| Retained call with 64/128/256 visible template patterns and one viable candidate | Overload candidates stay 2, conversion checks 5, specialization requests 2, functions 3, and instructions 11; semantic time 2.34/4.62/8.95 ms and lookup visits 364/684/1,324 scale linearly with the published pattern set. |
| Explicit class definition with 64/128/256 defined members | One specialization request; exactly 64/128/256 demand pushes and emissions, 129/257/513 instructions, 80,198/159,366/317,702 typed bytes, and 1.10/2.12/3.96 ms semantic time. Prior ordinary use plus explicit demand gives one cache hit and still emits its member once. |
| Builtin enum competition with 128/256/512 indexed `operator&` declarations | Associated declaration visits 128/256/512, overload candidates 129/257/513, conversion checks 263/519/1,031, constant two functions/six instructions; typed bytes 132,259/263,203/525,091 and semantic time 2.45/4.56/9.50 ms. |

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
| Canonical enum builtin competition and class default arguments | Pass | Directional enum conversions, ranked builtin competition, preserved widening, demanded empty-aggregate constructor action; PA19 261 to 264, linear candidate probe, prior 1713/1713, audit pass |

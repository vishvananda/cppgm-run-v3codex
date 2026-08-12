# PA26 Plan

## Stage Design and Spec Alignment

PA26 is a monotonic extension of the PA25 semantic graph and typed LowIR path.
PA12 completes polymorphism before publishing canonical class-special-member,
construction/destruction, lifetime, and ordered unwind facts. PA18 owns demanded
ABI RTTI/runtime symbols; PA15 and its PA16/PA17/PA26 mixins consume those facts
per function. Runtime initializer roots are published before destination
lifetime registration, selected class-value ownership crosses the typed call
boundary once, and nested default arguments retain one marked semantic subtree.
Aggregate/array construction keeps projected destinations and explicit partial
state. Builtin logical nodes publish compact control identity; root branch
cleanup uses complete `(owner, child)` keys, deeper dependence uses runtime
lifetime slots, and each scope publishes a cumulative nontrivial-object prefix
for O(1) enclosing-lifetime queries. This follows `spec.md` sections 2, 4-6,
and 8-10: compact identity, monotonic facts, direct typed lowering, bounded
phase-local state, observable work, and no textual, lookup-recovery,
whole-program retry, or external fallback.

## Current Failure Map

The current result is **109/110**, and the last earlier-stage report passes
**3,607/3,607**.

| Owner | Failing | Shared behavior |
|---|---:|---|
| PA12/PA17 conditional lifetime lowering | 1 | a nonthrowing conditional initializer publishes branch identity but bypasses managed cleanup, so destruction occurs after the merge instead of only on the taken arm |

## Active Checkpoint

Finish conditional-initializer cleanup ownership. PA26 goal 5 and `spec.md`
sections 4-6 and 9 require PA12 to mark a declaration managed whenever its
initializer publishes a control-dependent destructor action; PA17 must consume
the existing `(owner, child)` identity on the evaluated arm and retire it before
the merge. Data flows as initializer temporary -> branch-owned destructor fact
-> bounded branch index -> path-local typed cleanup. Expected work is O(nodes +
temporary actions), with O(1) branch-key probes. Validate the remaining fixture,
the conditional/short-circuit cleanup family, both stage reports, and the audit.

## Performance Evidence

Instrumented current-binary runs show identity and demand work bounded by the
number of closures and specialization requests. Template-argument index probes
equal list requests; RTTI lookup counts equal demand events rather than graph
size. Times are representative single runs in milliseconds.

| Witness | Bytes / nodes | Lambda requests | Specializations (req/hit) / argument probes | RTTI demand/types | IR / storage | Semantic / lowering |
|---|---:|---:|---:|---:|---:|---:|
| one lambda RTTI | 262 / 45 | 1 | 0/0 / 0 | 1/1 | 17 / 9,482 B | 0.462 / 0.208 |
| six member-template lambdas | 2,565 / 398 | 6 | 22/16 / 30 | 0/0 | 299 / 95,832 B | 2.912 / 1.395 |
| captured template lambda RTTI | 233 / 33 | 1 | 2/1 / 3 | 2/1 | 17 / 9,038 B | 0.471 / 0.234 |
| polymorphic array-reference specialization | 1,726 / 121 | 0 | 13/4 / 27 | 0/0 | 174 / 62,636 B | 1.763 / 0.680 |

Earlier nested-scope evidence remains linear through 512 scopes: 2,060 semantic
nodes, 1,025 temporary visits, 517 instructions, and 513 O(1) lifetime queries.

## Completed Checkpoints

| Checkpoint | Result | Validation |
|---|---|---|
| Canonical RTTI demand and query lowering | Canonical query/cast identity and ABI RTTI | RTTI 14/17; PA26 30/110; through PA25 3,607/3,607; audit pass |
| Lambda capture ownership | Closure-owned explicit/default captures and projected access | +12; PA26 42/110; through PA25 3,607/3,607; audit pass |
| Scalar initializer-list interoperation | Canonical specialization, scalar backing, references, `auto`, range-for | +14; PA26 56/110; through PA25 3,607/3,607; linear to 512 |
| List overload and class-backing boundary | List ranks/deduction, selected source, typed class recipes | +7; PA26 63/110; focused 7/7; linear to 512 |
| Initializer-list and aggregate lifecycle | Static backing/finalization, local frontier, parameter teardown | +4; PA26 67/110; focused 4/4; linear to 1,024 |
| Scalar source-exception foundation | Typed throw/handler facts, scalar/ellipsis catches, rethrow, runtime roles | +8; PA26 75/110; focused 8/8; nested handlers linear to 127 |
| Lexical unwind snapshots and handler continuation | Live-action snapshots, segmented exits, dispatch interning | +5; PA26 80/110; focused 9/9; calls/handlers linear through 256/64 |
| Class exception objects and typed-handler routing | Canonical polymorphic special-member facts, selected direct construction/destructor transfer, temporary retirement, projection-safe call ABI | +6; PA26 86/110; focused 6/6 plus PA18 projection witness; through PA25 3,607/3,607; file/audit pass; throws linear to 128 |
| Guard-edge full-expression cleanup | Typed logical facts, complete root guard/child identity, branch-local destruction, retained nested values, runtime fallback | +4; PA26 90/110; focused 4/4 plus ELF/template witnesses; through PA25 3,607/3,607; file/audit pass; both paths linear to 128 |
| Construction and call-ABI ownership | Runtime/default initializer staging, member/array source handlers, transferred class parameters | +4; PA26 94/110; focused 5/5; through PA25 3,607/3,607; audit pass; corrected counters show arguments linear to 128 and array IR fixed through 2,048 |
| Nested call and full-expression lifetime frontier | Evaluated member/special-member demand, default-subtree identity, eager typed regions, retained destinations, guarded-static and aggregate cleanup | +7; PA26 101/110; focused 7/7 and five ELF witnesses; through PA25 3,607/3,607; file/audit pass; O(1) scope-prefix queries through 512 |
| Canonical lambda specialization identity | Source ranges, concrete context arguments, call signature, on-demand ABI/RTTI/EH facts | +4; PA26 105/110; focused 4/4 |
| Construction, projected lifetime, and polymorphic template boundaries | Destination-side aggregate value-init, selected allocation copy, projected empty-chain destruction, ABI-shaped array/ref RTTI, managed polymorphic specialization cleanup | +4; PA26 109/110; focused 4/4; through PA25 3,607/3,607; audit pass |

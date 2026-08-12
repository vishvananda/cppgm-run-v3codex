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

The current result is **101/110**, and all earlier stages pass **3,607/3,607**.

| Owner | Failing | Shared behavior |
|---|---:|---|
| PA12/PA17 conditional lifetime lowering | 1 | branch-owned conditional temporary cleanup |
| lambda/template identity and RTTI presentation | 4 | stable closure specialization identity, EH fallback emission, ABI names |
| object construction/access and cleanup | 2 | protected empty-base scope and polymorphic array-reference cleanup |
| aggregate control flow | 1 | empty indirect result through `switch` |
| typeid object conversion | 1 | cv/reference stripping without a spurious copy |

## Next Substantial Checkpoint

Unify branch-owned conditional cleanup with subobject construction scope. PA26
goal 5 and `spec.md` sections 2, 5, 6, 8, and 9 require PA12 to publish the
selected constructor/destructor and branch owner once, PA16 to retain projected
destinations, and PA17 to retire only the taken branch's actions. Data flows as
canonical initializer/conditional facts -> projected construction actions ->
branch-local LowIR cleanup. Expected work is O(expression nodes + emitted
subobject/actions), with no lookup recovery or function rescans. Validate the
conditional temporary, protected-base default constructor, and polymorphic
array-reference cleanup failures, then both stage reports and the file audit.

## Performance Evidence

Current-binary five-run medians retain bounded cleanup work. `Queries` is the
audited `enclosing_lifetime_queries` counter; `visits` and `NT` are temporary
dependency and nonthrowing-action visits.

| Witness | Nodes / queries / visits / NT | Instructions | Dispatch probes / entries | Typed storage | Semantic / lowering |
|---|---:|---:|---:|---:|---:|
| nested default argument | 99 / 3 / 88 / 25 | 134 | 9 / 9 | 38,343 B | 0.649 / 0.330 ms |
| shared non-LIFO dispatch | 42 / 1 / 25 / 6 | 50 | 4 / 3 | 18,985 B | 0.438 / 0.252 ms |
| guarded local static | 127 / 2 / 75 / 87 | 132 | 3 / 3 | 39,864 B | 0.723 / 0.346 ms |

A nested-scope family holds one outer destructible object and adds `N` nested
literal-initialized scalars. The pre-audit parent walk implies quadratic probes;
the scope-prefix query and all checkpoint-owned counters remain proportional.

| N | Old parent probes / queries | Nodes / visits | Instructions | Typed storage | Semantic / lowering |
|---:|---:|---:|---:|---:|---:|
| 32 | 562 / 33 | 140 / 65 | 37 | 14,112 B | 0.562 / 0.185 ms |
| 128 | 8,386 / 129 | 524 / 257 | 133 | 47,808 B | 1.526 / 0.297 ms |
| 512 | 131,842 / 513 | 2,060 / 1,025 | 517 | 182,592 B | 5.754 / 0.915 ms |

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

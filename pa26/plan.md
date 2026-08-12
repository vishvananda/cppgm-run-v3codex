# PA26 Plan

## Stage Design and Spec Alignment

PA26 remains a monotonic extension of the PA25 semantic graph and typed LowIR
path. PA12 publishes canonical closure, initializer-list, RTTI, exception,
construction, and lifetime facts; PA15 and its PA16/PA17/PA26 lowering mixins
consume those facts without lookup recovery. Conditional temporary cleanup uses
compact `(owner, child)` identity at root guards and runtime state only for
deeper dependence. Destructor demand follows reachable semantic actions,
including retained/deferred function bodies. This aligns with `spec.md`
sections 2, 4-6, and 8-10: stable identity, monotonic demand, explicit phase
boundaries, typed lowering, bounded phase-local state, and observable work.

## Current Failure Map

The current PA26 report passes **110/110** and the through-PA25 report passes
**3,607/3,607**. The complete current-PA failure set is empty; file audit also
passes.

| Owner | Failing | Shared behavior |
|---|---:|---|
| PA26 stage | 0 | All capturing-lambda, initializer-list, RTTI/cast, and exception/lifetime groups pass |

## Active Checkpoint

Stage complete. The final checkpoint moved nonthrowing conditional-temporary
destruction to the constructing arm, conservatively suppresses an unreachable
arm only for a literal-initialized automatic scalar with one semantic use, and
defers destructor demand until that reachability fact is final. Retained lambda
bodies republish reachable demand when attached to their emitted function.
PA17 consumes the existing branch identity and retires path-local cleanup
before the merge. Work is O(function nodes + touched bindings + cleanup
actions), with epoch-indexed scratch reused across functions. Validation is the
required PA26 report, the full through-PA25 report, true/modified/deferred-lambda
counterexamples, and file audit.

## Performance Evidence

The final branch-cleanup witness scales one independent conditional temporary
per generated function. Medians are from three current-binary runs; node,
temporary, lifetime-query, and storage growth track case count. Epoch scratch
grows only when graph/binding indexes grow and is not cleared per function.

| Cases | Semantic nodes | Temporary visits | O(1) lifetime queries | Typed storage | Semantic / lowering |
|---:|---:|---:|---:|---:|---:|
| 1 | 36 | 18 | 2 | 10,638 B | 0.361 / 0.213 ms |
| 16 | 351 | 243 | 32 | 93,581 B | 1.318 / 0.381 ms |
| 64 | 1,359 | 963 | 128 | 365,453 B | 4.275 / 1.230 ms |

Earlier nested-scope evidence remains linear through 512 scopes: 2,060
semantic nodes, 1,025 temporary visits, 517 instructions, and 513 indexed
lifetime queries.

## Completed Checkpoints

| Checkpoint | Result | Validation |
|---|---|---|
| Canonical RTTI demand and query lowering | Canonical query/cast identity and ABI RTTI | PA26 30/110; through PA25 3,607/3,607 |
| Lambda capture ownership | Explicit/default closure fields and projected access | +12; PA26 42/110 |
| Scalar initializer-list interoperation | Canonical scalar backing, references, `auto`, range-for | +14; PA26 56/110; linear to 512 |
| List overload and class-backing boundary | List ranking/deduction and typed class recipes | +7; PA26 63/110 |
| Initializer-list and aggregate lifecycle | Static/local backing lifetime and parameter teardown | +4; PA26 67/110; linear to 1,024 |
| Scalar source-exception foundation | Throw/handler facts, catches, rethrow, runtime roles | +8; PA26 75/110; handlers linear to 127 |
| Lexical unwind snapshots and continuation | Segmented live-action snapshots and dispatch interning | +5; PA26 80/110; linear through 256/64 |
| Class exception objects and routing | Direct construction/destructor transfer and projection-safe ABI | +6; PA26 86/110; through PA25 clean |
| Guard-edge full-expression cleanup | Root branch identity, path cleanup, runtime fallback | +4; PA26 90/110; both paths linear to 128 |
| Construction and call-ABI ownership | Initializer staging, source handlers, transferred parameters | +4; PA26 94/110; arrays linear to 2,048 |
| Nested call and lifetime frontier | Default-subtree identity, eager regions, guarded-static cleanup | +7; PA26 101/110; indexed scopes to 512 |
| Canonical lambda specialization identity | Source/context identity and on-demand ABI/RTTI/EH | +4; PA26 105/110 |
| Projected lifetime and template boundaries | Destination init, projected destruction, polymorphic cleanup | +4; PA26 109/110; through PA25 clean |
| Conditional initializer lifetime ownership | Reachable branch cleanup, demand publication, unreachable-arm suppression | +1; PA26 110/110; through PA25 3,607/3,607; audit pass |

# PA26 Plan

## Stage Design and Spec Alignment

PA26 is a monotonic extension of the PA25 semantic graph and typed LowIR path.
PA12 owns canonical types, selected construction/destruction bindings, lifetime
actions, and ordered unwind snapshots; PA18 owns ABI RTTI/runtime symbols; PA15
and its PA17/PA26 lowering mixins consume those facts per function. This follows
`spec.md` sections 2, 4-6, and 8-10: compact identity, demand separated from
presentation order, direct typed lowering, bounded phase-local state, observable
linear work, and no textual or external-tool fallback.

## Current Failure Map

Current result is **86/110**, up from the 80/110 turn baseline.

| Owner | Failing | Shared behavior |
|---|---:|---|
| PA12/PA17 lifetime and EH lowering | 17 | aggregate/call ownership, conditional and logical paths, static/default arguments, parameter teardown |
| lambda/template identity and RTTI presentation | 4 | stable closure specialization identity, fallback emission, ABI names |
| object construction/access | 1 | protected empty-base constructor scope |
| aggregate control flow | 1 | empty indirect result through `switch` |
| typeid object conversion | 1 | cv/reference stripping without a spurious copy |

## Active Checkpoint

Next, complete full-expression cleanup and result retention across nested `&&`,
`||`, and conditional control flow. The PA26 assignment boundary and `spec.md`
sections 2, 5, 6, 8, and 9 require PA12 to attach each temporary to its evaluated
arm and immutable cleanup action, while PA17 owns branch-local cleanup segments,
outer logical result slots, and action-sequence interning. Expected work is
O(expression nodes + evaluated cleanup actions + control-flow edges), with O(1)
average action/context cache probes. Validate both short-circuit RHS fixtures,
the condition-RHS fixture, nested logical result retention, nested short-circuit
cleanup, and the conditional-expression temporary fixture; measure nested depth
and sibling-arm scaling before all gates.

## Performance Evidence

Repeated class throws with one live guard per typed handler exercise selected
construction, RTTI demand, lexical unwind routing, and typed emission:

| Throws | Semantic nodes | Demand pushes/emissions | Blocks | Instructions | Typed storage | Semantic | Lowering |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 16 | 276 | 3/2 | 99 | 394 | 92,145 B | 0.99 ms | 0.37 ms |
| 32 | 532 | 3/2 | 195 | 778 | 174,800 B | 1.48 ms | 0.51 ms |
| 64 | 1,044 | 3/2 | 387 | 1,546 | 340,112 B | 2.74 ms | 0.79 ms |
| 128 | 2,068 | 3/2 | 771 | 3,082 | 670,736 B | 5.18 ms | 1.50 ms |

Nodes, blocks, instructions, storage, and phase time scale linearly; constructor
and destructor emission demand stays constant. Earlier repeated-snapshot and
nested-handler measurements remain linear through 256 calls and 64 handlers.

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
| Class exception objects and typed-handler routing | Selected direct construction, destructor transfer, polymorphic copy chain, temporary retirement | +6; PA26 86/110; focused 6/6; through PA25 3,607/3,607; audit pass; throws linear to 128 |

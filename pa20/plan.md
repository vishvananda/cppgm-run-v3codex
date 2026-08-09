# PA20 Full-Stage Plan

## Stage Design and Spec Alignment

PA20 extends the retained PA19 template graph rather than reconstructing source
during lowering. Canonical tagged type/value arguments and parameter-partition
offsets own specialization identity; indexed primary sets and O(1)-average
caches publish class, function, member, and variable specializations; immutable
parent-linked scopes carry fixed and packed substitutions into replay. Demand
remains separate from completion, and LowIR consumes the selected typed binding.

The completed stage adds width-aware integral constant evaluation, dependent
qualified values and `decltype`, explicit and supported partial specialization,
variable templates, lockstep type/value/function packs, declaration-time
validation, and pack-aware initialization/lowering. Array-capable aggregate
helpers are confined to the functional-cast boundary that needs them, preserving
earlier aggregate representations. PA21 general constexpr interpretation and
later SFINAE remain outside this stage.

## Current Failure Map

No open failures. PA20 improved from the turn checkpoint of 136/164 to 164/164;
PA1-PA19 pass 2,013/2,013, and the PA20 file audit passes. The prior PA3 recovery
blocker was a linear punctuator-table scan on the 12.0-MB triple-token fixture;
direct token dispatch removed the timeout without changing oracle output.

## Active Checkpoint

Full-stage closure is complete. Semantic ownership flows from parsed template
parameters and terminal template-ids through canonical argument construction,
indexed specialization selection/publication, retained-scope replay, monotonic
completion/demand, and typed lowering. Expected lookup/publication cost is
O(argument syntax plus same-name candidate shape), with O(1)-average cache
access; pack replay and generated output are linear in produced elements.

Validation is the required PA20 report, the exact prior-through-PA19 report,
file audit, focused accept/reject pairs for empty packs and argument kinds, and
representative specialization/tokenizer scaling.

## Performance Evidence

| Case | Sizes | Release evidence |
|---|---:|---|
| Integral assertion folding | 64/128/256 | Semantic nodes 453/901/1,797; peak bytes 147,772/279,396/542,605; median semantic time 0.704/1.303/2.526 ms |
| Function specialization cache | 32/64/128, each requested twice | Requests 128/256/512; hits 96/192/384; emitted functions 33/65/129; median semantic time 2.558/4.770/9.408 ms |
| Pack/base/demand checkpoints | 16/32/64 representative elements | Nodes, lookup/layout visits, demand, storage, and output grew linearly; lockstep and independent-pack cases showed no Cartesian expansion |
| Explicit class/function specializations | 16/32/64, seven-run medians | Tokens 793/1,545/3,049; nodes 248/488/968; lookups 294/582/1,158; requests 96/192/384 with 64/128/256 hits; peak bytes 298,217/588,249/1,168,314; output bytes 4,798/9,388/18,637; semantic time 1.527/2.695/5.164 ms |
| PA3 12,039,435-byte tokenizer fixture | before/after | 8.220 s -> 3.395 s compiler time (8.41 s -> 3.58 s wall); output remained byte-identical |

## Completed Checkpoints

| Checkpoint | Result |
|---|---|
| Typed integral constants and assertions | Canonical width/signedness-aware literals, casts, folds, and assertion validation; PA20 15 -> 39 |
| Fixed-arity integral arguments | Tagged canonical type/value keys, defaults, substitution, ABI identity; PA20 39 -> 83 |
| Trailing type/function packs | Ordered pack ranges, offsets, empty/fixed-prefix deduction, forwarding, `sizeof...`; PA20 83 -> 96 |
| Lockstep expansion | Nested call, functional, braced, member, and out-of-class owner replay; PA20 96 -> 103 |
| Multiple pack partitions | Canonical per-parameter offsets, symbolic value shapes, ADL; PA20 103 -> 106 |
| Base packs | Ordered base identities, lookup/layout, initializer actions, offset lowering; PA20 106 -> 110 |
| Dependent declaration boundaries | `decltype`, `alignas`, `typename`, and type/expression disambiguation; PA20 110 -> 116 |
| Dependent helper values | Structural constant conversion and recursive pack element scopes; PA20 116 -> 120 |
| Specialized owners and demand | Stable static definitions plus deduplicated object/function/vtable demand; PA20 120 -> 125 |
| Literal packs | Typed character/string decoding and literal-operator dispatch; PA20 125 -> 130 |
| Retained template-id conversions | Angle matching, defaults, derived deduction, target-directed function addresses; PA20 130 -> 136 |
| Full specialization and replay closure | Explicit class/function/member/variable specializations, partial class/variable selection, qualified constants, constexpr-call subset, empty/defaulted packs, aggregate functional lowering, and PA3 recovery; PA20 136 -> 164, PA1-PA19 2,013/2,013, audit pass |

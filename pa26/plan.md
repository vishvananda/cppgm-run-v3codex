# PA26 Plan

## Stage Design and Spec Alignment

PA26 remains a monotonic extension of the shared PA25 pipeline. PA12 owns
canonical types, declarations, lifetime facts, normalized throw-object and
handler types, catch bindings, and rethrow legality. PA18 demand maps those
canonical IDs to ABI RTTI/runtime symbols. PA15 consumes the typed graph and
owns per-function exception regions; initializer-list and aggregate storage
continue to use their existing explicit identities and destruction plans. This
follows `spec.md` sections 2-6 and 8-10: canonical identity, explicit ownership,
direct typed lowering, demand-driven emission, and bounded work.

## Current Failure Map

Current result is **75/110**, up from the turn-start baseline of **67/110**.

| Owner | Failing | Shared behavior |
|---|---:|---|
| EH and lifetime lowering | 31 | class exceptions, unwind snapshots, branch/full-expression cleanup, construction failure |
| lambda/RTTI presentation integration | 2 | closure ABI spelling in RTTI name objects |
| template emission ordering | 1 | stable anonymous ordering across member-template specializations |
| object construction lowering | 1 | nontrivial copy in the cv/reference `typeid` fixture |

## Active Checkpoint

Next, extend the same typed-action boundary to exceptional call cleanup.
`spec.md` sections 5, 6, 8, and 9 require lifetime analysis to publish immutable
cleanup frontiers and lowering to attach may-throw calls to those facts without
re-walking syntax. PA12 lifetime/action analysis owns snapshot identity; PA15
owns exception-region selection and dispatch reuse. Expected work is
O(action nodes + unique cleanup snapshots), with bounded lookup per call.
Validate hidden call/constructor cleanup, branch-specific temporaries, catch
misses, nested handlers, and cross-function cache reset; measure snapshot and
dispatch growth before the PA26, through-PA25, and audit gates.

## Performance Evidence

One release run per point on macro-expanded binary trees of nested one-handler
`try` statements; exact counters are the primary complexity evidence:

| Handlers | Semantic nodes | Lowered nodes | Instructions | RTTI visits | Typed storage | Semantic | Lowering | RSS |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 15 | 81 | 64 | 303 | 81 | 63,715 B | 0.21 ms | 0.31 ms | 6,460 KiB |
| 31 | 161 | 128 | 623 | 161 | 124,048 B | 0.31 ms | 0.37 ms | 6,424 KiB |
| 63 | 321 | 256 | 1,263 | 321 | 244,720 B | 0.57 ms | 0.54 ms | 6,600 KiB |
| 127 | 641 | 512 | 2,543 | 641 | 486,064 B | 0.91 ms | 0.91 ms | 6,864 KiB |

Doubling the handler tree doubles semantic/lowered nodes, RTTI visits,
instructions, and storage; the scalar exception path is O(exception nodes).

## Completed Checkpoints

| Checkpoint | Result | Validation |
|---|---|---|
| Canonical RTTI demand and query lowering | Canonical query/cast identity and ABI RTTI | RTTI 14/17; PA26 30/110; through PA25 3,607/3,607; audit pass |
| Lambda capture ownership | Explicit/default captures, closure members, projected access, cycle-safe identity | 12 new passes; PA26 42/110; timeout removed; through PA25 3,607/3,607; audit pass |
| Scalar initializer-list interoperation | Canonical specialization, two-phase construction, scalar backing, references, `auto`, range-for | 14 new passes; PA26 56/110; through PA25 3,607/3,607; audit pass; linear to 512 |
| List overload and class-backing boundary | Separate list/element ranks, whole-list template deduction, selected source, typed class recipes and compact backing addresses | 7 new passes; PA26 63/110; focused 7/7; through PA25 3,607/3,607; audit pass; class scaling linear to 512 |
| Initializer-list and aggregate lifecycle | Namespace backing globals/finalization, exact local element frontier, aggregate parameter teardown, direct nested destination | 4 new passes; PA26 67/110; focused 4/4; through PA25 3,607/3,607; audit pass; nontrivial lists linear to 1,024 |
| Scalar source-exception foundation | Typed throw/handler facts, scalar/ellipsis catches, rethrow, decay, runtime roles, retained catch scope | 8 new passes; PA26 75/110; focused 8/8; through PA25 3,607/3,607; audit pass; nested handlers linear to 127 |

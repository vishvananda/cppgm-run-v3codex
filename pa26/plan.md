# PA26 Plan

## Stage Design and Spec Alignment

PA26 remains a monotonic extension of the shared PA25 pipeline. PA12 owns
canonical types, declarations, lifetime obligations, ordered unwind suffixes,
handler-boundary markers, normalized throw/handler types, catch bindings, and
rethrow legality. PA18 maps canonical IDs to ABI RTTI/runtime symbols. PA15
consumes those immutable facts directly and owns per-function exception regions
and dispatch interning. Existing temporary-cleanup owners are extended rather
than duplicated. This satisfies `spec.md` sections 2-6 and 8-10: compact
identity, explicit ownership, direct typed lowering, demand-driven emission,
observable bounded work, and no text or external-tool fallback.

## Current Failure Map

Current result is **80/110**, preserving the checkpoint-audit baseline.

| Owner | Failing | Shared behavior |
|---|---:|---|
| EH and lifetime lowering | 26 | class exceptions, construction/destruction failure, remaining branch-temporary cleanup |
| lambda/RTTI presentation integration | 2 | closure ABI spelling in RTTI name objects |
| template emission ordering | 1 | stable anonymous ordering across member-template specializations |
| object construction lowering | 1 | nontrivial copy in the cv/reference `typeid` fixture |

## Active Checkpoint

Next, complete class exception-object construction and class-reference handler
matching as one substantial checkpoint. `spec.md` sections 2, 4, 6, 8, and 9
require PA12 to retain the selected move/copy constructor, destructor,
canonical handler type, base adjustment, and demand edges. PA18 owns the
corresponding RTTI/destructor ABI symbols; PA15 constructs directly in allocated
exception storage and consumes the recorded adjustment. Expected work is
O(selected construction/subobject actions + handler clauses), with each
demanded definition emitted once. Validate class-template move construction,
base-reference catches, destructor-body unwind, throw-operand temporary
retirement, and sibling snapshot isolation; measure action, demand, RTTI,
instruction, and storage growth before all gates.

## Performance Evidence

Release runs on macro-expanded repeated calls with one live lexical guard:

| Calls | Unwind actions | Lowered nodes | Instructions | Cache probes/hits/entries | Typed storage | Semantic | Lowering |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 32 | 32 | 68 | 104 | 32/31/1 | 20,069 B | 0.44 ms | 0.17 ms |
| 64 | 64 | 132 | 200 | 64/63/1 | 35,429 B | 0.68 ms | 0.18 ms |
| 128 | 128 | 260 | 392 | 128/127/1 | 66,149 B | 1.09 ms | 0.22 ms |
| 256 | 256 | 516 | 776 | 256/255/1 | 127,589 B | 1.97 ms | 0.34 ms |

Nested catch-all handler evidence exercises segmented handler exits:

| Handlers | Scope/action visits | Lowered nodes | Instructions | Cache entries | Typed storage | Semantic | Lowering |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 8 | 8/8 | 67 | 254 | 16 | 60,815 B | 0.42 ms | 0.28 ms |
| 16 | 16/16 | 131 | 502 | 32 | 114,685 B | 0.66 ms | 0.41 ms |
| 32 | 32/32 | 259 | 998 | 64 | 222,431 B | 1.10 ms | 0.55 ms |
| 64 | 64/64 | 515 | 1,990 | 128 | 437,919 B | 2.25 ms | 1.04 ms |

Actions, nodes, instructions, storage, and measured phase time scale linearly.
Repeated identical snapshots share one dispatch body; catch-all regions do not
materialize impossible catch-miss suffixes.

## Completed Checkpoints

| Checkpoint | Result | Validation |
|---|---|---|
| Canonical RTTI demand and query lowering | Canonical query/cast identity and ABI RTTI | RTTI 14/17; PA26 30/110; through PA25 3,607/3,607; audit pass |
| Lambda capture ownership | Explicit/default captures, closure members, projected access, cycle-safe identity | 12 new passes; PA26 42/110; timeout removed; through PA25 3,607/3,607; audit pass |
| Scalar initializer-list interoperation | Canonical specialization, two-phase construction, scalar backing, references, `auto`, range-for | 14 new passes; PA26 56/110; through PA25 3,607/3,607; audit pass; linear to 512 |
| List overload and class-backing boundary | Separate list/element ranks, whole-list template deduction, selected source, typed class recipes and compact backing addresses | 7 new passes; PA26 63/110; focused 7/7; through PA25 3,607/3,607; audit pass; class scaling linear to 512 |
| Initializer-list and aggregate lifecycle | Namespace backing globals/finalization, exact local element frontier, aggregate parameter teardown, direct nested destination | 4 new passes; PA26 67/110; focused 4/4; through PA25 3,607/3,607; audit pass; nontrivial lists linear to 1,024 |
| Scalar source-exception foundation | Typed throw/handler facts, scalar/ellipsis catches, rethrow, decay, runtime roles, retained catch scope | 8 new passes; PA26 75/110; focused 8/8; through PA25 3,607/3,607; audit pass; nested handlers linear to 127 |
| Lexical unwind snapshots and handler continuation | Typed live-action snapshots, complete bounded full-expression owners, ordered handler exits, sequence interning, catch-miss cleanup | PA26 80/110; focused 9/9 plus 4 ownership traces; through PA25 3,607/3,607; file/audit pass; repeated calls and nested handlers linear through 256/64 |

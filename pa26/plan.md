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

Current result is **80/110**, up from this turn's **75/110** baseline.

| Owner | Failing | Shared behavior |
|---|---:|---|
| EH and lifetime lowering | 26 | class exceptions, branch/full-expression cleanup, construction failure |
| lambda/RTTI presentation integration | 2 | closure ABI spelling in RTTI name objects |
| template emission ordering | 1 | stable anonymous ordering across member-template specializations |
| object construction lowering | 1 | nontrivial copy in the cv/reference `typeid` fixture |

## Active Checkpoint

Next, complete class exception-object construction and class-reference handler
matching. `spec.md` sections 2, 4, 6, 8, and 9 require PA12 to retain the chosen
move/copy constructor, destructor, canonical handler type, and demand edges;
PA18 owns RTTI/destructor ABI symbols; PA15 constructs directly in allocated
exception storage and uses recorded single-inheritance adjustment facts.
Expected work is O(selected construction/subobject actions + handler clauses),
with each demanded definition emitted once. Validate class-template move
construction, base-reference catches, destructor-body unwind, throw-operand
temporary retirement, and sibling snapshot isolation; measure class action,
demand, RTTI, instruction, and storage scaling before all gates.

## Performance Evidence

One release run per point on macro-expanded repeated calls with one live lexical
guard; exact counters are the primary complexity evidence:

| Calls | Unwind actions | Lowered nodes | Instructions | Cache probes/hits/entries | Typed storage | Semantic | Lowering | RSS |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 32 | 32 | 69 | 104 | 32/31/1 | 20,069 B | 0.42 ms | 0.17 ms | 6,652 KiB |
| 64 | 64 | 133 | 200 | 64/63/1 | 35,429 B | 0.69 ms | 0.19 ms | 6,660 KiB |
| 128 | 128 | 261 | 392 | 128/127/1 | 66,149 B | 1.08 ms | 0.23 ms | 6,904 KiB |
| 256 | 256 | 517 | 776 | 256/255/1 | 127,589 B | 1.96 ms | 0.38 ms | 7,152 KiB |

Actions, nodes, instructions, and storage scale linearly. Identical typed
snapshots share one dispatch body, with all probes after the first hitting.

## Completed Checkpoints

| Checkpoint | Result | Validation |
|---|---|---|
| Canonical RTTI demand and query lowering | Canonical query/cast identity and ABI RTTI | RTTI 14/17; PA26 30/110; through PA25 3,607/3,607; audit pass |
| Lambda capture ownership | Explicit/default captures, closure members, projected access, cycle-safe identity | 12 new passes; PA26 42/110; timeout removed; through PA25 3,607/3,607; audit pass |
| Scalar initializer-list interoperation | Canonical specialization, two-phase construction, scalar backing, references, `auto`, range-for | 14 new passes; PA26 56/110; through PA25 3,607/3,607; audit pass; linear to 512 |
| List overload and class-backing boundary | Separate list/element ranks, whole-list template deduction, selected source, typed class recipes and compact backing addresses | 7 new passes; PA26 63/110; focused 7/7; through PA25 3,607/3,607; audit pass; class scaling linear to 512 |
| Initializer-list and aggregate lifecycle | Namespace backing globals/finalization, exact local element frontier, aggregate parameter teardown, direct nested destination | 4 new passes; PA26 67/110; focused 4/4; through PA25 3,607/3,607; audit pass; nontrivial lists linear to 1,024 |
| Scalar source-exception foundation | Typed throw/handler facts, scalar/ellipsis catches, rethrow, decay, runtime roles, retained catch scope | 8 new passes; PA26 75/110; focused 8/8; through PA25 3,607/3,607; audit pass; nested handlers linear to 127 |
| Lexical unwind snapshots and handler continuation | Typed live-action snapshots, may-throw boundaries, sequence interning, nested selectors/catch misses, active-handler close | 5 new passes; PA26 80/110; focused 6/6; through PA25 3,607/3,607; audit pass; one dispatch entry through 256 repeated calls |

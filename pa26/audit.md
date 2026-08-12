# PA26 Audit

## Current Checkpoint Review

The guard-edge full-expression checkpoint (`e4d47678` plus this audit
follow-up) passes its bounded review. The landed increment covers destructible
temporaries on directly guarded `&&`, `||`, and conditional arms, retained
result slots for nested logical values, and the runtime-lifetime fallback for
deeper dependence. Aggregate, parameter, static-initializer, and unrelated
template/lambda failures remain mapped to later checkpoints.

The audit removed two checkpoint-owned identity shortcuts. Logical control
kind was rediscovered from rendered operator text during semantic traversal,
slot planning, and lowering; builtin logical nodes now publish a compact
`LogicalOperation` once during semantic construction. Branch cleanup heads
were indexed by child alone even though the semantic fact is the complete
`(owner, child)` identity; a per-function flat pair map now owns that exact key.
Overloaded logical operators remain typed call nodes and never acquire the
builtin short-circuit fact.

The durable ownership path is source operator -> PA12 typed logical node ->
temporary-owned root/child fact -> immutable destructor action -> PA17
per-function pair index or runtime lifetime slot -> direct typed LowIR. Root
actions are destroyed on the evaluated edge and retired before final cleanup;
deeper guards use explicit runtime marks. The index and linked action state are
cleared at the function boundary, lookup is O(1) average, and each expression
node and cleanup action is visited a bounded number of times. No checkpoint
path performs lookup recovery, reparses rendered text, retries whole programs,
uses test/source-name dispatch, invokes an external compiler, or consults a
reference implementation.

Current-binary sibling runs at N=8/32/128 record 146/482/1,826 semantic nodes,
85/301/1,165 dependency visits, 8/32/128 branch actions, 73/265/1,033 blocks,
and 240/888/3,480 instructions. Nested fallback runs record 87/255/927 nodes,
75/243/915 visits, 8/32/128 slots and marks, 75/267/1,035 blocks, and
280/1,024/4,000 instructions. Typed output grows from 52,207 to 690,847 bytes
and 60,666 to 833,320 bytes respectively. A demanded `Probe<N>` witness uses
15 specialization requests with 8 cache hits, 8 demand pushes/emissions, and 2
branch actions; it and all four focused fixtures execute successfully through
the LowIR-to-CY86 adapter and Linux ELF path.

Final validation preserves both baselines: focused PA15-PA17 ownership tests
pass, the four checkpoint fixtures pass, PA1-PA25 pass 3,607/3,607, and PA26
remains 90/110 with the same 20 failures and no timeout. The required PA26
report therefore preserves the turn-start baseline. The file audit passes with
the same 19 inherited division warnings. No relevant correctness, performance,
shortcut, timeout, ownership, or file-audit issue remains in this landed
increment.

## Checkpoint Audit Ledger

| Checkpoint | Audit result |
|---|---|
| Canonical RTTI demand and query lowering (`9eb277da`, audit follow-up) | Pass: evaluated demand, reachable collection, complete canonical RTTI categories, cast legality, linear counters, baseline and earlier stages preserved. |
| Lexical unwind snapshots and handler continuation (`e05062b1`, audit follow-up) | Pass: complete bounded owners, ordered handler exit, non-duplicated typed suffixes, linear counters, and both baselines preserved. |
| Class exception objects and typed-handler routing (`336f0c80`, audit follow-up) | Pass: canonical special-member facts, direct typed construction/destructor transfer, projection-safe call ABI, linear evidence, and both baselines preserved. |
| Guard-edge full-expression cleanup (`e4d47678`, audit follow-up) | Pass: typed logical identity, complete owner/child indexing, path-local retirement with bounded runtime fallback, linear evidence, and both baselines preserved. |

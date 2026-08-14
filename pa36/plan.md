# PA36 Implementation Plan

## Stage Design and Spec Alignment

PA36 extends the existing semantic graph -> typed LowIR -> per-function MIR ->
direct ELF path with demand-driven hosted definitions and host ABI identities.
Relevant `spec.md` requirements are canonical declaration/fact identity (§2),
separate template definition and emission demand (§4), recorded-fact typed
lowering (§6), bounded per-function backend work (§§7 and 9), explicit phase
ownership (§8), and no name-based output shortcut (§10).

Compiler builtin spelling is classified only at semantic ingress. Every closed
handler, including `__builtin_invoke` and `__builtin_addressof`, now runs before
the generic hosted `__builtin_x` -> `x` compatibility lookup. Recognized calls
publish typed semantic operations or canonical builtin `BindingId` facts;
lowering never recovers their identity from a spelling.

## Current Failure Map

The 42/79 handout baseline is preserved; with the new collision regression the
combined report is 43/80. The remaining 37 handout failures group into 25
required-definition/ABI link failures, 10 semantic/template compile failures,
and 2 runtime lifetime/lowering failures. There are no timeout failures or
generated PA36 object relocations to `alloca`.

## Active Checkpoint

Next: repair the 25-case required-definition/ABI closure at the semantic graph
to emission boundary. The owner flow is demanded-use edge -> canonical function
identity -> one definition or external ABI symbol -> ELF relocation. Expected
work is O(demand edges + emitted bodies), with no rendered-name scan or global
retry. Validate representative stream, tree-node, and lambda closures before
the full PA36, through-PA35, and file-audit gates.

## Performance Evidence

Three audited runs of the 83,073-token hosted string workload took
0.91/0.92/0.91 s at 43,160/43,336/43,316 KiB RSS. Each produced exactly 231
functions, 6,132 LowIR instructions, 8,984 MIR instructions, and a 3,207,736-byte
object. Relative to the landed checkpoint sample, closed dispatch avoided four
lookup queries and one scope visit; semantic peak storage remained 41,842,977
bytes. The 227-token stack-pressure probe remained 0.01 s at 8,896 KiB with 88
LowIR and 138 MIR instructions. Dispatch adds fixed spelling classifications,
no container, worklist, demand, lowering, or backend iteration, and all three
hosted objects were byte-identical with zero `alloca` relocations.

## Completed Checkpoints

| Checkpoint | Result | Validation |
| --- | --- | --- |
| Closed builtin ownership before hosted alias fallback (`1f918c84` plus audit repair) | Removed observed `alloca` relocations, raised PA36 handout coverage 26 -> 42/79, and closed `invoke`/`addressof` alias collisions | Collision 1/1; PA33/PA34 focused pass; PA36 43/80 combined; through PA35 4,907/4,907; file audit pass |

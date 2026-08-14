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

The standard-owner checkpoint raises the combined report from 43/80 to 56/80.
The remaining 24 failures group into 9 missing implicit destructor definitions,
1 nested hosted-name substitution mismatch, 1 cross-TU weak-definition
coalescing failure, 10 semantic/template compile failures, and 3 runtime
lifetime/numeric failures. There are no timeout failures, malformed `3std`
references, or generated PA36 object relocations to `alloca`.

## Active Checkpoint

Next: close implicit-destructor demand for local closures and hosted class
specializations. `spec.md` §§2, 4, and 6 assign this to canonical class/function
facts and the deduplicated demand worklist, then typed lowering consumes one
selected destructor identity. Trace destruction use -> implicit special-member
completion -> one demand edge -> weak definition/ELF symbol. Expected work is
O(new special members + demand edges + emitted bodies), with each canonical
destructor transitioning once. Validate local lambda, hash-node, tree-node, and
pair cases before the full PA36, through-PA35, and file-audit gates.

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

The standard-owner checkpoint's 136,183-token stream workload took
1.34/1.35/1.33 s at 64,924/64,720/64,796 KiB RSS. All runs produced 310
functions, 6,915 LowIR instructions, 9,936 MIR instructions, and identical
3,782,808-byte objects. The classifier replaces one ordinary component fact
with `NAME_STD`; it adds no lookup, allocation, or iteration and removes one
substitution-table entry per affected name. Raw objects contain the host's
`St` spellings and zero malformed `3std` undefined references.

## Completed Checkpoints

| Checkpoint | Result | Validation |
| --- | --- | --- |
| Closed builtin ownership before hosted alias fallback (`1f918c84` plus audit repair) | Removed observed `alloca` relocations, raised PA36 handout coverage 26 -> 42/79, and closed `invoke`/`addressof` alias collisions | Collision 1/1; PA33/PA34 focused pass; PA36 43/80 combined; through PA35 4,907/4,907; file audit pass |
| Canonical structured `std` owner identity | Emitted non-numbered `St`, removed substitution-slot pollution, and raised PA36 43 -> 56/80 | Raw host symbols match; stream/tree focused checks; stable 3-run profile; through PA35 4,907/4,907; file audit pass |

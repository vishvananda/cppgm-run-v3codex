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

The ABI-designated global `std` namespace is likewise classified once at
semantic namespace ingress and retained as a canonical `ScopeId`. Hosted-trait,
initializer-list, standard-template, and class-owner ABI paths consume that
identity; rendered component text is payload only and cannot select `NAME_STD`.

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

The audited 136,183-token stream workload took 1.33/1.33/1.34 s at
65,032/64,860/65,176 KiB RSS. All runs retained 45,257 lookup queries, 69,440
scope visits, 2,608 template requests, 329 demand pushes, 310 functions, 6,915
LowIR instructions, 9,936 MIR instructions, and 67,966,620 semantic peak bytes.
The 3,782,808-byte objects were SHA-256 identical with host `St` spellings and
zero malformed `3std` references. Namespace identity adds one translation-unit
`ScopeId` and an owner-depth query bounded by the ABI path already traversed; it
adds no allocation, semantic retry, lowering pass, or backend iteration.

## Completed Checkpoints

| Checkpoint | Result | Validation |
| --- | --- | --- |
| Closed builtin ownership before hosted alias fallback (`1f918c84` plus audit repair) | Removed observed `alloca` relocations, raised PA36 handout coverage 26 -> 42/79, and closed `invoke`/`addressof` alias collisions | Collision 1/1; PA33/PA34 focused pass; PA36 43/80 combined; through PA35 4,907/4,907; file audit pass |
| Canonical structured `std` owner identity (`03e47d62` plus audit repair) | Emitted non-numbered `St`, removed substitution-slot pollution and lowering text recovery, and raised PA36 43 -> 56/80 | Standard-owner focus 16/16; stable 3-run profile and byte-identical output; through PA35 4,907/4,907; file audit pass |

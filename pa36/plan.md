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

Explicit destructor call formation now consumes the selected canonical
destructor and owner lifecycle fact. On the host-object path, a trivial call
retains object evaluation as a typed void no-op; a nontrivial call retains the
ordinary demand edge. Staged textual LowIR retains its established lifecycle
symbol contract.

## Current Failure Map

The trivial-destructor checkpoint raises PA36 from 56/80 to 65/80. The remaining
15 failures are 10 semantic/template compile failures (2 direct-base completion,
2 invalid callable-type boundaries, 2 unordered-container overload failures,
and one each in constructor selection, enum constants, nested lifetime prefix,
and tuple overload ordering), 1 external locale-constructor ownership mismatch,
1 cross-TU weak-definition coalescing failure, and 3 runtime numeric/lifetime
failures. There are no timeout failures, target trivial-destructor references,
malformed `3std` references, or generated PA36 object relocations to `alloca`.

## Active Checkpoint

Next: close hosted class/base completion and callable-boundary ownership across
the two vector/pair direct-base failures and two iostream function-type failures,
bundling the tuple qualified-base ambiguity if it shares the same fact edge.
`spec.md` §§2-5 assign class specialization, base, lookup, and selected callable
facts to canonical owners and deduplicated completion work; §6 requires lowering
to consume those facts without another lookup. Trace specialization request ->
completed base/callable fact -> selected declaration. Expected work is O(new
specialization facts + indexed lookup candidates), with each completion state
transitioning once. Validate those five cases and neighboring PA35 hosted-header
fixtures before the full gates; profile the largest iostream source and inspect
specialization transitions, lookup visits, and demand pushes.

## Performance Evidence

The audited 136,183-token stream workload took 1.33/1.33/1.34 s at
65,032/64,860/65,176 KiB RSS. All runs retained 45,257 lookup queries, 69,440
scope visits, 2,608 template requests, 329 demand pushes, 310 functions, 6,915
LowIR instructions, 9,936 MIR instructions, and 67,966,620 semantic peak bytes.
The 3,782,808-byte objects were SHA-256 identical with host `St` spellings and
zero malformed `3std` references. Namespace identity adds one translation-unit
`ScopeId` and an owner-depth query bounded by the ABI path already traversed; it
adds no allocation, semantic retry, lowering pass, or backend iteration.

For explicit destructor pruning, three local-lambda runs were 0.42 s at
23,456-23,848 KiB RSS with identical 371,944-byte objects, 54 demand pushes,
53 demanded emissions, 50 functions, and 537 LowIR instructions (one fewer
demand/emission/instruction than baseline). Three 173,757-token tree workloads
were 1.81/1.82/1.81 s at 87,420-87,656 KiB RSS with identical 5,531,208-byte
objects, stable 498 pushes/493 emissions/468 functions/10,053 instructions, and
zero target unresolved destructor symbols. The new work is one O(1) owner-fact
test per explicit destructor call and allocates no state.

## Completed Checkpoints

| Checkpoint | Result | Validation |
| --- | --- | --- |
| Closed builtin ownership before hosted alias fallback (`1f918c84` plus audit repair) | Removed observed `alloca` relocations, raised PA36 handout coverage 26 -> 42/79, and closed `invoke`/`addressof` alias collisions | Collision 1/1; PA33/PA34 focused pass; PA36 43/80 combined; through PA35 4,907/4,907; file audit pass |
| Canonical structured `std` owner identity (`03e47d62` plus audit repair) | Emitted non-numbered `St`, removed substitution-slot pollution and lowering text recovery, and raised PA36 43 -> 56/80 | Standard-owner focus 16/16; stable 3-run profile and byte-identical output; through PA35 4,907/4,907; file audit pass |
| Canonical trivial explicit-destructor calls | Preserved host object evaluation as a void no-op, removed nine unresolved local/hosted trivial destructors, and raised PA36 56 -> 65/80 | Destructor focus 9/9; PA17 textual-LowIR and PA32 lifecycle focus pass; stable 3-run profiles and byte-identical objects; target unresolved symbols 0; file audit pass |

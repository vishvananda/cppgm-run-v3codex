# PA36 Checkpoint Audit

## Current Checkpoint Review

Scope: landed checkpoint `1f918c84` and its changes to builtin call dispatch.
The checkpoint correctly moved immediate intrinsic recognition ahead of the
hosted `__builtin_x` -> `x` alias fallback, preserving `__builtin_alloca` as a
canonical builtin call through lowering. Audit found the same ownership defect
still present for two closed operations: `__builtin_invoke` remained after the
fallback, and `__builtin_addressof` was implemented after the fallback inside
the same mixed handler. Matching global `invoke` and `addressof` functions stole
their calls; the representative collision program compiled and linked but
returned 1 with the landed compiler.

The repair separates generic alias lookup from closed compiler-builtin
recognition and invokes the fallback only after every closed handler declines.
The ref-generated course regression now returns 0 and its object has one direct
relocation to the typed invocation target, with none to either colliding alias.
This closes the checkpoint's `spec.md` §§2, 6, and 10 correctness/identity issue
without broadening the hosted alias policy.

The affected ownership trace is source spelling -> closed semantic classifier
-> canonical builtin `BindingId` or typed invoke/address node -> typed LowIR ->
function-local MIR -> direct ELF. `__builtin_alloca` reaches
`BUILTIN_FUNCTION_ALLOCA`, then `STACK_ALLOC`, native stack adjustment, and ELF
without an external symbol. Invoke consumes analyzed callable/member-pointer and
conversion facts; addressof consumes the operand's type, value category, and
binding fact. Neither lowering path performs lookup or name reconstruction.

Representation ownership is unchanged: dispatch adds no token, syntax,
semantic, or IR copy and no retained phase pointer. Closed calls avoid the
indexed global candidate lookup; unknown compatibility names alone may enter
it. Dependent invoke calls continue through existing canonical specialization
state and do not add replay, cache, invalidation, or retry behavior. No new
container, allocation, whole-program scan, serializer, external tool, source
path, test-name, or timeout path was introduced.

Three runs of the 83,073-token representative workload were stable at
0.91/0.92/0.91 s and 43,160/43,336/43,316 KiB RSS, each with 231 functions,
6,132 LowIR and 8,984 MIR instructions, 41,842,977 semantic peak bytes, and
byte-identical 3,207,736-byte objects. The audit repair performed four fewer
lookups and one fewer scope visit than the landed sample. The stack-pressure
probe remained 0.01 s at 8,896 KiB, and all generated PA36 objects had zero
undefined `alloca` symbols. Thus the dispatch boundary remains fixed work per
direct call and adds no scaling-sensitive semantic or backend work.

The sequential PA36 report preserves all 42/79 handout passes and adds the new
course pass (43/80 combined); the same 37 handout failures remain for later
checkpoints. PA1–PA35 pass 4,907/4,907, and the PA36 file audit passes with only
22 inherited nonfatal header-division advisories. No relevant correctness,
identity, shortcut, performance, timeout, ownership, or file-audit issue remains
in this checkpoint increment.

## Checkpoint Audit Ledger

| Checkpoint | Audit disposition |
| --- | --- |
| Closed builtin dispatch (`1f918c84` plus audit repair) | Pass after separating generic aliases from all closed handlers; canonical typed identity, bounded work, baseline preservation, and required gates are evidenced. |

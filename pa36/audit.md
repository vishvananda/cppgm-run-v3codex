# PA36 Checkpoint Audit

## Current Checkpoint Review

Scope: landed checkpoint `03e47d62`, which changed class-specialization owner
facts so the leading standard namespace encodes as Itanium `St` instead of
`3std`. The behavior is correct and raised the combined PA36 baseline from
43/80 to 56/80, but the landed two-line classifier compared the rendered
component string to `"std"` in ABI lowering. That left a relevant `spec.md`
§§2, 6, and 10 violation: output identity was being rediscovered from
presentation text rather than consumed from a canonical semantic fact.

The audit repair classifies the ABI-designated global `std` namespace once in
`Program::OpenNamespace` after namespace lookup establishes its canonical
`ScopeId`. `Program` retains that compact ID, and standard-template,
initializer-list, hosted-trait, and class-owner ABI consumers query it by scope
identity. `AppendClassTemplateOwner` still renders source names for the ABI fact
payload, but selects `ABI_FUNCTION_RECORD_NAME_STD` from the owning entity's
scope ancestry; it no longer parses or compares the rendered component.

The complete affected trace is source namespace declaration -> interned
`NameId` and canonical namespace `ScopeId` -> entity owner scope -> structured
class-template owner facts -> non-numbered `NAME_STD` component -> Itanium `St`
encoder state -> LowIR symbol identity and direct ELF definition/relocation.
Template arguments remain canonical `TypeId`/argument facts, and the mangler's
numbered substitution state remains continuous across the whole entity. No
lowering lookup, rendered-name reconstruction, or hosted-only side channel is
left on this path.

The fact has translation-unit lifetime with `Program`; no token, syntax,
semantic, LowIR, or MIR representation is duplicated and no phase-local pointer
is retained. Demand state, specialization completion, emission worklists,
per-function MIR, and direct ELF ownership are unchanged. Interned-name and
designated-namespace identity code now lives in the responsibility-named
`pa11_name_identity.cpp`, keeping `pa11_model.cpp` below its file-audit limit.
The query adds only an owner-depth walk where ABI path construction already has
the same O(depth) bound; it adds no allocation, cache, invalidation, global scan,
retry loop, serializer, external tool, test/path shortcut, or timeout path.

On the 136,183-token hosted stream workload, three release runs took
1.33/1.33/1.34 s at 65,032/64,860/65,176 KiB RSS. Every run retained 45,257
lookup queries, 69,440 scope visits, 2,608 template requests, 329 demand pushes,
310 functions, 6,915 LowIR instructions, 9,936 MIR instructions, and 67,966,620
semantic peak bytes. The 3,782,808-byte objects had identical SHA-256 hashes,
host `St` spellings, and zero malformed `3std` undefined references. This is
representative evidence that the repair preserves semantic/backend work and
object output while removing the textual identity dependency.

All 16 checkpoint-focused standard-owner, direct-substitution,
initializer-list/trait, and prior builtin checks pass. The required sequential
PA36 report remains 56/80 with the same 24 out-of-scope-for-this-increment
failures and no timeout; PA1–PA35 pass 4,907/4,907. The PA36 file audit passes
with 22 inherited nonfatal header-division advisories. No relevant spec,
correctness, performance, shortcut, timeout, ownership, or file-audit issue
remains in this checkpoint increment.

## Checkpoint Audit Ledger

| Checkpoint | Audit disposition |
| --- | --- |
| Closed builtin dispatch (`1f918c84` plus audit repair) | Pass after separating generic aliases from all closed handlers; canonical typed identity, bounded work, baseline preservation, and required gates are evidenced. |
| Canonical structured `std` owner (`03e47d62` plus audit repair) | Pass after replacing lowering's rendered-name test with canonical namespace-scope identity; 56/80 baseline, stable profile/output, prior stages, and file gate are preserved. |

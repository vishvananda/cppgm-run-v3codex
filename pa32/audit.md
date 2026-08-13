# PA32 Checkpoint Audit

## Current Checkpoint Review

Scope: `03c66170` (`pa32 canonicalize anonymous entity ownership`). Verdict: pass after checkpoint-scoped identity, projection, and container fixes.

The declaration trace follows an anonymous class storage member from its nested canonical entity and direct storage `BindingId`, through compact alias-to-`InjectedMemberInfo` and storage-to-alias indexes, constructor selection, canonical member offsets, typed initializer/lifetime actions, LowIR member projection, MIR, and direct ELF. The landed path lost projected initializers when the enclosing owner was itself a union, treated anonymous structs as inactive anonymous unions, and did not propagate nested default-member demand to the storage owner. The audit now selects the one direct storage member as the union variant, rejects competing variants, preserves struct/union defaults, and lowers projected actions from the canonical alias binding. The reverse index uses `IndexedSequenceTable`/`CompactIndexSequence` rather than one heap-backed vector per storage.

The demanded-template trace uses two same-named local `Local` entities as distinct arguments to `width<T>`. Source construction assigns a compact `(function binding, identity name)` occurrence ordinal; typed PA15 emission identity now includes that ordinal; `AbiFactBuilder::MakeType` carries it into `ABI_TYPE_LOCAL_TYPE`; the mangler emits the matching Itanium discriminator; and the two internal LowIR symbols remain distinct through ELF. Before the audit, type facts hardcoded discriminator zero and PA15 collapsed the specializations, producing `conflicting PA15 ABI object identity`. The added regression verifies both host spellings and runtime behavior. No changed path reparses rendered text, retries the translation unit, shells out, or adds filename/type-name shortcuts; template declaration/body/demand state remains owned by the existing canonical specialization records.

Representative 16/64/256-block sources combine a same-named local class, two projected anonymous-struct members, explicit construction, a member call, and a `width<Local>` demand per block. Semantic nodes were 754/2,962/11,794; lookup-scope visits 1,037/4,109/16,397; template requests 48/192/768; LowIR instructions 572/2,252/8,972; semantic plus typed-lowering time 5.76/21.57/83.73 ms; semantic peak bytes 911,790/3,625,062/14,475,554; and object bytes 280,208/1,102,160/4,397,840. Across 16x input, structural work and storage grow 15.6–16.0x and measured semantic/lowering time 14.5x. The 256-block object links and runs; stats-on/off 64-block objects are byte-identical (`sha256 c603aa374c78095d0831a3eccd1e0f83cb44dd70d09c3825d80a6643ddd8ccc1`).

Validation: the five landed fixtures and two audit regressions pass. The required PA32 report preserves the exact seven turn-start failures, with 133/140 passing after adding the regressions (the original suite remains 131/138). PA1–PA31 pass 4150/4150, and the file audit passes with the same 21 inherited warnings.

## Checkpoint Audit Ledger

| Checkpoint | Audit result |
| --- | --- |
| Structured dependent result/expression recipes (`f642998a`) | Pass after typed argument framing, canonical source types, and transactional ABI publication; prior and checkpoint baselines preserved. |
| Canonical callable/member-entity ABI facts (`45e35717`) | Pass after typed member terminals/template recipes and behavior-neutral telemetry; host symbols, linear scaling, prior tests, and checkpoint baseline verified. |
| Canonical external object data/addressing (`0d3e1179`) | Pass after carrying typed local/preemptible address binding through ELF fixups; exact relocations, linear scaling, prior tests, and the 96/138 baseline verified. |
| Typed ELF sections and host TLS (`b206d7c2`) | Pass after centralizing encoded label ownership and removing section-product lookups/empty relocation sections; TLS/custom-section behavior, 32→512 scaling, prior tests, and the 99/138 baseline verified. |
| Canonical linkage and anonymous-entity ownership (`03c66170`) | Pass after completing local ordinals through typed emission/ABI identity, preserving projected union/default ownership, and compacting the reverse index; original 131/138 baseline, two regressions, prior 4150/4150, file audit, and 16→256 scaling verified. |

# PA32 Checkpoint Audit

## Current Checkpoint Review

Scope: `b206d7c2` (`Implement PA32 ELF sections and host TLS`). Verdict: pass after a checkpoint-scoped ownership and scaling fix.

The nontrivial declaration trace covers the self-relocating `section_alias` in a selected GNU section and the imported/exported `thread_local extv`. Canonical `BindingRecord` storage, section, linkage, and TLS facts select typed `SymbolId` records; LowIR and MIR retain section/TLS targets and relocation kinds; `EncodedSection` owns bytes, labels, and fixups; and the direct ELF writer publishes SHF_TLS/STT_TLS/TPOFF32 or the selected custom section without parsing a mangled name. The Itanium TLS wrapper is produced from the semantic binding path and retained as an ABI symbol fact. The demanded `extern_template_add<int>` split sample still uses its existing specialization and emission state and passes; this increment introduces no template replay, global retry, external tool fallback, or alternate textual transport.

The landed writer hash-interned section names but then rediscovered label ownership by scanning every data section once per relocation, export, and alias, for O(labels + (fixups + exports + aliases) * sections) work. That contradicted `spec.md` §§7–9 and the checkpoint's own O(1)-average lookup claim. The audit gives the encoded ELF owner one label-to-section/offset index, shares it across alias publication, relocation targeting, and exported-symbol publication, moves encoded section buffers into that owner, and omits empty relocation sections. Each label is indexed once and each consumer now performs one average-O(1) lookup; stable symbol ordering remains the sole O(n log n) step.

Representative 32/128/512-case sources produced 1,130/4,490/17,930 tokens, 421/1,669/6,661 semantic nodes, 64/256/1,024 globals, 193/769/3,073 fixups, 32/128/512 custom sections, 64/256/1,024 TPOFF32 relocations, and 139,544/553,608/2,220,352-byte objects. Semantic time was 1.87–27.87 ms, typed lowering 0.63–7.30 ms, and native encoding 1.27–20.28 ms over the 16x range; structural counters, storage, and output scale linearly. The 512-case object links and runs, and stats-on/off 128-case objects are byte-identical (`sha256 d370ba1a87c07d3856076f39cfd6953b9abd0c4d65e9c9c708eeb7a81dae0fa5`).

Validation: all eight focused section/TLS tests and the demanded-template sample pass; PA32 remains 99/138 with the exact turn-start failure set; PA1–PA31 pass 4150/4150; and the file audit passes with the same 21 inherited warnings.

## Checkpoint Audit Ledger

| Checkpoint | Audit result |
| --- | --- |
| Structured dependent result/expression recipes (`f642998a`) | Pass after typed argument framing, canonical source types, and transactional ABI publication; prior and checkpoint baselines preserved. |
| Canonical callable/member-entity ABI facts (`45e35717`) | Pass after typed member terminals/template recipes and behavior-neutral telemetry; host symbols, linear scaling, prior tests, and checkpoint baseline verified. |
| Canonical external object data/addressing (`0d3e1179`) | Pass after carrying typed local/preemptible address binding through ELF fixups; exact relocations, linear scaling, prior tests, and the 96/138 baseline verified. |
| Typed ELF sections and host TLS (`b206d7c2`) | Pass after centralizing encoded label ownership and removing section-product lookups/empty relocation sections; TLS/custom-section behavior, 32→512 scaling, prior tests, and the 99/138 baseline verified. |

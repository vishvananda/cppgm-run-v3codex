# PA32 Checkpoint Audit

## Current Checkpoint Review

Scope: `0d3e1179` (`Implement PA32 external object data addressing`). Verdict: pass after a checkpoint-scoped ownership fix.

The nontrivial declaration trace is the prior `extern "C"` declaration followed by weak namespace definitions of `api::alias`; direct C declarations, braced C definitions, strong defined data, and imported data cover the adjacent cases. Source linkage syntax publishes canonical `BindingRecord` language/storage/weak/section facts; `Symbol` owns definition and weak-preemption state; typed LowIR and MIR operands own address binding; `EncodedFixup` owns relocation intent; and the direct ELF writer maps local binding to PC32 or preemptible binding to GOTPCRELX. Mangled spelling remains presentation data and is never parsed to recover linkage.

The landed ELF code had discarded the typed definition/weakness fact and rebuilt it per fixup by probing string-keyed encoded-label and weak-symbol sets. That violated `spec.md` §§2 and 6 and the checkpoint's claimed owner flow. The audit adds a compact `AddressBinding` fact derived once from `Symbol::definition_emitted` and `weak_linkage`, preserves it through global value, pointer-cell, EH, LowIR, MIR, and fixup copies, and removes ELF-side name/label classification. The demanded `Split<int>::emplace_back<int>` template sample still reuses its existing specialization/emission facts and passes; this checkpoint adds no template replay, retry loop, semantic lookup, or owning representation.

Generated 32/64/128-case sources produced 993/1,985/3,969 tokens, 417/833/1,665 semantic nodes, 64/128/256 functions, 128/256/512 LowIR instructions and fixups, 96/192/384 PC32 plus 32/64/128 GOT relocations, and 108,424/215,880/431,664-byte objects. Semantic time was 1.69–6.59 ms, typed lowering 0.64–2.15 ms, and native encoding 1.09–4.32 ms across the 4x range. Structural work and output are linear; relocation selection is one enum branch per address fixup with no symbol-set probe. Stats-on/off objects are byte-identical (`sha256 6ca023bff96c62d6677f8cd9b3fb407d2681d4ba7ee8c0a9945a7b04cefb72ff`), and process tracing observed only `cppgm++`'s own `execve`.

Validation: the five landed gains and the demanded-template sample pass; PA32 remains 96/138 against the 96/138 turn-start baseline; PA1–PA31 pass 4150/4150; and the file audit passes with the same 21 inherited warnings. GNU section placement and TLS are unchanged baseline failures owned by the next checkpoint, not fallbacks in this address-relocation path.

## Checkpoint Audit Ledger

| Checkpoint | Audit result |
| --- | --- |
| Structured dependent result/expression recipes (`f642998a`) | Pass after typed argument framing, canonical source types, and transactional ABI publication; prior and checkpoint baselines preserved. |
| Canonical callable/member-entity ABI facts (`45e35717`) | Pass after typed member terminals/template recipes and behavior-neutral telemetry; host symbols, linear scaling, prior tests, and checkpoint baseline verified. |
| Canonical external object data/addressing (`0d3e1179`) | Pass after carrying typed local/preemptible address binding through ELF fixups; exact relocations, linear scaling, prior tests, and the 96/138 baseline verified. |

# PA32 Checkpoint Audit

## Current Checkpoint Review

Scope: `f642998a` (`Implement PA32 structured dependent ABI results`) and its structured dependent result/expression increment. Verdict: pass after checkpoint-scoped audit fixes.

The audited declaration is `distance_like<RandIter, enable_if_t<cat<RandIter>::value, unsigned long> = 0>` and the demanded expression template is its `operator-` trailing `decltype`. The ownership path is source syntax -> PA23 expanded result identity -> PA32 immutable ABI type/expression recipes on `FunctionTemplatePattern` -> PA15 ABI facts and canonical symbol metadata -> typed LowIR/MIR -> direct ELF. Consumers use compact type, entity, parameter, and expression IDs; they do not reparse rendered names or repeat semantic lookup.

The landed identity stream delimited only the complete template-argument list. Its PA32 reader then recovered fundamental types by comparing individual source spellings. A single `unsigned long` type argument was therefore read as two arguments and emitted `...valueEjlE...` instead of the required `...valueEmE...`. A failed read could also append partial type, argument, and expression nodes before abandoning the recipe. This violated `spec.md` §§2, 6, and 8 at the complete producer/consumer ownership boundary, not in the mangler or ELF writer.

The fix gives every argument explicit begin/end atoms, publishes nondependent source types as canonical `TypeId`s, removes spelling-to-type reconstruction, and commits ABI nodes only after a complete successful read. Dependent and pack-bearing syntax remains retained for substitution replay. The focused unsigned-long regression now emits the required canonical symbol, while the earlier dependent pack, cast, alias-value, array-result, `decltype`, class-result, and ostream cases remain intact.

Performance was checked with 16/32/64/128 independent alias/result templates. Syntax visits were 272/544/1,088/2,176, environment probes 128/256/512/1,024, identity requests 16/32/64/128, LowIR instructions 163/323/643/1,283, and object bytes 87,104/171,344/340,016/677,648. The structural counters are linear; no specialization scan, retry loop, name-keyed fallback, or extra lowering pass was introduced. Two final object builds of the regression were byte-identical (`sha256 91472f83fcb9177b44365971ecda716533a5fc4245cfbd2a990f2000f32410bb`), and process tracing observed only the compiler's own `execve`.

Validation: PA1–PA31 pass 4150/4150; PA32 is 85/134 including the new passing regression, preserving the turn-start 84/133 baseline and leaving the same 49 original failures; the PA32 file audit passes with its 21 inherited warnings.

## Checkpoint Audit Ledger

| Checkpoint | Audit result |
| --- | --- |
| Structured dependent result/expression recipes (`f642998a`) | Pass after typed argument framing, canonical source types, and transactional ABI publication; prior and checkpoint baselines preserved. |

# PA37 Plan

## Stage Design and Spec Alignment

PA37 owns deterministic optimization of `lowir_model::LowirProgram` between typed lowering/parsing and native lowering. Text remains an explicit tool/object adapter; direct source compilation stays typed and is checked against a serialize/parse boundary. O1 uses bounded function-local value, CFG, DCE, and direct-inline passes; O2 adds conservative scalar/pointer slot dataflow. Indexed call/CFG facts preserve EH, debug, ABI, linkage, and object metadata. This follows `spec.md` §§4–6 (demand identity, deterministic final order, typed adapters) and §§7–9 (bounded passes, phase-local ownership, observable O(n) work).

## Current Failure Map

| Group | Turn-start | Current | Owner / resolution |
|---|---:|---:|---|
| Tool/O0 boundary | 0/2 | 2/2 | parser/serializer and exact typed round trip |
| O1 value/CFG/EH | 0/48 | 48/48 | `lowir_opt` plus bounded direct inlining |
| O2 slot/dataflow | 0/12 | 12/12 | executable-edge slot promotion and dead stores |
| Driver integration | 0/9 | 9/9 | O1/O2 routing; debug driver lanes also 2/2 |
| Object round trip | 0/7 | 7/7 | byte identity at O0/O1/O2; debug lanes also 7/7 |

No current PA37 failures; turn-start baseline was 0/78 and the complete local stage plus debug/object buckets pass.

## Active Checkpoint

Checkpoint and release validation complete: PA37 is 78/78, debug lanes pass, prior-through-PA36 is 4987/4987, and file audit passes. Data flow is `TypedProgram -> LowirProgram -> lowir_opt -> native backend`; explicit text input/output uses the parser/serializer adapters. Typed object normalization reconstructs only text-representable call, operand, EH, and export facts, so direct compilation matches serialized LowIR without a production serialize/reparse side channel.

## Performance Evidence

Synthetic O2 constant chains at 257/513/1025 input instructions recorded 258/514/1026 instruction visits, 256/512/1024 worklist pushes, and 256/512/1024 rewrites. Doubling input doubled measured work (about 1.99x each step); the bounded pass pipeline showed linear representative growth.

## Completed Checkpoints

| Checkpoint | Result | Validation / evidence |
|---|---|---|
| Typed optimizer | O0 canonicalization, O1 fold/CSE/DCE/CFG/EH/direct inline, O2 slot promotion | lowiropt buckets 62/62; debug lowiropt 3/3 |
| Driver and metadata | O1/O2 routing, line tables, canonical demand order, lifecycle/linkage facts | required driver 9/9; debug driver 2/2 |
| Serialized object boundary | direct-call ABI restoration, valid EH endings, declaration/reference canonicalization, native promoted-value support | object round trip 7/7 and debug 7/7 at O0/O1/O2 |
| Compatibility and release | preserve established source-view behavior while sharing the typed optimization boundary | PA1–PA36 4987/4987; PA37 78/78; file audit pass |

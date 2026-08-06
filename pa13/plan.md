# PA13 Implementation Plan

## Stage Design and Spec Alignment

PA13 is the explicit textual-adapter exception to the production path in
`spec.md` §6: input LowIR text is parsed once into one typed program, validated
by stable symbol/block indexes, and rendered directly as PA9 CY86 text.  The
adapter will not invoke CY86, a host compiler, or a reference tool.  Source
buffers are file-owned only during parsing; typed LowIR owns declarations,
definitions, metadata, operands, and instructions; function-local layout and
emission state dies after each function; the final string is the only output
view.

Ownership is split at stable boundaries: `lowir_parse.*` owns lexical parsing,
typed construction, and whole-program structural validation;
`lowir_cy86.*` owns deterministic frame layout and instruction/data lowering;
`lowir2cy86.cpp` owns only CLI/file output. Serialized names remain adapter
spellings in typed records and are indexed once per owner with average-O(1)
lookup; blocks, locals, temporaries, and top-level symbols are never found by
whole-program rescans. This applies `spec.md` §§6 and 8–10 to the explicit
text-adapter surface. Canonical frontend IDs remain owned by the production
semantic path rather than reconstructed by this backend adapter.

## Current Failure Map

Turn-start baseline was 0/96 checked-in PA13 tests (29 rejection and 67 success
fixtures); the current implementation passes 96/96.
The additional 18 `tests/debuginfo` fixtures exercise later `cppgm++`,
`lowiropt`, native-object, and debugger surfaces; `pa13/README.md` explicitly
keeps those out of the PA13 `lowir2cy86` contract and required report.

- Text/model and symbol ownership: resolved for declarations/definitions,
  metadata, aliases, types, globals, functions, slots, blocks, operands, debug
  locations, and multi-file concatenation.
- Structural/type validation: all 29 expected-failure fixtures pass, covering
  duplicate or missing entities, terminators/targets, metadata domains,
  pointer parameter constraints, call signatures, conversions, and spans.
- Deterministic scalar/control lowering: all integer/pointer values, frames,
  globals, calls, memory/address/index, arithmetic, comparisons, conversions,
  branches/switch, hooks, and object boundaries match their output fixtures.
- Specialized lowering: structured/floating data, f32/f64/f80, atomics, bulk
  object operations, and exception handler-stack operations all match CY86
  fixtures exactly after trailing-whitespace normalization.

## Active Checkpoint

Completed: the full typed-text-to-CY86 boundary parses every required PA13
form, validates the whole program, and lowers each validated function/global
once using fixture-defined PA9 conventions. Parsing, indexing, validation,
layout, and emission are O(input bytes + symbols + instructions + output
bytes), with average-O(1) local/top-level lookup and geometric output growth.

## Performance Evidence

A generated one-function add chain was measured three times per size with
`CPPGM_LOWIR_STATS=1`. At 2,000/4,000 links, input was 69,869/141,869 bytes,
tokens 16,019/32,019, instructions 2,002/4,002, and CY86 output
165,551/333,551 bytes. Median parse time was 8.56/17.18 ms (2.01x), median
lowering time 6.87/13.44 ms (1.96x), and median RSS 7,956/12,120 KiB. Work,
time, and generated output follow the 2x semantic/output growth; no repeated
whole-program scan is evident.

## Completed Checkpoints

| Checkpoint | Result | Evidence |
|---|---|---|
| Contract and complete failure inventory | Complete | Governing docs, all 96 inputs, exit sidecars, and representative CY86 refs inspected; baseline 0/96 |
| Typed parse/validation/CY86 adapter | Complete | Required PA13 report 96/96; ASan/UBSan 96/96; all 29 rejection families and 67 output fixtures pass |
| Complexity and architecture evidence | Complete | 2,000/4,000-link counters and timings scale with input, instruction, and output growth; file audit passes |
| Regression and exit gates | Complete | Required prior-through report passes 820/820; root through-PA13 report passes 916/916 |

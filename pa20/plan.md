# PA20 Full-Stage Plan

## Stage Design and Spec Alignment

PA20 extends the retained PA19 graph through the assignment's LowIR endpoint.
The forward path is immutable source buffers -> streaming preprocessing ->
interned PA10 tokens and one retained syntax arena -> canonical PA11/PA12
semantic IDs -> typed PA15 LowIR -> terminal text rendering. The PA10 syntax
boundary and terminal LowIR text are assignment adaptations to `spec.md`;
machine IR and ELF are later-stage surfaces and are not claimed here.

Template identity is `(pattern ID, canonical type/value arguments, pack
partition offsets)`. A pattern ID also owns its lexical scope, so member
patterns replayed in an enclosing specialization receive distinct context.
Patterns and retained definitions have stable ownership; parent-linked
template scopes bind fixed arguments and pack slices. Class completion,
member-definition demand, function demand, and virtual demand use separate
monotonic states/worklists. Lowering consumes selected `BindingId`, `TypeId`,
layout, conversion, and action facts without semantic lookup or text parsing.

The audit repaired the remaining phase-boundary gaps: scalar literals now carry
their phase-7 type and decoded value in a dense side table; dependent
`decltype(... )::name` paths carry structured interned components; and
constant folding implements the target's usual arithmetic conversions,
short-circuit selection, and signed-overflow rejection.

## Performance Evidence

Seven-run medians were measured with release telemetry on fresh generated
families. Counts below are for 1x/2x/4x semantic inputs.

| Family | Sizes | Work counters | Peak bytes / LowIR bytes | Median semantic time |
|---|---:|---|---|---:|
| Integral folding | 128/256/512 assertions | tokens 3,210/6,410/12,810; nodes 1,669/3,333/6,661; lookups 5/5/5 | 551,252/1,088,724/2,165,716; output 106 fixed | 2.561/5.072/10.145 ms |
| Class/function specialization reuse | 16/32/64 keys, each called twice | requests 128/256/512; hits 96/192/384; demanded functions 16/32/64; lookups 827/1,643/3,275 | 379,669/753,245/1,500,397; output 6,008/12,100/24,292 | 2.006/3.693/7.273 ms |
| Type-pack relay expansion | 16/32/64 elements | nodes 79/143/271; lookups 67/115/211; requests 3 fixed; hits 1 fixed; demanded functions 2 fixed | 89,858/152,264/293,912; output 3,921/7,609/14,985 | 0.544/0.781/1.198 ms |

Doubling ratios are 2.00x/2.00x, 1.84x/1.97x, and 1.44x/1.53x respectively.
Counters explain the slopes, cache hits stay at 75% in the specialization
family, and pack work follows produced elements without a Cartesian product.
No unexplained slow path met the profiling trigger.

## Architecture Review

A representative constant declaration,
`static_assert(0xffffffff > 0)`, enters post-tokenization with the phase-7
`unsigned int` type and decoded value. Its 8-byte token refers to one 16-byte
dense scalar fact. PA10 attaches that fact ID to the literal node without
growing every syntax node. PA12 interns the semantic fundamental `TypeId`,
normalizes the value at its width, applies typed conversions/folding once, and
records the constant on the semantic dump. Static-assert validation consumes
that fact; lowering ignores the declaration because it has no runtime unit.

For the demanded probe `twice<N>() -> Box<N>::value`, PA10 parses each template
body once. Explicit integral arguments become canonical `TemplateArgument`
records; the class/function tables probe compact specialization keys. A miss
creates a parent-linked substitution scope, rechecks dependent retained nodes,
publishes one specialization binding, and marks completion. Duplicate calls hit
the same table entry. Typed demand enqueues each function once; lowering uses
the binding index to emit one function and direct calls, then the typed program
is rendered once as terminal LowIR.

Checklist disposition:

- Representation/ownership: source, token/literal facts, syntax, synchronous
  semantic graph, and typed LowIR each have explicit owners. The staged
  syntax/semantic overlap is bounded; no rendered text is parsed back.
- Identity/lookup: names, types, scopes, entities, bindings, template arguments,
  specializations, and pack offsets use compact identities and dense indexes.
  Structured qualified names remove semantic spelling keys from PA20 paths.
- Templates/repeated work: retained patterns are stable, scopes are overlays,
  cache misses return `kNoBinding`/false where candidate failure is expected,
  and completion/demand states prevent duplicate or global retry work.
- Lowering/backend: `SemanticGraphView` is borrowed synchronously and lowers
  directly to `TypedProgram`; no lookup, host compiler, LowIR reparse, machine
  IR, or ELF path participates in PA20.
- Allocation/scaling: hot tables use vectors/open addressing and compact IDs;
  no per-node ownership was added. Telemetry exposes specialization requests,
  hits, worklist pushes, demand emissions, graph sizes, storage, and phase time.
- Self-containment: compiler-source scans found no host/reference execution,
  test/ref-name branch, cached output, or hosted-library shortcut. The fork in
  `test_runner.cpp` is harness-only and absent from the compiler source set.

## Final Architecture Review

All audit findings are closed. The PA20 path is canonical, demand-driven,
self-contained, and linear in the measured semantic input and produced output.
The file audit has no fatal issue; its advisory header-division findings and the
pre-existing duplicate optional partial-specialization matcher do not alter the
in-scope ownership or complexity bounds. The required report passes 2,185/2,185
tests across all 20 stages. The cohesive audit commit and clean-worktree proof
close the stage.

## Checkpoint Audit Ledger

| Checkpoint group | Independent disposition | Final evidence |
|---|---|---|
| Integral assertions (`c74ce9d5`) | Pass after audit repair | phase-7 typed scalar facts; correct mixed-sign conversion, selection, and overflow rules |
| Integral template arguments (`c7783d8d`) | Pass after audit repair | canonical width/value arguments now originate from retained typed literals |
| Pack identity and expansion (`6d3d2a75`-`80cef651`) | Pass | canonical partition offsets, lockstep element scopes, no Cartesian scaling |
| Base packs (`e744d35c`) | Pass | ordered bases feed indexed lookup/layout and typed lowering actions |
| Dependent boundaries/helpers (`d8e5ca44`-`dee259a3`) | Pass after audit repair | structured `decltype` qualified paths and canonical constant lookup |
| Specialized artifacts/demand (`c8745d36`) | Pass | stable patterns, monotonic class/member/function/vtable demand |
| Literal dispatch (`10b67478`) | Pass after audit repair | scalar type/value handoff complements retained UDL/string syntax |
| Target-directed conversions (`7f74da10`) | Pass | selected callable/binding facts survive into lowering |
| Specialization closure (`40fc166d`) | Pass after audit repair | explicit/late specialization path retained; operator owner split restores file-audit limits |
| Final PA-wide audit | Pass | file audit passes; full report passes 2,185/2,185 and 20/20 stages |

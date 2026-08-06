# PA15 Final Plan

## Stage Design and Spec Alignment

PA15 consumes PA12's semantic graph through a synchronous borrowed callback and
builds a separately owned typed LowIR program. The pipeline is:

`source bytes -> PA10 syntax IDs -> PA12 type/binding/expression IDs -> PA15
value/address lowering -> typed symbols/functions/blocks/instructions -> LowIR text`.

For a representative assignment, the semantic binding ID selects a stable local or
global storage ID, expression facts select the already-resolved operand and result
types, PA15 emits ordered load/arithmetic/store instructions, and the renderer follows
typed IDs plus flat argument/switch side tables. Cross-source symbols are keyed by
canonical owner/name/type records; ABI text is output metadata, never an identity key.
No semantic pointer escapes the callback and no text is serialized and reparsed.

The applicable `spec.md` checklist is satisfied for the C++11 compiler surface:
canonical integer IDs and geometric arrays own hot data; strings are interned or
deferred presentation; lookup and identity tables are indexed; deep top-level,
statement, switch, and type worklists are iterative; lowering, model storage, and
rendering have separate owners; and output streams directly. GC roots, interpreter
frames, ELF layout, and machine-code backends are not PA15 surfaces.

## Performance Evidence

| Workload | Scale | Measured result |
|---|---:|---|
| Scalar assignments | 5k / 10k | 15,003 / 30,003 instructions; 7.31 / 15.58 ms lowering; 1,967,706 / 3,933,786 typed bytes; 0.05 / 0.11 s elapsed |
| Independent functions | 4k / 8k / 16k | 12.27 / 24.84 / 50.48 ms lowering; 2,494,308 / 4,988,100 / 9,975,684 typed bytes; 0.07 / 0.13 / 0.28 s elapsed |
| Nested `if` | 8k / 16k | 18.86 / 46.23 ms semantic analysis; 10.43 / 23.16 ms lowering; 0.07 / 0.16 s elapsed |
| Nested namespaces | 4k / 8k / 12k | 3.62 / 11.20 / 12.85 ms semantic analysis; 0.02 / 0.05 / 0.07 s elapsed |
| Depth robustness | 32k compounds / 32k pointer modifiers | 0.09 / 0.05 s elapsed; both complete successfully |

The assignment model shrank from 448-byte instructions, 104-byte operands, and
56-byte types to 120, 32, and 16 bytes. Before the audit, 5k/10k assignments retained
7,791,668/15,581,700 typed bytes and took 22.32/44.80 ms to lower. A `perf` profile of
the former many-function curve found whole-TU zero-fill in every function; a second
profile of nested control attributed 55.15% to `LookupName` and 42.35% to `FindEntry`.
The measured post-fix curves above are linear in emitted work.

## Architecture Review

- Ownership: PA12 owns semantic facts for callback lifetime; PA15 owns all emitted
  symbols, literals, side tables, functions, blocks, and instructions.
- Identity: strong LowIR IDs and canonical structural path/type indexes replace
  spelling identity and interchangeable cross-link integers.
- Complexity: ID maps initialize once per graph, inherited lookup results use a
  mutation-revisioned cache, and qualified namespace prefixes materialize lazily.
- Recursion: PA15 top-level, statement/control, switch-case, and semantic identity
  walks use explicit stacks; 16k control and 32k structural probes complete.
- Phase separation: `pa15_lowir_types.h` defines value records,
  `pa15_lowir_model.{h,cpp}` owns program/index storage,
  `pa15_lowering.cpp` transforms facts, and `pa15_lowir_render.{h,cpp}` emits text.
- Self-containment: process tracing shows only the compiler's `execve` and
  `exit_group(0)`; there is no child tool, reference binary, or host compiler.

## Final Architecture Review

All audit findings are closed. Sanitizers cover PA11, PA12, and all 108 PA15 fixtures;
the required file audit has no findings; and the through-PA15 report passes all 1,145
tests. No correctness, ownership, scaling, self-containment, or file-division blocker
remains in the PA15 path.

## Checkpoint Ledger

| Checkpoint | Final state | Evidence |
|---|---|---|
| Semantic handoff and scalar spine (`1735bfbe`, `0a425234`) | Closed | Typed facts, canonical cross-source identity, ABI metadata, scalar correctness, deep type path |
| Procedural LowIR closure (`2dde017d`) | Closed | Values/addresses, calls, globals, initialization, functions, CFG/control, 108/108 PA15 |
| Final full-stage audit | Closed after refactor | Compact model, iterative lowering, stable semantic snapshots, revisioned lookup cache, lazy prefixes, clean file audit, sanitizers, 1,145/1,145 through PA15 |

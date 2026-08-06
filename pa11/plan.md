# PA11 Plan

## Stage Design and Spec Alignment

`--emit-types` consumes the compact PA10 syntax arena directly, builds one
canonical semantic graph, renders it, and then releases both phase-local
representations. `dev/src/pa11_semantic.*` owns interned names and types,
scope/declaration records, direct `(scope,name)` indexes, explicit using edges,
lookup, declarator type construction, integral constants, and deterministic
output; `pa10_syntax.*` owns parsing and a read-only arena-consumer boundary;
`dev/cppgm++.cpp` owns multi-translation-unit framing. This applies spec §§1–3,
8–10: one parse, identity-based types/declarations, indexed lexical/qualified
lookup, explicit using edges, compact IDs/flat tables, and no text round trip.
Construction and rendering are O(syntax nodes + declarations + emitted type
structure); each lookup is O(lexical depth + relevant using edges), with no
whole-program declaration scan.

## Current Failure Map

Resolved from the 0/68 turn-start baseline to 68/68. The complete set was owned
by core scope/declarator construction (16), indexed namespace/class/using lookup
and conflicts (19), class/enum/template named-type identity (16), and constants/
`decltype`/type traits plus required diagnostics (17). The three PA10 ambiguity
gaps for elaborated declarations and qualified enum definitions are also closed
without changing established PA10 output.

## Active Checkpoint

Completed. No narrower follow-up checkpoint remains for PA11.

## Performance Evidence

On the namespace-alias/using-edge representative: 39 tokens, 24 syntax nodes,
3 lookup queries, 8 relevant-scope visits, 1 using-edge visit, and 1,952 semantic
bytes. Repeating that translation unit 64/256/1,024 times took 0.00/0.01/0.05 s,
produced 16,652/66,730/267,204 bytes, and held max RSS to 4,464/4,328/4,976 KiB.
Work/output scale linearly while per-translation-unit storage is reclaimed.

## Completed Checkpoints

| Checkpoint | Result |
|---|---|
| PA11 semantic spine | 68/68 PA11; 644/644 through PA11; file audit clean; linear scaling evidence above |

# PA34 Plan

## Stage Design and Spec Alignment

PA34 extends the shared source-to-object compiler at its existing boundaries: hosted
configuration feeds `PreprocessingOptions`; `PreprocessFile` remains the one streaming
preprocessing path used by `-E` and compilation; parser, semantic, and lowering work
remain shared with prior stages. This aligns with `spec.md` §§1, 8, 9, and 10: immutable
source buffers, interned preprocessing names, minimal typed phase input, linear work in
source/configuration bytes plus expansion output, and no runtime host-tool invocation.

## Current Failure Map

- Preprocess: 0/45 failures; the hosted preprocessing boundary is complete.
- Compile: 164/281 failures: 74 extension grammar/type-spelling cases owned by the
  parser, 16 hosted intrinsic expression cases owned by semantic/lowering, 40
  trait/layout/constant cases owned by semantic facts, and 34 template lookup/demand
  cases. By tier these are 63 tier-500, 55 tier-600, and 46 tier-700 failures; all four
  negative tests are still correctly rejected.
- Run: 36/41 failures; 34 stop in the compile pipeline, while 2 link and expose
  generated-code behavior (backend/lifetime and forced-inline handling).

## Active Checkpoint

Next, implement the parser-owned hosted extension syntax boundary: a bounded spelling
registry/classifier feeds explicit syntax nodes for declaration/type attributes and
extension type/statement forms, then semantic analysis normalizes only the facts needed
downstream. This applies `spec.md` §§4, 6, 8, and 10: interned names, parser-owned
grammar, typed phase boundaries, and no source-text replay in lowering. Lookup is
O(log E), parsing is O(tokens), and syntax storage is O(tokens). Validate representative
tier-500 grammar groups, PA34 above 167/367, prior-through-pa33, audit, and repeated-input
scaling. This checkpoint is queued; implementation has not begun.

## Performance Evidence

`400-has-feature-cxx11-core.t` processed one source plus the immutable built-in source
as 19,846 bytes, 3,948 preprocessing tokens, 541 directives, and 24 probes in 2.174 ms.
Eight independent inputs produced exactly 8x each work counter and 15.174 ms aggregate
(6.98x elapsed), consistent with O(H + S + X) translation-unit scaling.

`600-builtin-pointer-reference-traits.t` (seven static assertions) used 89 tokens, 14
semantic nodes, five lookups, 33,970 peak semantic bytes, and 0.151 ms semantic time.
Eight inputs took 61.665 ms and 64 took 386.474 ms (8x work, 6.27x elapsed), consistent
with bounded registry lookup and linear per-translation-unit semantic work.

## Completed Checkpoints

| Checkpoint | Result | Validation |
| --- | --- | --- |
| Hosted preprocessing boundary | `-E`, CLI/include controls, host metadata, probes, directives, and hosted literals; +51 PA34 passes | preprocess 45/45; PA34 121/367; PA1-33 4387/4387; audit pass |
| Hosted builtin type-query boundary | Registered trait/transform syntax, retained type operands, interned transforms, structural constants, and native member-pointer null adaptation; +46 PA34 passes | PA34 167/367; PA1-33 4387/4387; focused diagnostics pass; audit pass |

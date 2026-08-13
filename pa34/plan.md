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
- Compile: 210/281 failures, partitioned by test tier: 68 tier-500 hosted syntax and
  builtin surface cases (parser/type construction), 72 tier-600 trait/template/layout
  cases (semantic evaluation), and 70 tier-700 recursive hosted/template-demand cases
  (lookup, instantiation, and lowering).
- Run: 36/41 failures; 34 stop in the same compile pipeline, while 2 link and expose
  generated-code behavior (backend/lifetime and forced-inline handling).

## Active Checkpoint

Next, extend the syntax/type-construction boundary for the tier-500 hosted surface:
GNU/Clang attributes and aliases, block/nullability/atomic/bit-int types, asm forms,
deduction guides, and builtin expression spellings. Ownership/data flow is post-tokens
-> syntax model -> semantic type/expression construction; extension spelling remains
interned and parser nodes carry structure rather than evaluated meaning. Expected work
is O(T) in consumed tokens with bounded declaration lookahead. Validate all tier-500
cases, the full PA34 report, prior-through-pa33, file audit, and repeated declaration
scaling.

## Performance Evidence

`400-has-feature-cxx11-core.t` processed one source plus the immutable built-in source
as 19,846 bytes, 3,948 preprocessing tokens, 541 directives, and 24 probes in 2.174 ms.
Eight independent inputs produced exactly 8x each work counter and 15.174 ms aggregate
(6.98x elapsed), consistent with O(H + S + X) translation-unit scaling.

## Completed Checkpoints

| Checkpoint | Result | Validation |
| --- | --- | --- |
| Hosted preprocessing boundary | `-E`, CLI/include controls, host metadata, probes, directives, and hosted literals; +51 PA34 passes | preprocess 45/45; PA34 121/367; PA1-33 4387/4387; audit pass |

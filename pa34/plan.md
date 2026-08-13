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
- Compile: 136/281 failures: 51 extension grammar/type-spelling cases owned by the
  parser, 8 remaining hosted intrinsic expression cases owned by semantic/lowering, 41
  trait/layout/constant cases owned by semantic facts, and 36 template lookup/demand
  cases. By tier these are 46 tier-500, 46 tier-600, and 44 tier-700 failures; all four
  negative tests are still correctly rejected.
- Run: 36/41 failures; 34 stop in the compile pipeline, while 2 link and expose
  generated-code behavior (backend/lifetime and forced-inline handling).

## Active Checkpoint

Next, extend the same hosted call boundary with the memory/string intrinsic family:
`memset`/`bzero`, byte and character searches, alignment assumptions, and prefetch, while
canonicalizing existing copy/move/length support into registry metadata. The registry
owns spelling, arity, effects, and lowering IDs; semantic analysis emits typed calls and
typed LowIR selects explicit no-op, native memory operation, or external ABI boundary.
This applies `spec.md` §§2, 3, 6, 7, 8, 9, and 10: indexed identity, typed phase facts,
effect-aware demand, bounded allocation, and no host fallback. Lookup is O(log I),
checking is O(arity), and lowering is O(1) per call apart from the operation's byte
extent. Validate focused compile/run cases, PA34 above 195/367, prior-through-pa33,
audit, and repeated-call scaling. This checkpoint is queued.

## Performance Evidence

`400-has-feature-cxx11-core.t` processed one source plus the immutable built-in source
as 19,846 bytes, 3,948 preprocessing tokens, 541 directives, and 24 probes in 2.174 ms.
Eight independent inputs produced exactly 8x each work counter and 15.174 ms aggregate
(6.98x elapsed), consistent with O(H + S + X) translation-unit scaling.

`600-builtin-pointer-reference-traits.t` (seven static assertions) used 89 tokens, 14
semantic nodes, five lookups, 33,970 peak semantic bytes, and 0.151 ms semantic time.
Eight inputs took 61.665 ms and 64 took 386.474 ms (8x work, 6.27x elapsed), consistent
with bounded registry lookup and linear per-translation-unit semantic work.

`500-parser-recovery-and-using.t` exercised mixed GNU/standard annotations over 1,315
bytes and 328 tokens: 51 declarations, 69 lookups, six template requests (four cache
hits), 106,020 peak semantic bytes, and 0.852 ms semantic time. Eight inputs took 71.850
ms and 64 took 481.563 ms (8x work, 6.70x elapsed), consistent with linear parsing and
bounded extension lookup.

The integer-intrinsic scaling probe used four registry-dispatched calls per function.
One, eight, and 64 functions emitted 81/648/5,184 typed LowIR instructions and
175/1,400/11,200 MIR instructions exactly proportionally; the 64-function object build
took 0.07 s wall time and 23,980 KiB peak RSS. Lowering work is linear in calls, with each
count operation bounded by O(log W) for W <= 64.

## Completed Checkpoints

| Checkpoint | Result | Validation |
| --- | --- | --- |
| Hosted preprocessing boundary | `-E`, CLI/include controls, host metadata, probes, directives, and hosted literals; +51 PA34 passes | preprocess 45/45; PA34 121/367; PA1-33 4387/4387; audit pass |
| Hosted builtin type-query boundary | Registered trait/transform syntax, retained type operands, interned transforms, structural constants, and native member-pointer null adaptation; +46 PA34 passes | PA34 167/367; PA1-33 4387/4387; focused diagnostics pass; audit pass |
| Hosted declaration/type annotations | Canonical GNU aliases and int128 signedness; nullability, diagnostic, namespace, enum, using, pointer, and type-id attributes; +20 PA34 passes | PA34 187/367; compile 137/281 with no negative over-acceptance; PA1-33 4387/4387; audit pass |
| Hosted integer bit intrinsics | Registry IDs, fixed/generic conversion rules, constexpr `clz`/`ctz`/`popcount`/swap, typed bit-network lowering, and native swaps; +8 PA34 passes | focused 8/8; emitted runtime values exact; PA34 195/367; PA1-33 4387/4387; audit pass |

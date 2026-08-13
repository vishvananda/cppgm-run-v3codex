# PA34 Plan

## Stage Design and Spec Alignment

PA34 extends the shared source-to-object pipeline at existing boundaries. Hosted
configuration feeds the single streaming preprocessor; parser extensions publish
canonical syntax/type facts; semantics owns typed operands, lookup, demand, and compact
builtin IDs; typed LowIR owns effects and value behavior; native lowering alone selects
host ABI symbols. This follows `spec.md` §§1-3 and 6-10: immutable/interned inputs,
bounded registries, no spelling recovery after semantics, demand-driven emission,
linear lowering/allocation, and no host compiler fallback.

## Current Failure Map

- Preprocess: 0/45 failures; this boundary is complete.
- Compile: 134/281 failures: 50 extension grammar/type cases (parser/type system), 7
  hosted intrinsic/predefined-expression cases (semantic/lowering), 41
  trait/layout/constant cases (semantic facts), and 36 template lookup/demand cases.
  Tier counts are 46/45/43 for tiers 500/600/700; all negative tests remain rejected.
- Run: 29/41 failures; 27 stop in the compile pipeline and 2 expose linked
  backend/lifetime or forced-inline behavior.

## Active Checkpoint

Implement the 12 failing C11/GNU atomic and sync compile cases. The parser/type system
will own `_Atomic` syntax, canonical atomic qualification, size, and alignment; a sorted
operation registry will own spellings, compact IDs, arity, order constraints, and
read/write class. Semantics will convert typed operands, validate constant memory
orders, and publish compact operation facts; typed LowIR/native lowering will consume
those facts without reparsing names. This applies `spec.md` §§2, 3, and 6-10. Lookup is
O(log A), checking O(arity), type interning amortized O(1), and each lowered operation
O(1). Validate all 12 focused compile tests, representative generated atomic behavior,
PA34 above 204/367, PA1-33, audit, and 1/8/64 operation scaling.

## Performance Evidence

Hosted preprocessing scaled from one to eight inputs with exactly 8x work counters and
6.98x elapsed time. Type-query semantics scaled from 8 to 64 inputs with 8x work and
6.27x elapsed; annotation parsing scaled with 8x work and 6.70x elapsed. The integer
intrinsic probe emitted 81/648/5,184 LowIR instructions for 1/8/64 functions; the
64-function object took 0.07 s and 23,980 KiB RSS.

The memory probe used five registry-dispatched calls per function. At 1/8/64 functions
it emitted 20/160/1,280 LowIR and 37/296/2,368 MIR instructions exactly proportionally;
source bytes were 202/1,616/12,982 and semantic nodes 34/265/2,113. The 64-function
object took 0.04 s and 12,220 KiB RSS, supporting O(calls) checking and lowering.

## Completed Checkpoints

| Checkpoint | Result | Validation |
| --- | --- | --- |
| Hosted preprocessing boundary | CLI/include controls, host metadata, probes, directives, and literals; +51 | preprocess 45/45; PA34 121/367; prior/audit pass |
| Hosted builtin type queries | Registered traits/transforms, retained operands, structural constants, and member-pointer null adaptation; +46 | PA34 167/367; prior/audit pass |
| Hosted annotations | GNU aliases/int128 and declaration/type-id attributes; +20 | PA34 187/367; negatives and prior/audit pass |
| Integer bit intrinsics | Compact registry IDs, constexpr semantics, typed bit-network lowering, and native swaps; +8 | focused 8/8; PA34 195/367; prior/audit pass |
| Memory/string intrinsics | Shared probe/semantic registry, typed controls, identity/no-op lowering, effect metadata, staged ABI preservation, and libc object symbols; +9 | focused 8/8 plus GNU alignof; linked effects/symbols exact; PA34 204/367; PA1-33 4387/4387; audit pass |

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
- Compile: 117/281 failures: 43 extension grammar/type cases (parser/type system), 38
  trait/layout/constant cases (semantic facts), and 36 template lookup/demand cases.
- Run: 23/41 failures; 20 stop in the compile pipeline and 3 expose linked
  backend/lifetime or forced-inline behavior. PA34 is 227/367 overall.

## Active Checkpoint

No implementation is in flight. The next checkpoint should select one owner from the
remaining 43 extension grammar/type cases—block pointers, GNU asm, or retained scalar
vector layout—without conflating parser acceptance with backend behavior.

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

The atomic bitwise probe used three CAS-lowered updates per function. At 1/8/64
functions it emitted 38/304/2,432 LowIR and 89/712/5,696 MIR instructions exactly
proportionally; the 64-function object took 0.04 s and 16,464 KiB RSS. This supports
O(calls) checking/lowering; runtime retry work remains contention-dependent only.

The scalar floating probe used classification, finite/normal, and sign-bit calls per
function. At 1/8/64 functions it emitted exactly 411/3,288/26,304 native instructions;
objects were 40,784/317,160/2,528,640 bytes and took 0.00/0.01/0.07 s with
8,284/10,564/26,244 KiB RSS. This supports O(calls) semantic and fixed-graph lowering.

The repeated-empty-member layout probe used 1/8/64 potentially overlapping members.
It recorded exactly 1/8/64 layout-member visits and 2/9/65 zero-offset-subobject visits;
semantic elapsed time was 0.49/0.58/1.43 ms. This supports O(members + visited
zero-offset subobjects) placement with bounded reusable marker scratch.

## Completed Checkpoints

| Checkpoint | Result | Validation |
| --- | --- | --- |
| Hosted preprocessing boundary | CLI/include controls, host metadata, probes, directives, and literals; +51 | preprocess 45/45; PA34 121/367; prior/audit pass |
| Hosted builtin type queries | Registered traits/transforms, retained operands, structural constants, and member-pointer null adaptation; +46 | PA34 167/367; prior/audit pass |
| Hosted annotations | GNU aliases/int128 and declaration/type-id attributes; +20 | PA34 187/367; negatives and prior/audit pass |
| Integer bit intrinsics | Compact registry IDs, constexpr semantics, typed bit-network lowering, and native swaps; +8 | focused 8/8; PA34 195/367; prior/audit pass |
| Memory/string intrinsics | Shared probe/semantic registry, typed controls, identity/no-op lowering, effect metadata, staged ABI preservation, and libc object symbols; +9 | focused 8/8 plus GNU alignof; linked effects/symbols exact; PA34 204/367; PA1-33 4387/4387; audit pass |
| C11/GNU atomic and sync | Canonical `_Atomic`, 16-byte alignment, compact registry/typed operands, first-class atomic LowIR, packed class bridge, and bounded bitwise CAS graphs; +12 | focused 12/12; scalar/class linked behavior; exact 1/8/64 scaling; PA34 216/367; PA1-33 4387/4387; audit pass |
| Scalar floating builtins and abort | Sorted compact IDs, source-width classification, typed infinity/quiet/signaling NaNs, predicates, and nonreturning hosted `abort`; +6 | focused 6/6; exact abort ABI/effects; exact 1/8/64 instruction scaling; PA34 222/367; PA1-33 4387/4387; audit pass |
| Class layout attributes | Canonical GNU aligned/packed and standard no-unique facts, entity/binding ownership, conflict-safe empty overlap, and template replay; +5 | focused compile 5/5 and linked copy assignment; 1/8/64 linear markers; PA34 227/367; PA1-33 4387/4387; audit pass |

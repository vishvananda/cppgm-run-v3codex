# PA16 Plan

## Stage Design and Spec Alignment

PA16 extends the shared PA11/PA12 graph and PA15 typed LowIR path in place:
`class syntax -> canonical entity/binding facts -> resolved object expressions ->
typed object/address LowIR`. Class layout belongs to the canonical class entity;
member offsets belong to canonical member bindings; expression nodes carry the
selected binding; lowering consumes those IDs without name lookup or text
round-trips. This follows `spec.md` sections 2, 3, 6, 8, and 9: O(1) identity,
indexed lookup, recorded semantic facts, typed lowering, explicit ownership,
and work proportional to declared fields and emitted uses.

## Current Failure Map

Baseline was 26/243; the direct-member checkpoint reaches 38/243 (+12).
The complete 205 remaining failures divide at shared owners:

- 181 expected-success exit failures: missing class lookup in PA11/PA12
  (advanced layout, access, nesting, inheritance, overload/ADL,
  initialization/lifetime),
  then missing object/member/action lowering in PA15.
- 3 unexpected successes: semantic access/qualification rejection is absent.
- 21 LowIR diffs: partial facts reach lowering, but advanced layout, lifetime,
  operator selection, or required metadata differs from the oracle.

## Active Checkpoint

**Resolved member-call spine.** PA12 will make the object expression an explicit
overload input, gather only the selected class scope's indexed method set, rank
object cv plus explicit arguments, and retain the chosen binding/conversions on
the call node. PA15 will consume that call fact, prepend the lowered object
address only for non-static methods, and use the stored binding for emission;
lowering will not repeat lookup. This applies `spec.md` sections 2, 3, 6, and 9.

Ownership is class-scope/function binding IDs -> resolved PA12 call facts ->
typed PA15 call operands. Expected cost is O(candidates in the member overload
set + explicit arguments) per call and O(1) lowering after selection. Validate
simple/parenthesized/implicit/static calls, cv-overload cases, out-of-class
methods, all PA16 tests, through-PA15, audit, and a doubled overload-set curve.

## Performance Evidence

| Workload | Scale | Evidence |
|---|---:|---|
| One class layout plus one final-field use | 5k / 10k fields | 5,000 / 10,000 layout visits; 7.49 / 15.92 ms semantic; 0.03 / 0.06 s elapsed; 7 instructions at both sizes |

The exact visit count and near-2x time/memory curve show O(fields) layout;
lowering remains proportional to emitted member uses.

## Completed Checkpoints

| Checkpoint | Final state | Evidence |
|---|---|---|
| Direct-member object spine | Closed | Canonical size/alignment/offset facts, object globals/slots, `.`/`->` field projections, trivial ctor suppression at lowering, static arity fix; 38/243 PA16 (+12), 1,145/1,145 through PA15, clean audit |

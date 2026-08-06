# PA16 Plan

## Stage Design and Spec Alignment

PA16 extends the shared PA11/PA12 graph and PA15 typed LowIR path in place:
`class syntax -> canonical entity/binding facts -> resolved object expressions ->
typed object/address LowIR`. Canonical class entities own completion, size,
alignment, and constructor facts; canonical member bindings own class identity,
offset, static category, and default-member-initializer presence. Expression nodes
retain the selected binding, and lowering consumes that identity without lookup,
semantic reconstruction, or a text round trip.

The direct-member checkpoint aligns with `spec.md` sections 2, 3, 6, 8, and 9:
class/member equality is ID equality, class-scope lookup is indexed, layout is one
monotonic pass over owned members, the PA12 graph is borrowed synchronously by
typed lowering, and field projection is O(1) per resolved use. Audit fixes reject
duplicate definitions/bindings before facts are published and keep default-
construction conditions on their canonical semantic owners.

## Current Failure Map

The checked-in PA16 suite remains at its checkpoint baseline of 38/243. The full
PA16 report is 42/247 after adding four passing audit regressions. The 205 product
suite failures remain grouped by shared owner:

- 184 expected-success exit failures: incomplete PA16 class lookup, access,
  inheritance, overload/ADL, initialization, and lifetime semantics.
- 3 unexpected successes: missing access, overloaded-operator, and list-narrowing
  rejection.
- 18 LowIR differences: partial facts reach lowering, but advanced layout,
  lifetime, operator selection, or metadata is not complete.

## Performance Evidence

| Workload | Scale | Representative evidence |
|---|---:|---|
| One class plus final-field use | 5k / 10k fields | 5,000 / 10,000 layout visits; 7.91 / 16.49 ms semantic; 7 instructions at both sizes; 0.03 / 0.06 s elapsed |
| One field used repeatedly | 5k / 10k uses | 5,003 / 10,003 binding probes; 25,008 / 50,008 instructions; 19.87 / 39.26 ms semantic and 10.73 / 23.48 ms lowering; 0.07 / 0.13 s elapsed |

Exact work-counter scaling and near-2x time/output curves support O(fields) class
completion and O(1) typed field projection per use; no class-wide scan occurs in
lowering.

## Next Substantial Checkpoint

**Resolved member-call spine.** PA12 will make the object expression an explicit
overload input, gather only the selected class scope's indexed method set, rank
object cv plus explicit arguments, and retain the chosen binding and conversions
on the call node. PA15 will consume that call fact, prepend the lowered object
address only for non-static methods, and use the stored binding for emission.

Ownership is class-scope/function binding IDs -> resolved PA12 call facts -> typed
PA15 call operands. Expected cost is O(candidates in the member overload set plus
explicit arguments) per call and O(1) lowering after selection. Validate explicit,
parenthesized, implicit, and static calls; cv-overload and out-of-class cases; the
full PA16 report; through-PA15; file audit; and a doubled overload-set curve.

## Completed Checkpoints

| Checkpoint | Final state | Evidence |
|---|---|---|
| Direct-member object spine | Pass after checkpoint audit | Canonical size/alignment/offset and default-construction facts; typed `.`/`->` field projections; duplicate class/member rejection; 38/243 product baseline and 42/247 augmented report; 1,145/1,145 through PA15; linear field/use curves; file audit pass |

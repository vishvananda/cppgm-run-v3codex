# PA27 Plan

## Stage Design and Spec Alignment

PA27 extends the PA26 graph and typed-LowIR pipeline in place:

`structured syntax -> canonical owner/member type -> selected member/adjustment -> typed LowIR`

Per `spec.md` sections 2-4 and 6, member-pointer owners, member types, selected
bindings, base paths, and null state remain canonical facts; lowering does not
repeat lookup or parse rendered names. Sections 8-9 require work proportional
to visited syntax, type structure, candidates, and lowered nodes. Virtual-base
member pointers and multi-vptr RTTI remain outside PA27.

## Current Failure Map

The stage baseline was **19/96**, this checkpoint started at **60/96**, and the
current report is **74/96**. Canonical owner deduction, overloaded member
addresses, packs, and retained address demand now pass. The complete remaining
22-test set is:

| Tests | Shared behavior | Owner |
|---:|---|---|
| 9 | cv/ref and prvalue object propagation, derived adjustment, safe-bool branches, local-static initialization, and overloaded `->*` | PA12 value semantics and PA15 lowering |
| 7 | dependent target lookup, friend ADL, constructor NTTPs, inherited/ref-qualified SFINAE, and recollection | PA19-PA24 template lookup/deduction |
| 6 | NTTP specialization identity, access substitution, ref-qualified invocation, and deduplicated lowering demand | PA19-PA24 identity/SFINAE and PA15 demand |

## Active Checkpoint

**Member-pointer object semantics and runtime encoding (9 tests).** Preserve
cv/ref and value category through `.*`/`->*`, encode owner/base adjustment once,
and make contextual bool, local-static initialization, and indirect invocation
consume the same canonical value.

Owner and flow: PA12 publishes object category, selected binding, null state,
and explicit base adjustment; PA15 lowers those facts without lookup or name
reconstruction. Expected work is O(expression nodes plus traversed base edges),
with each address/application lowered once. Validate the nine grouped failures,
the PA27 report, through-PA26 report, and file audit.

## Performance Evidence

`CPPGM_FRONTEND_STATS=1` measurements on Linux x86_64 show proportional work:

| Witness | Small | Medium | Large |
|---|---:|---:|---:|
| declarators (64/128/256): parser storage | 18,578 B | 37,010 B | 73,874 B |
| overloads (32/64/128): candidate-index visits | 128 | 256 | 512 |
| overloads (32/64/128): deduction visits | 129 | 257 | 513 |

Doubling syntax or overload count approximately doubles storage and semantic
visit counters; target-directed replay performs one bounded pass over the
indexed overload set.

## Completed Checkpoints

| Checkpoint | Result | Validation |
|---|---|---|
| Non-virtual multi-base projection and single-vptr `dynamic_cast<void*>` | explicit offsets, qualified lookup, pack bases, and all-base generated lifecycle | `100-*` 17/17; PA27 30/96; through PA26 3717/3717 |
| Member-pointer syntax, canonical values, and ordinary runtime application | structured owner parsing; 64/128-bit null/address values; base conversion; contextual bool; `.*`/`->*`; indirect calls; data-member NTTPs | PA27 60/96; focused LowIR checks pass; through PA26 3717/3717; file audit pass |
| Dependent owner deduction, target replay, and retained address demand | owner TypeIds traverse substitution/partial matching; candidate-targeted overloaded addresses; pack deduction; runtime symbol demand | PA27 74/96; 7 focused regressions pass; scaling 32/64/128 linear in candidate visits; file audit pass |

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

The stage baseline was **19/96**, the checkpoint baseline was **30/96**, and the
current report is **60/96**. All `100-*` tests and ordinary member-pointer
syntax, null, conversion, `.*`, `->*`, invocation, and NTTP execution paths now
pass. The complete remaining 36-test set is:

| Tests | Shared behavior | Owner |
|---:|---|---|
| 11 | member-pointer cv/ref propagation, prvalue objects, safe-bool branches, local-static initialization, and overloaded `->*` | PA12 value semantics and PA15 lowering |
| 16 | dependent owner/member replay, lookup, target-directed overload resolution, packs, and function-template deduction | PA19-PA24 template lookup/deduction |
| 9 | NTTP specialization identity, substitution failure, ref-qualified invocation, and deduplicated demand | PA19-PA24 argument identity/SFINAE and PA15 demand |

## Active Checkpoint

**Dependent member-pointer substitution, deduction, and NTTP identity (25
tests).** Preserve the canonical owner/child type and selected binding through
dependent replay, make failures candidate-local, and use binding plus encoded
adjustment as the non-type argument identity.

Owner and flow: PA19-PA24 substitute structured owner/member atoms, perform
lookup and target-directed overload selection, form one `TemplateArgument`,
then publish it to instantiated PA12 expressions and PA15 demand. Expected
work is O(substituted type nodes plus viable candidates), with canonical type
and specialization tables preventing whole-program retries. Validate all 25
dependent/NTTP failures, the PA27 report, through-PA26 report, and file audit.

## Performance Evidence

`CPPGM_FRONTEND_STATS=1` measurements on Linux x86_64 show proportional work:

| Witness | 64 | 128 | 256 |
|---|---:|---:|---:|
| member-pointer declarators: tokens | 779 | 1547 | 3083 |
| median parse time (5 runs) | 0.217 ms | 0.395 ms | 0.715 ms |
| parser storage | 18,578 B | 37,010 B | 73,874 B |
| wide base edges: lookup / special-member visits | 192 / 512 | 384 / 1024 | 768 / 2048 |

Doubling syntax or base edges approximately doubles time, storage, and visit
counters; the member-pointer parser witness performed no template scan.

## Completed Checkpoints

| Checkpoint | Result | Validation |
|---|---|---|
| Non-virtual multi-base projection and single-vptr `dynamic_cast<void*>` | explicit offsets, qualified lookup, pack bases, and all-base generated lifecycle | `100-*` 17/17; PA27 30/96; through PA26 3717/3717 |
| Member-pointer syntax, canonical values, and ordinary runtime application | structured owner parsing; 64/128-bit null/address values; base conversion; contextual bool; `.*`/`->*`; indirect calls; data-member NTTPs | PA27 60/96; focused LowIR checks pass; through PA26 3717/3717; file audit pass |

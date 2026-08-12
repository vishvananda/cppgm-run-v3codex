# PA27 Plan

## Stage Design and Spec Alignment

PA27 extends the PA26 graph and typed-LowIR pipeline in place:

`direct-base/type edges -> selected conversion/member facts -> typed LowIR`

Per `spec.md` sections 2-4 and 6, declarations, types, base edges, and selected
conversions remain canonical identities; lowering consumes offsets and member
targets already selected by semantics. Sections 8-9 require compact facts and
work proportional to visited syntax, base edges, candidates, and lowered nodes.
Virtual inheritance and multi-vptr RTTI remain outside PA27.

## Current Failure Map

The turn-start baseline was **19/96**; the current report is **30/96**. All 17
`100-*` tests pass. The complete remaining 66-test set is:

| Tests | Shared behavior | Owner |
|---:|---|---|
| 20 | pointer-to-member declarator/expression grammar (`expected parameter/call/binary operand`) | PA10 parser and declarator-shape facts |
| 22 | member-pointer object application, address/null/conversion, contextual bool, and invocation | canonical type/conversion model; PA12 operators/calls |
| 22 | dependent member-pointer types, NTTPs, lookup, deduction, SFINAE, and specialization state | PA19-PA24 template semantics |
| 2 | duplicate/unwanted function emission after otherwise successful member-pointer analysis | PA15-PA18 demand and lowering |

## Active Checkpoint

**Member-pointer syntax and canonical type-formation boundary (20 parser
failures).** Accept pointer-to-member declarators in parameters, aliases,
casts, template arguments, packs, and nested function types, preserving owner,
pointee function cv/ref qualifiers, and dependency as structured facts.

Owner and flow: PA10 parses the declarator/operator shape once; PA12 declarator
analysis interns `TYPE_MEMBER_POINTER(owner, child)` without rendered-name
reparsing; PA19-PA24 retain dependent owner/child atoms for later substitution.
Expected work is O(declarator syntax plus interning probes), with no candidate
scan until overload resolution. Validate the complete 20-test parser group,
the PA27 report, through-PA26 report, file audit, and nested/pack scaling cases.

## Performance Evidence

`CPPGM_FRONTEND_STATS=1` generated inheritance witnesses (Linux x86_64,
wall time rounded to 0.01 s) show proportional work:

| Shape / edges | 64 | 128 | 256 |
|---|---:|---:|---:|
| wide: time / lookup-edge visits / special-member visits | .01 s / 192 / 512 | .01 s / 384 / 1024 | .02 s / 768 / 2048 |
| deep: time / lookup-edge visits / special-member visits | .01 s / 128 / 260 | .01 s / 256 / 516 | .01 s / 512 / 1028 |

Peak RSS ranged from 7.0 to 8.1 MiB. Doubling direct-base edges doubles the
relevant counters; no whole-program retry or rendered-name lookup was observed.

## Completed Checkpoints

| Checkpoint | Result | Validation |
|---|---|---|
| Non-virtual multi-base projection and single-vptr `dynamic_cast<void*>` | all 11 baseline `100-*` failures fixed; explicit path offsets, null-preserving adjustment, qualified lookup, pack base initialization, and all-base generated lifecycle traversal | `100-*` 17/17; PA27 30/96; through PA26 3717/3717; file audit pass |

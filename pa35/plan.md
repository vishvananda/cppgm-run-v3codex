# PA35 Plan

## Stage Design and Spec Alignment

PA35 extends the existing source -> streaming preprocessing/post-tokenization ->
integrated syntax/semantics -> typed LowIR -> native ELF path; it adds no
hosted-only compiler route.  The relevant `spec.md` requirements are linear
preprocessing/parsing (§1, §9), one canonical typed fact flow into lowering
(§2, §6), demand-scoped template work (§4), and observable work counters for
heavy-header scaling (§9).  The stage owner remains the shared front end and
its existing translation-unit/per-phase storage.

## Current Failure Map

Turn-start baseline: 6/104 overall (the PA-local compile report is 6/103).
The completed checkpoint raises the PA-local report to 15/103.  The remaining
88 failures group by first diagnostic and owner: using merge 19; attributed enum
syntax 17; constant-expression formation 17; dependent nested-type lookup 15;
explicit-template member-call syntax 8; legacy adapter lookup 6; allocator
corruption 2; dependent pack lookup 2; and four singleton semantic/lowering
cases.  These are shared boundaries, not individual-header workarounds.

## Active Checkpoint

**Hosted front-end ingress — completed.**  `MacroProcessor` now maps GNU named
variadics onto the ordinary variadic argument tail and carries builtin probe
operators through replacement into one post-expansion condition rewrite.
Post-tokenization accepts syntactically complete target `_Float128` spellings;
speculative type-id parsing rolls back failed abstract declarators; and hosted
builtin type operands and class/union/destructor traits remain typed facts.

At template demand boundaries, incompatible dependent conditional/unary shapes
carry one canonical dependent result until specialization, while parameter-
dependent `noexcept` is evaluated in the specialization's function scope.  Data
continues through the existing parser/semantic/LowIR path.  Work is linear in
directive, argument, condition, and retained syntax size, with O(1) added state
per macro or retained expression; specialization performs the exact reanalysis.

## Performance Evidence

Wrapped-probe stress at 2,000/4,000 directives produced 18,000/36,000 macro
lookups, 4,000/8,000 builtin probes, 27.1/52.4 ms preprocessing time, and
4,852/4,860 KiB peak RSS.  Doubling variable input doubled counted work, took
1.94x time, and left peak line/rescan storage fixed at 18/12 tokens.

## Completed Checkpoints

| Checkpoint | Result | Validation |
|---|---|---|
| Hosted front-end ingress | PA35 6 -> 15/103 | focused utility/probes pass; PA1-34 4756/4756; audit pass |

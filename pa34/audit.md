# PA34 Checkpoint Audit

## Current Checkpoint Review

Scope: the trait-pack and assignment increment landed in `e8c174e9`. The
review covered the PA34 contract, `spec.md`, the checkpoint diff, its shared
pack-expansion, overload-resolution, special-member, and trait owners, focused
tests, the source-to-ELF trace, and the turn-start 292/367 handout baseline.

The increment keeps pack replay at template substitution: each concrete pack
element is rebuilt once in its ordered element scope and becomes a canonical
`TypeId`. Assignment traits build typed hypothetical operands, use indexed
member/function-template lookup and the shared overload/conversion engine, and
publish only a specialization-owned Boolean constant. A representative member
assignment template records 3 specialization requests (1 cache hit), 8
overload candidates, 1 demanded body, 10 LowIR and 12 MIR instructions, and a
6,904-byte ELF64 relocatable. No source spelling, rendered type, textual LowIR,
whole-program retry, host compiler, or test identity enters this path.

Two declaration-owner defects were found and closed. An assignment operator
defaulted after its first declaration was incorrectly classified as trivial
and inherited its subobjects' implicit nonthrowing fact, although it is
user-provided and its first declaration's exception specification controls.
Also, a non-special assignment operator such as `operator=(int) = default` was
accepted. `FunctionInfo` now retains the user-provided fact; trait semantics
uses it while synthesized body generation separately retains the valid bulk
copy optimization. Declaration configuration now rejects defaulting a
non-special assignment operator. Two course regressions cover copy/move,
trivial/nothrow results, and invalid defaulting; no fixture was weakened.

Representative 1/8/64 unique pack-plus-assignment workloads recorded
5/40/320 overload candidates, 8/64/512 specialization requests,
13/69/517 semantic nodes, and 90,239/467,809/3,661,927 peak semantic bytes.
Semantic time was 0.690/2.400/16.499 ms, RSS 8,264/8,680/10,060 KiB, and
backend output stayed at one function, one LowIR instruction, five MIR
instructions, and 1,792 bytes. Thus 8->64 gives exact 8x candidate/request
work, 7.49x nodes, 7.83x storage, and 6.87x semantic time; no unexplained
superlinear or timeout path remains in the landed ownership chain.

Validation: 22/22 landed-surface handout tests and 2/2 audit regressions pass;
the PA17 out-of-class-defaulted LowIR contract passes; PA34 is 294/369 with the
same 292/367 handout result; PA1-PA33 are 4,387/4,387; and the PA34 file audit
passes with 22 inherited nonfatal header-division warnings.

## Durable Architecture Decisions

- Retained trait packs expand only in specialization element scopes and publish
  canonical ordered operand identities.
- Assignment traits reuse indexed lookup, overload, conversion, access, and
  completed special-member facts; lowering consumes only the resolved constant.
- Language triviality/nonthrowing facts are distinct from synthesized-body
  storage-copy choices, so an optimization cannot redefine a type trait.

## Checkpoint Audit Ledger

| Checkpoint | Audit result |
| --- | --- |
| Trait packs and assignment (`e8c174e9`) | Pass after closing user-provided/defaulting ownership; focused 24/24, PA34 handout 292/367 preserved, PA1-PA33 and file audit pass, proportional 1/8/64 evidence. |

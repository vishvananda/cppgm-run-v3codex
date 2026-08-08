# PA17 Audit

## Current Checkpoint Review

**Checkpoint:** `f28a83f9` (`Implement PA17 constructor delegation`)

**Result:** Pass after audit fixes. The landed increment is bounded to
delegating constructors, out-of-class constructor/destructor definitions, and
qualified default completion. Composite copy/move subobject transfer remains
the next checkpoint.

The ownership path is source special-member syntax -> the class declaration's
canonical `BindingId` and completed `FunctionInfo` -> selected constructor and
conversion facts -> one normalized complete-constructor delegation edge and
typed delegation action -> demand-keyed complete/base ABI entries -> the
existing `%this` destination -> typed LowIR. Default completion uses canonical
member/layout/destructor identities at the same declaration owner. Lowering
does no lookup, signature rendering, semantic reconstruction, or text round
trip.

Audit findings are closed:

1. A qualified `= default` definition could be accepted when it was not a
   valid special-member signature or when completion made it deleted. The
   deleted fact disabled demand, so an unused ill-formed constructor or
   assignment escaped; defaulted union destructors also skipped member
   destruction and escaped. Qualified constructor/assignment definitions now
   validate the implicit signature and reject a deleted completion
   immediately. Defaulted destructor completion visits canonical base/member
   destructor facts once and records deletion, destructibility, and the
   conservative nonthrowing fact. Six focused regressions cover wrong
   signatures and deleted default constructor, move constructor, move
   assignment, and union-destructor definitions.
2. Delegation normalizes complete/base ABI identities before recording one
   selected edge, rejects mixed initializers and direct/indirect cycles, and
   lowers the retained typed action into existing storage. The graph walk is
   bounded by the indexed constructor set, demand states suppress duplicate
   emission, and no retry, global invalidation, or fallback path is present.
3. The dedicated PA17 completion source remains registered in the compiler
   source set and below the file-audit limit. The changed path contains no
   test-name, reference-binary, host-compiler, subprocess, timeout, or cached
   output shortcut.

Representative 32/64/128-link delegation probes recorded 32/64/128 actions,
66/130/258 demand pushes, 296/584/1,160 instructions, and
130,531/258,787/515,504 typed bytes. Five-run semantic medians were
3.073/8.948/29.744 ms. Candidate visits (6,732/25,740/100,620) and conversions
(9,770/37,962/149,642) grow roughly fourfold because each same-arity link must
inspect the full overload set; actions, demand, IR, storage, and output remain
linear. Defaulted-destructor probes at 32/64/128 nontrivial members recorded
64/128/256 complete/base actions, 224/448/896 access checks, and semantic
medians of 0.228/0.363/0.572 ms, with proportional storage.

Validation:

- `make test-report ACTIVE_TEST_REPORT_PAS='pa17'`: expected incomplete-stage
  failure, 199/239. The landed 193/233 pass set and all six audit regressions
  pass; the same 40 residual PA17 tests fail.
- Required through-stage command: PA1-PA16 pass 1,436/1,436.
- `perl scripts/cppgm_file_audit.pl --stage pa17 --paths dev/src`: pass with
  the same ten header-division warnings and no fatal issue.
- Focused landed and audit coverage passes 15/15; source/ownership audit finds
  canonical compact keys, typed demand/action facts, and no semantic or textual
  fallback.

## Checkpoint Audit Ledger

| Checkpoint | Audit result | Closure evidence |
|---|---|---|
| Ref-qualified member identity and selection | Pass after audit fixes | Canonical declaration/call/ABI path; complete mixed-set key; correct object ranking; focused, baseline, scaling, and required gates pass |
| Branch-local class values and full-expression cleanup | Pass after audit fixes | Typed construction state on normal/unwind exits; exact flat and linked cleanup reuse; 173/231 baseline intact; linear probes and required gates pass |
| Loop full-expression regions | Pass after audit fixes | Typed discarded materialization; bounded-inline and linked-suffix cleanup; 174/231 baseline, linear probes, and required gates pass |
| Class direct-initialization recipes | Pass after audit fixes | Canonical list conversions and selected constructor are reused; original pass set, audit regressions, proportional probes, and required gates pass |
| Typed constructor delegation and qualified default completion | Pass after audit fixes | Canonical declaration/complete-constructor identities and typed action reuse; invalid/deleted defaults rejected; 193/233 baseline, six regressions, proportional probes, and required gates pass |

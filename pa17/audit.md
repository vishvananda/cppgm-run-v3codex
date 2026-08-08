# PA17 Audit

## Current Checkpoint Review

**Checkpoint:** `2e42c865` (`Implement PA17 branch-local temporary cleanup`)

**Result:** Pass after audit fixes. The landed increment is bounded to typed
class-conditional arms, temporary materialization across conditional and
short-circuit CFG, full-expression destruction, and return/member destinations.
Loop condition and iteration re-evaluation remain the next checkpoint; their
existing failure is not hidden by this closure.

The ownership path is source expression -> PA12 typed expression and stable
temporary node identity -> ordered destructor actions carrying selected binding
and canonical `TypeId` -> PA17 function-local cleanup state and CFG -> typed
LowIR. PA12 now records whether a temporary is conditionally constructed while
walking only the relevant expression subtree. Lowering uses compact IDs and
flat function-local tables to reset and mark construction, and all normal and
unwind exits consume the same typed action chain. It performs no name lookup,
rendered-signature recovery, or text round trip.

Audit findings are closed:

1. Normal completion destroyed the entire semantic suffix, although the
   constructed-prefix filter was applied only to unwind dispatch. A temporary
   in the right operand of `&&` was therefore destroyed on the short path.
   Region-owned runtime state now guards each potentially path-dependent
   temporary and is cleared before its destructor call.
2. Expression statements and scalar returns bypassed the cleanup region and
   had the same unconditional-destruction defect. They now use the same
   action-owned region as declarations and control conditions; class arm-local
   destinations remain branch-local.
3. Cleanup reuse used heap-owned vector keys and repeatedly walked every
   constructed prefix. A 64-temporary probe exposed 4,098 dispatch probes for
   65 entries. Ordinary semantic-action sequences now use an exact flat cache
   with contiguous key slices, while path-dependent regions intern one linked
   dispatch node per action. The final probe records 64 probes and 64 entries.
4. Audit instrumentation initially pushed `pa15_lowering.cpp` over its file
   limit. Lifetime-slot and reset ownership moved into the PA17 lowering mixin;
   the implementation file is 2,999 lines and the stage file audit passes.

Representative release probes with 16/32/64 conditionally evaluated right-hand
temporaries recorded 16/32/64 lifetime slots, marks, dispatch probes, and
entries; 171/331/651 blocks; 619/1,211/2,395 instructions; and
30,359/59,815/119,238 output bytes. Five 100-compile batch medians were
0.43/0.50/0.65 seconds, with 7.2-7.9 MiB peak RSS. Work, storage, CFG, and output
are proportional to obligations rather than constructed-prefix products.

Validation:

- Focused checked-in branch-lifetime set: 4/4 pass. Skipped and constructed
  right-hand temporary probes at condition, discarded-expression, and scalar
  return boundaries compile successfully and emit guarded cleanup.
- `make test-report ACTIVE_TEST_REPORT_PAS='pa17'`: expected full-stage
  failure, 173/231. The checkpoint pass set is intact.
- `make test-report-through-pa16`: 1,436/1,436 and 16/16 stages pass.
- `perl scripts/cppgm_file_audit.pl --stage pa17 --paths dev/src`: pass with
  the same ten turn-start header-division warnings and no fatal issue.
- Valgrind reports no errors or definite/indirect leaks on the right-hand
  temporary probe. A process-only trace contains the compiler
  `execve` and `exit_group(0)` only, with no child process or external tool.

## Checkpoint Audit Ledger

| Checkpoint | Audit result | Closure evidence |
|---|---|---|
| Ref-qualified member identity and selection | Pass after audit fixes | Canonical declaration/call/ABI path; complete mixed-set key; correct object ranking; focused, baseline, scaling, and required gates pass |
| Branch-local class values and full-expression cleanup | Pass after audit fixes | Typed construction state on normal/unwind exits; exact flat and linked cleanup reuse; 173/231 baseline intact; linear probes and required gates pass |

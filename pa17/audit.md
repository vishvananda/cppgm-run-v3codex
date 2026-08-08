# PA17 Audit

## Current Checkpoint Review

**Checkpoint:** `12ac2a13` (`Implement PA17 class direct initialization`)

**Result:** Pass after audit fixes. The landed increment is bounded to direct
class construction through casts, braced/default arguments, member/base
initializers, returns, and their existing temporary, reference, ABI, and return
slot recipes. Delegating constructors and out-of-class special-member
definitions remain the next checkpoint.

The ownership path is source initializer syntax -> one PA12 analysis of each
list clause -> canonical `TypeId` list-conversion facts and selected
`BindingId` plus `CallConversionFact` records -> typed constructor, temporary,
lifetime, and return actions -> PA15-PA17 destination storage and ABI result
slots -> typed LowIR. Lowering consumes those identities directly; it does not
repeat lookup, reconstruct conversion facts from names, or use a text round
trip.

Audit findings are closed:

1. The landed braced-list viability check ranked each candidate as though its
   own parameter type were the source. Consequently `X({1})` was ambiguous for
   `X(int)` and `X(double)`, and shape-only acceptance omitted narrowing.
   PA12 now computes the actual recursive list-initialization sequence,
   including scalar rank, aggregate/class conversion, explicit copy-list
   rejection, and arithmetic narrowing. Focused rank and narrowing regressions
   cover both sides of the defect.
2. Candidate checking could reanalyze list clauses and rerun nested constructor
   selection; expected nested rejection also escaped through exceptions. A
   constructor-operation context now owns flat `(NodeId, TypeId)` conversion
   tables, separate direct/copy selection tables, explicit in-progress state,
   and the selected conversion facts. Clauses are prepared once, nested
   rejection returns `kNoBinding`, and the common one/two-conversion selection
   record stays inline.
3. Moving constructor selection and action construction into the dedicated
   list-initialization source keeps the existing source files within the audit
   limit. The new source is registered in the per-tool source set and carries
   no test, reference-binary, host-compiler, subprocess, or timeout shortcut.

Representative 32/64/128-candidate braced-list probes recorded
236/460/908 candidate visits, 240/464/912 conversion checks, 1 cache hit and
36/68/132 cache misses, and 261,656/519,736/1,035,952 peak semantic bytes.
Five-run semantic medians were 1.115/2.065/4.029 ms; lowering medians were
0.105/0.105/0.134 ms with fixed 2,990 typed bytes and 262 output bytes. Work
and semantic storage are proportional to the required candidate/list facts,
while the retained destination recipe is fixed. A side-effecting list-clause
probe contains exactly one LowIR call to its source function.

Validation:

- `make test-report ACTIVE_TEST_REPORT_PAS='pa17'`: expected incomplete-stage
  failure, 188/233. The original 186/231 checkpoint pass set is unchanged and
  both audit regressions pass; the same 45 residual PA17 tests fail.
- Required through-stage command: PA1-PA16 pass 1,436/1,436.
- `perl scripts/cppgm_file_audit.pl --stage pa17 --paths dev/src`: pass with
  the same ten header-division warnings and no fatal issue.
- Source and ownership audit finds compact canonical keys, bounded local
  caches, recorded selected conversions, and no semantic or textual fallback.

## Checkpoint Audit Ledger

| Checkpoint | Audit result | Closure evidence |
|---|---|---|
| Ref-qualified member identity and selection | Pass after audit fixes | Canonical declaration/call/ABI path; complete mixed-set key; correct object ranking; focused, baseline, scaling, and required gates pass |
| Branch-local class values and full-expression cleanup | Pass after audit fixes | Typed construction state on normal/unwind exits; exact flat and linked cleanup reuse; 173/231 baseline intact; linear probes and required gates pass |
| Loop full-expression regions | Pass after audit fixes | Typed discarded materialization; bounded-inline and linked-suffix cleanup; 174/231 baseline, linear probes, and required gates pass |
| Class direct-initialization recipes | Pass after audit fixes | Canonical list conversions and selected constructor are reused; original pass set, audit regressions, proportional probes, and required gates pass |

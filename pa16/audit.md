# PA16 Audit

## Current Checkpoint Review

**Checkpoint:** `790bc91a` (`Implement PA16 constructor initialization spine`)

**Result:** Pass after audit fixes. The landed increment is bounded to stable
constructor declarations/candidate sets, direct/copy/list/default construction,
declaration-ordered direct-member/default-member actions, and their typed calls
and stores. Base construction, destruction, arrays of class objects, and global
lifetime remain assigned to later checkpoints.

The ownership path is initializer syntax with an interned initialization mode ->
canonical class `EntityId`, constructor `BindingId` candidate sequence, and data-
member declaration ordinal -> selected constructor/member action nodes -> typed
subobject addresses, calls, and stores. Selection and target lookup occur once in
semantic construction. Lowering consumes only typed IDs and recorded conversions;
it does not rerun lookup, reconstruct from names, or serialize/reparse LowIR.

Audit findings are closed:

1. Deleted constructors were discarded before overload resolution, every member
   specifier was mistaken for `explicit`, and copy-list initialization did not
   distinguish selection from the final explicit-constructor rejection. Deleted
   candidates now participate, `explicit` is its own canonical fact, and the
   recorded copy/list modes drive the required selection and rejection rules.
2. User-provided constructors could leave reference or const-scalar members
   uninitialized. Every demanded constructor now walks its owning member sequence
   in declaration order and rejects those missing required actions. A compact
   canonical member ordinal scopes reusable initializer scratch to the class;
   candidate sequences are borrowed, so neither path copies or sizes work by
   unrelated translation-unit declarations.
3. Every defaulted constructor was labeled nonthrowing. Generated initialization
   actions are now scanned by stable callee binding and only proven nonthrowing
   bodies publish `unwind=no`; potentially throwing DMIs remain conservative.
4. Nested constructor aggregate lowering stored reference values instead of
   addresses and replayed each complete member path per leaf. Reference actions
   now store typed identity. Eight-entry inline paths retain a typed address at
   bounded depth, preserving shallow LowIR while reducing representative 40x40 /
   80x80 cases from 1,765 / 6,725 to 127 / 247 instructions and from 248,820 /
   986,100 to 18,420 / 33,780 typed bytes.
5. The new implementation-heavy constructor header triggered the file-division
   advisory. ABI-fact adaptation now has a normal `.cpp` owner, generic array
   lowering returned to its PA15 owner, and the constructor header is below the
   implementation-body threshold. The new source is in the `cppgm++` source set.

Validation:

- Focused existing plus audit set: 14/14 pass. The seven audit regressions cover
  deleted/explicit selection, required reference/const initialization, nested
  reference identity, and generated exception metadata.
- `make test-report ACTIVE_TEST_REPORT_PAS='pa16'`: expected full-stage failure,
  91/255. All 84/248 checkpoint-entry passes remain and all seven added audit
  regressions pass; the 164 previously mapped later-checkpoint failures remain.
- `make test-report-through-pa15`: 1,145/1,145 and 15/15 stages pass.
- `perl scripts/cppgm_file_audit.pl --stage pa16 --paths dev/src`: pass. The
  checkpoint-owned header warning is closed; the one remaining advisory is the
  pre-existing declaration-weight warning in `pa11_model.h`.
- Constructor member-action counters are exactly 1,000/2,000 for 1k/2k members
  and remain 1/1 beside 5k/10k unrelated globals. Constructor candidate visits
  are exactly 1,001/2,001 with 1,004/2,004 conversion checks and constant 10-
  instruction output.
- Valgrind on the nested-reference regression reports no errors or leaks.
  Process tracing contains only the compiler `execve` and `exit_group(0)`.

## Checkpoint Audit Ledger

| Checkpoint | Audit result | Closure evidence |
|---|---|---|
| Direct-member object spine | Pass after audit fixes | One-shot class completion, non-mergeable member IDs, sound implicit-construction conditions, typed field projection, linear curves, and all checkpoint gates preserved |
| Local aggregate-action spine | Pass after audit fixes | C++11 user-provided eligibility, one union active member, borrowed edge cursor, bounded typed projection reuse, 61/248 PA16 with no losses, and all audit gates preserved |
| Special-member initialization action spine | Pass after audit fixes | Init-mode-correct constructor selection, canonical member ordinals/actions, truthful exception facts, typed nested references, bounded projections, 91/255 with no existing loss, and all audit gates preserved |

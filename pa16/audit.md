# PA16 Audit

## Current Checkpoint Review

**Checkpoint:** `9651d43c` (`Implement PA16 direct member object spine`)

**Result:** Pass after audit fixes. The landed increment is bounded to ordinary
non-static data-member layout, default-construction classification needed by that
layout, object storage, and resolved `.` / `->` field projection. The remaining
PA16 full-stage failures belong to later layout, lookup/access, member-call,
initialization, lifetime, inheritance, and operator checkpoints.

The complete ownership path is source/class syntax -> canonical `EntityId` and
`BindingId` facts in the PA11 program -> binding-carrying PA12 member expressions
-> typed PA15 object storage and field-projection instructions. Class entities own
one final size/alignment/completion state, member bindings own offsets and
initializer presence, and the translation-unit semantic graph is borrowed only
during synchronous lowering. Class-scope lookup is indexed; lowering performs no
name lookup, whole-class scan, semantic fallback, serialization, or text parse.

Audit findings are closed:

1. A second class definition could append members while reusing the first final
   layout. Definition completion now rejects an already-complete canonical entity
   before mutating its member scope.
2. Duplicate data members could flow through generic variable-redeclaration
   canonicalization and receive separate layout slots. Class-member insertion now
   checks the direct class index and creates a non-mergeable canonical binding.
3. The new default-constructor facts treated uninitialized reference and const
   scalar members as usable and could suppress classes with default member
   initializers as trivial. Bindings now retain initializer presence, and the one
   layout visit derives reference/const/subobject construction facts without a
   second member scan.

Representative evidence supports the material complexity claims. A 5k/10k-field
class produced exactly 5,000/10,000 layout visits, 7 instructions at both scales,
and 7.91/16.49 ms semantic time. A one-field class with 5k/10k member uses produced
5,003/10,003 binding probes, 25,008/50,008 instructions, 19.87/39.26 ms semantic
time, and 10.73/23.48 ms lowering time. Elapsed time was 0.03/0.06 s and 0.07/0.13
s respectively.

Validation:

- Focused original and audit regression set: 12/12 pass.
- `make test-report ACTIVE_TEST_REPORT_PAS='pa16'`: expected full-stage failure,
  42/247 with four passing audit regressions; the original 38/243 checkpoint
  baseline is intact.
- `make test-report-through-pa15`: 1,145/1,145 and 15/15 stages pass.
- `perl scripts/cppgm_file_audit.pl --stage pa16 --paths dev/src`: pass. Its one
  pre-existing declaration-weight warning names `pa11_model.h`; inspection shows
  record/API declarations rather than implementation bodies, so it is not an
  ownership defect in this increment.
- Valgrind on nested direct-member projection reports no memory errors or definite/
  indirect leaks. A process-only `strace` contains the compiler `execve` and
  `exit_group(0)` only, with no child process or external tool.

## Checkpoint Audit Ledger

| Checkpoint | Audit result | Closure evidence |
|---|---|---|
| Direct-member object spine | Pass after audit fixes | One-shot class completion, non-mergeable member IDs, sound implicit-construction conditions, typed field projection, linear curves, and all checkpoint gates preserved |

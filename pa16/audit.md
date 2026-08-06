# PA16 Audit

## Current Checkpoint Review

**Checkpoint:** `76cd7bd0` (`Implement PA16 aggregate initialization actions`)

**Result:** Pass after audit fixes. The landed increment is bounded to local
aggregate eligibility and ordered initialization actions for direct scalar,
reference, union, and recursively aggregate class members. Array-member/global
aggregate lowering and non-aggregate special-member/lifetime actions remain in
later PA16 checkpoints and account for known full-stage failures.

The ownership path is source braced-list edges -> canonical class `EntityId`
eligibility plus declaration-ordered member `BindingId`s -> PA12 initializer
actions carrying those binding identities -> PA15 typed projections and stores.
Brace elision now advances the borrowed syntax-edge cursor directly rather than
copying initializer nodes. Shallow projection paths stay in an eight-entry inline
sequence for stable LowIR presentation; deeper lowering retains the typed
subobject address, so each remaining action projects once. Lowering performs no
lookup, constructor reselection, name-based recovery, or text transport.

Audit findings are closed:

1. Aggregate eligibility used "no user-declared constructor" instead of the
   C++11 "no user-provided constructor" rule. Canonical class facts now distinguish
   the two monotonically, so first-declaration `= default` and `= delete`
   constructors preserve aggregate eligibility while a body or ordinary
   declaration does not.
2. A union action list visited every variant, causing omitted zero-initialization
   to overwrite the selected first member. Union planning now emits at most that
   one active-member action and rejects remaining initializer elements normally.
3. Every nested leaf replayed its complete root path and the path used a heap
   vector. A representative 100-depth/100-leaf versus 200-depth/200-leaf pair
   produced 10,302/40,602 instructions, 1,967,706/7,865,946 typed bytes, and
   4.49/19.79 ms lowering. Inline bounded replay plus retained typed addresses
   reduces this to 303/603 instructions, 63,066/124,506 typed bytes, and
   0.22/0.31 ms; semantic nodes remain proportional at 408/808.

Validation:

- Focused aggregate/spec/audit set: 7/7 pass, including the checked union
  regression and the existing defaulted/deleted-constructor cases.
- `make test-report ACTIVE_TEST_REPORT_PAS='pa16'`: expected full-stage failure,
  61/248. Relative to the turn-start 58/247 baseline, exactly two existing tests
  were repaired, one passing audit regression was added, and no prior pass was
  lost.
- `make test-report-through-pa15`: 1,145/1,145 and 15/15 stages pass.
- `perl scripts/cppgm_file_audit.pl --stage pa16 --paths dev/src`: pass. The one
  pre-existing declaration-weight warning names `pa11_model.h`; this increment
  adds only a fact field there. Generic literal decoding moved to the existing
  lowering-support owner, leaving `pa15_lowering.cpp` below the file-size gate.
- Valgrind on the union regression reports zero errors and no live blocks.
  Process tracing contains only the compiler `execve` and `exit_group(0)`.

## Checkpoint Audit Ledger

| Checkpoint | Audit result | Closure evidence |
|---|---|---|
| Direct-member object spine | Pass after audit fixes | One-shot class completion, non-mergeable member IDs, sound implicit-construction conditions, typed field projection, linear curves, and all checkpoint gates preserved |
| Local aggregate-action spine | Pass after audit fixes | C++11 user-provided eligibility, one union active member, borrowed edge cursor, bounded typed projection reuse, 61/248 PA16 with no losses, and all audit gates preserved |

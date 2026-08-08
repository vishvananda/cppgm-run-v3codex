# PA17 Audit

## Current Checkpoint Review

**Checkpoint:** `8e3c7cdc` (`pa17: close value-category reference binding`)

**Result:** Pass after audit fixes. The landed increment is bounded to the seven
planned value-category/reference-binding cases and two adjacent gains in
brace-temporary materialization and shadowed-local cleanup. The remaining
23-test failure map is unchanged; canonical lookup/candidate identity is the
next substantial checkpoint.

The complete ownership path is source expression -> PA12 `ExpressionInfo`
with canonical `TypeId`, `ValueCategory`, and `BindingId` -> retained
`Conversion`/`CallConversionFact` and selected overload binding -> typed cast,
base projection, materialization, or lifetime action -> PA15-PA17 direct typed
LowIR. Member-call analysis preserves xvalues, reference binding materializes
only prvalues, class by-value staging consumes the retained base projection,
and array references remain references rather than being reclassified as array
objects. Empty inline destructor chains are elided only after accessibility is
checked and never for array objects. No template or ELF owner is introduced at
this PA17 boundary.

Audit findings are closed:

1. Standard derived/base conversion repeatedly walked the full direct-base
   chain while ranking each viable candidate, and one reference conversion
   queried the same relationship twice. This made a valid inheritance-overload
   family quadratic. `Program` now owns a compact canonical ancestry index:
   depth/access-prefix facts and variable-width binary-lifting rows are built
   once with the direct-base edge. Public ancestry and distance use that index;
   non-public paths retain the complete private/protected/friend context walk.
   Conversion computes the relationship once and reuses it for reference
   relatedness and ranking.
2. The landed semantic and lowering changes consume canonical types,
   categories, selected declarations, conversions, projections, and lifetime
   actions directly. The affected path performs no lowering-time lookup,
   signature rendering, fake semantic-node construction, text round trip,
   whole-program retry, or recovery fallback.
3. The xvalue/materialization changes remain scoped to the retained value
   category, and destructor elision remains scoped to accessible empty chains.
   The original pass set, rejection behavior, PA16 access rules, and nested
   class-array lifetime output remain intact.
4. The changed source contains no test-name, source-spelling, reference-binary,
   host-compiler, subprocess, timeout, or cached-output shortcut. The audit
   adds no source file, and all files remain within the file-audit limit.

On viable single-inheritance overload sets at 16/32/64/128 candidates, the
audit changed 405/1,333/4,725/17,653 repeated path visits into
105/217/441/889 indexed ancestry probes while preserving exactly
16/32/64/128 candidate visits, 30/62/126/254 comparisons, and
48/96/192/384 conversions. Five-run semantic medians were
0.668/1.213/2.271/4.503 ms and peak semantic storage was
150,852/299,580/597,156/1,195,292 bytes. An unrelated reference-overload probe
at 32/64/128 candidates likewise retained exact doubling in candidates and
conversions, with the same proportional time and storage trend.

Validation:

- `make test-report ACTIVE_TEST_REPORT_PAS='pa17'`: expected incomplete-stage
  failure, 216/239; the turn-start pass set and exact 23-test failure set are
  unchanged.
- Required through-stage command: PA1-PA16 pass 1,436/1,436.
- `perl scripts/cppgm_file_audit.pl --stage pa17 --paths dev/src`: pass with
  11 header-division warnings and no fatal issue.
- Landed focus and rejection cases pass 12/12; five PA16 inheritance,
  access-control, and nested-array lifetime cases pass; source/ownership scans
  find only compact typed identities and no shortcut or fallback.

## Checkpoint Audit Ledger

| Checkpoint | Audit result | Closure evidence |
|---|---|---|
| Ref-qualified member identity and selection | Pass after audit fixes | Canonical declaration/call/ABI path; complete mixed-set key; correct object ranking; focused, baseline, scaling, and required gates pass |
| Branch-local class values and full-expression cleanup | Pass after audit fixes | Typed construction state on normal/unwind exits; exact flat and linked cleanup reuse; 173/231 baseline intact; linear probes and required gates pass |
| Loop full-expression regions | Pass after audit fixes | Typed discarded materialization; bounded-inline and linked-suffix cleanup; 174/231 baseline, linear probes, and required gates pass |
| Class direct-initialization recipes | Pass after audit fixes | Canonical list conversions and selected constructor are reused; original pass set, audit regressions, proportional probes, and required gates pass |
| Typed constructor delegation and qualified default completion | Pass after audit fixes | Canonical declaration/complete-constructor identities and typed action reuse; invalid/deleted defaults rejected; 193/233 baseline, six regressions, proportional probes, and required gates pass |
| Composite subobject copy/move storage transfer | Pass after audit fixes | Canonical recipe and direct selected-binding lowering; bounded array loops; 207/239 baseline, focused/through-stage gates, file audit, and fixed-shape extent probes pass |
| Value-category and reference-binding closure | Pass after audit fixes | Canonical value/conversion facts and direct typed lowering; indexed ancestry closes repeated chain work; 216/239 baseline, focused/rejection/through-stage gates, file audit, and proportional probes pass |

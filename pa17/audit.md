# PA17 Audit

## Current Checkpoint Review

**Checkpoint:** `ad3272ab` (`Implement PA17 class-prvalue destination
propagation`)

**Result:** Pass after audit fixes. The landed six-case increment covers
conditional local construction, ref-qualified conditional calls, reverse
friend operators, indirect call-result reference materialization, demanded
member-return construction, and conversion-result placement construction. The
audited result remains 230/241, preserving the prior audited 224/241 pass set
and the checkpoint's six gains with the same 11 failures.

The affected ownership path is parsed call/initialization -> PA12 canonical
`TypeId`/`BindingId`, selected constructor, argument conversion, value category,
and temporary identity -> a typed `DUMP_CLASS_VALUE_TRANSFER` retaining the
selected copy/move constructor -> PA15/PA16 destination-aware lowering. An
indirect class result receives the final destination; a direct result is
produced as a typed operand and copied once. Conditional cleanup records one
control-dependency fact on the owned temporary during the existing semantic
walk, and destructor selection consumes it by identity. Demanded function
bodies run the same named-return finalization as source-order bodies. No lookup
or overload selection occurs in lowering, and the checkpoint adds no template,
machine-IR, or ELF owner.

Audit findings are closed:

1. A direct-result call was given an unusable destination, and integer-literal
   folding depended on whether that destination happened to be present.
   Direct and indirect result paths are now explicit. Ordinary overloaded
   operator folding is derived from the selected canonical operator binding;
   allocation calls and unrelated calls retain their established conversion
   shape.
2. Empty-destructor handling recursively searched each temporary subtree.
   `CollectTemporaryObjects` now retains the subtree control-dependency fact on
   each temporary, making every later cleanup decision O(1). The release
   telemetry reports `temporary_dependency_visits` for the bounded semantic
   walks.
3. Lowering could accept a class-value transfer without proving that semantic
   analysis had retained a selected constructor. It now treats a missing or
   non-constructor `selected_binding` as an invariant violation before emitting
   the direct transfer.
4. The complete changed path has one translation-unit-owned semantic graph and
   function-local typed lowering state. It has no source/test-name branch,
   host/reference invocation, subprocess, timeout, cached answer, text round
   trip, global retry, whole-program scan, or name-based lowering fallback.

The audited 16/32/64-function forwarding probe recorded 158/318/638 temporary
dependency visits, 251/475/923 semantic nodes, 96/176/336 lowered nodes,
175/335/655 instructions, 53,478/101,238/197,270 typed bytes, and
121,557/236,019/465,553 peak stage bytes. Nine-run median
semantic/lowering/render times were 0.545/0.357/0.093,
0.922/0.536/0.156, and 1.614/0.891/0.267 ms. Work, storage, and time are
proportional to the represented forwarding functions and produced IR.

Validation:

- `make test-report ACTIVE_TEST_REPORT_PAS='pa17'`: expected incomplete-stage
  failure, 230/241; the 224/241 prior audit set and all six landed gains remain.
- Required through-stage command: PA1-PA16 pass 1,436/1,436.
- `perl scripts/cppgm_file_audit.pl --stage pa17 --paths dev/src`: pass with
  11 existing header-division warnings and no fatal issue.
- The six landed cases plus private-copy rejection, explicit-conversion,
  prvalue/braced-move, overloaded-subscript, and conditional-lifetime controls pass;
  the stage report has no timeout or newly failing case.

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
| Canonical lookup and candidate identity | Pass after audit fixes | Indexed using-name relations, compact canonical overload merging, retained object conversions; 222/239 baseline preserved, two regressions, proportional probes, and required gates pass |
| Class-prvalue destination propagation | Pass after audit fixes | Selected constructor retained; direct/indirect results separated; O(1) temporary cleanup fact; 224/241 baseline, six gains, linear probe, and required gates pass |

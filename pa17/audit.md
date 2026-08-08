# PA17 Audit

## Current Checkpoint Review

**Checkpoint:** `b210bf54` (`Implement pa17 canonical lookup checkpoint`)

**Result:** Pass after audit fixes. The landed increment remains bounded to the
six planned lookup/candidate cases: zero-parameter same-name filtering,
parenthesized ADL suppression, destructor aliases, base `using` overloads,
using-directive ambiguity, and inherited callable surrogates. The audit adds
two general regressions for function-overload merging through using directives
and inherited-member ranking on a further-derived object. The original 222/239
pass set is intact; with those regressions the audited result is 224/241 and
the exact original 17 failures remain.

The affected ownership path is source lookup/call -> PA11 canonical `ScopeId`,
`NameId`, and `BindingId` indexes plus explicit using-name relations -> a
cached `LookupResult` containing compact overload-set representatives -> PA12
canonical candidate deduplication, conversion ranking, and selected binding ->
`BuildResolvedCall` with the retained object/argument conversions -> PA15
direct typed `LowerCall`. Destructor aliases compare canonical `TypeId`;
surrogate selection retains the conversion call before constructing the typed
indirect call. Lowering consumes the selected declaration and conversion facts
without lookup, spelling reconstruction, or semantic-node synthesis. The
checkpoint introduces no template, machine-IR, or ELF owner and does not alter
existing template demand.

Audit findings are closed:

1. Using-directive lookup treated distinct functions like conflicting values.
   `LookupResult` now keeps a two-entry inline sequence with geometric overflow
   for ordinary representatives, and call analysis expands their compact
   function sets and flat-deduplicates canonical declarations.
2. A base member introduced by `using` ranked its implicit object against the
   declaration owner, which was insufficient for a further-derived caller.
   Member and operator candidate selection now rank against the retained
   `access_owner` while preserving the actual object conversion and base
   distance used by lowering.
3. Lookup initially revisited unrelated using directives for each queried
   name. `Program` now owns dense `(scope,name)` visibility and `(edge,name)`
   relation indexes, reverse incoming edges, a deduplicated propagation
   worklist, and name-precise reverse invalidation. Late declarations and
   cycles propagate through the same explicit relation; lookup visits only
   edges that can contribute the requested name.
4. Surrogate conversion facts and pending lookup targets are flat indexed
   sequences rather than one owning vector per candidate or scope. The hot
   keys are compact IDs, common lookup results stay inline, and large sequences
   grow geometrically.
5. The complete changed path contains no test/source-name shortcut,
   reference/host compiler invocation, subprocess, timeout, cached answer,
   text round trip, global retry, or lowering fallback. Candidate rejection
   stays in typed control flow, and direct selected-binding lowering remains
   unchanged.

Representative using-merge probes at 32/64/128 contributing namespaces
recorded 134/262/518 scope visits, 32/64/128 relevant edge visits,
32/64/128 candidates, 33/65/129 conversions, and
263,007/525,935/1,051,779 semantic bytes. Five-run semantic medians were
1.344/2.449/4.600 ms. Callable-surrogate probes at 32/64/128 candidates
recorded 137/265/521 scope visits, no using-edge visits, 32/64/128 candidates,
66/130/258 conversions, 241,408/482,280/963,916 semantic bytes, and
1.465/2.689/5.378 ms medians. Required work, storage, and time are therefore
proportional to contributing names and candidates rather than unrelated using
edges.

Validation:

- `make test-report ACTIVE_TEST_REPORT_PAS='pa17'`: expected incomplete-stage
  failure, 224/241; the turn-start 222-test pass set is preserved and the
  original 17-test failure set is unchanged.
- Required through-stage command: PA1-PA16 pass 1,436/1,436.
- `perl scripts/cppgm_file_audit.pl --stage pa17 --paths dev/src`: pass with
  11 existing header-division warnings and no fatal issue.
- The six landed cases, two audit regressions, adjacent operator/using checks,
  late-name propagation, and cyclic-using probes pass. Source and ownership
  scans find only canonical typed identities and no shortcut or fallback.

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

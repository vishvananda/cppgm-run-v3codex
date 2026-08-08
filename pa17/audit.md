# PA17 Audit

## Current Checkpoint Review

**Checkpoint:** `b181f7d2` (`Implement pa17 loop full-expression cleanup`)

**Result:** Pass after audit fixes. The landed increment is bounded to loop
initializer and iteration full-expression cleanup, enclosing-scope unwind
actions, and the function-local index used to find those scopes. The next
checkpoint remains class direct-initialization; unrelated PA17 failures are not
attributed to this closure.

The complete ownership path is loop syntax -> PA12 typed expression and stable
temporary identity -> ordered normal and unwind destructor actions carrying
canonical type and selected destructor facts -> PA17 full-expression region,
compact action IDs, and cleanup CFG -> typed LowIR. Discarded class calls,
conditionals, casts, and comma value wrappers are materialized at the semantic
boundary. Lowering consumes those facts directly and does no lookup,
rendered-signature recovery, or text round trip.

Audit findings are closed:

1. A direct class prvalue in a nondeclaration `for` initializer or iteration
   expression could bypass materialization; nested comma and class-conditional
   values exposed the same ownership gap. PA12 now materializes the discarded
   class result while preserving the expression tree's selected call and type,
   and PA15 propagates discarded context through comma operands.
2. Exact cleanup-sequence reuse rebuilt every constructed prefix for wide
   temporary chains, producing superlinear CFG and storage. Regions retain the
   exact path for at most eight actions and then intern one linked suffix node
   per newly constructed nontrivial temporary. Normal destruction remains in
   reverse construction order, and each unwind edge reaches exactly the live
   suffix.
3. `FlatIdMap::Clear` scanned retained peak capacity even when a later region
   contained few live IDs. The map now records occupied slots and clears only
   those entries; rehashing rebuilds the same sparse ownership list.

Representative 16/32/64-direct-temporary iteration probes recorded 17/33/65
cleanup probes and entries, 26/42/74 blocks, 139/251/475 instructions, and
32,560/58,320/109,840 typed bytes. Five-run semantic medians were
0.330/0.389/0.577 ms and lowering medians were 0.247/0.357/0.399 ms. Work,
storage, and emitted CFG are proportional to owned cleanup obligations.

Validation:

- Focused checked-in loop/temporary coverage: 17/17 pass (7 PA15 loop-control
  tests and 10 PA17 lifetime/conditional tests). Direct call, nested comma,
  short-circuit, conditional, normal-exit, and unwind probes emit the expected
  construction and reverse-destruction paths.
- `make test-report ACTIVE_TEST_REPORT_PAS='pa17'`: expected full-stage
  failure, 174/231, exactly preserving the turn-start checkpoint baseline with
  no timeout.
- `make test-report-through-pa16`: 1,436/1,436 and 16/16 stages pass.
- `perl scripts/cppgm_file_audit.pl --stage pa17 --paths dev/src`: pass with
  the same ten header-division warnings and no fatal issue.
- Source audit across the changed ownership path finds no reference-binary,
  host-compiler, subprocess, test-path, or timeout shortcut.

## Checkpoint Audit Ledger

| Checkpoint | Audit result | Closure evidence |
|---|---|---|
| Ref-qualified member identity and selection | Pass after audit fixes | Canonical declaration/call/ABI path; complete mixed-set key; correct object ranking; focused, baseline, scaling, and required gates pass |
| Branch-local class values and full-expression cleanup | Pass after audit fixes | Typed construction state on normal/unwind exits; exact flat and linked cleanup reuse; 173/231 baseline intact; linear probes and required gates pass |
| Loop full-expression regions | Pass after audit fixes | Typed discarded materialization; bounded-inline and linked-suffix cleanup; 174/231 baseline, linear probes, and required gates pass |

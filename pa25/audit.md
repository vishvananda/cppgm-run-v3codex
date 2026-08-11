# PA25 Checkpoint Audit

## Current Checkpoint Review

Checkpoint `b985f854` (range-for statements) passes after audit repair. The
landed increment adds one PA10 range syntax node, PA12 range/iterator operation
selection and lifetime facts, retained-template validation, and direct PA15
lowering through the existing typed loop CFG.

The audit found and fixed three issues along that ownership path:

1. Declaration-form ordinary `for` initializers were parsed speculatively as
   range declarations and then parsed again. A bounded, allocation-free
   delimiter lookahead now selects the range grammar; each source region builds
   one syntax subtree and abandoned semantic name facts are not created.
2. Range materialization did not preserve every value category. A
   non-identifier array lvalue bypassed the hidden binding and ran its source
   call once per iteration; array prvalues failed conversion; and genuine
   array/class xvalues could enter by-value class transfer. Unstable glvalues
   now bind one canonical lvalue/rvalue reference, array prvalues materialize
   once with lifetime extension, and owned class prvalues retain direct
   storage/elision.
3. Manually built range conditions and iterations skipped ordinary
   full-expression finalization. Class prvalues returned by iterator
   `operator++` were allocated but never destroyed. Range conditions and
   discarded increments now use the shared temporary collection, cleanup,
   unwind, and slot-planning facts.

The array reducer traces source bytes -> bounded range delimiter scan -> one
`range-for-statement` syntax node -> one call analysis -> canonical
array-reference `TypeId` and hidden binding -> counted-loop semantic nodes ->
binding-indexed slots and direct typed LowIR. The call is in the loop
initializer, not the body. The retained-template trace keeps one parsed range
body, validates its control/loop-variable scope, substitutes only its dependent
facts when demanded, retains selected operations on call nodes, and emits the
demanded specialization once. Lowering performs no syntax replay, name lookup,
or text round trip.

Seven-run medians for 16/64/256 repeated member ranges recorded 358/982/3,478
tokens, 701/2,573/10,061 semantic nodes, 203/731/2,843 overload candidates,
450/1,554/5,970 lookup queries, 465/1,665/6,465 instructions, and peak semantic
storage of 353,997/1,229,663/4,637,012 bytes. Semantic time was
1.482/4.391/16.269 ms and lowering time 0.522/1.400/5.083 ms. Required work and
storage track expanded syntax, actual candidates, and emitted loops without a
superlinear trend.

No relevant source/test shortcut, global retry, lowering-time semantic search,
textual transport, timeout-adjacent behavior, or unresolved checkpoint-owned
correctness/performance/file-audit issue remains. All three audit reducers pass
through `lowir2cy86`/`cy86` and execute with status zero; PA29 native lowering
is a later staged surface. Validation preserved all 64 turn-start PA25 passes,
added three passing audit regressions (67/128), preserved PA1-24 at
3,471/3,471, and passed the file audit with the same 14 inherited nonfatal
header-division advisories.

## Checkpoint Audit Ledger

| Checkpoint | Audit disposition |
|---|---|
| Ordinary placeholder results (`583b174a`) | Pass after cv-reference, runtime-demand, direct-ownership, and retained-body copy repairs; shipped baseline and all earlier stages preserved; linear scaling and file audit verified. |
| Range-for statements (`b985f854`) | Pass after single-parse dispatch, category-correct one-time range binding, and condition/iteration cleanup repairs; 67/128 PA25 and 3,471/3,471 earlier tests preserved; linear scaling and file audit verified. |

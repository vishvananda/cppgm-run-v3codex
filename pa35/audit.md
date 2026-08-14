# PA35 Checkpoint Audit

## Current Checkpoint Review

The audit covered checkpoint commit `4c9ec3aa`, its changed front-end ownership
path, `spec.md`, the PA35 contract and plan, focused completion/access/noexcept
tests, and the primary 60/103 failure report. The landed increment correctly
defers class-template keys containing nested dependent type shapes, replays
retained members with the canonical specialization as class context, and makes
an in-class defaulted destructor's implicit exception specification a
post-layout demand fact. It advances the handout suite from 53 to 60/103: all
13 checkpoint barriers move and seven pass.

The complete affected path is retained parsed type/member syntax -> interned
template identity and canonical argument list -> specialization request state
-> canonical-TypeId shape readiness -> class definition/layout state ->
specialization-owned member replay and indexed access/lookup -> canonical
destructor fact -> post-layout base/member visits -> nonthrowing and exception
boundary facts -> typed LowIR and native exception-boundary lowering. Patterns
and canonical arguments remain TU-owned; the shape walk has request-local
scratch, member replay borrows class identity under RAII, and lowering consumes
the selected facts without lookup, rendering, or a hosted-only route.

Two related `spec.md` §§2/4/6/9 defects were repaired. The shape walk rejected
the highest valid canonical type ID and omitted dependency-bearing function,
member-pointer-owner, bound, block-pointer, vector, bit-int, and complex edges;
it now visits every reachable type identity once with a flat visited set.
Template-template arguments continue to use their own canonical dependence
marker rather than treating a concrete proxy as a dependent type. Separately,
lazy destructor completion updated `nonthrowing` but left the provisional
exception boundary consumed by lowering; the same canonical demand now
publishes both facts. A composite function/member-pointer and nested-destructor
course regression covers these paths.

For 8/16 independent composite-shape/defaulted-destructor families, template
requests were 67/123, cache hits 28/52, lookups 512/896, and overload candidates
16/32. Median semantic time was 3.07/4.83 ms and semantic peak storage was
0.786/1.185 MB. The counters and storage remain at or below the doubled semantic
input, with no retry or allocation cliff.

Validation is clean at the checkpoint boundary: the handout suite remains
60/103 with the same 43 later-owner failures, and the added course regression
makes the required report 61/104; PA1-34 passes 4,756/4,756; and the file audit
passes with 22 inherited nonfatal header-division warnings. No relevant landed
correctness, performance, shortcut, timeout, ownership, or file-audit issue
remains.

## Checkpoint Audit Ledger

| Checkpoint commits | Audit disposition |
| --- | --- |
| `ab8d37e6` | Pass after consolidating retained-pack publication and lookup into one canonical direct/per-scope index; correctness baseline preserved. |
| `e3f0f029` + audit fix | Pass after making source-location rendering error-only; mandatory evaluation, canonical cache ownership, and the 53/103 baseline are preserved. |
| `4c9ec3aa` + audit fixes | Pass after completing canonical type-edge traversal and publishing the delayed destructor boundary; the 60/103 handout baseline, PA1-34, and bounded scaling are preserved. |

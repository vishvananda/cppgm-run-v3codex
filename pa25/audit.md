# PA25 Final Audit

## Final Findings

1. Resolved architecture blocker: PA10 stored each lambda introducer as opaque
   text and PA22 reparsed it. This violated the one-parse rule and made strings
   semantic transport.
2. Resolved performance blocker: every default-reference closure rescanned its
   complete nested body, allocated a node-based parameter-name set, and
   repeated enclosing lookups. Deep nested lambdas showed rapidly increasing
   subtree work, semantic time, and peak storage.
3. Resolved identity/scaling blocker: local closure presentation names embedded
   the complete enclosing closure name recursively, causing avoidable LowIR
   symbol and line growth. Explicit-capture classification and duplicate
   suppression also had quadratic linear-search loops.
4. No additional blocker was found in placeholder deduction, retained-template
   demand, range materialization/lifetime, aggregate actions, class conversion
   selection, typed class-value boundaries, ABI handoff, lowering ownership, or
   production self-containment.

## Changes

- Added PA25 lambda-introducer syntax ownership. PA10 now emits semantic-only,
  interned facts for capture defaults, named reference/pack captures, copy forms,
  and `this`, while retaining the existing public syntax rendering.
- Added `LambdaCaptureUseTable`, keyed by lambda `NodeId`, with explicit
  unstarted/in-progress/succeeded/failed states, open-addressed indexing,
  contiguous name facts, lexical bound-name tracking, source-order deduplication,
  nested-summary reuse, direct explicit flags, and storage/work counters.
- Changed PA22 closure formation to consume the structured summary and canonical
  IDs directly. Named sources, parameter packs, implicit member/`this` use, and
  nested capture propagation still resolve through ordinary semantic lookup.
- Replaced recursively rendered local closure identity with a compact binding,
  token, and ordinal presentation component. Canonical ABI mangling remains
  based on local context and lambda ordinal.
- Rejected duplicate explicit named and `this` captures and added a course
  regression. Removed per-capture linear classification and duplicate scans.
- Exposed summary request/hit, syntax-visit, name-use, and storage telemetry in
  semantic and LowIR frontend statistics.

## Performance Evidence

Before repair, empty nested `[&]` closures at depths 16/32/64/128 performed
3,312/24,032/183,232/1,431,424 scope visits; semantic time rose to
2.46/9.62/48.46/307.55 ms and depth-128 peak semantic storage reached 55.4 MB.

After repair, depth 16/64/256 empty nesting performed 153/633/2,553 capture
syntax visits, 0 name uses, and produced 5,592/22,379/90,214 LowIR bytes with
43,263/173,086/692,669 typed-storage bytes. Median semantic times were
1.378/5.881/47.598 ms. The matching real-free-use family recorded exactly
16/64/256 name uses and 7,532/29,900/120,178 LowIR bytes. Wide explicit lists
of 16/64/256 names took 0.594/1.531/5.805 ms with 103/391/1,543 lookup-scope
visits. Output line length remained bounded at 108-144 bytes.

Counter profiling attributes the residual pathological nesting cost to the
sum of ordinary lexical parent-scope edges, not repeated capture-subtree scans.
An attempted generic lookup dependency-cache change increased cache storage and
misses without reducing those visits, so it was reverted.

## Validation

- PA25 focused aggregate/conversion/range/capture checks: 10/10 pass.
- Focused nested/pack capture checks after the final scaling cleanup: 5/5 pass.
- PA25 local plus course suite: 136/136 pass (121 local, 15 course).
- Duplicate explicit reference-capture regression: expected failure passes.
- Range-prvalue lifetime and capturing-template-pack traces: valid LowIR,
  PA13 LowIR-to-CY86 success, PA9 x86-64 Linux ELF success, execution status 0.
- PA29 adapter observation: explicit status 86 (`not yet implemented`) before
  MIR emission; no PA25 data-dependent failure.
- `perl scripts/cppgm_file_audit.pl --stage pa25 --paths dev/src`: pass with
  the same 15 inherited nonfatal header-division warnings.
- `make test-report-through-pa25`: pass, 3,607/3,607 tests and 25/25
  tracked stages.
- `git diff --check`: pass.

## Checkpoint Ledger

| Checkpoint | Audit result |
|---|---|
| Ordinary placeholders (`583b174a`, `7737d2a5`) | Pass: canonical placeholder results, direct initializer ownership, retained visible bodies. |
| Range-for (`b985f854`, `db9bf14a`) | Pass: bounded parse dispatch, one range evaluation, selected member/ADL calls, complete cleanup. |
| Aggregates (`ece08579`) | Pass: member-order actions, omitted zero initialization, nested arrays, typed helper ABI. |
| Class conversions (`cec97359`, `c3651ce0`) | Pass: canonical targets, selected conversion functions, modifiable references, class-value boundaries. |
| Template placeholder results (`2e7bf454`, `1d508e97`) | Pass: cache-before-body analysis, four-state demand, canonical type/ABI publication. |
| Captureless call operators (`60cd11b4`, `ade1022b`) | Pass: canonical closure key/body, pack identity, lexical access without implicit capture. |
| Captureless pointer conversion (`440c7070`) | Pass: semantic-owned static invoker and conversion fact, direct lowering. |
| By-reference/`this` captures (`7c963c77`) | Pass after final audit: canonical layout/aliases and nested propagation; text reparse, repeated subtree scans, and recursive presentation names removed. |
| Final PA-wide audit | Pass: findings closed, performance measured, executable traces and required gates validated. |

# PA37 Final Audit

## Findings

1. Optimized source compilation performed `BuildTypedLowIRProgram` twice: once
   for host-object facts and again to recover canonical presentation names.
   This reparsed, reanalyzed, reinstantiated, and relowered every source file;
   a 2,000-function `-O0` compile took 1.00 s versus 0.21 s without an explicit
   level.
2. The optimizer contained several scaling blockers hidden by counters that
   covered only simplification: a block-squared dominator matrix, one-node-per-
   retry DCE, whole-function slot retries, slot × instruction validation,
   repeated jump-chain walks, an eight-merge CFG ceiling, whole-program
   no-unwind retries, per-call EH analysis, recursive SCC/rematerialization
   walks, caller restarts, repeated whole-caller substitutions, and caller-tail
   copying at each inline site.
3. O2 promotion retained stale per-block load replacements after dataflow facts
   became less precise and propagated block-local temporary values across CFG
   boundaries. Hosted `ostringstream` and `vector<string>` object compilation
   exposed the result as undefined lowered temporaries.
4. Driver debug metadata repeatedly searched all source lines for every
   function and slot, forming a scaling-sensitive source cross-product. The
   production driver also lacked PA37 optimizer counters/timing.
5. After the repairs and full validation, no correctness, architecture,
   performance, self-containment, timeout, or file-audit blocker remains.

## Changes

- Replaced the second semantic/lowering run with an interned canonical
  specialization presentation fact on `EntityRecord`; PA15 consumes that fact
  during its sole lowering pass. Alias ordering is indexed by target, and typed
  PA15 storage is released immediately after structural adaptation.
- Replaced the dominator matrix with a near-linear Lengauer-Tarjan tree,
  recursive graph walks with iterative traversals, retry DCE with a def/use
  worklist, no-unwind retries with reverse dependencies, and repeated EH scans
  with one caller context worklist.
- Made jump bypass and whole straight-line-chain merging complete in one CFG
  cleanup, deduplicated high-fanout switch edges in O(E log E), and replaced
  convergence retries with an explicit bounded pass schedule.
- Added state-product budgets, dirty predecessor worklists, function-local temp
  stripping, fresh per-block replacement facts, transitive storage-address
  alias resolution, and single-pass slot validation/promotion classification.
- Batched call-free single-block inline sites through one block rebuild, used
  monotonic site allocation and one final substitution pass, and retained the
  general multi-block continuation path for produced CFG output.
- Indexed debug source words and return lines once and added optimizer visit,
  edge, update, candidate, rewrite, budget, IR-size, and elapsed-time telemetry
  to both `lowiropt` and production driver paths.
- Consolidated the checkpoint plan into this PA-wide architecture,
  performance, validation, and ledger record.

## Architecture Trace

For the nontrivial declaration trace, the hosted `ostringstream << unsigned`
source enters one preprocessing/token/syntax owner and one canonical semantic
graph. Canonical overload, template, layout, lifecycle, EH, linkage, and ABI
facts produce 311 demanded typed functions and 6,924 LowIR instructions. The
typed graph is structurally adapted and released; PA37 emits 5,685 optimized
instructions; native lowering emits 9,536 MIR instructions function by
function; the ELF writer directly publishes a 3,001,192-byte relocatable
object. No source-path LowIR text or second semantic graph exists.

For the demanded-template trace, `std::function` uses canonical template,
argument-list, specialization, binding, and demand identities. Its 727
specialization requests / 364 cache hits close 37 demanded bodies with 38
pushes. The presentation name needed by the staged LowIR boundary is a compact
semantic `NameId`. PA37 visits 95 direct calls, inlines 86, reduces 350 to 253
instructions in 7.52 ms, and direct native/ELF emission produces a
157,368-byte object.

At ownership boundaries, semantic parser/analyzer scratch dies before graph
consumption; PA15 typed and native LowIR coexist only during one structural
adapter; optimizer analyses are per-program summaries or per-function scratch;
MIR/encoding state is reclaimed per function; ELF alone retains final global
symbol/relocation/section state. Text parsing is confined to explicit staged
tool or `.lowir` input, and text serialization is confined to requested LowIR
output.

## Performance Evidence

The audit reproduced superlinear behavior before changing the passes. A dead
dependent chain at 1k/2k/4k/8k instructions took
0.17/1.63/2.94/12.86 s. A 1k/2k/4k/8k-block nontrivial CFG took
0.05/0.13/0.49/1.75 s and reached 82,072 KiB RSS. Tiny inline sites at
500/1k/2k/4k took 0.04/0.17/0.75/3.07 s.

After repair, the dead chain takes 0.01/0.02/0.04/0.08 s with exactly
2,007/4,007/8,007/16,007 instruction visits. The CFG chain takes
0.01/0.03/0.06/0.14 s with 999/1,999/3,999/7,999 block visits and complete
chain merging. Tiny inline optimizer time is 0.91/1.73/3.39/7.10/15.21 ms for
500/1k/2k/4k/8k sites, with exactly one candidate visit, update, and inline per
site.

The duplicate-frontend workload is now 0.20 s without an explicit level and
0.21 s at `-O0`. Indexed line-table generation at 1k/2k/4k/8k functions takes
0.15/0.30/0.61/1.24 s. The hosted `ostringstream` optimizer takes 132.52 ms in
a 1.47 s / 65,532 KiB compile; `std::function` takes 7.52 ms in a
0.40 s / 21,620 KiB compile. Counters account for both workloads and show no
unexplained retry or allocation cliff.

## Validation

- Focused stale-alias regressions: hosted `ostringstream` and
  `vector<string>::push_back` direct/serialized O2 object round-trips pass.
- PA37 functional buckets: O0 2/2, O1 48/48, O2 12/12, driver O1 3/3, driver
  O2 6/6, and object round-trip 7/7 pass.
- PA37 debug buckets: LowIR O1 2/2, LowIR O2 1/1, driver O1 1/1, driver O2 1/1,
  and debug object round-trip 7/7 pass.
- Required PA37 file audit and cumulative PA1-PA37 report are the final release
  gates: file audit passes with 23 inherited nonfatal header-division
  advisories; all 37 stages and 5,065/5,065 tests pass.

## Checkpoint Audit Ledger

| Checkpoint commits | Audit disposition |
| --- | --- |
| `0cad55d2` | Pass after repair: the stage's typed O0/O1/O2 optimizer and explicit text/object boundary remain intact; duplicate semantics, stale slot facts, repeated scans, scaling cliffs, and missing telemetry are closed across their owners. |
| Final PA-wide audit | Pass: architecture traces, controlled scaling, hosted profiles, self-containment, file audit, all 37 stages, and 5,065/5,065 cumulative tests are consolidated here. |

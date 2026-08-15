# PA37 Final Plan

## Stage Design and Spec Alignment

The PA37 production path is:

`source buffer -> preprocessing/token/syntax owners -> canonical semantic graph
-> PA15 typed LowIR -> one structural native-LowIR adapter -> lowir_opt ->
function-local MIR -> direct ELF64`

The PA15 typed graph is released immediately after adaptation. The source path
does not serialize or reparse LowIR, and it now performs semantic construction
and lowering exactly once even when an optimization level is present. Canonical
template presentation spelling is retained as a compact interned semantic fact
and consumed during the same lowering run. Text parsing and serialization are
restricted to the explicit `lowiropt`, `--emit-lowir`, and `.lowir` input
adapters required by the assignment.

PA37 owns one deterministic, explicitly bounded optimizer schedule. O1 performs
direct-call inlining, scalar simplification, worklist DCE, CFG cleanup, and dead
slot cleanup. O2 adds budgeted executable-edge slot propagation and liveness.
CFG, dominance, no-unwind inference, SCC discovery, EH-state propagation, DCE,
and slot liveness use indexed graphs or dirty worklists. The optimizer retains
LowIR ABI, linkage, EH, object, and debug metadata and exposes input/output size,
candidate, edge, visit, update, rewrite, budget, and elapsed-time telemetry.

This matches `spec.md` §§4–7 for typed one-way lowering and direct object
generation, §7 for explicit optimization budgets and worklist convergence, §8
for phase-local lifetimes, §9 for O(n)/O(n log n) ordinary optimization and
observability, and §10 for self-contained output.

## Performance Evidence

All measurements used release binaries, `/usr/bin/time`, and
`CPPGM_LOWIR_OPT_STATS=1` where applicable.

| Workload | Audit baseline | Final evidence |
| --- | --- | --- |
| Dead dependent chain, 1k/2k/4k/8k instructions | 0.17/1.63/2.94/12.86 s; repeated whole-function DCE | 0.01/0.02/0.04/0.08 s; 2,007/4,007/8,007/16,007 visits and 1,002/2,002/4,002/8,002 pushes |
| Nontrivial CFG chain, 1k/2k/4k/8k blocks | 0.05/0.13/0.49/1.75 s and 7.8/11.9/27.3/82.1 MiB; quadratic dominator matrix; only eight merges | 0.01/0.03/0.06/0.14 s and 9.7/15.1/26.1/55.3 MiB; 999/1,999/3,999/7,999 block visits; every chain fully merged |
| Tiny direct calls, 500/1k/2k/4k sites | 0.04/0.17/0.75/3.07 s from repeated caller-tail copies | 0.91/1.73/3.39/7.10 ms optimizer time; exactly one visit, update, and inline per site |
| 2,000 source functions, implicit level vs `-O0` | 0.21 vs 1.00 s because `-O0` repeated the complete frontend | 0.20 vs 0.21 s; one semantic/lowering run in both paths |
| Debug source indexing, 1k/2k/4k/8k functions | function and slot scans formed a source-lines cross-product | 0.15/0.30/0.61/1.24 s after one source index; RSS 23.8/38.9/71.4/134.3 MiB |

The hosted `ostringstream` trace has 311 functions and 6,924 input LowIR
instructions. PA37 performs 83,963 instruction visits, 4,047 block visits,
23,228 edge visits, 630 inline probes / 466 inlines, and 4,918 rewrites in
132.52 ms, producing 5,685 LowIR instructions. The native path then produces
9,536 MIR instructions and a 3,001,192-byte object; total compile time is
1.47 s at 65,532 KiB RSS.

The demanded `std::function` trace has 727 specialization requests / 364 hits,
38 demand pushes, 37 demanded functions, 350 input / 253 output LowIR
instructions, and 95 inline probes / 86 inlines. PA37 takes 7.52 ms; direct
native emission produces 391 MIR instructions and a 157,368-byte object in a
0.40 s, 21,620 KiB compile.

## Architecture Review

| Checklist surface | Final disposition |
| --- | --- |
| Representation and ownership | Source is analyzed/lowered once. PA15 typed LowIR overlaps native LowIR only during structural adaptation and is then released. Text is present only at explicit adapter boundaries; optimizer and object generation consume the in-memory model. |
| Identity and lookup | Frontend name/type/scope/entity/template identity remains compact and canonical. The presentation spelling needed by PA37 is one interned `NameId`, not a second semantic graph. Optimizer symbol maps are indexed once per program/function and deterministic order is isolated to final vectors. |
| Templates and repeated work | Existing canonical request states and demand worklists remain authoritative. PA37 no longer reruns semantic/template completion to recover names. No new fact triggers whole-program retries. |
| Lowering and backend | Each source emission unit is lowered once, optimized once, structurally lowered to per-function MIR, encoded, and written directly to ELF. Explicit `.lowir` input is the only parse-before-object path. |
| Allocation and scaling | Quadratic dominator storage, repeated DCE scans, slot × instruction scans, caller-tail copies, recursive graph walks, and debug source rescans are removed. State-product budgets prevent scalar-slot maps from becoming a block × fact allocation cliff. |
| Self-containment | `lowiropt` and compile mode invoke no host compiler, assembler, previous stage, reference binary, or answer cache. Host linking remains only the assignment-authorized final link operation. |

The nontrivial declaration trace is the hosted `ostringstream << unsigned`
fixture. One source analysis establishes canonical declarations, overloads,
class layouts, lifecycle and ABI entries. Typed lowering emits 311 demanded
functions; the structural adapter supplies the optimizer; PA37 preserves EH and
object boundaries while simplifying to 5,685 instructions; per-function native
lowering emits MIR and the direct ELF writer publishes symbols, relocations, and
sections. No semantic graph, textual LowIR, or complete-program validator is
reintroduced after adaptation.

The demanded-template trace is the hosted `std::function` fixture. Canonical
template and argument identities close 37 demanded function bodies through 38
worklist pushes. The canonical presentation `NameId` crosses into PA15 symbol
formation without another template analysis. PA37's indexed call graph infers
no-unwind facts, marks recursive SCCs, visits each direct call, clones eligible
output, and hands 253 instructions to direct native emission.

## Final Architecture Review

The final audit independently reviewed `spec.md`, the PA37 contract, the single
stage commit, every changed stage source, the previous plan, direct and textual
adapters, optimizer analyses, native/object boundaries, hosted object
round-trips, and the current cumulative test surface. It found and fixed a
duplicate frontend run, quadratic and repeated optimizer paths, stale slot
dataflow facts that could emit undefined temporaries, unbounded recursive graph
walks, missing production optimizer telemetry, an overlong typed-IR lifetime,
and source debug cross-products.

No known correctness, architecture, performance, timeout, self-containment, or
file-audit blocker remains. The PA37 file audit passes with 23 inherited
nonfatal header-division advisories, and the cumulative report passes all 37
stages and 5,065/5,065 tests.

## Checkpoint Ledger

| Checkpoint commits | Consolidated result |
| --- | --- |
| `0cad55d2` | PA37's typed optimizer, driver integration, explicit text adapters, O0/O1/O2 behavior, debug preservation, and object round-trip contract were reconstructed independently. The audit retained the stage design while repairing ownership, correctness, scaling, and observability across the full path. |
| Final PA-wide audit | Pass: one frontend/lowering run, compact presentation facts, bounded optimizer schedules, worklist analyses, near-linear scaling, direct ELF generation, full PA37/debug/object validation, file audit (23 inherited nonfatal advisories), and 5,065/5,065 cumulative tests are covered by the consolidated audit. |

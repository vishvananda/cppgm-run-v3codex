# PA31 Final Audit Plan

## Stage Design and Spec Alignment

PA31 owns the source-to-host-relocatable boundary over the shared front end and
incremental native backend:

```text
source/options -> canonical semantics -> typed PA15 LowIR
               -> direct PA30 typed adapter
               -> one-function-at-a-time MIR/encoding
               -> indexed code/data labels and typed runtime roles
               -> LSDA + CFI + RTTI/personality cells + relocations
               -> direct deterministic ELF64 relocatable
```

The production driver never renders or reparses LowIR or assembly and never
launches a host compiler/assembler. The PA30 binary compiler payload is retained
in the non-allocating `.cppgm_object` section solely for the cumulative internal
link contract. Semantic state is gone before native lowering; MIR analysis,
allocation, and region planning are function-local and are released after each
function is encoded. Final text/data, compact function layouts, symbols,
relocations, LSDA/CFI, and the ELF image are the required TU-wide owners.

Runtime helpers, RTTI, linkage, call unwind/return behavior, and object symbols
cross typed identity/role fields. Protected EH state is a canonical parent-linked
state ID, propagated once over explicit CFG and unwind edges. Merge equality is
O(1); the encoder consumes the resulting per-call landing-block ID. ELF-local
symbol and catch-reference order is a final stable sort. This aligns the PA31
surface with `spec.md` sections 2 and 5-10 and the PA31 README requirements.

## Performance Evidence

The audit's plain-function series exposed quadratic symbol sizing/type
classification. Pre-audit encode times for 1,000/2,000/4,000/8,000 functions
were 10.99/31.06/104.09/404.24 ms. Hash indexes keyed by function offset and
internal identity reduce the final series to 8.31/20.69/37.18/78.53 ms, with
834,104/1,676,104/3,360,104/6,728,104 output bytes and exactly
1,001/2,001/4,001/8,001 fixups. The 8k case is 5.15x faster; output and work
counters retain near-2x slopes.

The EH-heavy 50/100/200/400-function series records 250/500/1,000/2,000
canonical region states, 550/1,100/2,200/4,400 edges, 100/200/400/800 protected
calls, and 1,205/2,405/4,805/9,605 fixups. Lowering is
5.19/10.14/20.05/39.91 ms and encoding is 6.10/12.17/24.67/58.69 ms; output is
1.09/2.18/4.35/8.70 MiB. Counts are exactly linear and timing is consistent
with O(IR + edges + relocations + output), with final relocation sorting
O(R log R).

Representative PA31 objects remain small and bounded: unhandled throw is two
functions, 6 LowIR/16 MIR instructions, 5 fixups, 8,984 bytes, and 7,784 KiB
RSS; compact unwind is 23 functions, 671/1,059 instructions, 3 canonical EH
states, 5 edges, 3 protected calls, 233 fixups, 326,648 bytes, and 9,640 KiB
RSS. A cleanup-only object records 2 states, 2 edges, and 1 protected call.

## Architecture Review

- Representation and ownership: source/semantic/typed-LowIR ownership ends at
  explicit boundaries; binary compatibility payload, code/data, per-function
  MIR, compact layout facts, and final ELF each have one named owner. No text
  round trip or whole-program MIR retention exists in compile mode.
- Identity and lookup: frontend entities/types/templates remain canonical IDs;
  runtime and call facts are typed. MIR label strings are converted once to
  compact block IDs for EH-state interning. Function size/type and ELF symbol
  lookups are average-O(1); final sorts provide deterministic bytes.
- Templates and repeated work: a traced `raise_value<int>` specialization has
  3 requests, 2 cache hits, 1 demand push, and 1 demanded emission. It is
  lowered once as weak ODR code and participates in the same host-EH path.
- Lowering and backend: direct-call unwind/noreturn facts survive the adapter
  into MIR. Explicit cleanup clauses remain clauses rather than fake pushes.
  CFG/unwind worklist analysis validates marker underflow, landing targets,
  protected-state merges, generated catch-forward transfers, and normal exits
  before encoding call-site facts.
- Object generation: ordered LSDA action chains cover every handler; type-table
  selectors point at typed RTTI cells; mixed chains retain zero-filter cleanup
  actions; cleanup/resume uses `_Unwind_Resume`; FDEs carry CFI, indirect
  personality, and LSDA relocations. Code and ELF are emitted directly without
  private `cppgm_eh_*` imports.
- Allocation and scaling: canonical EH states are parent-linked compact facts,
  each block/edge is processed once, non-EH functions skip region analysis,
  buffers grow geometrically, and only final layout state crosses functions.
- Self-containment: process tracing observes only the requested `cppgm++`
  process; source searches find no compiler/assembler/reference launch or
  fixture dispatch. Host compilation exists only in the test harness after the
  requested object has been produced.

## Final Architecture Review

The independent review closed all in-scope findings at their owning boundaries.
The original writer emitted only the first handler action, reset EH state at
every MIR block, rescanned all functions for every label/export, dropped direct
call unwind/noreturn facts, conflated targetless cleanup clauses with pushes,
omitted cleanup actions from mixed LSDA chains, and allowed unordered containers
to define ELF order. The final path now has an ordered action chain, canonical
edge-driven region state, typed call facts, correct clause identity, indexed
symbol facts, and deterministic final ordering.

A demanded-template throw is caught by the second of two typed handlers, links
and runs with exit 0, and carries `_ZTIc`, `_ZTIi`, `__cxa_*`, personality,
LSDA, CFI, and relocation facts end to end. Two identical compiles produce the
same SHA-256 object. A no-child-process trace and private-symbol scan prove the
required path is self-contained. The wider earlier-EH object probe accepts all
45 cases, including the three state-analysis cases found during the audit. The
README-declared richer nested same-frame catch search remains outside PA31's
runtime contract and is not claimed as supported. No PA31 correctness,
architecture, performance, timeout, self-containment, placement, or fatal
file-audit blocker remains.

## Checkpoint Ledger

| Checkpoint | Final status | Evidence |
| --- | --- | --- |
| Host ELF64 and PA30 compatibility payload | Complete | Direct relocatable, embedded bounded payload, host link/run and internal-object reuse |
| Runtime imports, RTTI, personality, LSDA, and CFI | Complete after audit | Multi-handler action regression; normalized object facts; no private EH symbols |
| Protected regions and call facts | Complete after audit | Canonical CFG/unwind worklist, consumed cleanup states, O(1) merge checks, typed unwind/noreturn propagation |
| Linkage and deterministic symbols | Complete after audit | Local lambda binding, indexed function facts, stable symbol/catch ordering, byte-identical repeated objects |
| Performance and observability | Complete after audit | Plain and EH-heavy scaling series plus region/edge/call/fixup/time/RSS counters |
| Self-containment and ownership | Complete | Direct typed path, incremental MIR release, process trace with no child process |
| Final full-stage audit | Complete | Required gates, representative declaration/template traces, regression, and final ledger |

## Validation

- `make test-pa31`: 18/18 pass (17 assignment tests plus one audit regression).
- `perl scripts/cppgm_file_audit.pl --stage pa31 --paths dev/src`: pass; the
  21 warnings are inherited header-division warnings.
- `make test-report-through-pa31`: 4,150/4,150 tests and 31/31 stages pass.
- `git diff --check`: pass; final audit changes are committed and the worktree
  is clean.

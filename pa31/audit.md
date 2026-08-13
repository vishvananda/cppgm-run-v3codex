# PA31 Final Audit

## Findings

| Finding | Severity / pre-fix evidence | Disposition |
| --- | --- | --- |
| LSDA retained every type selector but emitted only the first handler action and omitted cleanup records from mixed chains | Correctness: a `char` then `int` handler object linked but aborted on thrown `int`; typed cleanup identity had no action-table record | Closed with a reverse-built ordered action chain, zero-filter cleanup actions, and host link/run regression |
| Host call-site state was reset for each MIR block and empty pops were ignored | Correctness/architecture: protected calls after branches could silently lose cleanup ownership | Closed with canonical CFG/unwind state IDs, explicit worklist propagation, consumed cleanup nodes, and strict merge/exit checks |
| Typed direct-call unwind/noreturn facts stopped at the PA30 adapter/MIR boundary; targetless cleanup clauses became pushes | Correctness/architecture: backend overprotected nonthrowing calls and misread landing-pad clauses | Closed by identity-based call fact transfer, MIR propagation, and distinct cleanup-clause adaptation |
| ELF symbol sizing and function/export classification repeatedly scanned all functions | Performance: 8k encode took 404.24 ms and scaled quadratically | Closed with average-O(1) offset/name indexes; final 8k encode is 78.53 ms |
| Unordered label/catch iteration selected output order | Architecture/reproducibility: object bytes depended on incidental hash order | Closed with final stable symbol and catch-reference ordering |

No fixture or existing reference was weakened. One course regression was added
for multiple typed catches.

## Changes

- `lowir_native_host_eh` now interns parent-linked protected states using compact
  block IDs and processes each reachable CFG/unwind edge once. Active and
  consumed cleanup states are separate; calls consult the nearest active owner,
  consumed ownership nodes remain transparent to later source-level pops, and
  pure selector dispatch is distinguished from cleanup forwarding.
- The direct encoder consumes precomputed per-call landing-block facts and
  exports region-state, edge, and call-site counters. Non-EH functions bypass
  this analysis.
- The typed adapter preserves direct callee unwind/noreturn metadata and maps a
  targetless `EH_CLEANUP` to `IK_EH_CLEANUP_CLAUSE`. MIR calls now retain those
  facts for call-site planning.
- LSDA construction emits each landing pad's full ordered action chain with
  correct signed next-action displacement, zero-filter cleanup records, and
  selector/type-table identity.
- ELF collection indexes function sizes and kinds once, then stably orders local
  symbols, global symbols, and catch RTTI reference cells.

## Performance Evidence

| Workload | Pre-audit encode ms | Final encode ms | Final structural counters |
| --- | ---: | ---: | --- |
| 1,000 plain functions | 10.99 | 8.31 | 1,001 functions/fixups; 834,104 bytes |
| 2,000 plain functions | 31.06 | 20.69 | 2,001 functions/fixups; 1,676,104 bytes |
| 4,000 plain functions | 104.09 | 37.18 | 4,001 functions/fixups; 3,360,104 bytes |
| 8,000 plain functions | 404.24 | 78.53 | 8,001 functions/fixups; 6,728,104 bytes |

The EH-heavy 50/100/200/400-function series has exact 1x/2x/4x/8x slopes for
states (250/500/1,000/2,000), edges (550/1,100/2,200/4,400), protected calls
(100/200/400/800), and fixups (1,205/2,405/4,805/9,605). Lowering and output
scale proportionally; final encoding is 6.10/12.17/24.67/58.69 ms.

The demanded-template trace records 3 specialization requests, 2 hits, 1
demand push, and 1 demanded function. It produces two functions, 45 LowIR/69
MIR instructions, 5 EH states, 8 edges, 1 protected call, 27 fixups, and a
28,328-byte object at 7,872 KiB RSS; host link and execution both return 0.

## Architecture Review

The `spec.md` checklist was applied to the actual source-to-object path. Typed
semantic facts flow directly through the PA30 adapter; no textual LowIR or
assembly exists in production. Function-local analysis/MIR and EH worklist
state die after encoding. Final code/data, compact layouts, relocation tables,
and ELF buffers remain TU-wide because their ownership crosses functions.

Semantic/template identity remains compact IDs. At the staged MIR boundary,
labels are indexed once and EH state uses block/state IDs rather than copied
strings. Runtime role, RTTI identity, selected object symbol, local-binding
preference, call unwind/return behavior, and handler selectors are recorded
facts, not lowering-time semantic searches. Final ordering is isolated from hot
lookup containers.

Each supplied function is lowered and encoded once. Region analysis has an
explicit deduplicated worklist and monotonic one-time block-entry fact; a state
mismatch is an invariant error rather than a retry. Symbol/fixup/image work is
linear except explicit final sorts. No global mutable TU cache, complete-program
validator between passes, source-name special case, or external process exists.

## Final Architecture Review

Representative traces cover a normal declaration with cleanup/resume and a
demanded function-template specialization thrown across a typed call and caught
by a non-first handler. Object inspection confirms host runtime imports, RTTI
cells, indirect personality, LSDA action/type facts, CFI, relocation classes,
local lambda binding, and absence of private runtime symbols. Process tracing
shows one `execve` for `cppgm++` and no child process. Repeated compiles are
byte-identical.

The wider earlier-EH host-object compile probe accepts all 45 cases; the three
cases that originally exposed state-transfer gaps now compile, and two nested
cleanup/catch regressions (including the PA30 copy-constructor case) also link
and run. A deeper same-frame catch miss still requires propagating outer
handlers into an inner call-site action chain, which is the README's explicit
nested/multi-frame cleanup boundary and is not claimed as PA31 runtime support.
Required and earlier-stage contracts remain green.

## Checkpoint Ledger

| Checkpoint | Audit result |
| --- | --- |
| Host relocatable/object compatibility | Pass |
| Runtime helpers, RTTI, personality, LSDA/CFI | Pass after full action-chain fix |
| Protected-region architecture | Pass after canonical edge worklist and typed call facts |
| Linkage/determinism | Pass after local-binding verification and stable final ordering |
| Scaling/observability | Pass after indexed symbol collection and EH counters |
| Self-containment/file placement | Pass |
| Final full-stage gate | Pass |

## Validation

- PA31 local/course tests: 18/18 pass.
- File audit: pass with 21 inherited non-fatal warnings.
- Through-PA31 report: 4,150/4,150 tests; 31/31 stages.
- `git diff --check`: pass.
- Final cohesive audit commit; clean `git status --short`.

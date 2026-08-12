# PA29 Final Audit Plan

## Stage Design and Spec Alignment

PA29 owns the standalone `LowIR -> typed MIR -> x86-64 ELF` stage. The actual
flow is:

```text
LowIR files -> lowir_parse -> LowirProgram
            -> program indexes + per-function analysis/ABI/selection
            -> MirProgram -> deterministic MIR view
                          -> direct x86-64 encoding/fixups -> ELF executable
```

`lowir_parse` is the explicit textual adapter allowed by `spec.md` section 6.
The driver passes typed LowIR directly to the backend; no CY86, assembly,
serializer/reparser, host compiler, or reference tool participates. Per-function
analysis owns definition/use, linearized live-interval, fixed-register clobber,
storage, and ABI facts. Selection owns physical-register demand and spills.
`MirProgram` is the single model consumed by both the required debug view and
native encoder. `CodeBuffer` owns bytes, labels, fixups, and internal label
identity through final ELF construction.

This is aligned with the PA29 README and `spec.md` sections 6-10: bounded typed
phase facts, direct machine code, per-function near-linear allocation, direct
ELF emission, observable work counters/timers, and self-contained output.

## Findings and Changes

| Final-audit finding | Full-path change | Result |
| --- | --- | --- |
| MIR-only helper inputs were rejected by shared entry validation | Added an explicit parser entry policy; only PA29 MIR-only mode allows helper-only programs, while executable and earlier-tool paths still require an entry | Helper MIR omits `startup`; executable/declaration error contracts remain strict |
| Slot planning scanned all instructions once per slot | Moved dead-store-only slot classification into the existing single storage-analysis pass | `O(slots + instructions + operands)` |
| Object parameter homes joined parameters to aliases with a nested scan | Indexed parameter identity once and joined each alias by average-`O(1)` lookup | Linear aggregate boundary setup |
| Liveness materialized string sets for every block and scanned all live values at each clobber | Replaced the values-by-block relation with unique-definition live intervals, indexed fixed-register clobbers, direct cross-block facts, and source-order loop extension | `O(IR log IR)` time, `O(IR)` storage; loop/backedge behavior retained |
| Promoted slots scanned every call and wide GPR promotion could reserve conflicting homes | Queried the sorted call index with `upper_bound` and kept wide-boundary promoted values in frame-safe ownership | Near-linear promotion analysis; high-cardinality inputs compile |
| Dead MIR results retained scarce GPR/XMM ownership | Release pool ownership when a definition has no consumers, while retaining callee-save evidence for emitted registers | No pressure failure from dead result chains |
| ELF absolute fixups discarded MIR address addends | Fixups now own signed addends and validate overflow/underflow before patching | Scalar and structured `addr @symbol +/- N` agree between MIR and runtime |
| x87 encoder labels used process-global mutable state | Moved internal label identity into each `CodeBuffer` | Deterministic writer-local ownership |
| Encoder timing existed in telemetry but was never populated or printed | Timed direct encoding/image construction and exposed `encode_ns` | Parse/lower/encode/write phases are independently observable |

No test or reference fixture was changed.

## Performance Evidence

Final release-build measurements use one scaling-sensitive function and the
existing `CPPGM_LOWIR_NATIVE_STATS` counters. Times are backend `lower_ns`; RSS
is `/usr/bin/time` peak KiB.

| Workload | 100 / 1,000 / 5,000 lower time | 100 / 1,000 / 5,000 RSS KiB | Review |
| --- | --- | --- | --- |
| Dead-store-only slots | 0.191 / 1.564 / 8.264 ms | 4,708 / 6,040 / 15,524 | Linear single-pass slot facts; old 8,000 case was 1.232 s |
| Direct-object parameter aliases | 0.903 / 10.425 / 72.370 ms | 4,900 / 10,060 / 34,256 | Linear identity join and bounded ABI pieces; old 4,000 case was 548 ms |
| Promoted slots with intervening calls | 0.906 / 11.072 / 71.992 ms | 4,880 / 11,348 / 40,676 | Sorted-call query; old path failed under wide promotion pressure |
| Cross-block live values | 0.629 / 8.018 / 57.695 ms | 4,936 / 10,716 / 37,632 | Interval/clobber indexes; old 2,000 case was 3.262 s and 585,756 KiB |

The final CFG series also measured 100/250/500/1,000/2,000/4,000/8,000 at
0.63/1.83/3.74/8.05/17.58/38.70/93.94 ms. Existing checkpoint families for
integer/frame, GPR and mixed ABI, SSE, aggregate, x87, atomics, variadics,
i128, storage, branch demand, and wide-call pressure remain covered by their
100/1,000/5,000 evidence and the unchanged PA29 structural/runtime suite.

## Architecture Review

- Representation and ownership: explicit LowIR input has one parsed typed
  model; MIR has one owning model plus optional rendered text. Text is never
  reparsed. Per-function analysis and allocator state die with
  `FunctionLowerer`; ELF bytes/labels/fixups die with `CodeBuffer`.
- Identity and lookup: PA13 textual names are indexed once at this standalone
  adapter boundary. Repeated function, block, parameter, TLS, slot, and value
  lookup uses direct hash indexes; no rendered signature or whole-program
  fallback is used. MIR opcodes, registers, conditions, and operand kinds are
  typed enums; fixed-vocabulary type tags are preserved for the required dump.
- Templates and demand: source template state is not present at PA29; PA15-PA28
  have already emitted demanded LowIR units. PA29 lowers every supplied
  function/global once and does not rediscover source semantics.
- Lowering and backend: direct/indirect calls, branches, switches, bulk memory,
  structured globals, atomics, scalar/XMM/x87 values, variadics, i128, and ABI
  pieces survive into first-class MIR. Dumping and encoding consume the same
  final frame and instruction facts. Native bytes and ELF are emitted directly.
- Allocation and scaling: physical pools are constant-size; live intervals and
  clobber indexes are per function; loop extension uses a monotonic block-range
  sweep; storage/call/alias joins are linear or logarithmic. No fixed-point
  whole-program retry, values-by-block relation, or per-slot instruction rescan
  remains.
- Self-containment: implementation searches find no process launch, host
  compiler, reference binary, CY86, filename/test recognition, or cached-answer
  path. The only filesystem writes are the requested MIR and executable.

## Final Architecture Review

All final-audit findings are closed across their parser, analysis, selection,
MIR, encoder, and driver owners. Representative direct data flow was traced
from a structured global address addend through LowIR parsing, MIR rendering,
absolute fixup resolution, ELF bytes, and a native exit value of 42. A second
trace covered a pre-loop value through block/loop interval extension, a call
clobber, spill ownership, MIR, and native loop tests.

Two deliberate PA29-stage boundaries remain: this standalone tool accepts
textual LowIR, and it retains `MirProgram` so the requested dump and executable
consume exactly the same model. Both are explicit assignment surfaces allowed
by the spec; the later in-process object path must bypass textual transport and
may encode/reclaim functions incrementally. Neither path introduces CY86,
assembly, semantic reconstruction, or an alternate production behavior here.

No open correctness, architecture, self-containment, timeout, file-placement,
or performance finding remains for PA29.

## Checkpoint Ledger

| Checkpoint | Final status | Evidence |
| --- | --- | --- |
| Foundation typed MIR and direct ELF | Complete | Direct model/view/encoder path and startup/global/function anchors |
| Integer, pointer, frame selection | Complete | Width, slot, branch, index, division/shift, and frame oracles |
| GPR ABI and stack calls | Complete | Direct/indirect, six-register, stack-argument, and pressure cases |
| Scalar f32/f64 and XMM ABI | Complete | Floating arithmetic/conversion, mixed ABI, and call-clobber cases |
| CFG liveness and reactive spilling | Complete | Loop/join runtime tests plus final interval scaling evidence |
| SysV aggregate ABI and bulk storage | Complete | One/two-eightbyte, padding, copy/zero, and alias scaling |
| x87/f80 scalar and ABI path | Complete | Arithmetic, comparisons, calls, conversions, writer-local labels |
| Scalar atomics and fences | Complete | i1/i8/i16/i32/i64/ptr operations and pressure cases |
| SysV variadic state | Complete | GPR/XMM save area, vector count, and `va_start` cases |
| i128 scalar/atomic ABI | Complete | Two-limb values, calls, equality, and cmpxchg16b anchors |
| Storage identity/materialization | Complete | Parameter aliases, TLS, readonly/global alignment, and scaling |
| Direct branch demand/result forwarding | Complete | Integer/pointer/float direct branches and deferred comparisons |
| Allocation/parameter identity/call setup | Complete | Final pressure anchors and dead-result ownership |
| Final full-stage audit | Complete | Helper-only policy, addend runtime probes, four scaling families, full architecture checklist |

## Validation

| Gate | Final evidence |
| --- | --- |
| PA29 local | `make -C pa29 test`: 183/183 pass (36 strict, 59 structural, 88 behavior) |
| Helper-only MIR | MIR-only helper succeeds without `startup`; the same input with `-o` fails; declaration-only entry still fails |
| Address addends | Positive and negative structured/scalar probes both execute with exit 42 |
| File audit | `perl scripts/cppgm_file_audit.pl --stage pa29 --paths dev/src`: pass; 21 warnings are unchanged earlier-stage header-division warnings |
| Through-stage report | `make test-report-through-pa29`: 4,040/4,040 tests and 29/29 stages pass |
| Repository | Cohesive final-audit commit; final `git status --short` verified empty at handoff |

# PA38 Final Audit Plan and Ledger

## Current Stage Design and Spec Alignment

The shared path is `typed LowIR -> function-local physical-register MIR ->
machine_opt(level) -> direct ELF encoding`. `ProgramLoweringSession` lowers,
optimizes, encodes, and releases one function at a time for ordinary `cppgm++`
object and executable output. The explicit `lowir2native` dump tool retains a
whole MIR program because its interface can request a dump and executable from
the same optimized value.

O1 uses fixed physical-register masks and per-block value facts for copy,
immediate, frame-address, zero-test, ABI, and branch cleanup. O2 first lays out
unconditional-jump traces and then finalizes the save set and stack reservation.
The optimized MIR is authoritative at encoding: branch, return, atomic,
bulk-memory, frame, and call operands and the final `stack_size` are the facts
serialized by the dump and consumed by the encoder. Lowering-only frame
requirements are combined with the surviving save set before that boundary;
the encoder does not reconstruct a different frame from binding names.

This matches `spec.md` §§6-10: typed one-way lowering, a small explicit pass
budget, per-function ownership, indexed/monotonic dataflow, direct ELF output,
near-linear work, opt-in telemetry, and no external compiler or reference path.
PA38 performs no source-language, lookup, or template work.

## Findings

| ID | Audit finding | Resolution |
| --- | --- | --- |
| F1 | `xchg`/`xadd` register outputs and `cmpxchg`'s implicit RAX result were absent from MIR effects; alias rewriting changed atomic results. | Model read/write operands and implicit RAX uses/defs; do not rewrite an exchanged output as a read-only input. |
| F2 | Scalar `f32`/`f64` returns were implicit, so dead-definition removal discarded the final XMM value. | O1/O2 canonicalize scalar returns to explicit `ret xmmN`; the encoder validates the function ABI and moves that operand to XMM0. O0 remains PA29-compatible. |
| F3 | Native encoding recomputed stack space from named bindings and ignored MIR scratch/pressure capacity; the first audit repair then preserved alignment padding too conservatively at O2. | Make `stack_size` the encoder authority and retain typed local/floor requirements only until O2 recombines them with surviving saves. |
| F4 | A zero-only structured global had alignment one, so code-size changes made its address unstable; mixed zero padding must still not raise typed-data alignment. | Use typed items for mixed data; for wholly zero storage use the natural power-of-two divisor of total size, capped at 16. |
| F5 | CFG successor deduplication linearly searched a growing vector, making one K-way switch O(K^2). | Use one dense target-mark vector, keeping edge construction O(B+E). |
| F6 | Optimizer telemetry had visits and time but no peak analysis storage, and `lowir2native` paid collection overhead when stats were not requested. | Add peak function-analysis bytes and make tool telemetry opt-in; compiler-driver telemetry was already opt-in. |
| F7 | The final data-layout repair pushed the monolithic ELF emitter over the PA38 file-size ceiling. | Move native scalar-size and global-alignment policy to the responsibility-named `lowir_native_data_layout` module and link it into both consumers. |

No correctness, architecture, performance, self-containment, or file-audit
finding remains open.

## Changes

- Corrected atomic use/def and rewrite ownership across optimizer and encoder.
- Made optimized scalar-float return values explicit and ABI-checked.
- Removed encoder-side frame reconstruction; added exact O2 frame finalization
  from lowering requirements and surviving saves.
- Preserved deterministic zero-only global alignment without changing mixed
  typed-data padding rules.
- Replaced quadratic high-fanout CFG deduplication with indexed marks.
- Added separable peak-memory telemetry through `lowir2native` and both
  `cppgm++` native output paths.
- Split global data-layout policy from the ELF byte emitter and registered the
  module in both tool source sets.

## Performance Evidence

Release binaries were driven from generated LowIR through `/dev/stdin`.
`machine_opt_ns` and counters isolate the optimizer; process RSS includes the
parser, typed LowIR, explicit dump path, and optimizer.

O2 four-operation jump-chain workload:

| Blocks | MIR input/output | Visits / CFG edges / pushes | Peak analysis | Optimizer | RSS |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1,000 | 3,001 / 2,002 | 7,005 / 1,998 / 1,000 | 184,856 B | 2.97 ms | 12,332 KiB |
| 2,000 | 6,001 / 4,002 | 14,005 / 3,998 / 2,000 | 370,840 B | 5.23 ms | 20,504 KiB |
| 4,000 | 12,001 / 8,002 | 28,005 / 7,998 / 4,000 | 744,680 B | 10.49 ms | 36,000 KiB |
| 8,000 | 24,001 / 16,002 | 56,005 / 15,998 / 8,000 | 1,490,168 B | 22.61 ms | 67,232 KiB |

The high-fanout switch profile located F5. Before the indexed fix, 8,000 and
16,000 cases took 24.85 ms and 75.16 ms in the optimizer. After the fix:

| Cases | MIR input | Visits / CFG edges / pushes | Peak analysis | Optimizer | RSS |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1,000 | 5,005 | 15,015 / 2,002 / 1,002 | 185,760 B | 3.92 ms | 9,452 KiB |
| 2,000 | 10,005 | 30,015 / 4,002 / 2,002 | 372,312 B | 6.09 ms | 14,216 KiB |
| 4,000 | 20,005 | 60,015 / 8,002 / 4,002 | 747,288 B | 11.10 ms | 23,680 KiB |
| 8,000 | 40,005 | 120,015 / 16,002 / 8,002 | 1,495,048 B | 26.99 ms | 42,972 KiB |
| 16,000 | 80,005 | 240,015 / 32,002 / 16,002 | 2,991,424 B | 54.01 ms | 81,312 KiB |

All work and live-storage counters are linear. A source-input `cppgm++ -O2`
link independently reported 2 input/2 output MIR instructions, 6 visits, one
worklist push, and a functioning generated executable, confirming reuse outside
the dump tool.

## Architecture Review

- Representation/ownership: no LowIR or MIR text round trip exists in the
  production source path. MIR and analysis allocations are per function; the
  full MIR owner exists only for the explicit dump interface.
- Identity/lookup: physical registers use fixed bit IDs. Label strings are
  indexed once per function into dense block IDs; CFG and liveness hot paths use
  those IDs and dense vectors. Deterministic order is a final vector order.
- Templates/repeated work: not a PA38 surface. No semantic/template search,
  completion, retry, or cache is invoked by machine optimization.
- Lowering/backend: each function is selected and optimized once. No
  per-function pass scans other functions or globals. Machine bytes,
  relocations, ELF sections, and executable images are emitted directly.
- Allocation/scaling: value facts are fixed arrays, vectors grow geometrically,
  CFG edges are indexed, and liveness uses a dirty predecessor worklist with a
  fixed-register monotonic mask. Telemetry is optional and observes the same
  path.
- Self-containment: the backend contains no shell-out, host compiler,
  assembler, reference binary, fixture lookup, filename recognition, or cached
  answer path.

## Final Architecture Review

Representative end-to-end traces were checked for (1) atomic exchange under
loop pressure, from LowIR atomic selection through read/write MIR effects to
x86 `xchg`; (2) scalar-float return across an integer call, from XMM value facts
through explicit return liveness to ABI encoding; and (3) O2 callee-save
removal, from local copy cleanup through final `stack_size` to prologue
allocation. MIR dumps and executable behavior agree in each trace.

The final implementation has one forward typed data flow, bounded
function-local analyses, direct ELF output, and O(I + (B+E)R) optimization with
fixed x86-64 register count R. The high-fanout edge path is O(E), frame output
has a single authority, and telemetry can be removed from the ordinary path by
leaving its environment switch unset. Final gate status is recorded below.

## Validation

- PA38 primary: 24/24 pass.
- PA38 debug metadata: 8/8 pass.
- Independent optimized PA29 replay: 366/366 O1/O2 build/status/stdout checks
  pass, including atomic, scalar-float return, global alignment, and frame
  witnesses.
- Shared source-driver O2 path: compile/link succeeds and reports machine-pass
  telemetry from the shared session.
- Required file audit: pass (23 pre-existing nonfatal division warnings).
- Required cumulative report: 5,089/5,089 tests and 38/38 stages pass.

## Checkpoint Ledger

| Checkpoint | Result |
| --- | --- |
| Provider baseline | fileAudit pass reused; 5,089/5,089 tests and 38/38 stages reported passing. |
| Contract reconstruction | `spec.md`, PA38 README/tests, stage commit, changed ownership paths, prior cumulative log, and plan reviewed independently. |
| Correctness audit | F1-F4 and exact O2 frame finalization fixed; 366/366 optimized PA29 behavior checks pass. |
| Performance audit | F5 fixed; jump-chain and high-fanout counters, peak bytes, isolated optimizer time, and RSS scale linearly. |
| Architecture/self-containment audit | Function ownership, typed facts, direct ELF, shared driver reuse, telemetry separation, and forbidden fallback search pass. |
| PA38 lanes | 24/24 primary and 8/8 debug tests pass. |
| Final required gates | `cppgm_file_audit.pl --stage pa38 --paths dev/src` passes; `make test-report-through-pa38` passes 5,089/5,089 across 38/38 stages. |

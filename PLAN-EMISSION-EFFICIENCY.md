# Plan: Frozen Object Emission Efficiency

Status: active

Date: 2026-08-17

## Objective

Reduce unnecessary native code and exception metadata in the frozen
`semantic_overload.cpp -O0` object without changing established LowIR or MIR
fixtures.  Evaluate one hypothesis at a time so fixture effects and object-size
effects remain attributable to one change.

The frozen source is read from the extended PA39 checkout, which remains
read-only:

`~/cppgm-extended-pa39-source-layout/benchmarks/self_compile/stable/semantic_overload.cpp`

## Decision protocol

For each candidate:

1. Implement only that candidate in the shared production path.
2. Build the compiler and record frozen object size, allocatable code/EH bytes,
   relevant function sizes, and a short timing screen.
3. Run the earliest owning PA with root `make test-report` and explicit PA
   names so every failure is reported at once.
4. If an existing checked-in LowIR, MIR, inspect, or behavior fixture changes,
   revert the implementation and mark the candidate `deferred: fixtures` here.
   Do not update the fixture in this pass.
5. If the change exposes an uncovered case, generate its reference only through
   the documented `ref-test` target.  If the reference compiler disagrees with
   the proposed shape, keep the source under `proposed/paN/` instead of adding a
   manually edited oracle.
6. If no existing fixture changes, run the downstream report set and retain the
   change only when it has a concrete structural benefit and preserves runtime
   behavior.
7. Commit each retained compiler changeset separately.  Run the full report,
   zero-fatal file audit, and inception comparison before completing the batch.

## Current baseline

Baseline implementation commit: `d549070e` (the later `5205d9d1` commit adds
only the proposed PA26 test directory).

Frozen `-O0` measurements after shared constructor cleanup suffixes:

| Metric | Baseline |
| --- | ---: |
| Three-run compile median | 6.09 s |
| Peak RSS median | 365,804 KiB |
| Object bytes | 4,141,520 |
| Native text bytes | 1,408,476 |
| `.gcc_except_table` bytes | 75,773 |
| `.eh_frame` bytes | 145,188 |
| Text plus EH bytes | 1,629,437 |

The remaining allocated-size gap from the last GCC comparison is concentrated
in emitted code and EH metadata, not symbols or ordinary data.

## Candidate ledger

| ID | Candidate and earliest owner | Expected structural effect | Fixture result | Frozen result | Decision |
| --- | --- | --- | --- | --- | --- |
| E1 | Share constructor-unwind destructor suffixes; PA26 exception-aware construction | Replace copied cleanup prefixes with one destructor block per constructed subobject | Existing PA16--PA38 fixtures unchanged; proposed PA26 reference uses a different valid layout | Object -27,376 bytes; code/EH -12,448 bytes; `Scope::Scope` 8,254 to 1,952 bytes | **Landed** in `d549070e`; proposed stress case in `5205d9d1` |
| E2 | Stop reserving frame holes for forwarded and dead-store slots; PA29 native lowering | Reduce frame size and avoid wide stack displacements without changing LowIR | Existing PA29--PA38 fixtures unchanged (1,274 tests); proposed PA29 reference retains dead slots | Object/text -4,624 bytes; three-run compile median unchanged at 6.09 s | **Retain**; proposed regression under `proposed/pa29/` |
| E3 | Relax known forward x86 branches after layout; PA29 native encoding | Use short encodings for local forward branches while keeping final offsets, EH ranges, symbols, and fixups coherent | Existing PA29--PA38 fixtures unchanged (1,274 tests); full report 5,165/5,165 | Object -56,504 bytes; text -56,246 bytes; EH -266 bytes; deterministic across three loaded-host runs | **Retain**; final function layout repatches existing backward rel8 branches and translates host EH call-site ranges |
| E3P | Make E3 branch compaction linear in the current function; PA29 native encoding | Compact bytes in place and adjust only the current function's labels and fixups, with binary-search offset translation | Exact E6 frozen object SHA preserved; PA29--PA38 1,274/1,274 and full report 5,165/5,165 remain clean | Current E6 stats screen: wall 15.16 to 6.18 s and encoding 9.081 to 0.249 s; object remains 3,977,672 bytes | **Retain**; fixes the quadratic whole-buffer copying and rescanning accidentally introduced with E3 |
| E4 | Coalesce adjacent constant byte stores during PA29 native encoding | Replace repeated address setup and scalar stores with packed 64/32/16/8-bit stores while restoring the final MIR-defined registers | Existing PA29--PA38 fixtures unchanged (1,274 tests); full report 5,165/5,165; proposed runtime reducer passes both implementations but reference MIR uses a different register/frame layout | Object -11,760 bytes; text -11,754 bytes; each frozen `__to_chars_10_impl` specialization falls from about 5.0 KiB to about 1.1 KiB | **Retain**; proposed regression under `proposed/pa29/` |
| E5 | Forward an immediately reloaded 64-bit frame store during PA29 native encoding | Keep the architecturally visible store but replace the following reload with a register move, or no instruction when its destination already contains the value | Existing PA29--PA38 fixtures unchanged (1,274 tests); full report 5,165/5,165 | Object -53,720 bytes; text -53,398 bytes; EH -329 bytes; `analyze_call_expression` -15,486 bytes | **Retain**; encoding-only change preserves O0 MIR and final frame/register state |
| E6 | Elide single-use temporary frame stores paired with an adjacent 64-bit reload; PA29 native encoding | Prove from all MIR operands that the compiler-only home has exactly one store and one load, then forward the register without materializing unobservable temporary memory | Existing PA29--PA38 fixtures unchanged (1,274 tests); full report 5,165/5,165 | Object -37,240 bytes; text -36,836 bytes; EH -408 bytes; `analyze_call_expression` -14,536 bytes | **Retain**; one linear use-count pass per function preserves O0 MIR and final observable state |
| E7 | Extend scalar temporary forwarding beyond adjacent store/reload pairs; PA29 native encoding | Reduce the remaining load/store and displacement volume in `analyze_call_expression` and other shared bodies | Pending | Establish bounded alias, call, and register-clobber rules before editing | **Investigation**; split into independently measurable local patterns |

## Deferred candidates

Move an entry here as soon as an existing fixture changes.  Record the exact
PA report, failing fixture count, and whether behavior remained correct; do not
carry the experimental implementation into the next candidate.

None yet.

## Completion gate

The batch is complete only when retained changes have:

- deterministic frozen output and recorded structural measurements;
- a clean full `make test-report`;
- zero fatal file-audit findings;
- a byte-identical PA39 self/inception comparison; and
- separate pushed commits with this ledger updated to their final hashes.

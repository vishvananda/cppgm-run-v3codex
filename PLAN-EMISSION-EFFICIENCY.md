# Plan: Frozen Object Emission Efficiency

Status: complete

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
   a course-approved shape, update the authoritative reference before adding
   the active fixture; never add a manually edited oracle.
6. If no existing fixture changes, run the downstream report set and retain the
   change only when it has a concrete structural benefit and preserves runtime
   behavior.
7. Commit each retained compiler changeset separately.  Run the full report,
   zero-fatal file audit, and inception comparison before completing the batch.

## Current baseline

Baseline implementation commit: `d549070e` (the later `5205d9d1` commit added
the PA26 stress input that normalization commit `ac1c39a2` activated in the
course suite).

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

## Corrected controlled timing

After E3P removed the accidental whole-buffer rescans, three interleaved
`--stats` runs under the same host conditions produced:

| Variant | Wall median | User median | Encode median | RSS median | Object bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| E4 baseline | 6.16 s | 5.60 s | 0.243 s | 365,256 KiB | 4,068,632 |
| E5 | 6.17 s | 5.62 s | 0.248 s | 365,160 KiB | 4,014,912 |
| E6 | 6.29 s | 5.74 s | 0.259 s | 365,504 KiB | 3,977,672 |

E5 is timing-neutral versus E4.  E6's additional linear MIR-operand census is
about 1.9% in the wall median while remaining below the project's 3% noise and
RSS guardrails.  Keep later measurements on the corrected E3P implementation;
the earlier 15-second screens measured the quadratic relaxation bug rather
than host contention or E5/E6 cost.

## Candidate ledger

| ID | Candidate and earliest owner | Expected structural effect | Fixture result | Frozen result | Decision |
| --- | --- | --- | --- | --- | --- |
| E1 | Share constructor-unwind destructor suffixes; PA26 exception-aware construction | Replace copied cleanup prefixes with one destructor block per constructed subobject | Existing PA16--PA38 fixtures unchanged; the older PA26 reference used a different valid layout | Object -27,376 bytes; code/EH -12,448 bytes; `Scope::Scope` 8,254 to 1,952 bytes | **Landed** in `d549070e`; stress case added in `5205d9d1` and activated by normalization commit `ac1c39a2` |
| E2 | Stop reserving frame holes for forwarded and dead-store slots; PA29 native lowering | Reduce frame size and avoid wide stack displacements without changing LowIR | Existing PA29--PA38 fixtures unchanged (1,274 tests); the older PA29 reference retains dead slots | Object/text -4,624 bytes; three-run compile median unchanged at 6.09 s | **Landed** in `05b46fd9`; regression is active in the PA29 structural course lane |
| E3 | Relax known forward x86 branches after layout; PA29 native encoding | Use short encodings for local forward branches while keeping final offsets, EH ranges, symbols, and fixups coherent | Existing PA29--PA38 fixtures unchanged (1,274 tests); full report 5,165/5,165 | Object -56,504 bytes; text -56,246 bytes; EH -266 bytes; deterministic across three loaded-host runs | **Landed** in `633fe401`; final function layout repatches existing backward rel8 branches and translates host EH call-site ranges |
| E3P | Make E3 branch compaction linear in the current function; PA29 native encoding | Compact bytes in place and adjust only the current function's labels and fixups, with binary-search offset translation | Exact E6 frozen object SHA preserved; PA29--PA38 1,274/1,274 and full report 5,165/5,165 remain clean | Current E6 stats screen: wall 15.16 to 6.18 s and encoding 9.081 to 0.249 s; object remains 3,977,672 bytes | **Landed** in `eb0f7c2a`; fixes the quadratic whole-buffer copying and rescanning accidentally introduced with E3 |
| E4 | Coalesce adjacent constant byte stores during PA29 native encoding | Replace repeated address setup and scalar stores with packed 64/32/16/8-bit stores while restoring the final MIR-defined registers | Existing PA29--PA38 fixtures unchanged (1,274 tests); full report 5,165/5,165; runtime reducer passes both implementations while reference MIR uses a different valid register/frame layout | Object -11,760 bytes; text -11,754 bytes; each frozen `__to_chars_10_impl` specialization falls from about 5.0 KiB to about 1.1 KiB | **Landed** in `a1c74fb1`; regression is active in the PA29 behavior course lane with informational MIR |
| E5 | Forward an immediately reloaded 64-bit frame store during PA29 native encoding | Keep the architecturally visible store but replace the following reload with a register move, or no instruction when its destination already contains the value | Existing PA29--PA38 fixtures unchanged (1,274 tests); full report 5,165/5,165 | Object -53,720 bytes; text -53,398 bytes; EH -329 bytes; `analyze_call_expression` -15,486 bytes | **Landed** in `ec680854`; encoding-only change preserves O0 MIR and final frame/register state |
| E6 | Elide single-use temporary frame stores paired with an adjacent 64-bit reload; PA29 native encoding | Prove from all MIR operands that the compiler-only home has exactly one store and one load, then forward the register without materializing unobservable temporary memory | Existing PA29--PA38 fixtures unchanged (1,274 tests); full report 5,165/5,165 | Object -37,240 bytes; text -36,836 bytes; EH -408 bytes; `analyze_call_expression` -14,536 bytes | **Landed** in `c7e1715e`; one linear use-count pass per function preserves O0 MIR and final observable state |
| E7 | Forward a single-use temporary reload across one independent register instruction; PA29 native encoding | Skip the unobservable store/reload when the sole intervening load, LEA, or move writes a different register | Existing PA29--PA38 fixtures unchanged (1,274 tests); full report 5,165/5,165 | Object -5,368 bytes; text -5,318 bytes; EH -34 bytes; `analyze_call_expression` -1,485 bytes; timing screen 6.15 s | **Landed** in `714575a6`; all calls, stores, EH markers, arithmetic, and source-register definitions remain barriers |
| E8 | Extend E7 across short runs of independently checked instructions; PA29 native encoding | Reuse the same per-instruction proof for two to five intervening load/LEA/move instructions | Existing PA29--PA38 fixtures unchanged (1,274 tests); full report 5,165/5,165 | Object -6,544 bytes; text -6,502 bytes; EH -55 bytes; `analyze_call_expression` -2,387 bytes; timing screen 6.15 s | **Landed** in `a5997d8e`; the five-instruction bound captures all but no-value outliers without requiring CFG dataflow |
| E9 | Extend frame reload forwarding to narrow integer transfers; PA29 native encoding | Preserve x86 partial-register semantics with width-matched register moves for 8/16/32-bit store/reload pairs | Existing PA29--PA38 fixtures unchanged (1,274 tests); full report 5,165/5,165 | Object -8,144 bytes; text -8,090 bytes; EH -41 bytes; `analyze_call_expression` -3,196 bytes; timing screen 6.12 s | **Landed** in `d613a2d0`; width-matched register moves reproduce each x86 load's partial-register behavior |

## Optimization placement

E2, E3/E3P, and E5--E9 operate on facts created only by native lowering:
frame bindings, physical registers, x86 branch widths, and final code offsets.
They therefore belong in the backend and should not appear in pre-LowIR output.
The E5--E9 traffic is introduced by reactive register allocation, not emitted by
the frontend LowIR builder.  A future allocator could avoid those homes while
building MIR, but doing so would deliberately change PA29's checked O0 MIR;
the late encoder proof removes the machine operations without changing that
assignment contract.

E4 is a target-specific instruction-selection peephole and is appropriate at
`-O0`: optimization level zero does not require literal one-for-one encoding
of MIR artifacts.  Packing byte stores relies on x86-64 little-endian layout,
legal unaligned stores, immediate encodings, and preservation of the physical
register state defined by MIR, so spelling the result as a wide LowIR store
would put target facts above the native backend boundary.  A frontend may
instead canonically lower an actual constant aggregate or string initializer
to constant data or a bulk-memory operation even at `-O0`; that is distinct
from rewriting an arbitrary sequence of scalar LowIR stores.  The encoder
peephole remains useful for both canonical and handwritten scalar LowIR while
preserving the established O0 LowIR/MIR fixtures.

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

## Final evidence

The retained E2--E9 series reduces the frozen object from 4,141,520 to
3,957,616 bytes (183,904 bytes, 4.44%).  Native `.text` falls from 1,408,476
to 1,225,708 bytes (182,768 bytes, 12.98%); `.gcc_except_table` falls from
75,773 to 74,640 bytes, and `.eh_frame` remains 145,188 bytes.  Separating the
shared x86 encoders in `fb07fb77` preserves the final frozen object exactly at
SHA-256 `2cb1b4717cff7536b0f42dd630a6a9c143152ea4ba8a33dc5d7992c9b0007436`.

Final validation on the committed source:

- `make test-report`: pass, 5,165/5,165.
- `perl scripts/cppgm_file_audit.pl --stage pa39 --paths dev/src`: pass with
  zero fatal findings and the 23 pre-existing advisory header warnings.
- Clean 32-way `cppgm++-self`: 18.21 seconds wall, 414.48 seconds user,
  35.65 seconds system, and 330,164 KiB maximum per-process RSS.
- Clean 8-way inception generation and comparison: 4:19.47 wall,
  1,997.63 seconds user, 39.02 seconds system, and 301,320 KiB maximum
  per-process RSS.  All 154 objects match; the two 26,874,936-byte binaries
  have SHA-256
  `1d28155b35b66ace3ea3890261c28921b5ffb65e3a892384e217883060a4c382`.

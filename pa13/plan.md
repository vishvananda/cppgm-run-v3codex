# PA13 Final Audit

## Stage Design and Spec Alignment

PA13 is the explicit textual-adapter exception allowed by `spec.md` section 6.
`lowir2cy86` reads each LowIR file in command-line order, lexes one token
sequence, parses one typed `LowirProgram`, releases the token sequence, validates
the complete explicit-input program, and mechanically renders PA9 CY86. The
driver only owns CLI, timing, and output-file I/O; `lowir_parse.cpp` owns lexical
construction and validation; `lowir_cy86.cpp` owns program symbol classification,
per-function layout, and CY86 lowering. It invokes no CY86 compiler, host
compiler, prior stage, reference binary, or cached result.

Types cross the parse/lower boundary as kind, width, storage size, and alignment
facts plus their durable spelling. Built-in equality is kind identity and object
type equality is the fixed `(size, alignment)` pair; lowering no longer parses
type or generated-memory strings. Serialized names remain necessary adapter
spellings, but each owner uses an average-O(1), owner-bounded index. Parser token
storage dies before validation, validation indexes die before lowering,
function-local locations die after one function, and the CY86 buffer is moved
to the driver without a second owning output copy.

Representative typed trace: `atomic_compare_exchange i8` becomes one typed
instruction whose operands are checked against function-local/top-level indexes;
its result type is recorded as canonical `i64`, frame layout allocates the `i64`
result location, and lowering emits the single-threaded compare/update plus a
full-width canonical boolean. A generated executable exercises that path and
returns 0 on failure as required. A second trace carries `obj<4x4>` size/alignment
facts through hidden-result layout and an exact four-byte typed memory copy; the
generated executable returns 7 without an over-copy.

Templates, C++ lookup/demand, machine IR, ELF writing, and production
source-to-object transport are not PA13 surfaces. The applicable `spec.md`
checks are typed adapter ownership, bounded lookup, direct one-unit lowering,
linear scaling, observability, and self-containment.

## Performance Evidence

Fresh release measurements used one generated add-chain function, three runs
per size, `CPPGM_LOWIR_STATS=1`, and `/usr/bin/time`. Generated CY86 was
byte-identical before and after the audit.

| Links | Bytes / tokens / instructions / output | Before parse / lower / RSS | Final parse / lower / RSS |
|---:|---:|---:|---:|
| 2,000 | 69,869 / 16,019 / 2,002 / 165,551 | 8.91 / 6.35 ms / 8,728 KiB | 7.33 / 5.37 ms / 7,596 KiB |
| 4,000 | 141,869 / 32,019 / 4,002 / 333,551 | 18.02 / 12.89 ms / 13,668 KiB | 15.90 / 11.73 ms / 11,876 KiB |
| 8,000 | 285,869 / 64,019 / 8,002 / 669,551 | 37.86 / 26.91 ms / 23,536 KiB | 32.48 / 22.60 ms / 20,184 KiB |

Semantic work and output double with each size. Final parse/lower time remains
linear, and the 8,000-link audit reduced parse time 14.2%, lowering time 16.0%,
and RSS 14.2%. Counters explain the measured growth, so no unexplained slow path
remained to sample-profile.

## Architecture Review

- Representation/ownership: current file bytes, a compact token vector, and the
  typed program overlap only during parsing. Tokens are released before
  complete-program validation. One typed program then feeds one CY86 output
  buffer; the output buffer is moved at the driver boundary.
- Identity/lookup: LowIR types carry fixed typed facts and compare in O(1).
  Top-level, function-signature, local-value, slot, block, role, TLS-wrapper,
  and alias lookups are owner-bounded hash indexes; source vectors preserve
  observable ordering. Lowering does not reconstruct type facts from spelling.
- Lowering/backend: each function receives one bounded layout scan and one
  emission scan. There is no per-function whole-program scan, fixed-point retry,
  serializer/parser round trip, semantic fallback, optimizer, or native backend
  on this explicit CY86-text adapter path. Typed memory references replaced a
  helper that reparsed rendered `[bp-N]` strings.
- Allocation/scaling: vectors grow geometrically; validation indexes are
  phase-local; layout indexes are function-local; no `shared_ptr`, explicit
  per-node `new`/`delete`, ordered semantic map, mutable process-global cache, or
  retained source path exists. Large bulk operations perform work proportional
  to required CY86 output bytes.
- Self-containment: source inspection finds only input/output file I/O, and a
  process-only `strace` of an EH input records the initial `execve` and exit with
  no child process or external tool invocation.

## Final Architecture Review

The audit removed all identified representation, type-identity, output-copy,
metadata-domain, narrow-result, exact-object-copy, and indirect-call ownership
defects while preserving all checked CY86 fixtures. Sanitizers, focused runtime
probes, linear scaling, the file audit, and the through-stage report cover the
applicable PA13 architecture.

One correctness conflict remains external to an implementation-only fix: the
checked positive CY86 fixtures for `100-small-direct-object-argument` and
`200-call-boundary-metadata` require argument-staging sequences that overwrite
live argument registers. Both fixture-matching CY86 programs segfault when run
through the passing PA9 `cy86` tool. Correct staging changes the required CY86
text, while repository policy makes checked `.ref` text the grading oracle and
forbids editing or regenerating it without the documented reference path (PA13
has no reference binary). The textual PA13 gate passes, but semantic runnable
equivalence cannot honestly be marked complete until the oracle is corrected or
the fixture-defined behavior is explicitly accepted.

## Checkpoint Ledger

| Checkpoint | Result | Evidence |
|---|---|---|
| Contract, history, and full-stage reconstruction | Pass | `spec.md`, PA13 README/LowIR/grammar, tests, implementation commit, source, and prior plan reviewed |
| Typed ownership and architecture | Pass | Typed type facts, released token phase, moved output, typed memory references, owner-bounded indexes |
| Structural correctness hardening | Pass | 96/96 handout fixtures plus four new rejection regressions |
| Representative runtime probes | Partial | Indirect five-argument=15, `obj<4x4>` return=7, narrow CAS failure=0; two required fixture programs segfault |
| Scaling and observability | Pass | 2k/4k/8k counters, timings, RSS, and byte-identical output above |
| Sanitizers and self-containment | Pass | ASan+UBSan 100/100; process trace has no child execution |
| File audit | Pass | `perl scripts/cppgm_file_audit.pl --stage pa13 --paths dev/src` |
| Through-stage validation | Pass | `make test-report-through-pa13`: 920/920 tests, 13/13 stages |
| Semantic PA13 completion | Blocked by checked oracle | Correct direct object/five-argument staging cannot both match the current positive `.ref` files and run correctly |

## Findings

The implementation reconstructed type shape from strings on every lowering
use, retained an unused source-path copy in every token through validation,
copied LowIR types into hot validation maps, duplicated the final CY86 buffer,
and reparsed generated memory expressions. Validation admitted negative debug
coordinates, `storage=writable`, function-only metadata on globals, and a
declaration-only entry role. Narrow compare-exchange stored only the operand
width into an `i64` result location; small object returns copied eight bytes at
a time; indirect calls with stack arguments overwrote their saved callee.
These implementation defects are fixed. The two fixture/runtime contradictions
described above remain the first completion blocker.

## Changes

`LowType` now carries typed identity and layout facts. Validation maps borrow
those facts rather than copying rendered types. Token storage is released before
validation, function declaration/definition lookup is consolidated, CY86 output
is moved, and typed `MemoryRef` spans perform exact copies. Metadata domains,
positive locations, storage values, and entry definitions are enforced. Narrow
atomic booleans are canonical `i64`; narrow object returns copy exact bytes; and
indirect stack calls reserve separate callee storage and stage stack operands
before register operands. Four focused rejection tests cover the new validation.

## Validation

- PA13 checked text: 96/96 handout fixtures and 4/4 audit regressions pass.
- ASan+UBSan: 100/100 PA13 fixtures pass.
- Runtime probes: indirect five-argument exit 15, `obj<4x4>` return exit 7,
  narrow compare-exchange failure exit 0.
- Required file audit: pass, 55 implementation files inspected through includes.
- Required through-stage report: pass, 920/920 tests and 13/13 stages.

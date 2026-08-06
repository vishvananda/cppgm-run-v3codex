# PA14 Final Audit

## Stage Design and Spec Alignment

PA14 is the explicit ABI-fact text adapter permitted by `spec.md`, not a C++
source, semantic-analysis, lowering, backend, or object stage. The ordinary
`abimangle` path streams each input file in command-line order, tokenizes one
line, builds one typed `AbiFactCase`, canonicalizes and emits that case, and
releases it before reading the next. `dev/abimangle.cpp` owns only CLI and file
I/O; `abi_mangle_facts.h` owns the reusable typed vocabulary;
`abi_mangle_model.cpp` owns compact tagged-record lifetime;
`abi_mangle_parse.cpp` owns the line adapter and canonical serializer; and
`abi_mangle.cpp` owns canonical identity, substitution, and Itanium emission.

This applies the available surfaces of `spec.md` sections 2, 6, and 8-10.
Strings and binder names exist at the text boundary, but one per-case graph
interns spellings and qualified paths and gives types, arguments, expressions,
and substitution entities stable numeric IDs. Structural comparison occurs
once during interning; encoder lookup then uses `(kind, id)` keys. The default
CLI neither serializes typed facts nor reparses them, while an explicit
serializer remains available for the standalone adapter. A core-only handoff
probe linked `abi_mangle_model.cpp` plus `abi_mangle.cpp`, without the parser,
constructed a typed `AbiFactFile`, and emitted `i`.

Representative trace: each `let-type`/`let-arg` in the vector/string fixture is
parsed into typed records with binder references, resolved on demand into
interned path/type/argument IDs, and consumed in ABI encounter order by one
per-name substitution table. The chosen standard and ordinary substitutions
are emitted directly to the output stream. An entity-valued argument resolves
its typed entity binder and swaps in an isolated nested substitution table in
O(1), then restores the outer table without copying it. A structured thunk now
carries its parsed terminal and parameter facts through the same function
encoder after the typed call offset. Source-language template parsing,
instantiation environments, lookup, LowIR, machine IR, and ELF are not PA14
surfaces.

## Performance Evidence

Release measurements used `CPPGM_ABIMANGLE_STATS=1`, `/usr/bin/time`, and three
runs per size for repeated `300-std-vector-string-substitution` cases. Times and
RSS are medians.

| Cases | Input / records / output | Types / arguments / path visits / substitution lookups | Parse / encode | Wall / RSS |
|---:|---:|---:|---:|---:|
| 1,000 | 1,131,890 B / 26,000 / 104,000 B | 8,000 / 5,000 / 24,000 / 18,000 | 58.69 / 41.26 ms | 0.11 s / 7,348 KiB |
| 2,000 | 2,264,890 B / 52,000 / 208,000 B | 16,000 / 10,000 / 48,000 / 36,000 | 115.42 / 81.41 ms | 0.20 s / 7,452 KiB |
| 4,000 | 4,530,890 B / 104,000 / 416,000 B | 32,000 / 20,000 / 96,000 / 72,000 | 235.51 / 161.46 ms | 0.40 s / 7,496 KiB |

Every observed work count and output byte count doubles with semantic input;
RSS remains flat because file and case ownership is streamed. Focused before
and final probes established the repaired bounds:

- An 8,000-component qualified name fell from 0.62 s to 0.01 s. Final
  8,000/16,000/32,000 paths take 0.01/0.02/0.06 s.
- 4,000 entity arguments after 4,000 substitutions fell from 2.00 s and
  47,516 KiB to 0.07 s and 17,200 KiB; 8,000 complete in 0.12 s and
  31,108 KiB.
- An 8,000-level compact pointer fell from 0.15 s and 136,992 KiB to 0.01 s
  and 7,048 KiB. The former 12,000-level segmentation fault is gone; final
  8,000/16,000/32,000 levels take 0.01/0.02/0.04 s with linear memory.
- Tagged record size fell from 5,144 to 1,096 bytes by storing only the active
  record and definition payload, a 78.7% reduction.

The original path profile attributed 36.0% of samples to repeated full-path
diagnostic-string copies. A final 200,000-component profile is concentrated in
required path interning (24.5%), string interning (15.5%), and hash insertion
(9.8%); it contains no repeated-prefix scan or full-path diagnostic copy.

## Architecture Review

- Representation and ownership: the CLI retains one input line, one current
  typed case, one case-local canonical graph, and one emitted name at the hot
  boundary. It does not retain a full input file or full output buffer. Parsed
  words are line-local; graph and case die together after emission. The public
  whole-file parser remains an explicit API, not the CLI transport path.
- Identity and lookup: source spellings are interned on entry; paths are
  parent/name IDs; canonical type, argument, and expression nodes use typed
  child IDs. Definition caches and substitution tables are case/name-local
  average-O(1) indexes. A rendered qualified path is no longer synthesized as
  a semantic substitution key. Source vectors preserve observable order while
  ABI tags are canonicalized and deduplicated.
- Repeated work: type/argument/expression definitions resolve once per binder,
  canonical equivalents share IDs, recursive references are detected, and
  nested entity names isolate substitution state by swapping storage rather
  than cloning it. PA14 template facts encode ABI structure only; they do not
  trigger source template parsing or instantiation.
- Allocation and scaling: compact and multiword modifier chains are flat,
  iteratively resolved, iteratively emitted, and non-recursively destroyed.
  Record variants store only active payloads. Case storage, interning tables,
  substitution state, and output are reclaimed at their natural boundaries;
  there is no mutable process-global cache, owning `shared_ptr`, per-node heap
  allocation, ordered hot map, retry loop, or whole-program scan.
- Self-containment: source inspection finds only input/output file access. A
  process-only `strace` records the initial `execve` and `exit_group(0)` with no
  child process, compiler, reference tool, demangler, or host ABI oracle.

## Final Architecture Review

The audit removed every identified correctness and architecture defect across
its owner: structured global operators no longer dereference an empty name
sequence; structured thunk facts and inline operator arity reach the encoder;
typeinfo-name uses the typed special-name path; malformed cross-target records,
conflicting reference qualifiers, duplicate function states, and overflowing
discriminators fail cleanly. Prefix lookup, nested substitutions, compact types,
case/file ownership, output ownership, and inactive record storage now have the
measured linear behavior above. Typed-core linkage, canonical round trips,
sanitizers, process tracing, the warning-free file audit, and the complete
through-stage report leave no PA14 correctness, architecture, performance,
self-containment, timeout, or file-audit blocker.

## Checkpoint Ledger

| Checkpoint | Result | Evidence |
|---|---|---|
| Contract, spec, history, plan, source, and fixtures | Pass | `spec.md`, PA14 README, Itanium chapter 5.1, commit `59c60846`, all changed source, and 111 handout fixtures reviewed |
| Stage/data-flow reconstruction | Pass | File -> line -> typed case -> canonical IDs -> one substitution table -> streamed name traced for template, entity, thunk, and operator paths |
| Correctness hardening | Pass | 111/111 handout plus 6/6 audit regressions |
| Typed handoff and round trips | Pass | Core-only encoder link without parser; 113/113 successful fixtures preserve output after canonical serialize/parse |
| Scaling and observability | Pass | Counters and timing table above; focused path/entity/modifier probes and before/final profile |
| Sanitizers and self-containment | Pass | ASan+UBSan 117/117 plus 32,000 modifiers; process trace has no child execution |
| File audit | Pass | `perl scripts/cppgm_file_audit.pl --stage pa14 --paths dev/src`: 59 files, no findings |
| Through-stage validation | Pass | `make test-report-through-pa14`: 1,037/1,037 tests, 14/14 stages |

## Findings

The checkpoint implementation passed fixtures but crashed for a valid
structured global operator, dropped structured function records from thunks,
misclassified compact inline operator arity, omitted the cited typeinfo-name
special form, accepted misplaced/conflicting function facts, and allowed local
discriminator overflow. It rebuilt a rendered path as semantic identity,
rescanned every remaining qualified prefix, eagerly copied the full qualified
name for a success-path diagnostic, copied the complete substitution map for
each nested entity, recursively substring-copied and destroyed modifier chains,
retained all cases and all output, exposed no work counters, and stored 5,144
bytes of inactive variant payload per fact record.

## Changes

The model is split into reusable fact vocabulary, compact tagged-record
lifetime, text adapter, and typed encoder ownership. Flat modifier chains,
canonical path-based keys, single-pass prefix selection, O(1) substitution
state swaps, per-case streaming, direct output streaming, tag deduplication,
typed typeinfo-name, complete thunk/function handoff, arity-aware operators,
strict function-state validation, and overflow checks replace the defective
paths. Optional `CPPGM_ABIMANGLE_STATS` telemetry reports phase time, input and
output bytes, canonical node/cache counts, path visits, substitution activity,
and isolated entity encodings without changing normal behavior. Six course
regressions cover the corrected and rejection paths and are wired into PA14.

## Validation

- PA14: 111/111 handout fixtures and 6/6 audit regressions pass.
- Canonical serializer/parser equivalence: 113/113 successful fixtures pass.
- ASan+UBSan: all 117 PA14 fixtures and a 32,000-modifier stress input pass.
- Typed handoff: model plus encoder links and emits without the text parser.
- Self-containment: source scan clean; process trace contains no child process.
- Required file audit: pass, 59 files checked with no findings.
- Required through-stage report: pass, 1,037/1,037 tests and all 14 stages.

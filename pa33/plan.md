# PA33 Final Audit Plan

## Stage Design and Spec Alignment

PA33 keeps one production path:

```text
source buffer -> streaming preprocessing/parser -> syntax arena
              -> canonical semantic graph (Name/Type/Entity/Binding IDs)
              -> direct typed LowIR (Symbol IDs and ABI/runtime roles)
              -> per-function MIR -> code/data/LSDA/CFI/relocations
              -> deterministic ELF64 relocatable
```

Parser, substitution, lookup, and demand scratch die before the semantic graph
is synchronously lowered. The graph dies before the typed program is adapted;
MIR is lowered, encoded, and released one function at a time. Production never
renders or reparses LowIR or assembly. The non-allocating `.cppgm_object`
section is the bounded binary TU payload required by PA30's cumulative internal
link contract; PA33 host linking does not consume it.

PA33 facts stay with their semantic owners: ABI tags and template recipes with
entities/bindings, virtual layout and final overriders with class IDs, RTTI with
a demand worklist, exception policy with canonical bindings and typed type
slices, and static/TLS lifecycle with indexed symbols. ABI strings are rendered
only when symbols are published. This matches `spec.md` §§2, 4, and 6-10 and
the PA33 host ABI/runtime contract.

## Architecture Review

- Representation and ownership: source, syntax, semantics, typed LowIR,
  function-local MIR, and ELF buffers have explicit destruction boundaries.
  No later phase borrows syntax or semantic pointers and no text round trip is
  present on `cppgm++ -c`.
- Identity and lookup: semantic hot paths use compact canonical IDs and indexed
  binding/entity/symbol tables. ABI path strings exist only at publication;
  ordered string maps are isolated to deterministic final EH/object layout.
- Templates and repeated work: retained dependent ABI type/expression recipes
  are keyed by canonical specialization facts; demand/cache counters show one
  emitted body per complete key and no global retry loop.
- Lowering and backend: RTTI demand, vtable publication, exception-boundary
  demand, and static/TLS lifecycle each use a bounded TU pass or dirty
  worklist. Exception policy visits each eligible function subtree at most
  once; the backend indexes cross-function facts once and then lowers each
  function once. Machine code and ELF are emitted directly.
- Allocation and scaling: front-end arenas own long-lived nodes, temporary
  vectors/maps are phase- or function-local, and epoch tables avoid TU-sized
  clears per function. Existing counters cover nodes, candidates, template
  transitions, demand pushes, RTTI/virtual work, IR, EH edges, fixups, bytes,
  and phase time.
- Self-containment: production source contains no compiler, assembler,
  reference-binary, or fixture subprocess. The only `fork`/`exec` code is the
  test-runner wrapper; host tools run after object production in the harness.

Representative declaration trace: adjacent and comma-separated `abi_tag`
literals become grouped syntax arguments, canonical entity tag slices, nested
member-template `AbiType` facts, tagged member/lifecycle/RTTI/vtable symbol
names, typed LowIR symbols, MIR labels/fixups, and final ELF symbols. A demanded
dependent `enable_if`/`decltype` callable trace retains its semantic recipe,
instantiates once by complete key, lowers once, and publishes the resulting ABI
symbol without reconstructing semantics from text.

## Performance Evidence

Five-run medians for independently generated 16/32/64-entity workloads:

| Tagged polymorphic classes | Semantic nodes | Functions / globals | LowIR instructions | Fixups | Semantic / typed-lower / ELF-encode ms |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 16 | 580 | 97 / 64 | 752 | 385 | 2.525 / 1.372 / 3.464 |
| 32 | 1,156 | 193 / 128 | 1,504 | 769 | 4.791 / 2.737 / 6.837 |
| 64 | 2,308 | 385 / 256 | 3,008 | 1,537 | 9.248 / 5.378 / 13.881 |

The 4x workload gives 3.98-4.00x produced facts and 3.66-4.01x median phase
time. Output grows linearly from 461,904 to 1,848,248 bytes, including the
assignment-required compatibility payload.

| `noexcept` call boundaries | Semantic nodes | LowIR instructions | EH states / edges / calls | Fixups | Semantic / typed-lower / ELF-encode ms |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 16 | 340 | 370 | 16 / 96 / 16 | 181 | 1.430 / 0.477 / 1.306 |
| 32 | 676 | 738 | 32 / 192 / 32 | 357 | 2.649 / 0.741 / 2.493 |
| 64 | 1,348 | 1,474 | 64 / 384 / 64 | 709 | 5.096 / 1.394 / 4.941 |

EH work counters are exactly proportional; the 4x series takes 2.92-3.78x in
the measured phases. A canonical repeated-type DAG at depth 16/32/64 records
111/207/399 declarations, 19/35/67 template requests, 151,533/287,661/563,501
peak semantic bytes, and 0.746/1.072/1.815 ms semantic medians. No unexplained
superlinear path remained to profile; a widest-case `perf stat` confirmed
65.64 ms task-clock while hardware counters were unavailable in the sandbox.

## Final Architecture Review

### Findings

The independent audit found one correctness chain missed by the checkpoints:
GNU `abi_tag("a", "b")` retained only its first argument, and class tags were
published on RTTI/vtable names but not all class-owned member and lifecycle
symbols. The same ownership gap exposed adjacent-literal grouping, local-class
component tags, tagged nested class-template arguments, and malformed
nonliteral argument acceptance. Extending the typed owner path initially
crossed the file-audit function-size limit and an unnamed local-template
representation boundary; both were corrected at their owners. No open
correctness, architecture, performance, self-containment, or file-audit blocker
remains.

### Changes

- GNU attribute syntax now preserves every literal argument and adjacent
  literal part, marks nonliteral lists, and decodes narrow literal sequences
  once at the semantic boundary. `abi_tag` consumes all arguments; `section`
  enforces one valid literal argument.
- ABI publication now carries class tags through ordinary, template, nested,
  and local owner representations into methods, C1/C2, D0/D1/D2, static data,
  RTTI names/objects, and vtables. Nested template argument slices and tagged
  substitution identities remain canonical.
- The structured local/template path remains for unnamed local classes; tagged
  nonlocal owners use typed member targets. Repeated class-template argument
  construction was extracted to restore the file-audit ownership limit.
- Two PA33 course regressions cover host-parity publication and malformed
  argument rejection. No fixture or reference was weakened.

### Performance Evidence

The tables above cover ABI/RTTI publication, exception policy/backend work, and
canonical template DAG growth. Counters and medians remain proportional to
semantic input and emitted output; no cleanup refactor introduced a scaling
regression.

### Validation

- `make test-pa33`: 94/94 handout and 2/2 course tests pass.
- `perl scripts/cppgm_file_audit.pl --stage pa33 --paths dev/src`: pass with 22
  inherited nonfatal header-division warnings and no fatal issue.
- `make test-report-through-pa33`: 4,387/4,387 tests and 33/33 stages pass.
- Host `g++`/`nm` comparison matches all 36 inspected tagged symbols for
  ordinary, top-level-template, nested-template, and local classes.
- `git diff --check`: pass.

## Checkpoint Ledger

| Checkpoint | Final audit result |
| --- | --- |
| ABI-tag publication | Pass after multi-argument and full class-owner publication closure |
| Dependent callable-type recipes | Pass; canonical owner/type recipes retained |
| Dependent expression recipes | Pass; dependent expression structure retained |
| Stack/SysV vararg intrinsics | Pass; typed intrinsic path remains direct |
| Dependent transform/layout | Pass; canonical transform/layout facts retained |
| EH nonlocal transfer | Pass; region cleanup and transfer ownership remain explicit |
| Exception-specification policy | Pass; one bounded demand scan and typed LSDA filters |
| Virtual-inheritance RTTI/casts | Pass; indexed layouts and demand-driven RTTI |
| Covariant thunks/VTT copy | Pass; finalized adjustment facts and canonical entries |
| Static/TLS lifecycle | Pass; indexed wrappers, guards, registration, and teardown |
| Identity/ODR publication | Pass; unnamed/local/lambda identities and weak ODR ownership |
| Final PA-wide audit | Pass; findings fixed, proportional scaling, required gates clean |

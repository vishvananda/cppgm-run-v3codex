# Plan: Deduplicate the Compiler Core Outside `lowir_opt`

Status: execution in progress

Date: 2026-08-28

## Objective

Reduce duplicated implementation in the compiler driver, semantic analysis,
typed source-to-LowIR lowering, shared LowIR infrastructure, ABI support,
frontend, and native backend without changing any student-facing language,
LowIR, MIR, object, or optimization contract.

This plan follows the completed `PLAN-OPTIMIZER-DUPLICATION.md` program.  It
does not reopen the optimizer pipeline or create a generic compiler framework.
Its purpose is to give each repeated mechanism one clear owner while keeping
the policy differences between assignments and compiler phases visible.

The program must:

- remove the two actionable production duplicate warnings outside the
  completed optimizer cleanup;
- give `cppgm++.cpp`, `lowir_native_elf.cpp`, `pa10_syntax.cpp`, and
  `macro_processor.cpp` useful file-audit headroom;
- consolidate exact semantic and lowering facts before considering broader
  abstractions;
- preserve all existing property and behavior tests through PA38;
- keep the PA38 file audit at zero fatal findings and add no advisory warning;
- preserve the corrected same-revision O1 performance ratios, with no cleanup
  regression hidden by an incorrect host-compiler comparison;
- use outer, inner, and object parallelism of 32 for every inception run; and
- commit each accepted increment after fast verification, with cumulative
  verification and a push after no more than three retained commits.

Source reduction is not sufficient by itself.  A helper is retained only when
it expresses one semantic fact or lifecycle more clearly than the copies it
replaces and does not add measurable runtime work on a hot compiler path.

## Governing rules

1. This plan does not authorize a public LowIR or MIR contract change.  If a
   cleanup exposes a missing semantic fact, stop and treat that as a separate
   contract-and-test increment rather than adding private frontend state to
   native preparation.
2. Before changing a family, identify structural or behavioral coverage for
   every policy branch being consolidated.  If it is absent, first add a
   high-level requirement to the earliest owning PA README and a focused
   student-implementable property test under `cppgm.tests/course/paN/`.
3. Tests must not match production source text, recognize a particular input
   program, or define a feature by comparing a complete LowIR/MIR program.
   Existing complete fixtures may remain compatibility oracles; new coverage
   checks only the relevant relationship or runtime behavior.
4. Never change a test source program to avoid exposing a compiler defect.
   Fixture regeneration uses only the documented `ref-test` target and is not
   part of a pure implementation refactor.
5. Do not replace direct calls and loops with `std::function`, virtual
   dispatch, heap-allocated policy objects, or a generic visitor framework.
   Hot shared helpers remain allocation-free and inline where measurement
   shows that a new call boundary is material.
6. Share exact facts, preparation, and finalization mechanics.  Keep
   eligibility, error/SFINAE behavior, ABI policy, EH policy, ordering, and
   ownership decisions local unless their complete truth tables are equal.
7. Do not consolidate staged PA7/PA8 models with the PA10/PA11 production
   model merely because their source has a similar shape.  Assignment
   boundaries and source-set independence are part of the architecture.
8. Prefer existing responsibility-named owners.  If a new `dev/src/*.cpp` is
   genuinely necessary, add it to every applicable tool list in
   `dev/frontend_source_sets.mk`.  Do not move substantial implementation into
   headers to evade file limits.
9. Preserve insertion order, stable identity, statistics, memory accounting,
   debug facts, cleanup order, and emitted object relationships.  These are
   observable or diagnostic invariants even when ordinary behavior still
   passes.
10. One helper family or ownership move per commit.  A rejected abstraction is
    reverted before the next candidate and recorded in the ledger.

## Starting checkpoint

The planning checkpoint is `d27de604`; it changes no production compiler code
from the completed optimizer-duplication tree.

The default PA38 file audit passes with zero fatal findings and 35 warnings.
Four warnings are duplicate windows:

| Warning | Classification |
| --- | --- |
| `dev/cppgm++.cpp:1832` versus `:1569` | actionable: compile and link driver native-stat reporting |
| `dev/src/lowir_native_elf.cpp:2541` versus `:1821` | actionable: ordinary and host-EH final-emission peephole pipeline |
| `dev/preproc.cpp:46` versus `dev/macro.cpp:40` | intentional staged executable wrapper surface; keep local |
| `dev/src/pa15_lowering.cpp:1` versus `dev/src/lowir_native.cpp:1` | include/namespace preamble noise; no semantic owner to extract |

Files with immediate structural pressure are:

| File | Lines | Audit concern |
| --- | ---: | --- |
| `dev/src/pa10_syntax.cpp` | 2,999 | one line of file-limit headroom |
| `dev/src/macro_processor.cpp` | 2,996 | four lines of file-limit headroom |
| `dev/src/lowir_native_elf.cpp` | 2,993 | seven lines of file-limit headroom and a production duplicate warning |
| `dev/cppgm++.cpp` | 2,930 | large repeated diagnostic body |
| `dev/src/pa15_lowering.cpp` | 2,738 | repeated per-function lifecycle and call construction |
| `dev/src/pa12_semantic_tables.cpp` | 847 | many compact-table probe/rehash implementations, not a size problem |

The corrected same-revision O1 checkpoint is:

| Generated compiler lane | Mean wall | Self ratio |
| --- | ---: | ---: |
| self-built `cppgm++` | 31.535 s | - |
| GCC-built `cppgm++` | 20.900 s | 1.509x |
| Clang-built `cppgm++` | 22.160 s | 1.423x |

All lanes compile the same complete candidate source corpus with a
`cppgm++` binary built from that same revision.  The denominator is **not** a
raw `g++` or `clang++` invocation compiling the corpus.  The 1.067x GCC and
1.030x Clang numbers in the D7 record of `PLAN-OPTIMIZER-DUPLICATION.md` used
that wrong comparison and must not be used as a retention oracle.  The global
performance goal remains below 1.50x versus GCC; cleanup does not receive a
budget to worsen the current 1.509x ratio.

Before C0 measurements, refresh these numbers if the production source has
moved.  Record hashes, compiler text, aggregate CPU, and lane order with the
wall results.

## Frozen duplication census

| Priority | Family | Evidence and semantic boundary | Disposition |
| --- | --- | --- | --- |
| P0 | Driver native-stat reporting | `run_compile_driver` and `run_link_driver` print a long common `lowir_native::Stats` sequence.  Serialization fields and link fields differ. | Move the common native sequence into `lowir_native_stats_report`; keep mode prefixes and mode-specific suffixes in the driver. |
| P0 | Native final-emission pipeline | Ordinary and host-EH functions run the same frame-forwarding, folding, normalization, coalescing, zeroing, and shared-return chain.  Only the final fallback and host landing context differ. | Share the exact pre-fallback sequence; keep ordinary and host-EH fallback emission local. |
| P1 | Semantic named-flavor facts | Struct/class/union and enum truth tables are repeated in PA12 initialization, list initialization, hosted builtins, PA21, PA26, and PA34.  Completeness and object/dependency policy differ. | Add neutral inline flavor predicates to the existing semantic vocabulary; callers retain all policy checks. |
| P1 | Deferred function queue | `QueueFunctionDefinitionValidation` and the tail of `DemandRuntimeDefinition` perform the same state transition and worklist append.  Vtable demand has a distinct non-deferred policy. | Share only the exact deferred-definition queue transition; keep prerequisite traversal and vtable policy local. |
| P1 | Lifetime scope preparation | Automatic and temporary obligations duplicate scope-vector preparation.  Goto cleanup duplicates reverse action construction for two ranges. | Share scope preparation and reverse cleanup-range emission; preserve temporary marking, validation, reverse order, and visit counts. |
| P1 | Semantic intrinsic-call shell | Builtin and integer intrinsic calls duplicate callee and CALL dump construction.  Integer folding and target application differ. | Share typed call-node construction only; keep folding and target conversion local. |
| P1 | Compact semantic tables | About ten PA12 compact tables repeat power-of-two growth, slot clearing, entry-index rehashing, and probe loops.  Key equality, hashing, counters, and entry layouts differ. | Share compact index-slot mechanics incrementally; do not replace the tables with `flat_hash_map`. |
| P1 | Lowering function lifecycle | Synthetic and ordinary functions repeat most transient-state resets.  Ordinary functions additionally own lifetime, parameter, source-name, and boundary setup. | Extract the exact common reset core; keep ordinary/synthetic initialization and finalization visible. |
| P1 | Direct-call construction | Roughly twenty lowering sites create a CALL, type it, install a direct symbol operand, mark the symbol referenced, attach arguments, and emit.  ABI/EH/special-member policy differs. | Introduce a narrow direct-callee construction primitive; migrate one call family at a time. |
| P1 | Trivial lifecycle metadata | Two predicates in `pa15_lowering_abi.cpp` have the same validation and constructor/destructor result. | Retain one predicate with a name that describes the shared fact. |
| P1 | LowIR signature parsing | Function declarations and definitions parse the same symbol, parameter, result, and metadata prefix.  Definitions then own values, debug facts, and a body. | Parse one typed function signature/header, then apply definition-only state locally. |
| P1 | Role-neutral LowIR operands | Preparation repeats traversal of `first`, `second`, `third`, and `args`.  The optimizer already has an allocation-free role-neutral view in `lowir_optimizer_support.h`.  Phi labels and role-sensitive calls must not use it blindly. | Promote the tiny operand view to a neutral internal LowIR owner; retain optimizer aliases and role-sensitive traversal. |
| P2 | Native fixup/relocation mechanics | Code-buffer fixups repeat relative-delta and absolute-addend arithmetic; object emission repeats relocation kind/offset/addend setup for label and symbol identities. | Share validated arithmetic and relocation-envelope construction; keep target lookup typed and local. |
| P2 | Native result finalization | Atomic load/exchange and integer division/shift paths repeat destination normalization, store/home publication, and definition accounting. | Extract only exact finalization tails after MIR and code-shape controls cover both paths. |
| P2 | Native address builders | Index, wide, varargs, and address lowering repeatedly construct LEA-like MIR. | Consider a typed MIR builder after proving identical width, scale, displacement, scratch, and definition semantics. |
| P2 | ABI hash primitive | Four ABI modules carry the same hash-combine primitive while selecting different key fields. | Share only hash mixing in an internal ABI utility; keep each hash's fields local. |
| P2 | Frontend arena and parser mechanics | Syntax arena append/prepend share mutation bookkeeping; PA10 complex unary construction and macro variadic closure have adjacent exact copies. | Extract small owner-local helpers to restore file headroom without changing grammar or tree shape. |

## Deliberate non-consolidations

The following similarities are not authorization to merge code:

- class declaration versus enum declaration: redeclaration, opaque enum,
  underlying type, specialization, and identity rules differ;
- ordinary native fallback versus host-EH fallback: landing pads, call-site
  regions, stack cleanup, and resume behavior differ;
- class result versus class parameter ABI policy: their forcing and aggregate
  rules are distinct even if their initial type-shape checks match;
- automatic runtime demand versus vtable demand: deferred-definition
  eligibility is intentionally inverted in one branch;
- frontend string interning versus LowIR string pooling: sealing, remapping,
  presentation retention, and statistics differ;
- PA8 versus PA11 type/name models: they are staged assignment surfaces with
  different linked tools;
- class/enum entity checks that include bounds, completeness, dependency, or
  CV/array peeling: only the base flavor truth table is shared;
- template pack expansion state machines and aggregate-cast finalization:
  error/SFINAE behavior and source kinds differ; and
- catch-all generic table, visitor, call, or pass frameworks introduced only
  to reduce the duplicate detector count.

## Coverage audit before implementation

Create a ledger row for each increment before editing code.  It records:

1. every old definition and caller;
2. the exact common truth table or state transition;
3. every intentional difference left at the caller;
4. the earliest owning README requirement;
5. existing positive, negative, structural, and behavioral tests; and
6. the fast and cumulative commands for that increment.

The initial coverage routing is:

| Family | Earliest and downstream coverage to inspect |
| --- | --- |
| driver stats and object/link modes | PA30 link driver, PA31 object path, PA37 optimization routing, PA38 backend diagnostics |
| semantic flavor, demand, intrinsic calls | PA12 semantic behavior, then PA19/PA21/PA26/PA34 consumers |
| lifetime and goto cleanup | PA12 object lifetime, PA16 destructor lowering, PA26 exceptions and goto cleanup |
| lowering reset and calls | PA15 LowIR lowering, then PA16/PA17/PA18/PA21/PA26/PA28/PA33 call and synthetic-function paths |
| LowIR parsing and operand traversal | PA13 parse/dump, PA29 native consumption, PA37 optimization and serialized object replay |
| native emission/fixups | PA29 structural and generated behavior, PA30/PA31 object/link inspection, PA38 MIR and native survivor properties |
| ABI hashing/mangling | PA14 mangling plus later template, RTTI, and object/link consumers |
| frontend syntax/macro helpers | earliest macro assignment, PA10 syntax, PA11 semantic/type consumers |

Pure refactors do not require artificial new fixtures when all old branches
already have meaningful coverage.  If coverage is missing, the backfill must
describe and validate behavior a student could implement from the README; it
must not validate the name or existence of the new helper.

## Execution program

Each increment is independently reviewable, tested, measured when its token or
call shape can affect compilation, committed, and either retained or reverted
before the next increment.

### C0. Freeze the baseline and coverage ledger

1. Save the default audit and the tight report-only duplicate scan.
2. Record current file/function sizes and classify all tight matches as
   actionable, policy-distinct, or syntax noise.
3. Re-run root through-PA38, the corrected three-compiler O1 oracle, and a
   fresh 32-way O1 inception if HEAD has moved since the checkpoint above.
4. Record existing tests for both branches of C1-C3 before changing them.
5. Confirm there is no stale `cachegrind`, Valgrind, `perf record`, detached
   benchmark, or old inception process before any timing.

Exit: one trustworthy correctness/audit/performance baseline and no unowned
P0 branch.

### C1. Move common native-stat reporting out of the driver

1. Add a `report_native_codegen_stats`-style function to the existing
   `lowir_native_stats_report` owner.
2. Preserve field spelling and order exactly for both existing records.
3. Keep `pa31_object_stats`, serialization fields, `pa30_driver_stats`, link
   fields, timing suffixes, final newlines, and function-census emission in
   their appropriate mode owners.
4. Do not introduce dynamic containers or construct a temporary string.

Fast coverage: compile/object stats, link stats, PA30, PA31, file audit, and
`git diff --check`.

Exit: the `cppgm++.cpp` production duplicate warning is gone, diagnostic
records are byte-identical, and normal non-stats paths have no new work.

### C2. Consolidate exact semantic vocabulary and lifecycle facts

Perform these as separate commits:

1. Add shared inline `IsClassFlavor` and `IsEnumFlavor` predicates to the
   existing semantic vocabulary and replace only exact flavor truth tables.
2. Merge the identical trivial constructor/destructor metadata predicate in
   `pa15_lowering_abi.cpp`.
3. Share the exact deferred-function transition from `NOT_STARTED` to
   `QUEUED`; leave recursive prerequisite demand and vtable policy local.

Fast coverage: PA12 for flavor/demand, PA15 for lifecycle ABI, plus the
downstream PA21, PA26, and PA34 suites whose callers migrate.

Exit: one definition for each fact, with bounds, completeness, dependency,
access, and demand policy still explicit at call sites.

### C3. Consolidate lifetime bookkeeping

1. Extract scope-vector preparation used by automatic and temporary lifetime
   obligations.
2. Extract reverse cleanup-action emission for a validated obligation range.
3. Preserve temporary-versus-object action creation, initializer-list scope
   marking, reverse destructor order, validation failures, dump edge order,
   and `lexical_cleanup_action_visits_` exactly.

Fast coverage: PA12 lifetime, PA16 destructor behavior, and PA26 exception/goto
properties.  Backfill an earliest-owned reducer first if temporary and object
obligations leaving nested scopes are not independently covered.

Exit: identical dump/LowIR relationships and runtime destructor order.

### C4. Consolidate semantic call construction without policy loss

1. Extract the common typed dump-node shell for builtin and integer intrinsic
   calls.
2. Keep binding selection, integer constant folding, explicit target
   conversion, failure behavior, and expression statistics local.
3. Do not extend the helper to ordinary overload resolution or template calls.

Fast coverage: PA12 builtin/intrinsic positive and invalid calls, with PA21 and
PA34 consumers where applicable.

Exit: the helper owns construction, not semantic selection.

### C5. Give function-lowering state one lifecycle owner

1. Enumerate every transient `GraphLowerer` field and classify it as common,
   ordinary-only, synthetic-only, or finalization-only.
2. Extract `ResetCommonFunctionLoweringState` for the exact common subset.
3. Leave result boundary, lifetime, parameter index, source-name collection,
   exception boundary, virtual-base preparation, and synthetic result policy
   in the appropriate caller.
4. Add a debug-only or test-visible invariant only if it expresses a genuine
   lifecycle rule and adds no release-build work.

Fast coverage: PA15 plus synthetic users in PA16, PA21, PA26, and PA33.

Exit: a newly added transient field has one obvious reset location, while
ordinary and synthetic function contracts remain separately readable.

### C6. Introduce a narrow direct-call lowering primitive

1. Define a primitive that creates a typed CALL with a direct function symbol
   and marks that symbol referenced.
2. Initially migrate one ordinary direct-call family and one synthetic-call
   family with strong structural and behavioral controls.
3. Migrate additional sites only when callee shape and reference ownership are
   exact.  Keep argument passing, indirect results, virtual-base arguments,
   no-return, unwind, cleanup, and result-use behavior local.
4. Stop if the helper needs a growing boolean option list; that indicates the
   callers do not share one operation.

Fast coverage follows each migrated family: PA15/PA16 ordinary and destructor
calls, then PA17/PA18/PA21/PA26/PA28/PA33 as touched.

Exit: direct callee/reference mechanics have one owner without obscuring ABI
or EH policy.

### C7. Consolidate shared LowIR parsing and operand access

Perform as two commits:

1. Parse a typed function signature/header once for declarations and
   definitions; keep definition-only parameter values, debug facts, block
   labels, and body parsing local.
2. Move the existing allocation-free role-neutral operand view from its
   optimizer namespace to a neutral internal LowIR owner.  Migrate preparation
   walks first and retain compatibility aliases or direct optimizer migration
   without changing optimizer behavior.

Phi predecessor labels, call callee/argument roles, EH edges, debug operands,
and any role-sensitive rewrite continue to use specialized traversal.

Fast coverage: PA13 parse/dump and malformed input, PA29 native input, PA37
roundtrip/optimization, and direct-versus-serialized object properties.

Exit: declaration/definition syntax stays identical and role-neutral walks do
not allocate, reinterpret labels as values, or add a public LowIR feature.

### C8. Consolidate exact native support mechanics

Land these independently:

1. Share the ordinary/host unconditional-control-flow opcode predicate.
2. Share code-buffer relative-delta and absolute-addend validation.
3. Share host relocation kind/offset/addend initialization while preserving
   typed label versus symbol target lookup.
4. Share exact integer/atomic destination finalization tails only after their
   code-shape controls demonstrate equivalent width and home publication.

Fast coverage: PA29 native structure/behavior, PA30/PA31 relocatable/link
inspection, PA38 MIR/native properties, and existing diagnostic counters.

Exit: typed target identity and relocation semantics remain explicit; emitted
bytes and relocations are unchanged.

### C9. Share the native final-emission peephole sequence

1. Freeze the ordinary and host-EH chain in an ordered table in the ledger:
   carry store, memory fold, constant division, fused normalization, delayed
   forwarding, forwarded reload, constant byte-store coalescing, flag-safe
   zero move, redundant normalization, and shared return.
2. Extract a small allocation-free helper that reports whether and how many
   MIR instructions the common chain consumed.
3. Keep landing-block lookup and `emit_host_instruction` entirely in the
   host-EH caller; keep `emit_instruction` in the ordinary caller.
4. Compare generated compiler text, native counters, per-TU objects, and
   corrected same-source performance.  Test an inline implementation and an
   out-of-line implementation only if needed; reject a persistent hot-path
   regression even if source duplication falls.

Fast coverage: PA29 and PA38, focused ordinary/host-EH generated behavior,
file audit, and byte/object comparison where deterministic.

Exit: the native production duplicate warning is gone, host-EH behavior is
unchanged, and the corrected performance ratio does not regress.

### C10. Consolidate PA12 compact-table mechanics incrementally

1. Document the slot sentinel, capacity/load rule, insertion order, hash,
   equality, probe accounting, and memory accounting for every table.
2. Start with the two tables whose `Rehash` bodies are exact after substituting
   entry type and hash expression.
3. Prefer a `.cpp`-local index-slot rebuilding primitive or a small typed
   algorithm over a replacement container.  Do not add tombstones or store
   full keys in the slot array.
4. Migrate at most two table families per commit.  Compare table statistics,
   semantic output, peak storage, and compile performance after each group.
5. Leave a table local when its probe termination, duplicate policy, or stable
   identity differs.

Fast coverage: PA12 semantic and table-capacity/stat properties plus the first
downstream suite using each migrated table.  Cumulative through-PA38 and the
performance oracle are required after every two table families.

Exit: common compact indexing has one implementation without changing entry
order, identity, statistics, allocation profile, or lookup cost.

### C11. Restore frontend headroom and share the ABI hash primitive

Land small independent commits:

1. Share syntax-arena mutation preparation between append and prepend.
2. Share PA10 unary node construction while retaining the special diagnostic
   and spelling classification for `__real__`/`__imag__`.
3. Share the exact macro variadic-close validation block.
4. Share the ABI hash-combine primitive; keep argument, type, substitution,
   expression, and path field selection in their current owners.

Fast coverage: the earliest macro suite, PA10, PA11, and PA14 respectively,
then their report-through gates.

Exit: `pa10_syntax.cpp` and `macro_processor.cpp` have useful headroom, with
unchanged token, syntax-tree, mangling, and diagnostic-success/failure
behavior.

### C12. Final audit, classification, and closure

1. Re-run the default and tight duplicate scans.
2. Classify every remaining semantic match as shared or intentionally local;
   do not create helpers for include/namespace or common loop syntax.
3. Run the full correctness, audit, corrected performance, and inception
   protocols.
4. Record final file sizes, warning count, source/compiler hashes, compiler
   text, timings, ratios, relevant stats, retained commits, rejected
   experiments, and push state.

Exit: all actionable families have a retained consolidation or a specific
semantic/performance reason to remain local.

## Verification cadence

### Fast gate for every increment

Build with 32 workers, run the focused property or behavior checks, then the
owning assignment and report-through gate:

```sh
make -j32 build
make -C paN check TEST='tests/path/to/focused.t'
make test-paN
make test-report-through-paN
perl scripts/cppgm_file_audit.pl --stage pa38 --paths dev
git diff --check
```

Use every affected PA when an implementation owner has downstream consumers;
`paN` above is not permission to run only one convenient suite.  The default
audit must remain zero-fatal and may not rise above the starting 35 warnings.

Commit each accepted increment after this gate.  Do not combine an unverified
cleanup with another family.

### Cumulative gate and push

After at most three accepted commits, before every multi-commit push, and
after any shared semantic, lowering, LowIR, or native owner changes:

```sh
make -j32 test-report-through-pa38
perl scripts/cppgm_file_audit.pl --stage pa38 --paths dev
git diff --check
```

Then push the verified commits.  Record the commit hashes and evidence in the
ledger; do not leave completed work only in a local worktree.

Run fresh PA39 inception at C0, after C7, after C9, after C10, and at final
closure.  Every setting is explicitly 32-way and self-host optimization is
explicitly O1:

```sh
RUN_ROOT=$(mktemp -d /dev/shm/v3codex-corededup-inception.XXXXXX)

make -C pa39 -j32 cppgm++-self \
  CXX=../dev/cppgm++ CPPGM_HOST_CXX=g++ \
  INCEPTION_OBJ_ROOT_BASE="$RUN_ROOT/obj" \
  INCEPTION_BIN_ROOT_BASE="$RUN_ROOT/bin" \
  INCEPTION_SELFHOST_OPT_LEVEL=1 \
  INCEPTION_BUILD_JOBS=32 \
  INCEPTION_OBJECT_BUILD_JOBS=32

make -C pa39 -j32 compare-cppgm++-inception \
  CXX=../dev/cppgm++ CPPGM_HOST_CXX=g++ \
  INCEPTION_OBJ_ROOT_BASE="$RUN_ROOT/obj" \
  INCEPTION_BIN_ROOT_BASE="$RUN_ROOT/bin" \
  INCEPTION_SELFHOST_OPT_LEVEL=1 \
  INCEPTION_BUILD_JOBS=32 \
  INCEPTION_OBJECT_BUILD_JOBS=32
```

Use a fresh, explicitly recorded root.  Confirm that the selected compiler
binary and every compared object come from the intended revision.

## Corrected performance retention protocol

Any token, include, file boundary, function boundary, table structure, or hot
call-path change requires a performance decision.

1. Before building, inspect running processes and wait for or terminate only
   positively identified stale benchmark/profiler processes.  Never start a
   competing measurement while Cachegrind, Valgrind, `perf record`, an old
   inception build, or a detached corpus compile is running.
2. Build self-, GCC-, and Clang-generated `cppgm++` binaries from the exact
   same candidate revision.  Use outer, inner, and object job counts of 32 for
   preparation.
3. Time those three generated `cppgm++` binaries compiling the exact same
   complete source corpus.  Do not time raw `g++` or `clang++` as the GCC and
   Clang parity lanes.
4. Use fresh output roots and reverse/interleaved lane order.  Record at least
   two clean lanes for each compiler; repeat to three when a ratio moves by
   less than one percent or straddles the acceptance boundary.
5. Record wall, user, system, aggregate CPU, peak RSS, compiler hash/text, all
   object hashes, and the phase statistics relevant to the changed family.
6. Compare candidate self time with the pre-change self compiler on identical
   candidate source, and compute self/GCC-built and self/Clang-built ratios on
   the candidate revision.  Both views matter: a small self slowdown can be a
   net parity win if GCC/Clang-built compiler time rises more, but a ratio win
   does not excuse a large unexplained absolute regression.
7. A repeated candidate-versus-pre-change self or aggregate-CPU regression
   above 0.5% requires explanation.  Reject it unless the corrected maximum
   parity ratio independently improves enough to justify the trade.
8. The completion target is self/GCC at or below 1.50x and no material
   regression from the starting self/Clang ratio.  Until self/GCC is below
   1.50x, do not describe the broader parity goal as complete.
9. Cachegrind, software emulation, and sampling profiles are attribution
   tools, not acceptance oracles.  The corrected same-source generated-
   compiler ratio is the final performance decision.
10. Also preserve the host-toolchain best case.  Build separate GCC and Clang
    `cppgm++` binaries from both the immutable pre-change revision and the
    exact candidate revision with the repository's fully optimized host
    setting (`-O3`).  Use those four compiler binaries to compile the
    unchanged frozen `semantic_overload.cpp` workload at its best-case
    `-std=gnu++11 -O0 -Idev/src` setting.  Run the baseline and candidate in
    interleaved order, require identical output for like compiler families,
    and record wall, user, system, peak RSS, compiler hash/text, and object
    hash.  A repeated GCC- or Clang-built candidate regression above 0.5%
    requires explanation and rejection unless a separately justified
    correctness or parity gain outweighs it.

## Commit, push, and ledger discipline

- Commit the plan first.
- Use one commit per numbered subincrement or exact helper family.
- Run the fast gate before every retained code commit.
- Record a rejected abstraction with its source delta, tests, audit result,
  compiler text, and performance reason before reverting it.
- Run the cumulative gate and push after no more than three retained commits.
- Push high-risk C7, C9, and C10 commits individually after their cumulative
  gates.
- Never leave the only copy of a retained measurement in `/tmp` or
  `/dev/shm`; summarize it in this plan's ledger.
- Keep fixture changes in an earlier, separately reviewed coverage commit.
  Never mix test backfill, semantic correction, and deduplication in one
  commit.

Suggested commit sequence:

1. `plan: define compiler core deduplication`;
2. `driver: centralize native codegen stats`;
3. `semantic: centralize named flavor facts`;
4. `lowering: centralize trivial lifecycle metadata`;
5. `semantic: centralize deferred function queueing`;
6. `semantic: centralize lifetime bookkeeping`;
7. `semantic: centralize intrinsic call construction`;
8. `lowering: centralize function state reset`;
9. one commit per migrated direct-call family;
10. `lowir: centralize function signature parsing`;
11. `lowir: promote role-neutral operand access`;
12. one commit per native support family;
13. `native: centralize final emission peepholes`;
14. one commit per compact-table pair;
15. one commit per frontend or ABI helper family; and
16. `plan: close compiler core deduplication`.

## Completion criteria

This plan is complete only when:

1. both actionable production duplicate warnings are removed;
2. every P0/P1 family has a retained consolidation or a recorded reason to
   remain local;
3. `pa10_syntax.cpp`, `macro_processor.cpp`, and `lowir_native_elf.cpp` have
   useful line-count headroom without dense formatting;
4. no new public LowIR/MIR fact or private frontend-to-native bypass exists;
5. every migrated policy branch has student-implementable structural or
   behavioral coverage at its earliest owning assignment;
6. root `make -j32 test-report-through-pa38` is clean;
7. the PA38 file audit passes with zero fatal findings and no more than the
   starting 35 warnings;
8. fresh explicit-O1 inception with all three job settings at 32 matches every
   object and the final compiler;
9. corrected same-revision measurements use self-, GCC-, and Clang-built
   `cppgm++` binaries, not raw host compilers;
10. the final self/GCC ratio is at or below 1.50x, self/Clang has no material
    regression, and any absolute movement is explained; and
11. fully optimized GCC- and Clang-built compilers show no unexplained frozen-
    compile regression versus the immutable pre-plan revision; and
12. all retained commits, ledger evidence, and final closure are pushed.

## Execution ledger

Append one entry for every baseline, coverage backfill, retained cleanup,
rejected abstraction, cumulative verification, inception run, performance
decision, and push checkpoint.  Each entry names the exact revision and does
not reuse measurements from a different source tree.

- **C0 (BASELINE).** Production source is unchanged from `d27de604`.  No
  stale Cachegrind, Valgrind, perf-recording, benchmark, or inception process
  was running; the process inspection matched only its own command line.
  Root `make -j32 test-report-through-pa38` passes 5,465/5,465.  The default
  PA38 audit passes with zero fatal findings and 35 warnings.  The report-only
  six-line/100-character scan reports 164 heuristic windows across the broad
  non-optimizer scope; these remain classification input rather than a target
  count.
- **C1 (DRIVER NATIVE-STATS PREFIX).** `lowir_native_stats_report` now owns
  the exact common codegen-pipeline diagnostic prefix through planned interval
  releases.  Compile-only and link modes retain their distinct record names,
  surrounding fields, timing suffixes, and function census.  Normalized
  diagnostic key order is unchanged; the focused compile object and linked
  executable are byte-identical before and after.  PA30 passes 100/100, PA31
  passes 31/31, and report-through-PA31 passes 4,377/4,377.  The original
  `cppgm++.cpp:1832` duplicate is gone.  The audit remains zero-fatal/35
  warnings because removing it exposes another previously shadowed exact ABI
  statistics block in `cppgm++.cpp`; that newly visible driver-report family
  is assigned to a separate C1 follow-up rather than hidden in this commit.
- **C1-A1 (DRIVER ABI-STATS BLOCK).** The newly exposed duplicate was one
  exact ABI resolution/cache/path diagnostic sequence used by integrated
  compile stats and `--emit-lowir` lowering stats.  It now belongs to
  `lowir_driver_stats_report`; the distinct production-definition prefixes
  and lowering suffixes remain local.  Both diagnostic key sequences and the
  focused object/LowIR output are unchanged.  PA30 passes 100/100, PA37 passes
  188/188, `git diff --check` is clean, and the default audit falls from 35 to
  34 warnings with no new duplicate exposed.
- **C2-A1 (NAMED-FLAVOR VOCABULARY).** The PA11 model vocabulary now owns the
  exact struct/class/union and enum/scoped-enum classifications.  PA12
  semantic analysis, later constexpr/RTTI/trait consumers, type layout,
  source-type lowering, and call/operator lowering reuse two inline contiguous-
  range facts;
  entity bounds, type shape, completeness, dependency, access, and ABI policy
  remain at each caller.  Direct truth-table search now finds only the two
  vocabulary definitions.  PA12 passes 183/183, PA15 121/121, PA16 300/300,
  PA21 149/149, PA26 114/114, and PA34 374/374.  The first inline spelling used
  the old equality chains verbatim; on the full candidate corpus it raised the
  corrected self/GCC ratio to 1.537x and was rejected.  An out-of-line trial
  removed repeated header parsing but left full-corpus aggregate CPU flat and
  wall parity near 1.51x.  The retained range spelling keeps the hot facts
  inline with a smaller source and generated-code shape.  Two clean candidate
  O1 corpus lanes average 31.520 s self and 21.180 s GCC-built, or 1.488x;
  their mean aggregate CPU is 901.515 s and 584.745 s.  Against the immutable
  old self compiler on the identical range-source corpus, aggregate CPU moves
  only +0.37%.  Two clean Clang-built lanes average 21.785 s, for a 1.447x
  self/Clang ratio.  Compiler SHA-256 values are `4634047a...` self,
  `601b9c10...` GCC, and `a4261481...` Clang.
- **C2-A1 BEST-CASE HOST GUARD.** The new requested guard builds `cppgm++`
  itself with GCC 15.2 and Clang 21.1 at `-O3`, then compiles the unchanged
  frozen source at `-O0`.  Three interleaved pairs show no repeated regression:
  GCC candidate-minus-baseline wall differences are -0.10, +0.08, and +0.04
  seconds (mean +0.007 s, about +0.15%); Clang differences are -0.02, +0.06,
  and -0.14 seconds (mean -0.033 s, about -0.7%).  GCC objects are exact at
  SHA-256 `b0d3d8d3...`; Clang objects are exact at `1fe5f0e4...`.  The GCC
  baseline/candidate text is unchanged at 7,066,657 bytes; Clang text falls
  64 bytes from 5,493,699 to 5,493,635.  This O3-compiler/O0-workload lane is
  now mandatory at performance checkpoints; the earlier self-built/O1 frozen
  screen is not evidence for this guard.
- **C2-A1 CUMULATIVE GATE.** Root report-through-PA38 passes 5,465/5,465 and
  the PA38 file audit remains zero-fatal with 34 warnings.  The first broad
  run had one isolated PA30 `runtime-to-float80` execution failure; its
  immediate focused rerun passed, and a second complete through-PA38 run was
  clean.  No source or fixture was changed in response.  C3 later reproduced
  this only under cross-assignment report concurrency, so it is no longer
  classified as an isolated transient; see the C3-A1 cumulative record.
  `git diff --check` is clean.
- **C2-A2 COVERAGE AUDIT (TRIVIAL LIFECYCLE FACT).** The two private predicates
  at the top of `pa15_lowering_abi.cpp` have the same invalid-binding,
  member-owner, function-type, zero-explicit-parameter, constructor/base-entry,
  and destructor truth table.  Their consumers remain intentionally distinct:
  lifecycle symbol metadata combines that binding fact with the lowered
  implicit-object parameter and `no_inline`, whereas host-object emission uses
  it only to omit a semantically trivial helper.  PA16 README's lifecycle
  contract owns both policies.  Existing structural controls include the
  unnamed-namespace constructor's `trivial_lifecycle`/alias relationship and
  the local-class trivial-lifecycle fixture; PA16 object-lifetime and PA30
  object/link suites cover demanded and omitted helper behavior.  Fast gates
  are PA15, PA16, and PA30 followed by report-through-PA30 and the audit.
- **C2-A2 (TRIVIAL LIFECYCLE FACT).** `IsTrivialLifecycleBinding` is now the
  sole owner of the exact validated binding fact; metadata and emission policy
  remain at their callers.  GCC-O3 compiler text falls 320 bytes from
  7,066,657 to 7,066,337.  PA15 passes 121/121, PA16 300/300, PA30 100/100,
  report-through-PA30 passes 4,346/4,346, the audit remains zero-fatal/34, and
  `git diff --check` is clean.
- **C2-A3 COVERAGE AUDIT (DEFERRED FUNCTION QUEUE).**
  `QueueFunctionDefinitionValidation` and `DemandRuntimeDefinition` end in the
  same fact-availability check and `deferred && NOT_STARTED` transition to
  `QUEUED`, followed by the same ordered worklist append and push counter.
  Exception-specification validation remains unique to the first caller;
  emission demand, demand-edge replay, vtable marking, and lifecycle base-entry
  recursion remain unique to the second.  `DemandVtableFunction` deliberately
  queues the inverse non-deferred policy and is excluded.  PA12's function-
  definition/address-taking contract and the `300-deferred-demand-closure`
  reducer cover runtime demand recursively materializing a selected member and
  its constructor dependency; out-of-class/defaulted special-member fixtures
  cover validation queueing, while PA16 demand-driven lifecycle tests exercise
  the later consumer.  Fast gates are that PA12 reducer, PA12, and PA16,
  followed by report-through-PA16 and the audit.
- **C2-A3 (DEFERRED FUNCTION QUEUE).**
  `QueueDeferredFunctionDefinition` now solely owns fact availability and the
  deferred `NOT_STARTED -> QUEUED` transition, ordered append, and statistics
  increment.  Validation, runtime-demand prerequisites, and the deliberately
  inverse vtable policy remain local.  The focused recursive-demand reducer
  passes 1/1, PA12 passes 183/183, PA16 passes 300/300, report-through-PA16
  passes 1,498/1,498, and the audit remains zero-fatal/34.  GCC-O3 compiler text
  is 7,065,729 bytes, 608 below C2-A2.  A first timing block was invalidated
  when an unrelated forced rebuild modified `dev/cppgm++` during the samples;
  the candidate was then copied immutably and all rerun objects matched at
  `b0d3d8d3...`.  Candidate median user time is 4.08 s versus 4.09 s at the
  most recent C2-A1 O3/O0 guard, so the new queue call boundary shows no
  incremental regression; wall/system samples were still cooling from the
  competing rebuild and receive no retention credit.
- **C3-A1 COVERAGE AUDIT (LIFETIME SCOPE PREPARATION).** Automatic-object and
  temporary obligations perform the same indexed growth of `scope_lifetimes_`
  and `nearest_lifetime_scopes_`, then make the scope its own nearest lifetime
  owner.  Destructor selection, accessibility, demand/elision, ordinary versus
  temporary obligation construction, and initializer-list scope marking remain
  distinct.  PA16 README owns local object lifetime and reverse destruction;
  PA26 owns control-dependent temporary and initializer-list cleanup.  Existing
  automatic controls include member-object and local class-array lifetime;
  temporary controls include the temporary functor and nested logical cleanup;
  initializer-list backing-array lifetime covers the required post-append
  marking.  Fast gates are the focused PA16 automatic/temporary tests and PA26
  initializer-list/cleanup tests, then PA16, PA26, report-through-PA26, and the
  audit.
- **C3-A1 (LIFETIME SCOPE PREPARATION).** `PrepareLifetimeScope` now owns the
  identical indexed-vector growth and nearest-scope publication; both callers
  still construct and append their own obligation, and only the temporary path
  marks initializer-list lifetime.  Five focused automatic/temporary/list
  controls pass, PA16 passes 300/300, PA26 114/114, report-through-PA26
  3,813/3,813, and the audit remains zero-fatal/34.  GCC-O3 compiler text grows
  616 bytes (7,065,729 to 7,066,345), so no size credit is claimed.  Two
  interleaved frozen O0 pairs have candidate-minus-baseline wall differences
  of +0.13 and -0.11 seconds and user differences of +0.12 and -0.13 seconds;
  all objects are exact at `b0d3d8d3...`.  The call boundary is neutral and
  retained for single lifecycle ownership.
- **C3-A1 CUMULATIVE GATE AND HARNESS FINDING.** The PA38 audit remains
  zero-fatal/34.  The default cross-assignment report repeatedly records only
  PA30 `200-runtime-to-float80` as an implementation failure; raising its
  request timeout to 120 seconds and forcing batch `exec` do not change that
  result.  The identical PA30 four-worker batch passes 100/100, and the full
  root target with assignments serialized but 32 subtest workers passes
  5,465/5,465:
  `make -j32 test-report-through-pa38 TEST_REPORT_ASSIGNMENT_JOBS=1
  TEST_REPORT_SUBTEST_JOBS=32`.  This isolates a cross-assignment harness or
  resource/path interaction rather than a source fixture or compiler-output
  change.  No test source, reference, or production behavior was changed to
  cover it.  The ordinary default-concurrency exit criterion remains open for
  final closure.

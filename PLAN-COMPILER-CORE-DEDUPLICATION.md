# Plan: Deduplicate the Compiler Core Outside `lowir_opt`

Status: complete

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
- **C3-A2 COVERAGE AUDIT AND BACKFILL (GOTO CLEANUP RANGES).** The two reverse
  loops in `ResolveControlFlowGoto` build the same ordinary/temporary
  destructor action, append it to the goto, and increment the lexical visit
  count; they differ only in validated begin/end indices for exited scopes and
  the common scope.  Ordinary lexical and unwind cleanup have different
  traversal/error/counter policy and remain outside this first extraction.
  Existing PA15 goto fixtures have no class lifetime, and PA32 covers only one
  goto out of a `try`, so neither controls both ranges.  PA16 README now states
  the student-facing rule.  A new PA16 control checks only the relevant LowIR
  relationships: inner-before-outer destructor calls precede a goto leaving
  nested scopes, and a backward same-scope goto destroys the object before
  jumping to its reconstruction block.  It does not compare a complete LowIR
  program or production source text.  This coverage commit precedes the helper
  change.
- **C3-A2 (GOTO CLEANUP RANGES).** `ResolveControlFlowGoto` now has one local,
  allocation-free range appender for ordinary and temporary destructor actions;
  the exited-scope and backward-common-scope paths retain their distinct
  validated bounds, while lexical and unwind cleanup remain separate policy.
  The focused PA16 property passes 1/1, PA16 passes 300/300, PA26 114/114, and
  PA32 151/151 including the existing runtime goto-out-of-try control.  The
  audit remains zero-fatal/34.  GCC-O3 compiler text is unchanged at 7,066,345
  bytes.  Three interleaved frozen O0 pairs have candidate-minus-baseline wall
  differences of +0.04, -0.03, and 0.00 seconds; all output objects are exact
  at `b0d3d8d3...`.  The range ownership is therefore retained without size or
  performance credit.
- **C4 COVERAGE AUDIT AND BACKFILL (BOUND INTRINSIC CALL SHELL).** Ordinary
  builtin and hosted integer-intrinsic call builders repeat the same typed call
  node, bound callee, ordered argument edges, and prvalue result shell.  Binding
  selection, integer folding, target conversion, and expression accounting
  remain separate.  PA12 already covers zero-argument `__builtin_abort` and
  PA34 covers constant and runtime bswap/clz/ctz/popcount families, but neither
  family had a focused invalid-arity control.  The PA12 and PA34 READMEs now
  state those arity rules, and reference-generated negative reducers cover
  both.  The focused cases pass 1/1 each, PA12 passes 184/184, and PA34 passes
  375/375.  This coverage commit precedes the construction-helper change.
- **C4 (BOUND INTRINSIC CALL SHELL).** One helper now owns the typed call node,
  bound callee, ordered argument edges, and prvalue result shared by builtin,
  integer, floating, and memory intrinsic calls.  Each caller still owns
  binding selection, external-function demand, integer constant folding,
  target conversion, and expression accounting.  Atomic and vector calls keep
  their separate unbound typed shapes.  The change removes 28 net source lines
  and reduces optimized compiler text by 4,756 bytes with GCC and 716 bytes
  with Clang.  The focused PA12/PA34 cases pass 7/7, PA12 passes 184/184, PA21
  149/149, PA34 375/375, and the through-PA34 report passes 5,001/5,001; the
  32-way through-PA38 gate passes 5,467/5,467, and the audit remains
  zero-fatal/34.  All frozen O0 output objects are exact.  Three
  interleaved GCC pairs differ by -0.06, -0.04, and +0.01 seconds.  Four
  order-alternated, CPU-pinned Clang pairs have median walls of 4.775 seconds
  before and 4.765 seconds after (identical 4.275-second mean user time), so
  the extraction is performance-neutral within measurement noise.
- **C5 COVERAGE AND STATE-OWNERSHIP AUDIT.** The exact common function-entry
  state is the active function, temporary counter, tracked slot maps, CFG
  incoming counts, full-expression/exception/initializer-list caches,
  break/continue targets, labels, current bit-field unit, local presentation,
  and the neutral current-this/member baseline.  Result shape, lifetime-return
  planning, parameter position, source-name collection, and virtual-base
  preparation are ordinary-only policy; synthetic result and virtual-base
  setup stay synthetic.  Current block selection, statement-task draining,
  constructor/destructor return routing, and namespace-initializer switches are
  scoped or finalization invariants rather than resettable common state, while
  symbol/type/entity maps are translation-unit caches.  Existing focused tests
  already exercise multiple ordinary functions, ordinary-to-synthetic
  aggregate helpers, local-static helpers, exception-cache reuse, and host TLS
  lifecycle wrappers; those five controls pass 5/5, so no source-specific
  coverage backfill is justified before the ownership-only extraction.
- **C5 (COMMON FUNCTION-LOWERING RESET).**
  `ResetCommonFunctionLoweringState` now owns exactly the state shared by
  ordinary and synthetic function entry.  Ordinary result/lifetime/parameter
  and virtual-base setup and synthetic result/entry setup remain local.  The
  focused transition controls pass 5/5; PA15, PA16, PA21, PA26, and PA33 pass
  781/781, and the 32-way through-PA33 report passes 4,626/4,626.  GCC-O3 text
  falls 676 bytes and Clang-O3 text falls 956 bytes; all frozen O0 objects are
  exact.  With ASLR disabled, six pinned, order-alternated GCC pairs have
  median walls of 4.545 seconds before and 4.535 seconds after.  Clang's clean
  samples center at 4.77--4.79 seconds (about +0.4% after, below the oracle's
  approximately 1% resolution); one isolated 6.74-second candidate sample did
  not recur in six immediate repetitions.  The extraction is retained as
  size-positive and performance-neutral within the measurement floor.
- **C6 COVERAGE AND CALL-SHAPE AUDIT.** A direct-call primitive may own only
  CALL construction, result type, the direct function operand, and marking the
  symbol referenced.  General expression lowering must retain direct/indirect
  selection, arguments and passing roles, indirect-result storage,
  virtual-base forwarding, virtual dispatch, cleanup/EH, no-return behavior,
  and result use.  Synthetic callers retain their own entry/result/return
  policy.  The PA15 simple direct-call fixture checks the exact typed callee,
  the mixed direct/function-pointer fixture protects indirect selection, and
  the PA33 host TLS wrapper test behaviorally reaches a synthetic wrapper's
  initializer call.  These controls pass 3/3, so no new fixture is needed for
  the initial one-ordinary/one-synthetic migration.
- **C6 (DIRECT-CALL INSTRUCTION PRIMITIVE).**
  `DirectCallInstruction` now exclusively owns GraphLowerer's typed direct
  CALL operand and reference mark.  General expression calls, TLS wrappers,
  constructors/destructors, aggregate helpers, array allocation/deallocation,
  special members, RTTI/EH runtime calls, catch objects, and lifecycle
  registration use it; the remaining manual call on this surface is virtual
  and indirect.  Arguments, passing roles, results, virtual bases, cleanup/EH,
  and emission stay in each caller.  The implementation removes 44 net source
  lines.  PA15, PA16, PA17, PA26, and PA33 pass 879/879, and the 32-way
  through-PA33 report passes 4,626/4,626; the periodic through-PA38 gate passes
  5,467/5,467 and the audit remains zero-fatal/34.  GCC-O3 text changes by +20
  bytes and Clang-O3 by +100 bytes; all frozen O0 objects are exact.  Six pinned,
  ASLR-disabled GCC pairs have a +0.01-second median paired wall delta, and six
  Clang pairs average +0.015 seconds (+0.3%).  Both are performance-neutral at
  the timing floor, so the coherent ownership and source reduction are kept.
- **C7-A COVERAGE AUDIT (FUNCTION HEADER PARSING).** Declaration and definition
  parsing share symbol, named typed parameters, return type, and function/symbol
  metadata.  Parameter value allocation, function debug location, slots,
  blocks, labels, and instructions remain definition-only.  Existing PA13
  controls cover declarations, both header forms with boundary metadata,
  definition debug facts, and malformed duplicate parameters; the focused set
  passes 4/4.  The malformed metadata suite also exercises both declaration
  and definition headers, so no new syntax-specific fixture is justified.
- **C7-A (SHARED FUNCTION HEADER PARSER).** One typed helper now parses the
  symbol, parameters, return type, boundary facts, and symbol metadata for both
  function declarations and definitions.  Definitions still allocate parameter
  values and parse debug facts, slots, blocks, labels, and instructions only
  after that header.  PA13, PA29, and PA37 pass 601/601, and the 32-way
  through-PA37 report passes 5,422/5,422; the audit remains zero-fatal/34.
  GCC-O3 text is byte-count unchanged, Clang-O3 text falls 92 bytes, and all
  frozen O0 objects are exact.  Four
  pinned, ASLR-disabled pairs have zero median paired wall delta with GCC and
  +0.005 seconds with Clang, both below the timing floor.
- **C7-B COVERAGE AUDIT (ROLE-NEUTRAL OPERAND VIEW).** The shared view visits
  the three fixed operand fields followed by every variadic argument without
  assigning value, label, callee, EH, or debug roles.  Existing PA13 direct and
  indirect calls exercise fixed and variadic operands, the phi control-flow
  case protects predecessor-label structure, and the empty EH-filter debug
  roundtrip protects its specialized canonicalization.  PA29 native call
  behavior and PA37 canonical plus direct-versus-serialized object roundtrips
  cover preparation and re-derived address facts across the object boundary.
  These structural and behavioral controls cover the ownership-only migration;
  no source-specific fixture is justified.
- **C7-B (NEUTRAL LOWIR OPERAND VIEW).** `lowir_operand_view.h` now owns the
  allocation-free `first`, `second`, `third`, then `args` traversal.  The three
  preparation walks use it for reference discovery, serialized-fact clearing,
  and derived address binding; optimizer support retains source-compatible
  aliases.  Phi, call, EH, debug, and all other role-sensitive logic remains
  specialized.  The focused PA13 call/phi/EH controls, PA29 direct/indirect
  calls, and PA37 canonical/object roundtrips pass; the PA13, PA29, and PA37
  assignment gates pass, the 32-way through-PA37 report passes 5,422/5,422,
  and both audits remain clean with the file audit at 34 warnings.  GCC-O3
  compiler text falls 60 bytes and Clang-O3 grows 144 bytes; frozen O0 objects
  are exact for both.  Four pinned, ASLR-disabled GCC pairs have a -0.06-second
  median paired wall delta despite one isolated system outlier, with a fresh
  confirmation pair at -0.01 seconds.  Four Clang pairs have a +0.005-second
  median paired delta.  Both are performance-neutral at the timing floor.  The
  32-way C7 inception checkpoint matches every object and the final compiler.
- **C8-1 COVERAGE AUDIT (UNCONDITIONAL MIR CONTROL FLOW).** Ordinary native
  block-entry discovery and host-EH region analysis share only the exact
  unconditional terminator opcode set.  PA38 branch-fallthrough cleanup and
  fallthrough-jump elision exercise ordinary block termination; PA31 shared
  resume after stack cleanup and ordered typed catches exercise host-EH
  terminal handling.  Conditional-branch classification and function-return
  classification remain separate policies.  Existing structural, object, and
  behavioral controls cover this ownership-only extraction, so no fixture is
  added.
- **C8-1 (UNCONDITIONAL MIR CONTROL-FLOW PREDICATE).** A neutral native-MIR
  helper now owns the exact JMP, indirect-JMP, RET, FRET, RESUME, THROW, and
  EXIT terminator set used by ordinary host block-entry discovery and host-EH
  region analysis.  Branch and function-exit predicates stay local.  Focused
  PA29/PA31/PA38 controls pass 5/5; the affected assignment gates pass PA29
  49/49, PA31 21/21, and all PA38 suites and property controls.  Both audits
  remain clean with the file audit at 34 warnings.  The frozen O0 object is
  exact and GCC-O3 compiler text grows 124 bytes.  Post-inception machine load
  roughly doubled absolute wall time and the two paired deltas reverse at
  -0.56 and +0.69 seconds, providing no directional regression signal.
- **C8-2 COVERAGE AUDIT (CODE-BUFFER FIXUP ARITHMETIC).** Local, named, and
  typed rel32 resolution repeat the same target/end/addend range validation;
  named and typed absolute fixups repeat the same signed-addend overflow and
  underflow checks.  PA29 direct branches cover local rel32, PA30 data
  relocation through an indirect call covers absolute addresses, PA31 runtime
  relocation facts cover hosted fixups, and PA38 fallthrough controls cover
  relaxed branches.  Buffers large enough to exceed process-sized offset
  validation cannot form a practical course fixture, and diagnostic text is
  not an oracle; structural/object coverage and byte-exact A/B are sufficient.
- **C8-2 (CHECKED CODE-BUFFER FIXUP ARITHMETIC).** One helper now validates
  and computes local, named, and typed rel32 deltas, and one helper applies a
  signed addend to named and typed absolute addresses.  Existing exception
  text is preserved, while intermediate signed overflow is now rejected rather
  than invoking undefined behavior.  Focused PA29/PA30/PA31/PA38 controls pass
  5/5, all four affected assignment gates pass, and both audits remain clean
  with the file audit at 34 warnings.  The frozen O0 object is exact and GCC-O3
  compiler text grows 340 bytes.  Performance is deferred to the cumulative C8
  checkpoint because this cold fixup path does not justify another noisy
  two-pair micro-run.
- **C8-3 COVERAGE AUDIT (HOST RELOCATION ENVELOPE).** Named and typed host
  fixups repeat relocation-kind selection, the nonlocal address LEA-to-load
  rewrite, offset publication, and ELF addend normalization.  Raw-name lookup
  and typed declaration/object/program-symbol lookup must remain separate.
  PA30 data/imported-function address cases cover absolute and external named
  targets, PA31 runtime relocation facts cover host call/EH classes, and PA32
  imported-global GOT plus thread-local import inspection covers typed
  GOTPCRELX and TPOFF32 targets.  These object facts distinguish both target
  identity and relocation class, so no additional fixture is needed.
- **C8-3 (HOST RELOCATION ENVELOPE).** One helper now initializes relocation
  kind, validates and rewrites nonlocal RIP-relative addresses, publishes the
  offset, and normalizes the ELF addend for both raw-name and typed-symbol
  fixups.  Raw-name lookup and typed declaration/object/program-symbol lookup
  remain local and unchanged.  The six focused PA30/PA31/PA32 controls and all
  three assignment gates pass; both audits remain clean with the file audit at
  34 warnings.  The frozen O0 object is exact and GCC-O3 compiler text grows
  80 bytes.
- **C8-4 COVERAGE AUDIT (PRESSURE-BACKED INTEGER RESULTS).** Atomic load,
  exchange, and add-fetch plus integer division and shift share an exact
  post-consumption tail: store a register result to its pressure home when one
  exists, then define the value at the home or register.  Narrow normalization
  and each operation's operand consumption must stay local and ordered.  PA29
  already has focused atomic-load pressure, atomic-exchange loop pressure,
  narrow signed frame-home shift/division, direct-return division, and
  register-pressure division controls.  Together they exercise register and
  frame results, narrow width, return placement, and liveness, so no fixture
  backfill is needed.
- **C8-4 (PRESSURE-BACKED INTEGER RESULT FINALIZATION).** `FunctionLowerer`
  now owns the exact conditional pressure-home store and result publication
  tail used by atomic load/exchange/add-fetch and integer division/shift;
  normalization and operand consumption remain at each operation site.  The
  five focused controls produce byte-identical MIR and identical executable
  outcomes with the isolated pre-change `3fed967f` compiler.  Three legacy
  PA29 exact-MIR sidecars disagree with both binaries, so they were not
  regenerated or used to hide the equivalence result; PA29's supported
  structural/behavioral gate passes 291/291 and PA38 passes 45/45.  Root
  32-way report-through-PA38 passes 5,467/5,467, the LowIR audit passes, and
  the file audit remains zero-fatal/34 at exactly 3,000 lines for
  `lowir_native.cpp`.  GCC-O3 compiler text grows 188 bytes.  Four pinned,
  ASLR-disabled frozen O0 pairs emit the exact `b0d3d8d3...` object; excluding
  the first cold pair, both baseline and candidate have a 4.57-second median,
  so the extraction is performance-neutral at this oracle's timing floor.
- **C8 CUMULATIVE CHECKPOINT.** Commits `f5a47d7f`, `1827e19d`,
  `3fed967f`, and `22263f1d` retain the four exact native-support families.
  Root 32-way report-through-PA38 passes 5,467/5,467; both audits pass and the
  file audit remains zero-fatal/34.  Three fresh, current-source O1 compiler
  lanes have median wall times of 31.71 seconds self, 23.02 seconds GCC-built,
  and 21.31 seconds Clang-built: 1.378x self/GCC and 1.488x self/Clang.  The
  repeated final compiler hashes are `3e4970b6...` for the common GCC-host
  configuration and `6fafe92b...` for the Clang-host configuration.  Thus the
  maximum wall ratio is below 1.5, but the Clang-relative ratio has materially
  worsened from the early-plan checkpoint and remains a final-closure item;
  the GCC improvement must not hide it.  A same-current-source self A/B
  against pre-C8 `216f5690` reverses direction across its two ABBA blocks;
  per-run median aggregate CPU moves only +0.25% and every final compiler is
  exact, so there is no repeated C8 regression above the 0.5% threshold.
  Across C8, GCC-O3 text grows 732 bytes and Clang-O3 text grows 44 bytes.
  Two pinned, ASLR-disabled frozen blocks are exact for both families; GCC
  baseline/candidate medians are 4.54/4.52 seconds and Clang medians are
  4.725/4.695 seconds.  C8 therefore preserves the fully optimized best case.
- **C9 COVERAGE AUDIT (FINAL-EMISSION PEEPHOLE CHAIN).** The ordinary and
  host-EH loops have the same ordered pre-fallback chain: carried scratch
  store, address fold, constant division, fused integer normalization,
  delayed frame forwarding, forwarded reload, constant byte-store
  coalescing, flag-safe zero materialization, redundant normalization, then
  shared return.  The ordinary fallback remains `emit_instruction`; the host
  fallback must continue to derive landing identity and call
  `emit_host_instruction`.  PA29 has focused generated-behavior controls for
  every common family, including its stats-bearing scratch/redundancy cases
  and multiple-return epilogue case.  PA31 host-EH execution/object controls
  cover protected calls, cleanup-only resume, landing pads, unwind ranges,
  and O1 forwarding, while PA38's layout-policy guard contains both ordinary
  and host-EH functions.  These tests plus exact frozen-object comparison
  cover the shared boundary without adding a source-specific fixture, so no
  README or test backfill is needed.
- **C9 (FINAL-EMISSION PEEPHOLE CHAIN).** One allocation-free per-block
  emitter now owns the frozen ten-stage chain and returns the exact number of
  MIR instructions consumed.  A force-inlined eight-opcode eligibility check
  (`mov`, `lea`, load/store, sign/zero extension, and integer/floating return)
  keeps all other instructions off the shared call boundary.  Ordinary and
  host-EH fallback emission remains entirely local.  Ten focused PA29
  programs have exact baseline/candidate MIR and native executables, and a
  focused O1 host-EH object is exact.  PA29 passes 291/291, PA31 31/31, PA38
  45/45, and root 32-way report-through-PA38 passes 5,467/5,467.  Both audits
  pass; the file audit drops from 34 to 33 warnings, its actionable native
  duplicate is gone, and `lowir_native_elf.cpp` falls from 2,983 to 2,980
  lines.  GCC-O3 text falls 1,852 bytes, Clang-O3 falls 676, and self/GCC/Clang
  O1 text moves +16/-260/-380 bytes.  Three current-source timing lanes have
  median wall times of 31.77 seconds self, 21.60 GCC, and 21.34 Clang, or
  1.471x self/GCC and 1.489x self/Clang; median self aggregate CPU is 901.50
  seconds, at the established pre-C9 level.  A direct old/current self ABBA
  has one load outlier but no repeated regression, and every final compiler
  is exact.  Two pinned frozen blocks are exact at `b0d3d8d3...` for GCC and
  `1fe5f0e4...` for Clang; baseline/candidate medians are 4.535/4.555 seconds
  (+0.44%) for GCC and 4.755/4.730 for Clang.  Explicit-O1 inception with all
  three job settings at 32 matches every object and the final compiler.
- **C9 REJECTED SHAPES.** Calling the plain outlined chain for every MIR
  instruction left the parity result at the 1.5 boundary and exposed a hot
  per-instruction call in the self compiler.  Forcing the complete chain
  inline instead grew self O1 text by 3,136 bytes and produced 34.91/33.60
  second self lanes, clearly worse than the retained guarded outline.  Both
  experiments were removed; only the small exact eligibility predicate is
  force-inlined in the retained implementation.
- **C10 TABLE INVARIANT AUDIT.** All ten PA12 open-address indexes use a zero
  slot sentinel, power-of-two capacity, linear probing, and 70% growth, but
  their policies are not otherwise interchangeable.  Using-function identity
  stores insertion-order entry index plus one and rejects duplicates; the
  request-local binding set stores binding plus one directly, starts at eight,
  and rebuilds from occupied slots rather than entry order.  Pack bindings,
  indexed sequences, and enum-operator candidates store stable entry index
  plus one and own distinct duplicate/secondary-sequence policy.  Call
  conversions and function signatures overwrite an existing value.  Template
  specializations own a request-state transition machine.  Their hashes are,
  respectively, typed key hashes or the shared two-half 64-bit hash; none have
  probe counters, and storage accounting includes only each table's owned
  entry, slot, secondary-head, and sequence capacities.  These eight families
  remain local.  Template-argument partitions and
  function-template result identities alone have byte-equivalent rebuilds:
  both store a cached hash in insertion-order entries and reconstruct a
  zero-filled entry-index-plus-one slot vector without changing counters.
  Partitions reserve identity zero for the empty sequence and otherwise count
  requests, cache hits, and occupied/final probes; result identities return an
  invalid sentinel for empty input and additionally count initial and equality
  atom visits.  Their offset/atom and entry/slot capacities remain separately
  included in semantic side storage.
- **C10-1 COVERAGE AUDIT (CACHED-HASH SLOT REBUILD).** Existing PA20 pack
  partition tests and PA23 dependent-result/SFINAE tests exercise insertion,
  cache hits, and stable semantic identity, but no one translation unit crossed
  the initial 32-slot table's 70% growth point.  PA20 and PA23 now state the
  scalable student-facing behavior.  Focused course reducers form 24 distinct
  two-pack argument partitions and 24 distinct, equivalently redeclared
  dependent function results, then validate ordinary source/LowIR behavior;
  they do not inspect table classes, helper names, or production source.  The
  pre-change compiler's stats and output hashes are frozen before the helper
  extraction.  Fast gates are those two reducers, PA20, and PA23, followed by
  report-through-PA23 and the audit; as the pair completes two table families,
  the full through-PA38, corrected performance, and inception checkpoints are
  also required.
- **C10-1 COVERAGE BACKFILL.** The reference-generated PA20 reducer passes with
  72 partition requests, 48 hits, 123 probes, 957,852 peak semantic-stage
  bytes, and current output SHA-256 `4e3f9813...`; its canonical reference is
  `992fd3e3...`.  The PA23 reducer passes with 48 result-identity requests, 24
  hits, 82 probes, 1,080 atom visits, 669,551 peak semantic-stage bytes, and
  output/reference SHA-256 `485fc8e3...`.  PA20 passes 175/175, PA23 passes
  414/414, report-through-PA23 passes 3,137/3,137, and the PA38 file audit
  remains zero-fatal/33.  This README-and-fixture increment is committed before
  the production slot-rebuild extraction.
- **C10-1 (CACHED-HASH SLOT REBUILD).** One `.cpp`-local typed algorithm now
  rebuilds the zero-sentinel, entry-index-plus-one slot vectors for template-
  argument partitions and function-template result identities.  It allocates
  the same one replacement vector, walks cached hashes in insertion order,
  uses the same mask and linear probe, swaps at the same point, and touches no
  request, hit, probe, atom-visit, identity, or storage-accounting state.  Both
  growth reducers retain their exact output hashes and all recorded counters
  and peak storage.  PA20 passes 175/175, PA23 414/414, root 32-way report-
  through-PA38 passes 5,469/5,469, and the audit remains zero-fatal/33.  The
  helper makes one implementation explicit at a net four source lines.
  Self-built O1 compiler text is unchanged at 7,898,066 bytes; GCC-O3 text
  grows 128 bytes and Clang-O3 text grows 64 bytes.
- **C10-1 PERFORMANCE.** Four rotating current-source lanes have median wall
  times of 32.15 seconds self and 21.675 seconds Clang-built; three GCC-built
  lanes have a 22.46-second median.  The corrected ratios are therefore 1.432x
  self/GCC and 1.483x self/Clang.  Median self aggregate CPU is 905.77 seconds,
  +0.47% from C9 and below the repeated-regression threshold; every lane has
  final compiler SHA-256 `ca24f63a...`, shared-object census SHA-256
  `6a271141...`, and the same text.  A direct pre-change/current self ABBA on
  identical current source had alternating load outliers; its clean lanes have
  31.74/31.64-second baseline/candidate wall medians and 906.13/906.35-second
  aggregate-CPU medians, or -0.32% wall and +0.02% CPU.  The GCC-O3 frozen
  block is exact at `b0d3d8d3...`, with baseline/candidate medians 4.55/4.56
  seconds and a -0.27% paired wall delta.  A final clean Clang-O3 block is
  exact at `1fe5f0e4...`, with 4.850/4.805-second medians and a -0.93% paired
  wall delta.  Earlier Clang samples contained late 6-second outliers that
  moved from candidate to candidate when labels were reversed; they receive no
  retention credit.
- **C10-1 REJECTED HELPER SHAPES.** Returning the replacement vector recovered
  GCC text but outlined two typed copies in the self compiler and grew final
  self O1 text by 320 bytes.  Forcing that version inline made GCC and Clang
  exact but grew self text by 1,488 bytes.  A raw-entry-pointer variant still
  grew the self table object by 156 bytes.  All three experiments were removed;
  the retained vector-reference/destination-pointer form keeps final self text
  exact and has the best cross-compiler balance.
- **C10 CLOSURE AND INCEPTION.** The other entry-index tables do not share the
  cached-hash contract: three recompute a two-half 64-bit key hash, four use
  distinct typed-key hash expressions, and the request-local binding set
  rebuilds direct binding-plus-one slots from the old slot array.  Their
  overwrite, reject, sequence, and request-state policies also differ.  A
  common functor-templated rehasher would therefore instantiate the loop once
  per entry/hash pair rather than provide one executable implementation, while
  making policy ownership less local.  No additional table is migrated.  A
  fresh explicit-O1 checkpoint at `78fdc731`, with outer, inner, and object
  parallelism all set to 32, compared 215/215 objects exactly and produced
  identical self/inception compilers with SHA-256 `ca24f63a...` and 8,631,459
  text bytes.
- **C11-1 (SYNTAX EDGE MUTATION PREPARATION).** `SyntaxArena` now has one
  private primitive that validates edge capacity, records the parent's prior
  first/last links for rollback, and appends the new edge record.  Ordinary
  append and prepend retain their distinct link-order tails and their common
  no-node no-op policy.  Existing PA10 structured-tree coverage is sufficient:
  ordinary parsing exercises append throughout, while the function-type alias
  pack fixture requires prepend to place `parameter-pack` before an existing
  declarator child.  That reducer passes 1/1, PA10 passes 164/164, report-
  through-PA10 passes 583/583, and the audit remains zero-fatal/33.  The O1
  self-built compiler falls 136 text bytes (8,631,459 to 8,631,323).  GCC-O3
  compiler text grows 1,088 bytes, while the corresponding Clang-O3 object
  falls 916 bytes.  Three pinned GCC-O3 frozen runs are exact at
  `b0d3d8d3...`, with 4.56/4.52-second baseline/candidate wall medians and
  equal 4.05-second user medians.  Three O1 self-built lanes are also exact,
  with 7.92/7.79-second wall medians and 7.36/7.27-second user medians; the
  isolated 8.41-second baseline rerun receives no retention credit.
- **C11-1 REJECTED INLINE SHAPE.** Forcing the shared preparation primitive
  inline changed the affected self/GCC/Clang object text by +2,055/+64/-24
  bytes versus the duplicated baseline.  The experiment was removed: the
  retained outline changes those objects by -115/+1,098/-916 bytes and avoids
  multiplying the self compiler's large vector-growth path.
- **C11-2 COVERAGE AUDIT (UNARY NODE CONSTRUCTION).** PA10's nested-unary AST
  fixture distinguishes recursive unary construction and exact operator-token
  payloads.  The special identifier-spelled `__real__` and `__imag__` branch is
  a PA34 hosted-extension contract rather than PA10 grammar; PA34's existing
  GNU complex template-constructor fixture compiles both forms through syntax,
  semantics, lowering, and object emission.  The PA34 README now names those
  two component operators explicitly.  No test inspects parser source or helper
  names, and no new fixture is needed before the construction-only extraction.
- **C11-2 (UNARY NODE CONSTRUCTION).** `ParseUnaryExpression` now classifies
  GNU complex-component spelling locally, then shares recursive operand
  parsing, token-backed unary-node construction, operand attachment, and return
  with the ordinary prefix operators.  The special missing-component-operand
  diagnostic remains selected only for `__real__`/`__imag__`.  The two focused
  controls pass 2/2, PA10 passes 164/164, PA34 passes 375/375, report-through-
  PA34 passes 5,003/5,003, and the audit remains zero-fatal/33.
  `pa10_syntax.cpp` falls from 2,999 to 2,994 lines.  Isolated O1-self,
  GCC-O3, and Clang-O3 object text changes by +20/-3/-128 bytes, respectively,
  all below a meaningful runtime threshold; cumulative C11 timing follows
  after this third retained commit.
- **C11 CUMULATIVE CHECKPOINT A.** Commits `617144de`, `157ca0a4`, and
  `458e7484` pass the 32-way report-through-PA38 gate at 5,469/5,469; the file
  audit remains zero-fatal/33.  Clean current-source medians are 31.79 seconds
  self, 20.85 GCC-built, and 21.66 Clang-built, giving corrected ratios of
  1.525x self/GCC and 1.468x self/Clang.  Median clean self aggregate CPU is
  906.31 seconds.  The self/GCC ratio is above the target because the two GCC
  lanes are consistently faster than the previously load-inflated denominator,
  not because self time increased.  Self and GCC lanes reproduce final compiler
  SHA-256 `203d8748...` and object census `f9caeb33...`; Clang's internally
  exact family has final SHA-256 `0815f263...` and census `4b0461b8...`.
  A direct pre-C11/current comparison on identical current source has clean
  baseline/candidate wall medians of 31.805/31.970 seconds and aggregate-CPU
  medians of 905.22/906.07 seconds (+0.09% CPU).  One 955-second baseline and
  one 986-second candidate lane are reciprocal load outliers and receive no
  retention credit.  C11 is therefore performance-neutral so far, while the
  now-more-accurate GCC-relative gap remains a final closure item.
- **C11-3 COVERAGE AUDIT (VARIADIC MACRO CLOSE).** PA4 already states that a
  function-like macro may use either `(...)` or `(identifier-list, ...)`, and
  existing positive fixtures exercise both forms.  No fixture rejected a token
  between either ellipsis and the closing parenthesis, even though that is the
  exact validation repeated in `ParseParameters`.  Two reference-generated
  negative reducers now cover those two student-visible grammar boundaries;
  they do not inspect implementation source.  GNU named variadic parameters
  retain their separate state transition and are not part of this extraction.
  The focused reducers pass 2/2, PA4 passes 74/74, and report-through-PA4
  passes 173/173.  This fixture commit precedes the production extraction.
- **C11-3 (VARIADIC MACRO CLOSE).** One parser-local helper now marks a
  standard variadic macro, advances past `...`, requires the immediate closing
  parenthesis, and returns the replacement-list boundary for both `(...)` and
  `(identifier-list, ...)`.  Identifier validation, duplicate detection,
  separator parsing, and GNU `name...` state remain local.  The four focused
  valid/invalid controls pass 4/4, PA4 passes 74/74, report-through-PA4 passes
  173/173, and the audit remains zero-fatal/33.  `macro_processor.cpp` falls
  from 2,996 to 2,993 lines.  Isolated O1-self/GCC-O3/Clang-O3 object text
  changes by +42/+18/0 bytes; this cold, exact extraction is retained as
  performance-neutral, with cumulative timing deferred to the next checkpoint.
- **C11-4 COVERAGE AUDIT (ABI HASH MIXING).** PA14's public contract requires
  structural comparison and deterministic substitution ordering, and its
  existing equivalent-value, equivalent-named-type, distinct-array-bound,
  equivalent-expression, distinct-type-trait, and qualified-member-template
  fixtures exercise argument, type, expression, path, and substitution keys.
  Those behavioral checks are the appropriate coverage for an implementation-
  private hash primitive; no fixture inspects source text or helper placement.
- **C11-4 (ABI HASH MIXING).** The four byte-for-byte copies of the ABI graph's
  two-word hash mixer now use one small ABI-detail header primitive.  Path,
  expression, argument, type, resolved-type, and substitution owners retain
  their own field selection and ordering, and the distinct vector traversal
  shapes remain local.  PA14 passes 117/117, report-through-PA14 passes
  1,080/1,080, both audits pass, and the file audit remains zero-fatal/33.
  The four affected objects are text-size exact before/after under O1 self
  (298,553 bytes), GCC O3 (196,486 bytes), and Clang O3 (174,075 bytes).
- **C11 CUMULATIVE CHECKPOINT B.** Commits `92b63c92`, `ce35359a`, and
  `b2f45fb7` pass the 32-way report-through-PA38 gate at 5,471/5,471; both
  audits pass and the file audit remains zero-fatal/33.  Three fresh 32-way
  current-source lanes have median wall/user/system times of
  32.50/855.21/50.15 seconds self, 21.00/541.94/45.01 GCC-built, and
  21.62/559.29/45.65 Clang-built.  The corrected wall ratios are therefore
  1.548x self/GCC and 1.503x self/Clang.  Median self aggregate CPU is 905.46
  seconds, essentially unchanged from checkpoint A's 906.31 seconds; maximum
  per-process RSS is 229,596/229,280/228,812 KiB.  Self and GCC lanes reproduce
  compiler SHA-256 `ae80b984...` and 215-object census `33e0cf17...`; Clang
  reproduces compiler `1ebb9254...` and census `f8a4263d...`.
- **C11 DIRECT AND BEST-CASE RETENTION.** A direct B/A/B/A comparison of the
  `8199bbf1` and current self compilers on identical current source produces
  the exact current compiler and object census in every lane.  Pre/current
  wall medians are 32.825/32.20 seconds and aggregate-CPU medians are
  908.05/907.85 seconds (-0.02%), confirming that C11 did not cause the
  less-efficient parallel scheduling seen in the ratio block.  Fully optimized
  host producers preserve the frozen O0 compile as well: GCC baseline/current
  wall medians are 4.535/4.550 seconds (+0.33%), user medians 4.03/4.05,
  system medians 0.50/0.49, and maximum RSS 369,184/367,856 KiB; Clang medians
  are 4.795/4.765 seconds (-0.63%), user 4.29/4.265, system 0.505/0.495, and
  maximum RSS 369,672/369,148 KiB.  All GCC objects are exact at `b0d3d8d3...`
  and all Clang objects at `1fe5f0e4...`.  C11 is retained, but the 1.548x
  self/GCC result leaves the parity target open and requires another lever.
- **C12-1 (PA29 CONTROL FAILURE PROPAGATION).** Investigation of the
  stable-address replay probe exposed a harness defect independent of that
  rejected optimization: PA29's final control recipe ran four property
  checkers in one shell without `set -e`, so a failure in an early checker
  could be hidden by a later successful checker.  The recipe now stops on the
  first failure, matching the already-correct oracle loop above it.  No
  student contract or fixture changes are needed for this harness-only rule.
  PA29 passes 291/291 and report-through-PA29 passes 4,251/4,251 with all four
  control invocations reported separately.
- **C12 STABLE-PARAMETER ADDRESS REPLAY RECHECK REJECTED.** The previously
  rejected P32-L115 replay class was reconstructed because its old static
  result suggested it might recover the remaining GCC-relative gap.  The
  implementation proved replay-safe identity-copy closures and storage,
  call-argument, pointer-store, and scalar-return boundaries.  Debugging the
  first self-host found two genuine proof/emission hazards: an ordinary
  allocator spill slot is not stable storage after its last modeled use, and
  replay must use the selected stable parameter home rather than switching
  opportunistically back to an incoming ABI register that parallel call setup
  may overwrite.  A temporary behavioral/relationship-level PA29 control
  exercised both constraints without matching a complete MIR program, and a
  Valgrind compile completed with zero errors after the fixes.

  The corrected same-source result still rejects the feature.  Three all-32
  self lanes were 34.45, 31.42, and 31.66 seconds; three GCC-built lanes were
  20.89, 21.20, and 20.75 seconds.  Their medians are 31.66/20.89 = **1.516x**,
  above the exit target.  A clean reverse-order identical-candidate-source
  pair was 31.89/905.01 seconds wall/aggregate CPU with the prior self compiler
  versus 31.54/903.32 with the candidate, only a 0.19% aggregate-work
  improvement.  Every candidate lane reproduced the 215-object compiler at
  SHA-256 `764d1aa1...`.

  More importantly, the current optimizer stack reverses the old density
  signal: relative to the identical candidate source compiled by the prior
  self compiler, `pp_tokenizer.o` grows 30,026 -> 30,344 text bytes,
  `Lexer::Run` grows 11,668 -> 11,715 bytes, and the complete compiler grows
  8,635,051 -> 8,665,879 text bytes.  Repeated address reconstruction is now
  destructively interacting with later layout/placement choices instead of
  removing profitable lifetimes.  The implementation, temporary README
  language, and temporary feature control are removed; rejected behavior is
  not made part of the student contract.  The independent PA29 harness fix is
  retained.  No Cachegrind, Valgrind, profiler, compiler, or build process
  remains active.
- **C13 COVERAGE AND ZERO-EXTENDING AND SELECTION.** The remaining tokenizer
  shape gap included 64-bit `and` instructions whose constant mask made every
  upper result bit provably zero.  PA29 now describes the student-visible
  target-selection relationship: an `i64` AND may use the equivalent 32-bit
  x86 operation only when a constant mask clears all upper 32 bits, while a
  mask that can preserve an upper bit must remain 64-bit.  Control
  `919-zero-extending-and-mask` validates the generated behavior and the two
  instruction-width relationships in the bounded entry function.  It does not
  match production source, helper names, a complete LowIR program, or a
  complete MIR program.  The relationship checker failed against the
  pre-change backend before the implementation was enabled.

  Native integer selection now gives that AND operation an `i32` MIR machine
  type when either resolved operand is an immediate in `[0, 0xffffffff]`.
  The ELF encoder honors the selected AND width; backend-created untyped ANDs
  retain their established 64-bit fallback.  The LowIR type and result remain
  `i64`, relying on the architectural zero extension of a 32-bit destination,
  so this adds no LowIR or MIR syntax.  PA29 passes 291/291 and PA38 passes
  45/45.  Coverage commit is `2bf9fa58`; implementation commit is `05bcee2d`.
- **C13 STATIC AND DIRECT RETENTION.** On identical current source, the final
  self-built O1 compiler falls from 8,631,371 to 8,627,271 text bytes and has
  SHA-256 `bc37312c...`.  `pp_tokenizer.o` falls 30,026 -> 29,969 text bytes;
  `Lexer::Run` falls 11,668 -> 11,627 bytes.  The immutable frozen O0 object
  selects 17 immediate 32-bit ANDs where the old object selected none; four of
  those also avoid a separately materialized wide constant.  Its total text
  falls 842,171 -> 842,140 bytes and its ELF file falls 32 bytes.

  Two clean reverse-order, identical-candidate-source direct pairs measured
  prior/candidate aggregate CPU as 904.84/898.43 and 909.76/899.44 seconds.
  Those are 0.71% and 1.13% reductions in generated work.  The corresponding
  wall pairs were 31.63/31.79 and 32.89/31.14 seconds, showing the expected
  parallel-tail variance but no repeated absolute regression.  This evidence
  is independent of movement in the host-compiler denominator.
- **C13 CORRECTED THREE-COMPILER ORACLE.** Three interleaved all-32 lanes on
  the exact current source measured self wall times of 31.67, 32.75, and
  31.36 seconds; GCC-built times of 22.16, 20.65, and 21.52 seconds; and
  Clang-built times of 21.58, 21.58, and 21.36 seconds.  Median wall times are
  therefore 31.67/21.52/21.58 seconds, giving corrected ratios of **1.472x
  self/GCC** and **1.468x self/Clang**.  The maximum ratio is again below the
  1.50x exit target.

  Median aggregate CPU is 902.71/588.95/607.17 seconds, or 1.533x GCC and
  1.487x Clang; median maximum RSS is 229,644/226,880/227,692 KiB.  The exact
  source-matched producer hashes are `bc37312c...` self, `e0e8e70f...` GCC,
  and `dd4c3b77...` Clang.  The primary metric remains the maximum same-source
  wall ratio because the 32-way build is the user-facing workload; aggregate
  CPU remains the lower-noise direct-retention guard above.
- **C13 FULLY OPTIMIZED HOST GUARD.** Fully optimized current GCC and Clang
  producers have SHA-256 `a965e677...` and `5f2e4665...`, with text sizes
  7,061,349 and 5,490,043 bytes.  Relative to exact pre-C13 producers, the
  feature adds only 324 and 164 compiler text bytes.  Five interleaved frozen
  `-O0` comparisons have pre-C13/current wall medians of 4.64/4.62 seconds
  for GCC and 4.87/4.83 seconds for Clang; aggregate CPU medians also improve.
  Thus the new target-selection check does not regress the best-case path.

  Against the immutable pre-plan O3 producers, the five-lane GCC medians are
  4.53/4.58 seconds and the three-lane Clang medians are 4.76/4.77 seconds.
  The small cumulative GCC difference is bounded to 50 ms (1.1%) and is not
  caused by C13, as the immediate-source control improves by 20 ms.  Candidate
  objects are deterministic per producer at `59c178ba...` GCC and
  `8b735b26...` Clang, each 32 bytes smaller than its established baseline.
- **C13 CUMULATIVE CHECKPOINT AND INCEPTION.** Root 32-way report-through-PA38
  passes 5,471/5,471.  The LowIR contract audit passes with 124 ledger rows
  and 99 retained rows; the PA38 file audit is zero-fatal with the established
  33 warnings; `git diff --check` is clean.  Fresh explicit-O1 inception under
  `/dev/shm/v3codex-c13-inception`, with outer, inner, and object parallelism
  all at 32, matched all 215 objects and the final compiler in
  49.70/1311.71/95.37 seconds wall/user/system.  Self and inception compilers
  are byte-identical at SHA-256 `bc37312c...` and 8,627,271 text bytes.
  Completed timing roots, frozen objects, and detached source worktrees were
  removed; no Cachegrind, Valgrind, profiler, compiler, or build process is
  left active.
- **C12/C13 FINAL CLOSURE.** The execution ledger gives every P0/P1 family a
  retained owner or a recorded policy boundary.  Both actionable production
  duplicate warnings are gone; the two remaining duplicate advisories are the
  deliberate staged macro/preprocessor wrappers and include/namespace preamble
  noise.  Final line counts are 2,994 for `pa10_syntax.cpp`, 2,993 for
  `macro_processor.cpp`, 2,982 for `lowir_native_elf.cpp`, and 2,664 for
  `cppgm++.cpp`.  No public LowIR/MIR bypass was introduced, and every added
  behavior has earliest-owned student-facing coverage.  The final cumulative
  gates, best-case host guard, 1.472x binding ratio, and 32-way inception above
  satisfy the completion criteria.  The C13 coverage, implementation, and
  ledger commits are pushed together to `origin/v3opt`, keeping the checkpoint
  at three retained commits since the preceding push.
- **C12 CLOSURE AUDIT REOPENED.** A requirement-by-requirement review did not
  accept the preceding closure assertion as evidence.  The final report-only
  six-line/100-character scan had not been recorded or classified.  A fresh
  scan reports 135 duplicate windows; together with the 31 existing division
  advisories this is 166 tight-scan warnings.  Two windows violate ownership
  already promised by this plan: compile and link modes still duplicate the
  native result-stat suffix after the shared codegen prefix, and loop-census
  construction still spells out C8's unconditional MIR control-flow set.
  Closure is reopened until both missed migrations, the complete tight-match
  classification, and the final gates are recorded.
- **C12-F1 COVERAGE AUDIT (NATIVE RESULT-STATS SUFFIX).** Compile-only object
  stats and link stats already exercise both driver records, while PA30 and
  PA31 cover their object/link behavior.  Before the ownership move, a focused
  explicit-O1 source compile froze object and executable SHA-256 values and
  the ordered diagnostic record/key schemas.  The helper may own only the
  four narrow-result counters, code-shape and edge-staging reporters, shared
  storage lifetime, and reclaim counters; planned-register prefixes, EH,
  output, serialization, link, timing, newlines, and function census stay in
  their mode owners.  This is diagnostic-preserving implementation coverage,
  so no source-specific fixture or README change is justified.
- **C12-F2 COVERAGE AUDIT (LOOP-CENSUS TERMINATOR CLASSIFICATION).** C8's
  neutral MIR helper already owns the exact JMP, indirect-JMP, RET, FRET,
  RESUME, THROW, and EXIT set used by ordinary emission and host-EH analysis.
  PA38 function/loop census controls exercise the missed loop-census caller;
  PA29 ordinary termination and PA31 host-EH termination exercise the shared
  predicate's other users.  Replacing the local copy with that existing
  predicate changes no student-visible MIR or diagnostic structure, so the
  existing structural and behavioral controls are sufficient.
- **C12-F1 (NATIVE RESULT-STATS OWNER).** Commit `9edd7df5` moved the exact
  common suffix into `lowir_native_stats_report`: the four narrow-result
  counters, code-shape and edge-staging reporters, shared-storage lifetime,
  and reclaim counters now have one source definition.  Compile and link keep
  their planned-register prefixes and their distinct EH, output, link,
  serialization, timing, newline, and function-census suffixes.  The focused
  before/after object and executable remain exact at `f479c1d3...` and
  `1510238d...`, respectively, and both ordered diagnostic key schemas are
  unchanged.  PA30 passes 100/100, PA31 passes 31/31, and the default audit is
  zero-fatal/33.
- **C12-F2 (UNCONDITIONAL CONTROL-FLOW OWNER).** Commit `a4515939` removes the
  loop census's private terminator truth table and calls C8's existing
  `mir_control_flow::ends_unconditional_control_flow` owner.  Function/loop
  census controls pass 2/2, PA29 passes 291/291, PA31 passes 31/31, and PA38
  passes 45/45.  The tight duplicate count falls by one, while GCC- and
  Clang-O3 code show that the shared predicate retains the established inline
  comparison sequence.
- **C12-F1 LAYOUT RETENTION AND REJECTED SHAPES.** The initial out-of-line
  result-stats definition was semantically cold but moved later linked code;
  six pinned Clang-O3 frozen samples measured 4.875/4.925-second
  pre/current medians, a repeated 1.03% regression.  Commit `44df85f3` keeps
  the one source definition in the stats-report owner but makes it naturally
  inline, which emits one weak executable copy near the driver callers under
  the measured compilers.  Six final pinned samples are exact at
  `8b735b26...` and improve 4.845 -> 4.830 seconds (-0.52%), with user time
  4.345 -> 4.335 seconds.  Forcing the helper inline was also tested and
  removed: it grew the self compiler by 408 text bytes and produced a 1.508x
  two-lane self/GCC ratio.  The retained natural-inline form therefore
  preserves source ownership without the late-layout or forced-duplication
  penalties.
- **C12 FINAL TIGHT-SCAN CLASSIFICATION.** The report-only six-line,
  100-character scan now contains 134 duplicate windows plus the established
  31 file-division advisories, for 165 tight warnings.  Every duplicate window
  is classified exactly once below; scan numbers refer to the final scan order
  at `44df85f3`.

  - Scanner scaffolding rather than semantic matches: `2-11`, `24`, `27-28`,
    `32`, `57-60`, `66`, `70-75`, `77-81`, `83-84`, `86`, `89-90`, `92-94`,
    `97`, `104`, `106-107`, `110`, `112-116`, `118`, `120-121`, `123-124`,
    `126`, `129`, and `134`.  These are include/namespace preambles, matching
    declarations and definitions, or repeated file-opening syntax.
  - Staged representation or lifecycle owners that must remain local: `1`,
    `13`, `15-16`, `62`, `64`, `88`, `128`, `130-131`, and `133`.  They cover
    PA10/PA11 mode shells, ABI fact/graph records, differently prefixed
    diagnostics, frontend versus LowIR string identity, distinct compact-table
    entries, PA15 versus LowIR force-inline stages, synthetic-symbol
    lifecycles, staged PA6/PA8/frontend hashes, and distinct PA8 fields/stats.
  - Optimizer-policy matches that remain pass-local: `17-20`, `38`, `40`, and
    `42-47`.  Their truth sets and rewrite policies differ; reopening them as a
    generic pass or visitor would undo the completed optimizer audit.
  - Existing shared exact core: `41`.  Preparation keeps policy-specific outer
    walks, while its exact `first`/`second`/`third`/`args` traversal already
    uses `lowir_operand_view.h`.
  - Native policy/mechanics that remain local: `21-23`, `25-26`, `29-31`, and
    `33-37`.  These are integer/XMM binding, LEA/MOV folding, atomic
    load/exchange, EH/malloc allocation, typed/named addresses, branch-local
    LEA construction, local/crossing allocation pools, span/reclaim lifetime,
    local/EH relocation identities, debug ranges, declaration/definition TLS,
    strlen/memcpy builtins, and composite/standalone condition emission.  The
    shared spelling is smaller than the distinct surrounding policy.
  - ABI, container, semantic, parser, and lowering policy that remains local:
    `12`, `14`, `39`, `48-56`, `61`, `63`, `65`, `67-69`, `76`, `82`, `85`,
    `87`, `91`, `95-96`, `98-103`, `105`, `108-109`, `111`, `117`, `119`,
    `122`, `125`, `127`, and `132`.  These cover mangle scheduling/emission,
    union wrappers, block transition/validation, macro and grammar state,
    array/conversion/entity policy, deliberately distinct compact tables,
    canonicalization and template identity, cleanup tables, conversion caches,
    construction/assignment and result/parameter ABI boundaries, aggregate
    casts and packs, evaluator maps, synthetic roles, constexpr access,
    initializer-list and RTTI sources, base-path updates, statement-expression
    kinds, attributes/defaults, generated identities, traits, array forms, and
    namespace grammar.  A common helper would be policy-parametric or merely
    instantiate the same loop once per type, not create one clearer owner.
- **C12 P0/P1 OWNER AUDIT.** Every frozen-census P0/P1 disposition was checked
  against its final definitions and callers.  Driver stats use
  `report_codegen_pipeline_stats` and `report_codegen_result_stats`; native
  final emission uses `CommonPeepholeEmitter`; semantic facts and worklists use
  `IsClassFlavor`/`IsEnumFlavor` and `QueueDeferredFunctionDefinition`;
  lifetime and call construction use `PrepareLifetimeScope`, the reverse
  cleanup helper, and `BuildBoundIntrinsicCallShell`; lowering uses
  `ResetCommonFunctionLoweringState`, `DirectCallInstruction`, and
  `IsTrivialLifecycleBinding`; LowIR uses `parse_function_header` and
  `lowir_operand_view.h`; compact cached-hash tables use
  `RebuildCachedHashSlots`.  The execution ledger records each migration's
  earliest student-facing structural or behavioral coverage.  The deliberate
  policy boundaries listed in this plan account for every non-migration; no
  P0/P1 family is unowned.
- **C12 FINAL CORRECTED PERFORMANCE.** Final source-matched producer hashes are
  `ef5c434d...` self, `0e850abb...` GCC-O1, and `b014364c...` Clang-O1; the
  self compiler has 8,626,303 text bytes.  Six interleaved self/GCC lanes have
  wall medians of 31.585 and 21.075 seconds; three Clang lanes have a
  22.14-second median.  The binding ratios are therefore **1.499x self/GCC**
  and **1.427x self/Clang**.  Median aggregate CPU is 902.505/588.375/603.25
  seconds and median maximum RSS is 229,026/228,130/226,352 KiB.  Self and GCC
  reproduce compiler `ef5c434d...` and 215-object census `ca7eac28...` in
  every lane; Clang is internally exact at compiler `482a98a4...` and census
  `e87c8d16...`.

  A final pre-audit/current self B/A/A/B guard on identical final source is
  exact at compiler `ef5c434d...` and census `19545ef3...`.  Pre/current wall
  medians are 31.785/31.95 seconds, with one pair moving each direction;
  aggregate-CPU medians are 903.21/904.99 seconds, a bounded +0.20%.  The
  cleanup therefore adds no repeated absolute-work regression.
- **C12 FINAL FULLY OPTIMIZED GUARD.** Final GCC-O3 producer `322ef6fc...`
  (7,061,845 text bytes) versus pre-audit `a965e677...` has exact frozen object
  `59c178ba...`, 4.565/4.550-second wall medians (-0.27%), and
  4.070/4.075-second user medians.  Final Clang-O3 producer `28a0e990...`
  (5,489,755 text bytes) versus `5f2e4665...` has exact frozen object
  `8b735b26...`, 4.845/4.830-second wall medians (-0.52%), and
  4.345/4.335-second user medians.  Neither best-case host path regresses.
- **C12 FINAL GATES, INCEPTION, AND CLOSURE.** At final source, root 32-way
  report-through-PA38 passes 5,471/5,471; the LowIR contract audit passes with
  124 ledger rows and 99 retained rows; the default PA38 audit is
  zero-fatal/33; and `git diff --check` is clean.  Final line counts are 2,632
  for `cppgm++.cpp`, 2,982 for `lowir_native_elf.cpp`, 2,994 for
  `pa10_syntax.cpp`, and 2,993 for `macro_processor.cpp`.

  Fresh explicit-O1 inception under
  `/dev/shm/v3codex-c12-final-inception.jdFrEx`, with outer, inner, and object
  job counts all 32, matches 215/215 objects and the final compiler.  Both
  object trees have census `f1f72cfe...`; both compilers are `ef5c434d...`
  with 8,626,303 text bytes.  The inception comparison takes
  32.84/856.88/50.87 seconds wall/user/system with 228,992 KiB maximum RSS.
  All retained code commits and this closure ledger are pushed to
  `origin/v3opt`; the reopened audit is satisfied requirement by requirement.

# Plan: Audit and Reduce Optimizer Duplication

Status: executing; D0 baseline and consolidation ledger frozen, no cleanup
implementation landed

Date: 2026-08-27

## Objective

Reduce duplicated implementation and make the LowIR and native optimizer
structure easier to reason about without changing the compiler contract,
generated behavior, pass ordering, or the retained O1 performance level.

This is primarily a maintainability program.  It may remove repeated analysis
work when that is independently proved safe, but a smaller source tree is not
valuable if it adds abstraction overhead or reopens a correctness/performance
gap.  In particular, the program must:

- give the LowIR optimizer driver meaningful file and function-size headroom;
- consolidate exact semantic duplicates in recently added optimization paths;
- name and preserve intentional differences instead of forcing them through
  one over-broad helper;
- keep every existing student-facing PA37 and PA38 optimization contract;
- keep the default PA38 file audit at zero fatal findings and add no advisory
  warnings;
- keep the maximum exact-source O1 wall ratio at the current level, with
  1.50x an absolute ceiling rather than a cleanup budget; and
- commit each accepted increment after fast verification, with cumulative
  verification and pushes at least every three accepted increments.

The expected end state is an explicit optimizer pipeline built from cohesive
pass modules and a small set of shared, typed analysis primitives.  It is not
a generic pass-manager framework.

## Governing rules

1. This plan does not authorize a public LowIR/MIR addition, removal, or
   semantic change.  A discovered semantic bug becomes a separately
   documented increment with its own README contract, property test, and
   performance decision.
2. Preserve the exact order and eligibility of retained transformations until
   a separate measured optimization proves a change.  In particular, retain
   the current late-inlining, post-prune, final O1 memory-GVN, predicate, and
   loop-store-forwarding boundaries.
3. Do not replace direct calls with `std::function`, virtual dispatch, a
   heap-allocated pass list, or an unbounded fixed point.  Shared helpers must
   remain allocation-free unless they replace an allocation or scan already
   present at the call site.
4. Do not cache value, CFG, dominance, loop, use, or provenance facts across a
   mutation unless the owning API has an explicit, tested invalidation rule.
5. Do not merge helpers merely because a short text window matches.  State the
   complete semantic truth table first, including binding, volatility, type,
   debug-location, EH, alias, and layout-lifetime distinctions.
6. Pure implementation cleanup does not need a new student-facing feature.
   Existing README descriptions and structural/behavioral properties remain
   the contract.  Never add a test that matches an entire LowIR/MIR program or
   recognizes production source text.
7. New `dev/src/*.cpp` files must be added to both applicable tool lists in
   `dev/frontend_source_sets.mk`.  Keep implementation bodies in `.cpp`
   owners; do not move substantial code into headers to evade source-file
   limits.

## Starting checkpoint

The starting tree is `beec7ac7`; the optimizer/codegen compiler is unchanged
from the measured binary with SHA-256
`8b6952075632e689e99e8a7f502e1f3c00b9cfa6d280e07e0e33c3653b69bbd4`.

| Lane | Mean wall | Mean aggregate CPU | Self ratio |
| --- | ---: | ---: | ---: |
| self | 31.895 s | 901.810 s | - |
| exact-source GCC | 21.460 s | 589.665 s | 1.486x wall / 1.529x CPU |
| exact-source Clang | 22.080 s | 608.730 s | 1.445x wall / 1.481x CPU |

The primary maximum same-source wall ratio is **1.486x**.  Complete compiler
text is 8,640,083 bytes.  Root through-PA38 passes 5,465/5,465, PA37 passes
188/188, PA38 passes 45/45, and fresh all-32 O1 inception matches every object
and the final compiler.

The default PA38 file audit passes with zero fatal findings and the established
36 advisory warnings.  It also exposes an immediate structural problem:

- `dev/src/lowir_opt.cpp` is 2,995/3,000 lines;
- `lowir_opt::optimize` occupies the full 240-line function allowance after
  token-preserving compaction; and
- another seven-line orchestration addition would fail both limits again.

Passing at the exact limit is not a stable design.  Restoring headroom is a
required outcome, not optional formatting work.

## Audit method

The initial census used three views:

1. recent retained commits and the files they expanded;
2. exact normalized source windows across optimizer modules; and
3. the existing file audit's duplicate detector at a tighter report-only
   threshold:

```sh
perl scripts/cppgm_file_audit.pl --stage pa38 --no-follow-includes \
  --duplicate-window 6 --duplicate-min-chars 100 \
  --paths dev/src/lowir_opt.cpp \
  --paths dev/src/lowir_boolean_cfg.cpp \
  --paths dev/src/lowir_loop_opt.cpp \
  --paths dev/src/lowir_memory_gvn.cpp \
  --paths dev/src/lowir_pre.cpp \
  --paths dev/src/lowir_cleanup_o1.cpp \
  --paths dev/src/lowir_scalar_rules.cpp \
  --paths dev/src/lowir_slot_forward_o1.cpp \
  --paths dev/src/lowir_small_object_promotion.cpp \
  --paths dev/src/lowir_staged_copy_forwarding.cpp \
  --paths dev/src/lowir_loop_simplify.cpp \
  --paths dev/src/lowir_inline_o1.cpp \
  --paths dev/src/lowir_native_opt.cpp \
  --paths dev/src/lowir_native_analysis.cpp \
  --paths dev/src/lowir_native_location_planning.cpp \
  --paths dev/src/lowir_native_frame_forwarding.cpp \
  --paths dev/src/lowir_native_address_folding.cpp
```

That intentionally sensitive run reports 14 duplicate windows.  Several are
only namespace/include preambles; the actionable families below also include
short semantic duplicates that a six-line exact matcher cannot find.  The
production audit remains at its current 28-logical-line threshold.  Do not
lower that global threshold merely to make the count a target.

## Initial duplication and coherence census

| Priority | Family | Current evidence | Disposition |
| --- | --- | --- | --- |
| P0 | LowIR pipeline ownership | `lowir_opt.cpp` and `optimize` have five and zero lines of audit headroom.  The driver directly repeats analysis construction and cleanup sequencing around many passes. | Split orchestration into a cohesive pipeline owner while leaving leaf transformations in responsibility-named modules. |
| P0 | Post-transform cleanup recipes | `lowir_opt.cpp` has 19 simplifier calls, 29 DCE calls, 13 CFG-cleanup calls, and seven local `FunctionAnalysis` constructions.  Several transforms repeat simplify/DCE/CFG sequences with subtly different invalidation. | Name only the exact recurring recipes and carry explicit effect/invalidation flags.  Do not replace the visible pass order with a data-driven loop. |
| P1 | LowIR storage identity | Equivalent temp/slot/global-plus-binding predicates occur in `lowir_boolean_cfg.cpp`, `lowir_loop_opt.cpp`, and `lowir_scalar_rules.cpp`.  The newest P31/P32 predicate and loop paths are two of the copies. | Introduce one narrowly named storage-location identity predicate and migrate one caller family at a time. |
| P1 | Full LowIR operand identity | `lowir_cleanup_o1.cpp` hashes/compares the full literal and binding state, while the exported `lowir_scalar_rules.cpp::same_operand` intentionally compares a smaller value subset. | First publish separate names and a truth table.  Share code only for the proven common core; never silently widen or narrow either caller. |
| P1 | Operand traversal | At least 25 optimizer sites rebuild arrays for `first`, `second`, `third`, then separately walk `args`; the wider search has 141 operand/argument traversal references. | Provide small const/mutable traversal primitives or views with no allocation and defined phi label/value handling.  Keep specialized operand roles explicit. |
| P1 | Hash mixing | The same `0x9e3779b9` combine body is independently owned by `lowir_cleanup_o1.cpp`, `lowir_expression_key.cpp`, and `lowir_memory_gvn.cpp`. | Consolidate the exact primitive in a small optimizer utility; keep each key's field selection local. |
| P1 | Definition/use indexing | Boolean CFG, PRE, loop optimization, memory GVN, scalar rules, slot forwarding, and promotion passes repeatedly scan all blocks into value-sized definition/use arrays.  `FunctionAnalysis` currently owns only CFG/dominator/loop facts. | Add a reusable immutable value index or scanner.  Reuse across passes only when mutation/invalidation is explicit and measured. |
| P1 | Pointer/offset provenance | `lowir_small_object_promotion.cpp` contains two memoized definition/fact/state walkers; `lowir_staged_copy_forwarding.cpp` contains a third. | Share traversal/cycle machinery behind policy-specific fact evaluation.  Preserve byte-versus-element scaling, negative-offset, zero-offset, and poison rules as separate policies. |
| P2 | CFG terminal and cold propagation | Branch/switch-to-jump rebuilding is duplicated inside `lowir_boolean_cfg.cpp`; two cold-mask routines in `lowir_cleanup_o1.cpp` duplicate block-ID indexing and reverse fixed-point propagation while intentionally differing on EH and switch edges. | Extract exact mechanics, pass an explicit successor policy, and retain separate named semantic entry points. |
| P2 | Inliner success accounting | Leaf-batch and general inline paths duplicate budget consumption aftermath, body discard, rewrite accounting, and late/post-prune statistics in `lowir_inline_o1.cpp`. | Centralize only post-success accounting.  Keep call replacement and EH/block mutation paths distinct. |
| P2 | Recent readonly analysis | The retained readonly string/global work made `lowir_scalar_rules.cpp` 1,035 lines and introduced additional value-definition, address, use, and table scans. | Reuse the P1 value/operand primitives, then split readonly-table analysis from unrelated scalar rewrites if ownership becomes clearer without adding scans. |
| P2 | MIR identity and register effects | `lowir_native_opt.cpp::same_operand` and phi lowering's `same_location` overlap but are not equivalent.  Register use/definition switches duplicate the full-width i128 opcode group. | Name full operand versus storage-location identity separately; share exact opcode group/effect facts without merging use and definition semantics. |
| P2 | Native address setup/store emission | `lowir_native_address_folding.cpp` repeats setup/store shape checks and frame/global/deref normalization in adjacent reducers. | Extract address normalization and final emission only after the existing PA29 structural/behavioral controls prove every accepted and rejected shape. |
| P3 | Native placement ordering | `lowir_native_location_planning.cpp` repeats use-count/definition ordering and register-claim loops for local phi, cyclic-region, invariant, crossing, and ordinary candidates. | Share comparators and claim mechanics only where tie-breaking and pool direction are identical.  Allocation priority is output behavior and must not drift. |
| P3 | Small syntactic matches | namespace preambles, common `using` declarations, and ordinary nested block loops appear in the tight duplicate report. | Ignore unless consolidation follows a real ownership boundary.  Do not create a utility solely to reduce the raw detector count. |

## Frozen consolidation ledger

This table is the D0 decision boundary.  Line numbers name the frozen
`74d1538e` tree; later ledger entries record their movement.  The coverage
column names student-facing properties, not exact whole-program output.

| Family | Frozen definitions and callers | Boundary and owner | Coverage | Disposition |
| --- | --- | --- | --- | --- |
| LowIR pipeline and cleanup recipes | `lowir_opt.cpp:2754-2994` owns `optimize`; its local pass calls include 19 simplify, 29 DCE, 13 CFG, and seven `FunctionAnalysis` constructions. | PA37 owns the visible O1-O3 pass order.  Recipes may share only identical transform-followup order and must expose CFG/value invalidation. | PA37 O1/O2/O3 optimizer and driver suites, especially late/post-prune controls 475, 476, 490, 509, 510, and 520-524. | **split/share** in D3; keep differing recipes local. |
| Exact LowIR storage identity | `lowir_boolean_cfg.cpp:87` called at 191; `lowir_loop_opt.cpp:234` called at 379; `lowir_scalar_rules.cpp:954` called at 1007. | All three mean exactly temp value, slot id, or global symbol plus address binding; no literals, volatility, or type comparison.  PA37 owns the optimizer helper. | PA37 387, 506, 526, 528, 529, and 540 plus survivor-property scripts. | **share first** in D1. |
| Full/scalar LowIR operand identity | Internal `lowir_cleanup_o1.cpp:112` is called by instruction identity at 216-228; exported `lowir_scalar_rules.cpp:881` is called by `lowir_opt.cpp:434,978,1492` and staged-copy lines 143, 166, 593-595. | Cleanup identity includes full literal/presentation/binding state.  Scalar value identity intentionally has a narrower truth table. | PA37 cleanup, phi, forwarding, and staged-copy controls 385-389, 507, 508, 511, 524, and 525. | **rename/split** in D1; share only a proven common core. |
| Operand traversal | At least 25 fixed `first`/`second`/`third` arrays plus `args` loops and 141 wider operand references across LowIR optimizer modules. | Allocation-free fixed-plus-variable traversal is shareable only when operand roles do not matter.  Phi odd `args` entries remain predecessor labels. | PA37 phi, CFG, scalar, memory, loop, and inlining suites. | **share** in D1, with specialized phi traversal local. |
| Hash combine | `lowir_cleanup_o1.cpp:91`, `lowir_expression_key.cpp:75`, and `lowir_memory_gvn.cpp:280`; cleanup also inlines the same expression into debug hashing at 44-48. | Only the exact `seed ^= value + 0x9e3779b9 + (seed<<6) + (seed>>2)` primitive is common; key field selection remains local. | PA37 tail merge, expression GVN/PRE, memory GVN, readonly, and scaled-index controls 385, 506, 511, 525, and 527. | **share** in D1. |
| Definition/use indexing | Repeated value-sized scans in Boolean CFG, PRE, loop optimization, memory GVN, scalar rules, slot forwarding, promotion, and `lowir_opt.cpp`; `FunctionAnalysis` owns CFG/dominator/loop facts only. | Immutable definitions/uses may be built without CFG.  No fact survives mutation without explicit invalidation; parameters and missing definitions remain distinct. | PA37 CFG, phi, PRE/GVN, slot, loop, readonly, and staged-copy controls. | **share one pass at a time** in D2; ablate reuse if work/bytes rise. |
| Pointer/offset provenance | `lowir_small_object_promotion.cpp:74` `AddressProvenance`, line 249 `OffsetProvenance`, and `lowir_staged_copy_forwarding.cpp:44` `OffsetProvenance`; callers at 467/988 and 301. | Recursion, cycle states, and memoization are common mechanics.  Address facts, typed aggregate offsets, and staged-copy byte offsets retain separate policies, including negative/poison rules. | PA37 388, 389, 507, 508, 511, 524, and 525. | **share mechanics/split policy** in D2; reject if templates grow text/work. |
| CFG terminal rebuilding | Branch/switch constant folds at `lowir_boolean_cfg.cpp:709-749` and equal-target repair at 819-831 rebuild a debug-preserving jump. | Selected target and debug location are inputs; reachability, phi repair, and bypass logic are not part of the helper. | PA37 385, 386, 390, 392, 395, 511, 523, and 528. | **share** in D4. |
| Cold propagation | `lowir_cleanup_o1.cpp:724` `cold_block_mask` and line 814 `raising_path_mask` duplicate id indexing and reverse fixed-point propagation. | Layout coldness excludes blocks with ordinary/EH successors and ignores switch cases; raising-path coldness includes switch labels and treats EH markers separately. | PA37 cold-path/inlining controls 390-392, 509-511, and 520-524. | **share mechanics/split successor policy** in D4. |
| Inliner post-success accounting | Leaf-batch success at `lowir_inline_o1.cpp:1516-1541` and general success at 1613-1640; shared budget/body helpers already at 866 and 922. | Budget consumption, body discard, rewrites, and stats are common after success.  Leaf rebuilding versus block/EH mutation remains separate. | PA37 ordinary, hinted, late, and post-prune controls 391, 392, 475, 476, 490, 509, 510, and 520-524. | **share post-success only** in D4. |
| MIR identity and register effects | Full identity at `lowir_native_opt.cpp:441`, physical location at `lowir_native_phi_lowering.cpp:11`; duplicated i128 use/def opcode groups at native-opt lines 190-197 and 258-265. | Full MIR operand state differs from assignable physical location.  Opcode membership may be common, but use and definition masks remain separate. | PA38 442-448, O1 406 and 425-428, plus native behavior fixtures. | **rename/split identity; share exact opcode group** in D5. |
| Native address folding | Adjacent dead setup/store reducers at `lowir_native_address_folding.cpp:202` and 253 repeat sequence validation and frame/global/deref normalization. | Normalize only after each reducer preserves its current sequence, scratch-liveness, index, sign-crossing, and R11 exclusions. | PA29 native contract and PA38 native survivor properties. | **share exact normalization** in D5; keep eligibility local. |
| Native placement | Candidate sorts and claims at `lowir_native_location_planning.cpp:322-448`, with further ordinary sorting at 869. | Use-count sorting, pool direction, span/clobber checks, i128 RBX exclusion, phi priority, and tie breakers are output behavior. | PA38 442-448 and placement/movement census properties. | **share only proven comparator/claim variants** in D5; otherwise keep local. |
| Tight-report syntax | Include/namespace preambles in loop, GVN, PRE, slot, and small-object modules. | No semantic ownership exists merely because normalized syntax matches. | Default file audit. | **keep local/non-actionable**. |

## Semantic distinctions that must survive

The audit already found several lookalike helpers that must not be collapsed
without policy parameters or separate names:

- full operand equality versus storage-location equality;
- LowIR value equality versus presentation spelling equality;
- slot byte offsets versus typed `index` element scaling;
- an unknown offset versus a known slot with a poisoned/nonconstant offset;
- negative byte offsets versus the aggregate-promotion nonnegative range;
- memory stability for a predicate path versus memory transparency for local
  duplicate-load elimination;
- MIR full operand identity versus equality of assignable physical locations;
- register uses versus definitions for atomics, calls, EH, and i128 helpers;
- cold-layout reachability versus raising-path execution frequency; and
- candidate sorting versus register-pool order and tie-breaking.

Every shared helper introduced by this plan must document which side of these
boundaries it implements.  If a helper needs several booleans to recover the
old meanings, prefer separate typed policies or keep the implementations
local.

## Execution program

Each numbered increment is independently reviewable, tested, measured when
required, committed, and either retained or reverted before the next one.

### D0. Freeze the duplication ledger and baseline

1. Record every actionable family above with its current definitions, callers,
   semantic differences, owning assignment, property tests, source lines, and
   disposition (`share`, `rename`, `split`, `keep-local`, or `ablate`).
2. Save both default-audit and tight optimizer-only audit output.  Classify
   preambles and common loop syntax as non-actionable rather than deleting
   useful structure.
3. Reconfirm the 5,465-test through gate, 36-warning/zero-fatal audit,
   all-32 inception hash, compiler text, and exact-source performance baseline
   before the first code change if the starting commit has moved.
4. Capture optimizer statistics for the all-source O1 workload, especially
   instruction visits, CFG/dominator/loop builds and reuse, simplifier/DCE/CFG
   runs, and elapsed time.  Later consolidation may not hide added work behind
   a smaller source count.

Exit when every proposed merge has a stated semantic boundary and baseline.

### D1. Consolidate exact LowIR primitives

1. Introduce a narrowly owned optimizer support module for:
   - exact storage-location identity;
   - the exact hash-combine primitive; and
   - allocation-free const/mutable operand traversal where operand roles do
     not matter.
2. Migrate one primitive and one caller family per commit.  Start with the
   three storage-location predicates because their truth tables are currently
   identical and they directly cover the latest predicate/loop work.
3. Rename remaining broad `same_operand` functions to state whether they mean
   full presentation identity, scalar value identity, or MIR identity.  A
   rename may precede consolidation and is preferable to a false abstraction.
4. Preserve specialized phi traversal, where even `args` are values and odd
   `args` are predecessor labels; do not feed labels to value-use accounting.

Exit when exact primitives have one owner, differently scoped predicates have
distinct names, optimized LowIR properties are unchanged, and the tight audit
loses the corresponding actionable findings without new production warnings.

### D2. Share value indexing and provenance mechanics

1. Add an immutable value-definition/use index with explicit parameter and
   missing-definition representation.  It must be constructible without CFG
   analysis and must expose its scan cost in existing optimizer statistics.
2. Convert one pass at a time.  Compare instruction visits, allocations, and
   output before allowing cross-pass reuse.
3. If reuse is profitable, put it behind an analysis session with explicit
   `invalidate_values` and `invalidate_cfg` operations.  Mutating passes must
   invalidate before a later query; appending a fresh value must not index past
   stale arrays.
4. Factor the three provenance walkers into shared recursion/cycle/memoization
   mechanics with policy-specific facts.  Keep staged-copy byte offsets and
   small-object typed offsets as different policies.
5. Reject a generic graph abstraction if templates materially grow compiler
   text or if runtime indirection costs more than the eliminated scans.

Exit when the shared API reduces source duplication and does not increase
all-source optimizer work, peak analysis bytes, or same-source time.

### D3. Restore coherent LowIR pipeline ownership

1. Make `lowir_opt.cpp` the public optimizer coordinator rather than the owner
   of every cleanup implementation and scheduling detail.
2. Introduce a pipeline/session owner for boundaries, statistics, reusable
   simplifier/DCE/CFG scratch, and the current per-function analysis.  Keep
   pass order in ordinary C++ control flow.
3. Name only demonstrated cleanup recipes, for example value rewrite followed
   by simplify+DCE, or CFG rewrite followed by simplify+DCE+CFG repair.  Each
   recipe returns/accepts explicit effects so invalidation is visible.
4. Preserve separate recipes where current order differs.  In particular, do
   not normalize away extra CFG cleanup after slot removal, final predicate
   folding, late load reuse, or post-inline promotion.
5. Split source ownership by responsibility and update both `cppgm++` and
   `lowiropt` source sets for every new `.cpp`.
6. Targets after the split:
   - `lowir_opt.cpp` at or below 2,600 lines;
   - `optimize` below 180 lines without dense-line compaction;
   - every new source below 2,500 lines and every new header comfortably below
     the file audit's substantial-body warning; and
   - no new file-audit warning.

Exit when the pipeline is readable in execution order, analysis invalidation
is explicit, and all output/performance gates pass.

### D4. Remove duplicated CFG and inliner mechanics

1. Use one debug-preserving branch/switch-to-jump builder in Boolean CFG
   cleanup.
2. Share cold-successor fixed-point propagation while keeping explicit layout
   and raising-path successor policies.
3. Share inliner post-success budget/body/stat accounting between leaf-batch
   and general paths.  Do not combine their EH, block mutation, or replacement
   mechanics.
4. Retest every late/post-prune path after each change; these passes have
   previously interfered with load reuse and predicate cleanup.

Exit when exact duplicate blocks are gone, deliberate policies remain named,
and inline census/output hashes stay stable unless a separately approved
semantic fix is being measured.

### D5. Clean native optimizer duplication conservatively

1. Define separate MIR full-operand and physical-location predicates and
   migrate only identical callers.
2. Give register-use/definition opcode families one declarative owner where
   their membership is identical, while keeping use and definition masks
   separate.
3. Extract shared address normalization from adjacent dead-setup/store
   reducers without changing their accepted sequences or scratch-liveness
   guards.
4. Share placement comparators and register-claim mechanics only after proving
   identical pool order, reverse/forward iteration, clobber tests, span tests,
   and tie breakers.
5. Treat any MIR text, frame size, saved-register set, or object-byte change as
   a behavior change requiring a focused explanation; source cleanup alone is
   not sufficient justification.

Exit when PA38 structural and behavioral checks retain every placement guard,
native code remains byte-identical where promised, and performance stays at
the checkpoint.

### D6. Re-audit and decide whether tooling should change

1. Rerun the tight optimizer-only duplicate report and update the ledger with
   remaining deliberate copies.
2. Keep the production audit threshold unchanged unless repeated execution
   shows a lower scoped threshold has low noise and catches a semantic family
   worth gating.
3. If a scoped check is added, make it classify normalized duplicate regions;
   do not hardcode forbidden function names or exact source snippets.
4. Remove no test or README description merely because its implementation was
   consolidated.

Exit when the remaining duplicates are either intentional and documented or
queued as bounded follow-ups.

### D7. Final cumulative checkpoint

Run the complete correctness, audit, performance, and inception protocols.
Record final file/function sizes, duplicate families, compiler text/hash,
optimizer counters, self/GCC/Clang timing lanes, ratios, commits, and push.

## Coverage matrix

Before changing a family, verify the named property still reaches every caller
being consolidated.  Backfill a structural or behavioral reducer only when a
semantic distinction lacks coverage.

| Cleanup family | Existing owning coverage |
| --- | --- |
| storage identity, volatility, calls, stores, and lifetime | PA37 controls 387, 506, 526, 528, 529, and 540; `check_lowir_survivor_properties.pl` and the focused late-load script |
| slot/address provenance and staged copies | PA37 controls 388, 389, 507, 508, 511, 524, and 525 |
| CFG folds, cold paths, and unreachable behavior | PA37 controls 385, 386, 390, 392, 395, 511, 523, and 528 |
| ordinary, hinted, late, and post-prune inlining | PA37 controls 391, 392, 475, 476, 490, 509, 510, and 520-524 |
| readonly globals/string tables and scaled indices | PA37 controls 525 and 527 plus PA16 readonly string storage properties |
| MIR identity, frame forwarding, placement, and recoloring | PA38 controls 442-448; O1 controls 406 and 425-428; O2/O3 and behavior fixtures |
| native address/bulk-memory lowering | PA29 native contract properties plus PA38 native survivor properties |

Tests validate documented properties, not complete program text.  An exact
compiler/object hash is appropriate as a reproducibility/performance datum,
but it is not a substitute for student-implementable structural or behavioral
coverage.

## Verification cadence

### Fast gate for every increment

Run the focused property script for the touched family, then:

```sh
make test-pa37
make test-pa38
perl scripts/cppgm_file_audit.pl --stage pa38 --paths dev
git diff --check
```

The default file audit must pass after every accepted code increment, not only
at final cleanup.  The warning count may fall; it may not rise above 36.

For a source-only move whose token stream and build topology are genuinely
unchanged, prove the generated compiler hash.  Moving tokens between files,
changing includes, or changing function boundaries is not automatically
performance-neutral because it changes the all-source workload and parallel
build tail.

### Cumulative gate

After at most three accepted increments, and before every push batch, run:

```sh
make test-report-through-pa38
perl scripts/cppgm_file_audit.pl --stage pa38 --paths dev
```

Then run fresh isolated PA39 inception with outer, inner, and object
parallelism all at 32 and explicit O1 self-hosting:

```sh
RUN_ROOT=$(mktemp -d /dev/shm/v3codex-optdup-inception.XXXXXX)

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

Use a fresh validated root and confirm no stale build, profiler, Valgrind, or
Cachegrind process before timing or inception.

## Performance retention protocol

Any token, file-boundary, include, function-boundary, data-structure, or call
change in production optimizer code requires a performance decision.

1. Build the candidate compiler with all three job levels at 32.
2. On the identical candidate source tree, run the pre-change and candidate
   self compilers in reverse/interleaved order for at least two clean lanes.
3. Prepare exact-candidate-source GCC and Clang compilers and time at least two
   clean all-source lanes for each.  Timed lanes use `CPPGM_HOST_CXX=g++`.
4. Record wall, user, system, aggregate CPU, final compiler hash/text, and
   relevant optimizer counters.  Do not use a mixed-revision denominator.
5. The hard gate is a maximum self/GCC and self/Clang wall ratio at or below
   1.50x.  Cleanup should preserve the current level: the provisional
   non-regression budget is 0.5% relative to 1.486x, or approximately 1.493x.
   A result between 1.493x and 1.50x requires a three-lane repeat and a concrete
   explanation; reject it if the regression persists.
6. A candidate-versus-pre-change identical-source wall or aggregate-CPU
   regression above 0.5% also requires a repeat.  Reject a confirmed
   regression unless the maximum primary ratio independently improves and the
   trade is recorded.
7. Cachegrind or software profiles are attribution tools only.  The all-32
   same-source wall ratio remains the acceptance oracle.

This protocol prevents a refactor from appearing beneficial merely because it
made the shared source easier for GCC/Clang to compile, and it also recognizes
legitimate denominator movement under the corrected same-source metric.

## Commit, push, and ledger discipline

- One semantic helper family or ownership move per commit.
- Run the fast gate before every commit.
- Record rejected abstractions as well as retained ones, especially when an
  abstraction reduces source duplication but adds compiler work or code size.
- Run the cumulative gate and push after no more than three retained commits.
- Never leave a retained performance result only in `/dev/shm`; record the
  source commit, compiler paths/hashes, timing lanes, ratios, tests, audit,
  inception, and process cleanup here.
- Keep the worktree clean and synchronized after each push checkpoint.

## Completion criteria

This plan is complete only when:

1. every actionable duplication family has a retained consolidation or a
   documented reason to remain local;
2. `lowir_opt.cpp` and `optimize` have the D3 structural headroom without dense
   formatting;
3. no new audit warning exists and the default PA38 file audit passes;
4. PA37, PA38, and root through-PA38 are clean;
5. fresh all-32 O1 inception matches every object and final compiler;
6. the final maximum exact-source wall ratio is no more than 1.50x and remains
   at the 1.486x checkpoint level within the protocol above; and
7. all retained commits and the final evidence are pushed.

## Audit ledger

- **D-A0 (INITIAL CENSUS).** At `beec7ac7`, the default PA38 audit passes with
  zero fatal findings and 36 established warnings.  `lowir_opt.cpp` is
  2,995/3,000 lines and `optimize` fills the 240-line function limit.  A
  six-line/100-character optimizer-only report finds 14 exact duplicate
  windows, including actionable storage-identity, CFG, inliner, native
  address, and placement families plus non-actionable preambles.  Independent
  searches find three optimizer hash mixers, three memoized pointer-provenance
  walkers, at least 25 fixed-plus-variable operand traversal loops, and
  repeated definition/use scans across the LowIR pass modules.  The retained
  performance baseline is 31.895 seconds self, 21.460 GCC, and 22.080 Clang,
  or 1.486x/1.445x wall, with compiler hash `8b695207...`.  This entry changes
  documentation only; no production code or test is changed.
- **D0 (BASELINE FROZEN).** The documentation-only planning head is
  `74d1538e`; its compiler is byte-identical to `beec7ac7`.  A fresh 32-way
  O1 all-source build with observational `--stats` compiled 215 translation
  units in 19.38 seconds wall (473.48 user, 45.24 system), reproduced compiler
  SHA-256 `8b6952075632e689e99e8a7f502e1f3c00b9cfa6d280e07e0e33c3653b69bbd4`,
  and reproduced 8,640,083 text bytes.  Because parallel stream insertion
  interleaved the initial records and three command lines, the authoritative
  capture enumerates `INCEPTION_SOURCES_cppgm++` directly and gives all 215
  compiles a private stderr file.  Aggregate O1 facts are: 124,273 functions,
  2,801,277 input and 1,519,629 output instructions, 46,956,639 instruction
  visits, 2,930,421 block visits, 9,487,042 CFG-edge visits, 93,746 CFG builds
  / 163,693 reuses / 4,676 invalidations, 79,126 dominator builds / 143,813
  reuses, 68,353 loop builds / 3,039 reuses, 332,009 simplify runs, 423,468
  DCE runs, 456,866 CFG runs, 700,431 slot runs, 77,208 each forward/local
  slot runs, 163,862 remove-slot runs, 133,607 promotion runs, and 124,273
  each small-object/dead-store runs.  Maximum per-TU promotion transient
  storage is 10,229,916 bytes.  Wall and internal nanosecond totals from a
  stats-enabled parallel run are observational only because stderr contention
  changes their scheduling.  Exact default and tight audit output is saved in
  `PLAN-OPTIMIZER-DUPLICATION-AUDIT-BASELINE.txt` (SHA-256
  `e0336e1cdc5fe5e58d199b32ab57803932db4506f96dbdbfef258d650cb88508`).
  The detailed matrix above freezes every merge boundary, assignment owner,
  property coverage, and disposition.  D0 is complete; no production code,
  test, or generated fixture changed.
- **D1-S (EXACT STORAGE IDENTITY, RETAINED).** On the tree based on
  `d6f27e46`, new `lowir_optimizer_support.h` gives the three identical PA37
  storage predicates one inline owner named `same_storage_location`.  Its
  documented truth table accepts only temp/value, slot/id, and
  global/symbol-plus-address-binding identity; type, volatility, literals,
  and general operand equality stay with their existing callers.  Boolean
  predicate reuse, loop-carried exact-store forwarding, and duplicate-block
  load elimination now call it; 26 duplicated implementation lines are gone.
  No fixture or README changed because the existing PA37 structural
  properties cover the unchanged contract.  Focused survivor properties pass
  2/2; `make test-pa37` passes 21/21, `make test-pa38` passes 161/161,
  `git diff --check` is clean, and the default audit remains zero-fatal/36
  warnings.  The tight report falls from 15 to 14 warnings by removing the
  loop/Boolean storage copy; its remaining scalar/Boolean match is the
  intentionally separate memory-transparency switch.  Candidate self output
  is byte-identical to the frozen compiler (SHA-256 `8b695207...`, text
  8,640,083), as is every timed lane's rebuilt compiler.  Reverse/interleaved
  same-source all-32 O1 lanes are old 32.41/31.33 seconds wall and candidate
  31.22/31.23; aggregate CPU means are 903.96/903.40 seconds.  Because the
  binaries are identical, the wall difference is scheduling noise.  Exact
  candidate-source GCC lanes are 20.98/21.20 seconds wall and 588.31/590.66
  CPU; Clang lanes are 25.57/22.55 wall and 619.75/613.34 CPU.  Candidate
  means are self 31.225/903.40, GCC 21.090/589.485, and Clang
  24.060/616.545 seconds wall/CPU: 1.481x self/GCC and 1.298x self/Clang wall,
  with 1.481x the primary maximum.  GCC benchmark compiler SHA/text is
  `7bca2106...`/5,787,308; Clang is `7fcbb888...`/5,026,953.  Direct 215-TU
  private-log captures on the baseline and candidate have every deterministic
  optimizer counter identical, including all D0 counts and the peak above.
  Retain.
- **D1-H (EXACT HASH COMBINE, RETAINED).** On the tree based on `3f7745df`,
  `lowir_optimizer_support.h::combine_hash` now owns only the exact LowIR key
  mixer.  Cleanup/tail identity, expression GVN/PRE keys, and memory-GVN keys
  retain their own field selection and ordering; the three direct debug-key
  expressions use the same primitive.  The sole remaining optimizer
  `0x9e3779b9` expression is the shared body (native host-EH has a separate
  one-shot two-field key and remains outside this LowIR family).  Historical
  LowIR properties, PA37, PA38, `git diff --check`, and the default
  zero-fatal/36-warning audit pass with no fixture movement; the tight report
  remains at 14 warnings because its six-line matcher never reported the
  short mixer bodies.  Candidate compiler SHA-256 is `992804fc...`; text is
  unchanged at 8,640,083 bytes.  Reverse/interleaved same-source all-32 O1
  lanes are candidate 31.54/32.00 seconds wall and old 31.93/32.23;
  aggregate CPU means are 909.28/908.76 seconds (+0.06%).  Exact candidate
  GCC lanes are 21.44/21.30 wall and 589.29/599.55 CPU; Clang lanes are
  21.64/21.60 wall and 609.73/609.27 CPU.  Means are self 31.770/909.28, GCC
  21.370/594.42, and Clang 21.620/609.50 seconds wall/CPU, or 1.487x and
  1.469x wall; the primary maximum remains within the 1.493x normal budget.
  GCC benchmark compiler SHA/text is `842b15a9...`/5,787,680; Clang is
  `7fcbb888...`/5,026,953.  Direct private-log captures cover the same 215
  TUs.  The candidate has 33 fewer input and three fewer output instructions,
  155 fewer optimizer instruction visits, 104 fewer block visits, two fewer
  CFG builds, four fewer dominator and loop builds, five fewer simplify runs,
  two fewer DCE and slot runs, and one fewer remove/promote run; CFG,
  forward/local-slot, small-object, and dead-store run counts are unchanged,
  as is the 10,229,916-byte peak.  Retain.

Append one entry for every retained consolidation, rejected abstraction,
re-baseline, cumulative gate, and push checkpoint.

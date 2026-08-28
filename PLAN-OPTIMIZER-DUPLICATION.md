# Plan: Audit and Reduce Optimizer Duplication

Status: complete; D0-D7 verified and pushed

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
4. Preserve specialized phi traversal, where even `args` are predecessor
   labels and odd `args` are values; do not feed labels to value-use
   accounting.

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
- **D1-N1 (LOWIR OPERAND-IDENTITY RENAME, REJECTED).** A naming-only trial
  changed cleanup's full identity to `same_presentation_operand` and scalar
  rules' narrower predicate to `same_scalar_value_operand`, including all
  callers.  Focused properties, PA37, PA38, and the zero-fatal/36-warning
  audit passed; compiler text stayed 8,640,083 bytes.  The executable hash
  changed to `c2afe336...`, and stripped old/new executables were not equal,
  so timing was required.  The first two lanes exceeded the hard ratio and a
  three-lane repeat confirmed it.  Candidate versus pre-change self means on
  the identical renamed source were 32.007 versus 31.967 seconds wall and
  904.76 versus 905.40 aggregate CPU, so self execution did not regress.
  Exact-source GCC, however, improved to 21.007 seconds wall, making the
  candidate/GCC ratio 1.524x; Clang's lanes were 21.54/21.71/25.26 seconds
  and do not set the maximum.  The disproportionate GCC code-layout gain
  makes this a relative regression under the corrected metric.  The rename
  is reverted rather than using the unchanged self time to cover the hard
  failure.  The semantic distinction remains frozen in the D0 ledger; a
  shorter or structurally neutral naming form may be reconsidered only after
  separate measurement restores the ratio.
- **D1-T1 (ROLE-NEUTRAL OPERAND VIEW, RETAINED).** On the tree based on
  `499eaa3d`, `lowir_optimizer_support.h` adds allocation-free const/mutable
  `all_operand_count`/`all_operand_at` access in serialized
  first/second/third/args order.  Its contract explicitly excludes operand
  role interpretation; phi value/label pairs and slot-position-sensitive
  escape logic remain specialized.  The first migration covers only
  slot-forwarding's complete temp-use census and its two complete alias
  rewrite loops.  Focused survivor properties, PA37, PA38, `git diff --check`,
  and the default zero-fatal/36-warning audit pass; the tight report remains
  14 warnings.  Candidate SHA-256/text is `be44056a...`/8,638,931, a 1,152-byte
  text reduction.  Reverse/interleaved same-source all-32 O1 lanes are
  candidate 31.40/31.86 seconds wall and old 31.66/31.68; aggregate CPU means
  are 903.78/906.18 seconds (-0.26%).  Exact candidate GCC lanes are
  21.55/20.94 wall and 593.04/588.19 CPU; Clang lanes are 21.37/21.48 wall and
  606.30/607.14 CPU.  Means are self 31.630/903.78, GCC 21.245/590.615, and
  Clang 21.425/606.72 seconds wall/CPU, or 1.489x and 1.476x wall; the primary
  maximum stays within the 1.493x normal budget.  GCC benchmark compiler
  SHA/text is `ad7a823f...`/5,787,104; Clang is
  `27db9476...`/5,026,473.  Direct 215-TU counters show 115 fewer input, 161
  fewer output, 7,238 fewer visited instructions, 49 fewer block visits, and
  240 fewer CFG-edge visits; peak analysis storage is unchanged.  Parsing the
  three inline accessors adds three functions and correspondingly tiny pass
  run deltas (at most 18 slot runs and 13 CFG runs), but total optimizer work
  falls rather than hiding added work behind the source reduction.  Retain.
- **D-C1 (FIRST CUMULATIVE CHECKPOINT).** The retained D1 storage, hash, and
  traversal increments pass root `make test-report-through-pa38` at
  5,465/5,465 and the default audit at zero fatal findings/36 established
  warnings.  A fresh isolated explicit-O1 inception used outer, inner, and
  object parallelism all at 32 under
  `/dev/shm/v3codex-optdup-d1-cumulative-inception.8INvjG`; every object and
  final `cppgm++` match.  No stale compiler, profiler, Valgrind, or Cachegrind
  process preceded the run.
- **D2-V1 (IMMUTABLE VALUE INDEX, FIRST CONSUMER RETAINED).** On the tree based
  on `15770b5e`, `lowir_function_analysis` now owns a CFG-independent immutable
  definition/use index.  Its public result distinguishes missing, parameter,
  and instruction definitions; its compact private definition location keeps
  the old one-word-per-value representation.  Use accounting visits the
  fixed operands and non-phi args, but only the value half of serialized phi
  label/value pairs.  The first and only consumer in this increment is the
  scaled-index multiplier factorization in `lowir_scalar_rules`; it retains
  the index only while instruction/value/use topology is unchanged.  PA37 now
  documents observational index-cost fields, and the generic stats property
  requires their presence and a positive workload without requiring exact
  values or implementation layout.  The existing historical-contract control
  continues to check the positive factorization plus multi-use and
  unfactorable rejection shapes structurally and behaviorally.  Both focused
  properties pass; `make test-pa37` passes 188/188, `make test-pa38` passes
  45/45, `git diff --check` is clean, and the default audit remains zero-fatal
  with 36 established warnings.  A private-log exact-source comparison covers
  all 215 translation units: old and candidate emit byte-identical objects and
  every pre-existing deterministic optimizer counter is identical.  The new
  fields report 124,242 builds, 1,441,222 scanned instructions, 4,620,666
  scanned value-operand positions, and 247,816 allocations.  The 72,160-byte
  maximum is exactly the two value-sized vectors already allocated by the
  replaced local scan, so allocation count and peak scratch do not rise; the
  existing 10,229,916-byte promotion peak is also unchanged.  Candidate
  SHA-256/text is `634887b3...`/8,640,175, a 1,244-byte text increase over
  `be44056a...`/8,638,931.  Reverse/interleaved all-32 same-source lanes are
  candidate 32.03/32.05 seconds wall and old 31.78/32.13; aggregate CPU means
  are 906.10/904.89 seconds (+0.13%), and wall means differ by +0.27%, both
  within the 0.5% repeat threshold.  Exact candidate-source GCC lanes are
  29.70/29.59 seconds wall and 572.86/569.88 CPU; Clang lanes are 30.42/36.00
  wall and 646.14/660.56 CPU.  Means are self 32.040/906.10, GCC
  29.645/571.37, and Clang 33.210/653.35 seconds wall/CPU, or 1.081x and
  0.965x wall.  Host wall utilization was lower and more variable than the D1
  checkpoint, but the paired self execution and aggregate CPU comparison are
  stable and non-regressing.  Self, GCC, and Clang benchmark compiler hashes
  are `634887b3...`, `21674ae0...`, and `d36eac81...`; text is 8,640,175,
  5,788,400, and 5,027,765 bytes respectively.  Retain this first consumer;
  do not permit cross-pass reuse until a later mutation-bounded measurement.
- **D2-V2 (COUNTED-LOOP VALUE INDEX, RETAINED).** On the tree based on
  `9b318336`, counted-loop simplification is the second independent value-index
  consumer.  It replaces a local definition-block vector, definition-index
  vector, use-count vector, complete instruction scan, and two lookup helpers
  with the same explicit missing/parameter/instruction result used by scaled
  index factorization.  It still returns before building any value facts for a
  loop-free function, and it does not reuse facts across a mutation boundary.
  This removes 28 net production lines, one allocation, and one machine word
  per value whenever the O2+ counted-loop pass has a loop to examine.  The
  PA37 historical control still checks O2 induction strength reduction and
  behavior; the focused control, PA37 188/188, PA38 45/45, `git diff --check`,
  and the zero-fatal/36-warning default audit pass.  The tight report remains
  at 14 warnings.  Exact O1 private-log builds cover 215 current translation
  units: old and candidate objects are byte-identical, and every optimizer
  counter, including the value-index fields and 10,229,916-byte promotion
  peak, is identical because the migrated pass begins at O2.  On the PA37 O2
  historical fixture, old/candidate value-index builds are 11/12, scanned
  instructions 65/76, operand positions 202/237, and reported index
  allocations 22/24, with the same 368-byte peak.  The added build's two
  allocations replace the old pass's three same-length arrays, and the native
  objects are identical.  Candidate SHA-256/text is `cb5a4452...`/8,637,627,
  2,548 text bytes below D2-V1 and 1,304 below the D1 checkpoint.
  Reverse/interleaved all-32 same-source lanes are candidate 32.37/31.44 and
  old 31.63/31.89 seconds wall; means are 31.905/31.760 (+0.46%).  Aggregate
  CPU means are 907.245/906.465 seconds (+0.09%).  A precautionary third
  candidate lane was clean at 31.96 wall/900.79 CPU, but its following old
  lane was excluded after thermal/load contamination raised aggregate CPU to
  1,090.30 seconds despite the same 215 successful objects.  Exact-source GCC
  lanes are 29.82/29.60 wall and 575.23/567.93 CPU; Clang lanes are
  30.54/30.36 wall and 648.54/649.86 CPU.  Accepted means are self
  31.905/907.245, GCC 29.710/571.58, and Clang 30.450/649.20 seconds wall/CPU,
  or 1.074x and 1.048x wall.  Self, GCC, and Clang hashes/text are
  `cb5a4452...`/8,637,627, `51b3518f...`/5,787,332, and
  `763b24f4...`/5,026,821.  Retain; cross-pass reuse remains a separate next
  experiment.
- **D2-VR (MUTATION-BOUNDED VALUE-INDEX REUSE, RETAINED).** On the tree based
  on `69ed83e9`, `FunctionAnalysis` now owns an empty-by-default value index
  with explicit lazy query and invalidation operations.  It stores the index
  directly, so no analysis-object heap allocation is added.  Scaled-index
  factorization builds the facts; O2+ counted-loop simplification reuses them
  immediately because both passes preserve value definitions and temporary-use
  topology; the coordinator then explicitly invalidates and releases the
  arrays before LICM, GVN, or any later mutation.  The stats contract reports
  reuse and invalidation, the general stats control checks their structural
  presence, and the PA37 O2 historical control requires positive reuse and
  invalidation without fixing exact counts.  Focused properties, PA37
  188/188, PA38 45/45, `git diff --check`, and the default zero-fatal/36-warning
  audit pass.  Exact O1 private logs cover all 215 current translation units:
  old and candidate objects are byte-identical and every pre-existing counter
  is identical, including 124,209 builds, 1,440,820 scanned instructions,
  4,619,399 operand positions, 247,750 allocations, the 72,608-byte index
  peak, and the 10,229,916-byte promotion peak.  The candidate additionally
  records 124,209 explicit end-of-window invalidations.  On the O2 historical
  fixture, reuse reduces builds 12 to 11, scanned instructions 76 to 65,
  operand positions 237 to 202, and allocations 24 to 22; it records one
  reuse and 11 invalidations, preserves the 368-byte peak, and emits the same
  native object.  Candidate SHA-256/text is
  `a460771a...`/8,637,371, 256 text bytes below D2-V2.  The first two
  reverse/interleaved self lanes triggered the repeat rule at candidate
  31.69/32.64 versus old 32.36/31.52 seconds wall despite lower candidate CPU.
  A clean third pair at 31.40/31.82 resolves the three-lane means to
  31.910/31.900 seconds wall (+0.03%) and 907.00/912.84 aggregate CPU (-0.64%).
  Exact-source GCC lanes are 30.22/29.66 wall and 580.65/576.50 CPU; Clang
  lanes are 30.91/30.54 wall and 654.38/651.60 CPU.  Accepted means are self
  31.910/907.00, GCC 29.940/578.575, and Clang 30.725/652.99 seconds wall/CPU,
  or 1.066x and 1.039x wall.  Self, GCC, and Clang hashes/text are
  `a460771a...`/8,637,371, `f1ce5efe...`/5,787,940, and
  `cba80c5e...`/5,027,345.  Retain this narrowly scoped reuse boundary; other
  passes still require their own invalidation proof.
- **D-C2 (VALUE-INDEX CUMULATIVE CHECKPOINT).** The retained D2 immutable
  index, counted-loop migration, and bounded reuse increments pass root
  `make test-report-through-pa38` at 5,465/5,465 and the default audit at zero
  fatal findings/36 established warnings; the tight optimizer report remains
  14 warnings.  A fresh isolated explicit-O1 inception used outer, inner, and
  object parallelism all at 32 under
  `/dev/shm/v3codex-optdup-d2-cumulative-inception.ZzvTLM`; every object and
  final `cppgm++` match.  No stale compiler, profiler, Valgrind, or Cachegrind
  process preceded the checkpoint.
- **D2-P1 (GENERIC PROVENANCE MEMO, REJECTED).** On the tree based on
  `02f97a00`, a non-template, non-virtual `ValueProvenanceMemo` moved the
  immutable definition scan and unvisited/active/resolved state machine out of
  both small-object walkers and the staged-copy walker.  Address facts, typed
  aggregate offsets, raw byte offsets, negative-offset handling, and poison
  rules remained in their three local policies.  Focused PA37 controls 388,
  389, 524, and 525 pass; PA37 is 188/188, PA38 is 45/45,
  `git diff --check` is clean, and the default/tight audits remain at zero
  fatal findings with 36/14 warnings.  Exact candidate-source private logs
  cover all 216 translation units: every non-timing optimizer counter matches,
  all 216 objects match, and old/candidate self compilers link the same
  `42df6d89...` output.  The candidate benchmark compiler is
  `42df6d89...`/8,637,267 text bytes versus pre-change
  `a460771a...`/8,637,371, a 104-byte text reduction with 464 additional data
  bytes.  Three reverse/interleaved self lanes are candidate
  33.16/31.63/32.18 seconds wall and 918.58/912.72/913.44 aggregate CPU,
  versus old 31.89/32.01/32.36 wall and 915.85/917.37/913.95 CPU.  Means are
  32.323/914.913 versus 32.087/915.723: wall regresses 0.738% while CPU
  improves 0.088%.  Exact-source GCC lanes are 29.78/29.89 wall and
  580.26/581.70 CPU; Clang lanes are 31.13/31.62 wall and 658.33/665.92 CPU.
  Candidate ratios are 1.083x GCC and 1.030x Clang, but the maximum ratio is
  worse than the old compiler's 1.076x on the same source.  GCC/Clang output
  hashes/text are `a9f30dd1...`/5,787,376 and
  `b339335b...`/5,027,065.  Reject under the confirmed greater-than-0.5% wall
  rule and restore the three local walkers; sharing definition storage through
  a future already-built index remains a distinct experiment only if it
  eliminates work rather than adding a call boundary.
- **D3-S1 (CROSS-MODULE SLOT PROMOTION SPLIT, REJECTED).** On the tree based
  on `4479ea9a`, the dead-slot-store and scalar-slot-promotion implementation
  moved from `lowir_opt.cpp` into the existing `lowir_slot_promotion.cpp`.
  This reduced `lowir_opt.cpp` from 2,997 to 1,923 lines and left the slot
  module at 1,212 lines.  A narrow internal interface kept constant folding,
  storage-temporary census, and the structured-EH predicate single-owned, but
  those helpers then crossed the translation-unit boundary.  Focused PA37
  controls 389, 508, 524, and 525 pass; PA37 is 188/188, PA38 is 45/45,
  `git diff --check` is clean, and the default audit remains zero-fatal with
  36 warnings.  Exact candidate-source private logs cover all 215 translation
  units: every non-timing optimizer counter and all 215 objects match, and
  old/candidate self compilers link the same `4f64e08e...` output.  The
  candidate benchmark compiler grows from `a460771a...`/8,637,371 text bytes
  to `4f64e08e...`/8,641,187, a 3,816-byte increase.  Three clean paired self
  lanes are candidate 32.37/31.45/31.24 seconds wall and
  904.33/898.50/899.38 aggregate CPU, versus old 31.65/31.30/31.27 wall and
  904.08/900.86/897.25 CPU.  Means are 31.687/900.737 versus
  31.407/900.730: wall regresses 0.892% while CPU is flat.  Exact-source GCC
  lanes are 29.61/29.48 wall and 571.28/572.51 CPU; Clang lanes are
  30.39/30.78 wall and 646.82/651.34 CPU.  Candidate ratios are 1.072x GCC and
  1.036x Clang, both worse than the old compiler's 1.063x/1.027x on the same
  source.  GCC/Clang hashes/text are `b9d32f8e...`/5,788,620 and
  `1d0cf297...`/5,027,845.  Two extra balanced pairs were excluded after old
  lanes rose to 33.13/35.36 wall with aggregate CPU 915.87/909.99, materially
  above the clean window; they do not overturn the exact clean-CPU result.
  Reject and restore the original ownership.  A future D3 boundary must move
  a cohesive helper unit rather than adding cross-module leaf calls merely to
  satisfy a line-count target.
- **D3-S2 (COHESIVE LOCAL-CLEANUP UNIT, REJECTED).** On the tree based on
  `9d5e27aa`, scalar simplification/folding, DCE, CFG cleanup, scalar slot
  promotion, and their reusable scratch moved together into a new
  `lowir_local_cleanup.cpp` behind a function-level `LocalCleanupSession`.
  The coordinator retained pass order and whole-pipeline boundaries; the new
  source was in both tool source sets.  This reduced `lowir_opt.cpp` from
  2,997 to 558 lines while keeping the new implementation at 2,499 lines and
  its header at 63.  A first private run exposed that the late-inline callback
  historically accounts nested slot transforms as inline work rather than
  standalone slot runs.  Three explicit untracked session entries restored
  that contract before measurement.  Focused PA37 controls 389, 508, 524,
  and 525 pass; PA37 is 188/188, PA38 is 45/45, `git diff --check` is clean,
  and the default audit remains zero-fatal with 36 warnings.  Fresh
  exact-candidate-source private logs cover all 216 translation units: every
  non-timing optimizer counter and all 216 objects match, and old/candidate
  self compilers link the same `c91c2910...` output.  The candidate benchmark
  compiler grows from `a460771a...`/8,637,371 text and 334,744 data bytes to
  `c91c2910...`/8,638,719 text and 335,336 data bytes.  Three
  reverse/interleaved self lanes are candidate 31.73/32.19/31.52 seconds wall
  and 905.46/907.30/905.28 aggregate CPU, versus old 31.44/31.47/31.50 wall
  and 905.44/902.45/907.81 CPU.  Means are 31.813/906.013 versus
  31.470/905.233: wall regresses 1.091% while CPU regresses only 0.086%.
  Candidate mean peak RSS is 229,455 KiB versus 228,540 KiB.  Exact-source
  GCC lanes are 30.26/29.50 wall and 576.59/571.40 CPU; Clang lanes are
  31.10/30.79 wall and 648.90/648.31 CPU.  Candidate ratios are 1.065x GCC
  and 1.028x Clang, both worse than the old compiler's 1.053x/1.017x on the
  same source.  GCC/Clang hashes and text are `30a4e3e9...`/5,787,720 and
  `45f011d2...`/5,023,761.  The exact counters and nearly flat CPU offer no
  pass-work attribution; the remaining suspects are the added session
  allocation, compiler code layout, and parallel tail latency.  Reject under
  the confirmed greater-than-0.5% wall rule.  A lower-overhead D3 boundary
  must keep scratch ownership local without adding a per-translation-unit
  heap allocation or a broad out-of-line call facade.
- **D3-P1 (STACK-OWNED PIPELINE SESSION, RETAINED).** On the tree based on
  `0f4a8227`, `OptimizerSession` groups boundary facts and reusable
  simplify/DCE/CFG scratch on the existing stack, while one coarse final
  pipeline phase leaves the public `optimize` entry at 87 lines.  Final
  retained-body census and output-stat assignment moved to the existing
  `lowir_driver_stats_report` owner, a once-per-translation-unit call.  No
  optimizer transform or hot local-pass boundary crosses translation units.
  `lowir_opt.cpp` is 2,999 lines, so this increment meets the `optimize`
  target but deliberately leaves the final 2,600-line ownership split for a
  separately measured step.  PA37 is 188/188, PA38 is 45/45,
  `git diff --check` is clean, and the default audit remains zero-fatal with
  36 established warnings.  Fresh exact-candidate-source private logs cover
  all 215 translation units: every non-timing optimizer counter and all 215
  objects match, and old/candidate self compilers link the same
  `6c48f22e...` output.  The benchmark compiler grows from
  `a460771a...`/8,637,371 to `6c48f22e...`/8,639,699 text bytes with unchanged
  334,744-byte data.  Reverse/interleaved self lanes are candidate
  31.50/31.31 seconds wall and 899.81/898.15 aggregate CPU, versus old
  31.11/31.83 wall and 896.51/901.00 CPU.  Means are 31.405/898.980 versus
  31.470/898.755: wall improves 0.207% and CPU regresses only 0.025%; mean
  peak RSS falls from 230,870 to 229,936 KiB.  The two clean exact-source GCC
  lanes are 29.64/29.44 wall and 570.32/567.78 CPU; Clang lanes are
  30.45/30.37 wall and 646.86/644.75 CPU.  Candidate ratios improve to
  1.063x GCC and 1.033x Clang from old 1.065x/1.035x.  GCC/Clang hashes and
  text are `9a47847c...`/5,787,888 and `567b7ce3...`/5,027,457.  A GCC lane
  at 37.50 wall/598.83 CPU and a mildly elevated 30.87/578.47 lane were
  excluded and replaced by the two clean lanes rather than blended into the
  denominator.  Retain: this establishes explicit lifetime ownership and
  coarse phase structure without the allocation and hot facade rejected by
  D3-S2.
- **D3-P2 (COARSE CLEANUP OWNERSHIP, RETAINED).** On the tree based on
  `1447ff9d`, the whole DCE implementation and reusable scratch moved into
  the existing `lowir_cleanup_o1` owner, while the whole dead-slot-store
  dataflow moved into the existing `lowir_slot_promotion` owner.  The shared
  cleanup header now owns the boundary/scratch declarations and one inline
  pure-value-definition predicate; both prior local EH predicates use
  `lowir_eh_context::is_eh_instruction`.  The split crosses one coarse call
  per pass, adds no heap allocation, callback facade, new `.cpp`, or source-set
  entry, and leaves the coupled simplifier/scalar-promotion mechanics local.
  `lowir_opt.cpp` is 2,593 lines, public `optimize` is 87, cleanup is 1,219,
  slot promotion is 319, and the declaration header is 73 lines, meeting all
  D3 structural targets without an audit warning.  PA37 is 188/188, PA38 is
  45/45, `git diff --check` is clean, and the default audit remains
  zero-fatal/36-warning.  Fresh exact-candidate-source private logs cover all
  215 translation units: every non-timing optimizer counter and all 215
  objects match, and old/candidate self compilers link the same
  `29460ef3...` output.  The benchmark compiler grows from
  `6c48f22e...`/8,639,699 text and 334,744 data bytes to
  `29460ef3...`/8,647,027 text and 334,888 data bytes.  Three clean paired
  self lanes are candidate 31.25/31.88/31.25 seconds wall and
  901.21/910.54/900.27 aggregate CPU, versus old 32.39/31.43/31.77 wall and
  904.31/900.93/899.02 CPU.  Means are 31.460/904.007 versus
  31.863/901.420: wall improves 1.266%, CPU regresses only 0.287%, and mean
  peak RSS is 229,880 versus 229,180 KiB.  Clean exact-source GCC lanes are
  29.49/30.01 wall and 572.24/576.51 CPU; Clang lanes are 30.43/30.02 wall
  and 645.83/647.52 CPU.  Candidate ratios improve to 1.058x GCC and 1.041x
  Clang from old 1.071x/1.054x.  GCC/Clang hashes and text are
  `9504e171...`/5,790,004 and `b18c82d4...`/5,027,601.  Candidate lanes at
  35.26/934.71 and 68.98/1,905.93 wall/CPU, GCC at 32.17/630.63, and Clang at
  37.82/673.03 were excluded during documented host memory pressure and
  replaced after available memory recovered.  Retain: this is the
  responsibility-aligned, low-overhead boundary that the D3-S1/S2 evidence
  called for.
- **D-C3 (PIPELINE-OWNERSHIP CUMULATIVE CHECKPOINT).** The retained D3 stack
  session and coarse cleanup-ownership increments pass root
  `make -j32 test-report-through-pa38` at 5,465/5,465, `git diff --check`,
  and the default audit at zero fatal findings/36 established warnings.  A
  fresh isolated explicit-O1 inception used outer, inner, and object
  parallelism all at 32 under
  `/dev/shm/v3codex-optdup-d3-cumulative-inception.56FuP4`; every object and
  the final compiler match.  Self and inception binaries both hash to
  `29460ef3...` with 8,647,027 text and 334,888 data bytes.  No stale
  compiler, profiler, Valgrind, or Cachegrind process preceded the checkpoint.
- **D4-P1 (DEBUG-PRESERVING CFG JUMP REBUILD, RETAINED).** On the tree based
  on `de7deaf7`, one allocation-free `rewrite_as_jump` helper now owns the
  complete reset, selected target, and debug-location preservation used by
  constant branch/switch folding and equal-target branch repair.  Target
  selection, reachability, phi repair, and bypass policy remain at their call
  sites.  PA37 is 188/188, PA38 is 45/45, `git diff --check` is clean, and the
  default audit remains zero-fatal with 36 established warnings.  Fresh
  exact-candidate-source private logs cover all 215 translation units: every
  non-timing optimizer counter and all 215 objects match, and old/candidate
  self compilers link the same `e43baa4d...` output.  The benchmark compiler
  shrinks from `29460ef3...`/8,647,027 to `e43baa4d...`/8,646,687 text bytes,
  with data unchanged at 334,888 bytes.  Two clean reverse/interleaved self
  lanes are candidate 31.33/31.32 seconds wall and 901.55/901.53 aggregate
  CPU, versus old 31.42/31.40 wall and 902.74/901.95 CPU.  Means are
  31.325/901.540 versus 31.410/902.345: wall improves 0.271%, aggregate CPU
  improves 0.089%, and mean peak RSS is 229,320 versus 229,382 KiB.  Clean
  exact-source GCC lanes are 29.36/29.91 wall and 571.64/572.16 CPU; Clang
  lanes are 30.57/30.98 wall and 646.65/647.73 CPU.  Candidate ratios are
  1.057x GCC and 1.018x Clang.  GCC/Clang hashes and text are
  `bd84fcb3...`/5,788,644 and `bc873fac...`/5,026,385.  Candidate 34.66/923.32
  and old 39.03/922.22 wall/CPU lanes were excluded after independent host
  contention raised both wall and aggregate CPU, then replaced by clean
  samples.  Retain: the helper removes four exact terminal-rebuild bodies,
  preserves the whole instruction-reset contract, and is performance-neutral
  within the retention protocol.
- **D4-S1 (RUNTIME COLD-SUCCESSOR POLICY, REJECTED).** A first implementation
  moved the shared block-ID index and bounded reverse fixed point behind a
  runtime `ColdSuccessorPolicy`.  It preserved the exact layout versus
  raising-path successor vocabularies, passed PA37/PA38 and the default audit,
  matched all 215 non-timing optimizer-stat records and objects, and shrank
  compiler text from 8,646,687 to 8,646,067 bytes.  Nevertheless, two clean
  paired candidate lanes were 32.51/32.11 seconds wall and 904.83/907.27
  aggregate CPU, versus old 31.11/31.46 wall and 899.77/903.42 CPU.  Mean wall
  regressed 3.276%.  Reject: branching on policy inside the instruction and
  operand walk is measurable hot-loop indirection; a smaller compiler is not
  sufficient justification.
- **D4-P2 (TYPED COLD-SUCCESSOR FIXED POINT, RETAINED).** On the tree based on
  `920827cc`, one block-ID index builder and one compile-time-policy fixed
  point now own cold-successor propagation.  `CSP_LAYOUT` admits jump, branch,
  and EH fixed labels while deliberately ignoring switch cases;
  `CSP_RAISING_PATH` admits jump, branch, and switch labels including switch
  arguments while deliberately excluding EH markers.  Seed classification
  and raising-path landing-pad exclusion remain separate.  The template is a
  bounded two-policy specialization, not a generic graph facade, and removes
  the runtime dispatch rejected by D4-S1.  `lowir_cleanup_o1.cpp` falls from
  1,219 to 1,211 lines.  PA37 is 188/188, PA38 is 45/45, `git diff --check` is
  clean, the default audit remains zero-fatal/36-warning, and the tight audit
  remains at its 14-warning checkpoint while no longer reporting this fixed
  point as the actionable duplicate.  Fresh exact-candidate-source private
  logs cover all 215 translation units: every non-timing optimizer counter
  and all 215 objects match, and old/candidate compilers link the same
  `2e084ad5...` output.  The benchmark compiler grows from
  `e43baa4d...`/8,646,687 to `2e084ad5...`/8,647,335 text bytes, with data
  unchanged at 334,888 bytes.  Three clean paired self lanes are candidate
  32.10/32.25/31.48 seconds wall and 904.77/900.48/902.04 aggregate CPU,
  versus old 32.02/31.57/32.11 wall and 901.37/900.44/903.71 CPU.  Means are
  31.943/902.430 versus 31.900/901.840: wall regresses only 0.136%, aggregate
  CPU regresses 0.065%, and mean peak RSS is 229,639 versus 229,223 KiB.  A
  32.27/929.94 candidate lane was excluded for objectively elevated aggregate
  CPU and replaced.  Exact-source GCC lanes are 29.91/29.22 wall and
  570.14/567.97 CPU; Clang lanes are 30.19/30.44 wall and 642.90/645.06 CPU.
  Candidate ratios are 1.080x GCC and 1.054x Clang.  GCC/Clang hashes and text
  are `229e05d7...`/5,788,736 and `ea5e215c...`/5,026,165.  Retain: this shares
  the mechanics and makes the semantic policies explicit without runtime
  indirection or a confirmed performance regression.
- **D4-P3 (INLINER POST-SUCCESS ACCOUNTING, RETAINED).** On the tree based on
  `fdf1576a`, `finish_successful_inline` now captures cloned size and hint
  state before optional single-call body discard, then owns rewrite and
  ordinary/hinted/single-call/post-prune/late statistic accounting.  Budget
  eligibility and consumption remain in the existing shared
  `consume_inline_budget`; leaf rebuilding and general block/EH/call mutation
  remain distinct.  `lowir_inline_o1.cpp` falls by 23 lines to 1,693.  PA37 is
  188/188, PA38 is 45/45, `git diff --check` is clean, the default audit
  improves from 36 to 35 warnings with zero fatal findings, and the tight
  optimizer audit improves from 14 to 13 warnings.  Fresh
  exact-candidate-source private logs cover all 215 translation units: every
  non-timing optimizer counter and all 215 objects match, and old/candidate
  compilers link the same `1f704cc9...` output.  The benchmark compiler shrinks
  from `2e084ad5...`/8,647,335 to `1f704cc9...`/8,646,551 text bytes, with data
  unchanged at 334,888 bytes.  Two clean paired self lanes are candidate
  31.63/31.19 seconds wall and 900.19/896.40 aggregate CPU, versus old
  31.56/31.60 wall and 899.96/897.83 CPU.  Means are 31.410/898.295 versus
  31.580/898.895: wall improves 0.538%, aggregate CPU improves 0.067%, and
  mean peak RSS is 230,520 versus 229,146 KiB.  Exact-source GCC lanes are
  29.46/29.37 wall and 569.02/567.99 CPU; Clang lanes are 30.22/30.06 wall and
  642.17/643.86 CPU.  Candidate ratios are 1.068x GCC and 1.042x Clang.
  GCC/Clang hashes and text are `643ffb31...`/5,788,048 and
  `4bf349c4...`/5,025,861.  Retain: the audit-visible exact duplicate is gone,
  output is exact, and performance improves slightly without merging the
  inliners' deliberate mutation mechanics.
- **D-C4 (CFG/INLINER CUMULATIVE CHECKPOINT).** The three retained D4
  increments pass root `make -j32 test-report-through-pa38` at 5,465/5,465,
  `git diff --check`, and the default audit at zero fatal findings with the
  warning count improved from 36 to 35.  A fresh isolated explicit-O1
  inception used outer, inner, and object parallelism all at 32 under
  `/dev/shm/v3codex-optdup-d4-cumulative-inception.reuymX`; every object and
  the final compiler match.  Self and inception binaries both hash to
  `1f704cc9...` with 8,646,551 text and 334,888 data bytes.  No stale compiler,
  profiler, Valgrind, Cachegrind, or perf process preceded the checkpoint.
- **D5-P1 (NAMED MIR IDENTITY AND SHARED ALL-GPR EFFECT, RETAINED).** On the
  tree based on `dd947151`, local predicates are now explicitly named
  `same_complete_operand` and `same_physical_location`; they remain separate
  because complete MIR identity includes immediates, symbols, labels,
  floating literals, and full dereference shape while assignable location
  identity accepts only frame/GPR/XMM homes.  One
  `has_all_gpr_use_and_definition` predicate owns the exact CMPXCHG16B/i128
  opcode membership shared by the otherwise separate use and definition
  masks.  PA37 is 188/188, PA38 is 45/45, `git diff --check` is clean, and the
  default audit remains zero-fatal/35-warning.  The tight audit remains at 13
  warnings, but no longer reports the i128 use/definition group; its native-opt
  warning is now a separate liveness-loop family.  Fresh exact-candidate-source
  private logs cover all 215 translation units: every non-timing optimizer
  counter and all 215 objects match, and old/candidate compilers link the same
  `436ea1d1...` output.  Compiler text changes only from 8,646,551 to 8,646,575
  bytes, with data unchanged at 334,888 bytes.  Two clean paired self lanes
  are candidate 31.11/31.91 seconds wall and 900.58/900.51 aggregate CPU,
  versus old 31.93/31.23 wall and 902.64/899.55 CPU.  Means are
  31.510/900.545 versus 31.580/901.095: wall improves 0.222%, aggregate CPU
  improves 0.061%, and mean peak RSS is 230,926 versus 229,452 KiB.
  Exact-source GCC lanes are 30.44/29.30 wall and 574.30/569.05 CPU; Clang
  lanes are 30.67/30.43 wall and 645.64/645.73 CPU.  Candidate ratios are
  1.055x GCC and 1.031x Clang.  GCC/Clang hashes and text are
  `372bc85a...`/5,788,048 and `5719374b...`/5,025,909.  Retain: semantic names
  prevent false equality reuse, the only exact opcode fact has one owner, and
  output/performance remain stable.
- **D5-P2 (EXACT FOLDED-STORE NORMALIZATION, RETAINED).** On the tree based on
  `35c36923`, `emit_folded_store` now owns the final frame adjustment,
  dereference base/offset, existing always-materialized global/R11 rule, width,
  and scalar store emission shared by adjacent dead-copy/address reducers.
  The generic address emitter was deliberately not reused because it chooses
  RIP-relative encoding for local globals.  Sequence shape, scratch liveness,
  indexed-address rejection, offset-overflow/sign-crossing checks, copied-value
  selection, and every accepted instruction count remain local and unchanged.
  Five focused PA29 address-folding behavior reducers pass under their owning
  `mir_behavior_t` oracle; PA37 is 188/188, PA38 is 45/45,
  `git diff --check` is clean, and the default audit remains
  zero-fatal/35-warning.  The tight audit remains at 13 because its normalized
  matcher still reports the deliberately separate reducer eligibility
  preambles; the duplicated final emission is gone.  Fresh
  exact-candidate-source private logs cover all 215 translation units: every
  non-timing optimizer counter and all 215 objects match, and old/candidate
  compilers link the same `9f5a9cb5...` output.  Compiler text changes from
  8,646,575 to 8,646,735 bytes and data from 334,888 to 334,936 bytes.  Three
  clean paired self lanes are candidate 31.22/32.26/31.21 seconds wall and
  899.14/901.67/899.53 aggregate CPU, versus old 31.35/31.66/32.28 wall and
  899.71/903.52/901.51 CPU.  Means are 31.563/900.113 versus
  31.763/901.580: wall improves 0.630%, aggregate CPU improves 0.163%, and
  mean peak RSS is 229,096 versus 228,969 KiB.  An old 36.00/914.13 wall/CPU
  lane was excluded for elevated aggregate CPU and replaced.  Exact-source
  GCC lanes are 29.42/30.15 wall and 569.67/568.99 CPU; Clang lanes are
  30.17/30.22 wall and 640.47/642.55 CPU.  Candidate ratios are 1.060x GCC and
  1.045x Clang.  GCC/Clang hashes and text are `143f1efb...`/5,788,384 and
  `df1e0fda...`/5,026,201.  Retain: exact encoding policy now has one owner,
  while superficially similar but semantically different eligibility stays
  explicit and measured performance is preserved.
- **D5-S1 (GENERIC FAR-END PLACEMENT CLAIM, REJECTED).** A proposed helper
  combined the cyclic-region and invariant loops that claim registers from
  the far end of their respective pools.  The first local-lambda form passed
  the host PA37/PA38 and audit gate but exposed an unsupported self-host
  frontend case (`array member requires a braced initializer`) when the lambda
  captured a local register array.  Replacing the lambda with a typed ordinary
  helper restored exact self-host output: all 215 non-timing counter logs and
  objects matched, and the generated compiler was `49a37085...` with
  8,645,687 text and 334,936 data bytes.  That smaller binary nevertheless
  developed a repeatable all-32 wall tail.  Two clean candidate lanes were
  31.84/32.26 seconds wall and 899.40/900.84 aggregate CPU, while three clean
  pre-change lanes were 31.94/31.79/31.05 wall and 901.58/899.64/898.40 CPU:
  the wall means regress 1.446% even though aggregate CPU is effectively flat.
  Removing only the claim helper and retaining the comparator consolidation
  below removed that tail.  Reject: the two callers keep explicit pool type,
  reverse iteration, busy/span checks, assignment, and statistics; their
  superficial mechanics are not worth a self-host representability hazard or
  a measured parallel-build regression.
- **D5-P3 (WEIGHTED PLACEMENT PRIORITY, RETAINED).** On the tree based on
  `35ca7ed7`, `compare_weighted_candidate_priority` now owns the exact shared
  use-count and definition-order prefix used by cyclic-region and crossing
  candidate sorts.  Region candidates deliberately leave equal priorities
  equivalent, while crossing candidates visibly retain their value-id final
  tie break.  Pool choice, claim direction, clobber/span exclusions, and claim
  loops remain local.  PA37 is 188/188, PA38 is 45/45, `git diff --check` is
  clean, and the default audit remains zero-fatal/35-warning.  The tight audit
  remains at 13 warnings because it now reports the intentionally separate
  far-end claim loops instead of the removed comparator prefix.  Fresh
  exact-candidate-source private logs cover all 215 translation units: every
  non-timing optimizer counter and object matches, and old/candidate compilers
  link the same `613e97a9...` output.  Compiler text changes from 8,646,735 to
  8,647,175 bytes, with data unchanged at 334,936 bytes.  Two reverse-order
  paired self lanes are candidate 31.38/31.40 seconds wall and
  897.64/898.39 aggregate CPU, versus old 31.92/31.07 wall and
  898.41/897.43 CPU.  Means are 31.390/898.015 versus 31.495/897.920: wall
  improves 0.333%, aggregate CPU regresses only 0.011%, and mean peak RSS is
  230,290 versus 229,864 KiB.  Exact-source GCC lanes are 29.37/29.54 wall and
  568.96/568.87 CPU; Clang lanes are 30.61/30.23 wall and 644.19/642.39 CPU.
  Candidate ratios are 1.066x GCC and 1.032x Clang.  GCC/Clang hashes and text
  are `97923fc6...`/5,788,440 and `455c4d42...`/5,026,233.  Retain: the exact
  shared ordering fact has one owner, caller-specific tie semantics remain
  explicit, output is exact, and measured performance is preserved.
- **D-C5 (NATIVE CONSOLIDATION CUMULATIVE CHECKPOINT).** The three retained D5
  increments pass root `make -j32 test-report-through-pa38` at 5,465/5,465,
  `git diff --check`, the 124-row LowIR contract audit, and the default file
  audit at zero fatal findings with 35 established warnings.  A fresh isolated
  explicit-O1 inception used outer, inner, and object parallelism all at 32
  under `/dev/shm/v3codex-optdup-d5-cumulative-inception.WvjWvV`; all 215
  objects and the final compiler match.  Self and inception binaries both hash
  to `613e97a9...` with 8,647,175 text and 334,936 data bytes.  No stale
  compiler, profiler, Valgrind, Cachegrind, or perf-recording process preceded
  the checkpoint.
- **D6-P1 (COLD-SINK ROLE-NEUTRAL OPERAND VIEW, RETAINED).** The D6 re-audit
  found that both complete operand scans in `sink_cold_only_definitions` still
  open-coded first/second/third/args traversal despite the allocation-free,
  serialized-order operand view established in D1.  Both scans inspect only
  temporary identity, so they now use `all_operand_count`/`all_operand_at`.
  The staged-copy scan remains explicit because its store/plumbing decisions
  interpret the operand index as a role.  PA37 is 188/188, including the
  focused cold-only definition reducer, PA38 is 45/45, `git diff --check` is
  clean, and the default audit remains zero-fatal/35-warning.  The tight audit
  falls from 13 to 11 warnings, removing both the internal cleanup match and
  the misleading cleanup/staged-copy syntax match.  Fresh
  exact-candidate-source private logs cover all 215 translation units: every
  non-timing optimizer counter and object matches, and old/candidate compilers
  link the same `beef91a2...` output.  Compiler text changes from 8,647,175 to
  8,647,263 bytes, with data unchanged at 334,936 bytes.  Two reverse-order
  paired self lanes are candidate 31.35/31.25 seconds wall and
  897.18/896.15 aggregate CPU, versus old 31.09/32.12 wall and
  896.65/897.55 CPU.  Means are 31.300/896.665 versus 31.605/897.100: wall
  improves 0.965%, aggregate CPU improves 0.048%, and mean peak RSS is 229,694
  versus 230,282 KiB.  Clean exact-source GCC lanes are 29.22/29.45 wall and
  567.68/569.24 CPU; a 30.35/573.97 lane with elevated aggregate CPU was
  replaced.  Clang lanes are 30.80/30.00 wall and 644.37/644.68 CPU.
  Candidate ratios are 1.067x GCC and 1.030x Clang.  GCC/Clang hashes and text
  are `8864af1c...`/5,788,632 and `122b0dc0...`/5,026,313.  Retain: an existing
  exact contract now owns the role-neutral walks, role-sensitive traversal is
  still local, output is exact, and measured performance improves.
- **D6-A1 (FINAL TIGHT-WARNING CLASSIFICATION; TOOLING UNCHANGED).** The
  six-line/100-character optimizer-only report now has 11 warnings.  Six are
  include/namespace/`using` preambles: loop-simplify versus loop-opt,
  memory-GVN versus loop-opt, the pipeline versus cleanup, PRE versus
  memory-GVN, slot-forwarding versus loop-simplify, and small-object promotion
  versus loop-opt.  They express no runtime ownership and need no helper.
  The five semantic matches are deliberately local: (1) optimized and
  post-prune inliner entry points visibly select different cleanup and
  single-call policies even though both snapshot current body sizes; (2) the
  two native address-folding reducers retain different sequence, liveness,
  index, and offset eligibility before the shared D5 emission; (3) ordinary
  and crossing placement retain different pools, span structures, i128
  exclusions, and claim publication after the shared D5 ordering prefix;
  (4) debug-register and debug-frame-offset queries remain typed predicates
  rather than a sentinel-bearing generic debug-location matcher; and (5) the
  scalar duplicate-load and Boolean-predicate memory whitelists intentionally
  disagree on unreachable instructions and volatile/nonvolatile loads.  The
  global 28-line production audit therefore remains unchanged.  Lowering it
  to the report-only threshold would add known syntax noise and make these
  policy distinctions look like defects; no stable semantic family remains
  that would justify a new normalized scoped gate without forbidden
  function-name or source-snippet matching.
- **D7 (FINAL CUMULATIVE CHECKPOINT).** The production head is `59c11bd5`.
  Root `make -j32 test-report-through-pa38` passes 5,465/5,465; PA37 is
  188/188, PA38 is 45/45, the LowIR contract audit retains 124 ledger rows and
  99 retained rows, `git diff --check` is clean, and the default file audit
  passes with zero fatal findings and 35 warnings (one fewer than D0).  The
  tight report falls from 14 to 11: all remaining matches are the six
  preambles and five policy distinctions classified in D6-A1.  Structural
  headroom is restored without dense formatting: `lowir_opt.cpp` is 2,593
  lines versus 2,995 at D0 and `optimize` is 87 lines versus the former
  240-line limit; final related sizes are cleanup 1,201, inliner 1,693, native
  optimization 1,313, native address folding 392, and native location
  planning 970 lines.

  The final private exact-source capture covers 215 translation units.  Its
  aggregate O1 facts are 124,704 functions, 2,808,864 input and 1,522,112
  output instructions, 47,029,756 instruction visits, 2,936,641 block visits,
  9,502,232 CFG-edge visits, 94,097 CFG builds / 164,312 reuses / 4,714
  invalidations, 79,433 dominator builds / 144,326 reuses, 68,623 loop builds /
  3,048 reuses, 333,283 simplify runs, 425,032 DCE runs, 458,474 CFG runs,
  702,906 slot runs, 77,475 each forward/local-slot runs, 164,451 remove-slot
  runs, 134,097 promotion runs, and 124,704 each small-object/dead-store runs.
  Maximum per-TU promotion transient storage remains 10,229,916 bytes.  Every
  non-timing counter and all 215 objects match between the pre-change and
  final compiler on this exact source.

  Final reverse-order self lanes are 31.35/31.25 seconds wall and
  897.18/896.15 aggregate CPU; clean exact-source GCC lanes are 29.22/29.45
  wall and 567.68/569.24 CPU, and Clang lanes are 30.80/30.00 wall and
  644.37/644.68 CPU.  Mean self/GCC is 1.067x and mean self/Clang is 1.030x;
  the maximum is well below 1.50x and improves on the 1.486x D0 checkpoint.
  Final self, GCC, and Clang compiler hashes/text are
  `beef91a2...`/8,647,263, `8864af1c...`/5,788,632, and
  `122b0dc0...`/5,026,313, with self data at 334,936 bytes.

  A fresh isolated explicit-O1 inception used outer, inner, and object
  parallelism all at 32 under
  `/dev/shm/v3codex-optdup-d7-final-inception.EIZZYR`; all 215 objects and the
  final compiler match, and both self/inception binaries hash to
  `beef91a2...`.  No stale compiler, profiler, Valgrind, Cachegrind, or
  perf-recording process preceded the final correctness, performance, or
  inception runs.  Retained production commits are `3f7745df`, `47df740f`,
  `15770b5e`, `9b318336`, `69ed83e9`, `1ee06c85`, `1447ff9d`, `08cf648b`,
  `920827cc`, `fdf1576a`, `9e69aad0`, `35c36923`, `35ca7ed7`, `b5da1483`, and
  `59c11bd5`; rejected experiments and cumulative checkpoints are recorded in
  the intervening plan commits.  The final evidence and every retained commit
  are pushed on `v3opt`.

Append one entry for every retained consolidation, rejected abstraction,
re-baseline, cumulative gate, and push checkpoint.

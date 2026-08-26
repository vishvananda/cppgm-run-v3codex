# PLAN-INLINE-PARITY: closing the O1 gap through inlining

Objective: bring the exact self-O1 compiler within 10% of gcc-O1 on the
frozen benchmark (`~/cppgm-extended-pa39-source-layout/benchmarks/
self_compile/stable/semantic_overload.cpp`, `-std=gnu++11 -O1 -Idev/src`).
Current honest state at the L38 landing (P24 protocol): self-O1
10.350 s / 42.36B dynamic instructions vs gcc-O1 5.895 s / 20.86B =
**1.756x wall, 2.031x instructions**.  (At L33/446f86ba: 1.769x,
2.052x; at L31/678c5091: 1.774x, 2.056x; at L26/342e1bfc: 1.791x,
2.123x; at L19/0c296b7a: 1.835x, 2.139x; at the first Phase A
landing e325dc7e, L7: 1.891x, 2.153x; at fd019bdc: 1.90x, 2.20x.)

This plan supersedes the inlining-related threads of PLAN-O1-PARITY.md
(P22-P28) and PLAN-OPT-PASS-IMPROVEMENTS.md (R10-R11) for forward work.
Those documents remain the historical record; every claim below carries
its provenance there.

## 1. Evidence

Ranked by how directly it constrains the program.  Confidence: HIGH =
reproduced at HEAD with deterministic metrics; MED = single measurement
or older tree.

- E1 (HIGH, P28 ablation): `gcc -O1 -fno-inline*` on the frozen TU goes
  from 5.69 s / 20.24B Ir to **13.30 s / 47.47B Ir** — worse than us.
  Disabling every other midend pass on top adds only +2.3%.  The entire
  gcc advantage flows through inlining; our backend BEATS gcc's at the
  matched no-inline operating point.
- E2 (HIGH, P28): gcc pays +25% text (3.96M -> 4.96M) for inlining and
  wins 57% wall.  Text growth of that magnitude pays on this host.
- E3 (HIGH, P28): Ir ratio (2.35) and wall ratio (2.34) for
  gcc-no-inline/gcc match to three digits.  **Dynamic instruction count
  is a 1:1, deterministic proxy for wall on this workload** — sweeps can
  be gated on cachegrind Ir and movement censuses, immune to the ~1%
  layout noise floor (P25c) that corrupted earlier wall-based sweeps.
- E4 (HIGH, P28): gcc-O1's inline policy is our policy CLASS —
  keyword/hint-driven plus called-once plus always_inline — but at
  ~10x our limits: hinted bodies to ~70 GIMPLE statements (~100+ LowIR
  instructions) and called-once unbounded, vs our hinted late cap 7,
  early cap 40, called-once cap 160.
- E5 (HIGH, P28 census): the per-body structural gap at HEAD: we emit
  14,700 functions (11,305 local) vs gcc's 4,718 (1,420 local); 96% of
  our functions carry frames vs 63%/43% (gcc/clang); 50% of our static
  instructions have memory operands vs 41%/38%; we use <=1 callee-saved
  register in 56% of functions while gcc/clang use all 6 in ~40% — and
  where we do use callee-saved registers, we use them as SCRATCH (the
  ordinary reactive pool is {R8,R9} then straight into callee-saved),
  paying save/restore without cross-call persistence value.
- E6 (HIGH, FindChild dissection, P28): 76 instructions vs gcc's 33 for
  the same function: 15 of frame ceremony on a leaf, the loop-carried
  cursor living in the frame (3-4 memory ops/iteration), invariant bases
  reloaded per iteration, `lea`+load chains where gcc folds one
  scaled-index memory-operand compare, and zext self-moves.
- E7 (HIGH, re-verified at HEAD): the O2-built self compiler is +2.0%
  SLOWER than the O1-built one — the O2 value stack's harvest is
  inverted by the backend.  Consistent with E1's "value passes are
  marginal": they are not the path, with or without us.
- E8 (MED, R10i-b + P13 + P26d, pre-P27 trees, wall-gated): raising
  nonleaf caps uniformly (8/192 +1.5%, 18/512 +9.4% wall), loop-site
  budgets (+1.6%), and hot-region selection (census: +25% MIR, +58%
  scalar loads) all regressed.  REINTERPRETED under E1-E4: those doses
  were far below gcc's operating point, applied without hint targeting,
  measured in wall at the noise floor, and their post-inline regions
  drowned in frame movement (E5/E6).  They are evidence about *those
  configurations*, not about inline depth as such.
- E9 (HIGH, P27a/b landed): the collapse machinery now in-tree —
  aggregate SROA, small-object and scalar-slot promotion, staged-copy
  forwarding, all run in the callee-first late-inline cleanup hook, so
  admitted sizes reflect collapsed shapes.  Cold-path discounting
  (throw/noreturn-call blocks free) already admits the checked-accessor
  class; FixedQueue ops inline completely.
- E10 (HIGH, P26a/P27c): uses-per-epoch ~1 on the current IR; segments
  and caller-saved amortization have nothing to bite on UNTIL inlining
  merges epochs.  Placement work is downstream of inline depth, not
  upstream.
- E11 (MED, P27d): collapse-proven trial inlining (snapshot, inline,
  keep iff collapse removes half the clones) measured 0-1 keeps at the
  41-96 size class — the cursor-tower bodies are arithmetic, not
  staging.  The trial MECHANISM is sound and reusable; its criterion
  (collapse) is the wrong keep-test for arithmetic bodies whose value is
  call-overhead and epoch merging (E1 says that value is the whole
  game).
- E12 (MED): clang beats gcc's I1 misses by 18% at equal wall with more
  instructions — layout matters second-order; body count (E5) is the
  icache lever we control.

Reference builds preserved: /tmp/v3codex-wt-hostO1 (gcc-O1 at HEAD),
/tmp/v3codex-wt-clangO1 (clang-O1), /tmp/v3codex-wt-ablate (ablations),
/tmp/v3codex-p27b-o1 (self-O1), /tmp/v3codex-base-o1 (pre-P27 self).
Ablation harness: scratchpad ablate/run.sh.

## 2. Model

gcc's win = deep hint/called-once inlining x a backend that holds the
merged regions in registers.  Our failures decompose as: (a) caps ~10x
too small (E4), (b) post-inline regions drown in frame movement because
the walk demotes edge-live values at definition and scratch allocation
burns callee-saved registers (E5, E6), (c) sweeps judged at the wall
noise floor (E3 fixes this).  The program below raises depth toward
gcc's point with Ir-census gating, and sizes the backend work by
measuring the merged-region movement multiplier rather than assuming it.

## 3. Investigations

Each has a protocol, a decision rule, and an owner phase.  Ir census and
`--stats`/`--stats-functions` movement counters are the primary metrics;
wall A/B (PSI-gated, 5 ABBA blocks vs /tmp/v3codex-p27b-o1) only
confirms multi-percent Ir effects.

- I1 **Inline dose-response in Ir** (decides Phase A's operating point).
  Grid: hinted-late-cap {12, 24, 48, 96} x caller budget {384, 768,
  1536} with called-once cap raised to 512/unbounded, hint-targeted only
  (non-hinted caps unchanged).  For each point: frozen-TU Ir
  (cachegrind, ~1 min), mir/movement census, text size.  Decision: if Ir
  falls monotonically toward ~20B, take the knee point to Phase A wall
  confirmation; if Ir falls but movement RISES steeply, Phase C work is
  the binding precondition and its size is the divergence; if Ir does
  not fall, the inliner's site selection is broken and I2 localizes it.
- I2 **Merged-region movement multiplier** (sizes Phase C).  Force-
  accept the P27d trial machinery over the lexer chain
  (Peek/TC::Next/PC::Next/DecodeOne) in a probe build; extract the
  merged function's MIR movement share and compare to gcc's inlined
  equivalent (asm dissection, E6 method).  Output: one number, "our
  merged regions carry Nx the movement" — N < 1.3 means depth alone
  suffices; N > 2 means Phase C gates Phase A's deeper doses.
- I3 **Called-once population** (cheap, feeds Phase A).  Census how many
  of the 11,305 local bodies are single-call (the class gcc inlines
  unbounded at O1); our cap is 160 with a 10,240 TU budget.  Count the
  sites rejected by those two limits.
- I4 **Scratch and frame ceremony bound** (feeds Phase B).  Count
  dynamic prologue/epilogue and frame-home traffic attributable to
  leaf/near-leaf functions (per-function census x asm push counts).
  Bound = what Phase B can recover independent of depth.
- I5 **Site-selection audit** (only if I1 shows non-monotone Ir).
  Instrument which sites each dose admits (symbol, size, hint,
  call-depth) and diff against gcc's actual inline decisions
  (-fdump-ipa-inline on the reference build) for the same TU.
- I6 **Post-Phase-A re-sweeps** (bookkeeping): GVN@O1, segments (P26
  spec), LICM (parked stash) re-arm on merged-epoch IR per their
  recorded conditionals.

## 4. Phases

Standing gates for every landing: zero-fatal audit; full `make
test-report`; O3+O0 restored-self inception lanes MATCH on the final
tree; verified self-O1 frozen output; Ir/movement census as the go/no-go
BEFORE wall; PSI-gated wall A/B recorded.  PA37 owns exact reducers for
every policy change; intentionally moved fixtures regenerate in place.

- **Phase A: depth to the knee.**  Run I1 and I3; land the best
  operating point as new constants (hint-targeted late cap, budget,
  called-once limits), with a PA37 reducer per changed limit.  Include
  E9's collapse hooks in the admission loop (already landed) and keep
  cold-path discounting.  Exit: frozen-TU Ir at or below the I1 knee;
  wall confirms at 1:1; no lane regressions.  Expected from E1-E4: this
  is where the bulk of 44B -> ~25-30B lives if I2's N is small.
- **Phase B: ceremony removal.**  Scratch-pool widening (RCX/RDX/RSI/
  RDI as clobber-guarded scratch), leaf frame + rbp elision when no
  spills/calls, zext self-move elimination, complex-addressing folds in
  compare/load selection.  Sized by I4; independent of depth; lands in
  census-gated slices.  Also directly shrinks per-body text, buying
  headroom for Phase A doses (E12).
- **Phase C: merged-region placement.**  Scope set by I2's multiplier:
  loop-carried phis in registers first (E6), then liveness-based
  allocation for the post-inline function class, prototyped behind the
  per-function census harness (`--stats-functions`), enabling class by
  class.  The P26 segment spec and parked LICM stash re-arm here (I6)
  once epochs have merged.
- **Phase D: body-count and layout.**  Post-inline reachability pruning
  of the excess local/weak bodies (E5), then layout only if I1's
  residual shows icache-bound points (E12).
- **Phase E: re-baseline and iterate.**  Refresh the gcc/clang
  references, re-run the ablation attribution at the new operating
  point, and re-enter at I1 with the residual.

Ordering: A and B are independent and can interleave; C is gated by I2;
D follows A.  After each landed phase, re-run the honest ratio
(gcc-built vs self-built from the SAME revision — the P24 protocol;
source reshaping remains out of scope per the standing directive).

## 5. Risks

- R1: I1 shows Ir falling but wall flat (IPC loss in merged regions) —
  Phase C promotes to the front; the multiplier from I2 predicts this.
- R2: deep doses break self-hosting (the P22d phi-class bugs).  The
  bisection toolkit stands: CPPGM_INLINE_CAP monotone caps, hybrid-link
  bisection, LowIR dump diffing; lanes catch wrong-code before A/B.
- R3: optimizer compile-time growth (the inliner itself runs in the
  measured numerator AND denominator symmetrically; only asymmetric
  codegen effects move the ratio, but gross lowiropt slowdowns still
  hurt absolute wall) — track lowir_opt_ns per dose point.
- R4: text growth beyond gcc's +25% envelope — cap Phase A points at
  that envelope; Phase D reclaims.
- R5: fixture churn — every intentionally moved PA37 fixture regenerates
  in place with the movement explained in the landing ledger row.

## 6. Ledger

- L1 (toolkit): `--inline-limit name=value` driver option exposing the
  four policy limits (hinted late cap, ordinary caller budget,
  called-once cap, called-once caller budget), threaded through
  `lowir_opt::optimize` into all three waves; defaults byte-stable on
  the frozen TU; the hinted late cap also widens the hinted
  cold-discounted size gate to max(40, cap).  The audit rejects
  environment variables as compiler configuration, so the R2 toolkit is
  a flag, not an env var.  `INCEPTION_EXTRA_CC_FLAGS` (exported `?=`
  hook in pa39) doses selfhost builds; a plain `INCEPTION_CC_FLAGS`
  command-line override is silently dropped by the MAKEFLAGS-clearing
  object sub-make.  Landed `d752de22` + `7b64f27f`; full report
  5427/5427; zero-fatal audit; test-debuginfo pa13/pa37/pa38 failures
  pre-exist at fd019bdc (verified on the untouched reference worktree).
- L2 (I3 RESULT): the called-once population above the 160 cap is ~70
  sites on the frozen TU.  once-cap 512 + caller 1024 captures 61 of
  them (+14,930 cloned instructions), post-prune reject_size 34 -> 2,
  and both total post-inline instructions (85,906 -> 84,744) and frozen
  text (363,002 -> 362,576) SHRINK.  The TU budget is input-scaled
  (135,407 with 73,869 remaining) — the 10,240 floor never binds.
  Unbounded called-once is WORSE than 512 in Ir (44.399B vs 44.316B):
  giant single-call merges dilute.  512/1024 goes into the operating
  point; called-once depth is exhausted as a dimension.
- L3 (I1 RESULT, 15-point grid at d752de22): Ir falls monotonically in
  caller budget everywhere and in hinted cap through 48, then REGRESSES
  at 96 at every budget (budget crowding plus the next wall: at cap 96
  `inline_hint_size_rejects`=0 and `inline_reject_callee_eh` jumps 378
  -> 3,269 — the EH wall stands behind the size wall).  Best point
  h48-b1536: 42.055B (-5.4%) at +68% frozen text.  Ir does NOT fall
  toward 20B: full hint-depth at gcc's operating point recovers ~10% of
  the 24.2B gap.  Movement RISES steeply with dose: census movement
  share 65.4% (p0) -> 72.6% (h96-b1536), absolute movement 65,355 ->
  128,162 while Ir falls only 5%.  Per the I1 decision rule this makes
  Phase C the binding precondition for deeper doses.
- L4 (I2 RESULT): same-scope merged-region comparison
  (PhysicalCursor::Next with DecodeOne absorbed, present in the
  h48-b1536 dosed self binary and in gcc-O1): ours 542 insns / 220
  memory operands (40.6%) / 45 residual calls vs gcc 189 / 57 (30.2%)
  / 0.  N ~ 2.9x instructions, 3.9x memory operands — N > 2, so Phase C
  gates Phase A's deeper doses.  Lexer::Peek is UNCHANGED at every dose
  (its callees exceed even cap 96); TC::Next grows without absorbing
  PC::Next.  The gcc tower for reference: Peek 47/18/1,
  TC::Next 481/156/3, PC::Next 189/57/0.
- L5 (wall A/B, PSI-gated 5-block ABBA vs same-revision p0): h12-b384
  -1.44% wall / -1.47% user; h24-b384 -1.40% / -1.27%; h24-b768 -0.65%
  / -0.83%; h48-b768 -4.18% / -1.91% (wall<user divergence = load
  noise; user is the steady signal).  Wall converts at roughly HALF the
  Ir delta and stops improving past the text envelope — R1 confirmed,
  as I2's N predicted.  The Ir:wall 1:1 of E3 holds only between
  same-policy binaries; dosed binaries lose IPC to text and movement.
- L6 (Phase A landing): operating point h24-b384 — hinted late cap
  7 -> 24, ordinary caller budget 128 -> 384, called-once cap
  160 -> 512, called-once caller budget 320 -> 1,024; the hinted
  cold-discounted size gate stays 40 (max(40, 24)).  Measured at the
  point: Ir 43.080B (-3.12%), wall -1.40% / user -1.27%, frozen text
  +23.6% (inside gcc's +25% envelope, R4), lowir_opt_ns +15% (R3),
  self-hosted binary reproduces the host frozen object byte-for-byte.
  PA37 reducers: 475 and 490 resized to the new 512/513 and 24/25
  boundaries; new 391-inline-growth-budget-boundary (384 pinned per
  wave: 26 calls x 32-instruction leaf -> 12 early + 12 late + 2
  residual) and 476-inline-single-call-caller-budget (1,024 pinned per
  wave: 5 x 512-instruction single-call bodies -> 2 early + 2
  post-prune + fifth retained); pa37/README.md constants updated.
  Deeper doses (h48+) re-enter at I1 after Phase C per L3/L4/L5.
  Landed `e325dc7e`: full report 5429/5429, zero-fatal audit, O3 and
  O0 inception lanes MATCH, self/host frozen outputs byte-identical.
- L7 (post-landing P24 re-baseline at e325dc7e): the grid's Ir numbers
  are CONSTANT-WORKLOAD codegen measurements (dosed binaries compiling
  at the old default policy).  After landing, the compiler also RUNS
  the deeper policy on the frozen TU: +23% MIR through the backend and
  +23.6% object text cost the self binary +1.82B of compile work
  against its -1.39B codegen gain, and cost the gcc-built reference
  +0.61B.  Honest pair: self-O1 44.895B Ir / 10.990 s wall vs gcc-O1
  20.851B / 5.810 s = **2.153x Ir (from 2.197x), 1.891x wall (from
  1.898x)**; both compilers emit byte-identical frozen objects.  The
  Ir-ratio gain converts to wall only partially — the conversion is
  blocked by exactly the L4/L5 movement and text material, so Phase C
  (merged-region placement) and Phase B (ceremony) now carry the
  program; Phase A depth beyond this point is parked until they land.
- L8 (I4 RESULT, callgrind + per-function asm census on the landing
  binary): 738.8M dynamic calls; the census-matched 609.6M of them pay
  a **6.92B-instruction ceremony bound (15.4% of the 44.9B total)** in
  prologue/epilogue plus call/ret alone.  Top payers: Lexer::Peek 99.0M
  calls x 12 = 1.19B (2.6% of the program in ceremony!), the lexer
  tower (Peek/PC::Next/TC::Next/Take) 2.28B (5.1%).  Static shares at
  the landing: ours 5.8% ceremony / 34.7% frame-relative memory
  operands vs gcc 4.0% / 21.0% — 2.34x the ceremony and 2.66x the
  frame traffic in absolute terms.  Phase B's recoverable bound is the
  6.92B (frame elision, scratch widening); the 34.7% frame-operand
  share is Phase C material.  Bonus finding: trivial leaves still take
  millions of dynamic calls (vector::operator[] 8.3M at 9
  instructions, SyntaxToken::Kind 12.2M at 11) — budget-exhausted
  callers skip near-free bodies whose inline cost is below the call
  overhead itself; a trivial-leaf budget exemption is the cheapest
  Phase B slice.  Artifacts: ~/i1-roots/{callgrind-landing.out,
  ceremony-static-dem.txt, i4-join.py}.
- L9 (Phase B slice 1, trivial-leaf budget exemption): a leaf body of
  at most 4 instructions is substituted even when the caller growth
  budget is exhausted — the replaced call sequence is at least as
  large, so text cannot grow; the definition-removing waves keep their
  own accounting.  Census on the frozen TU: budget_skips 8,866 ->
  8,061 (-805 sites), MIR 122,714 -> 122,633, frozen text -1,344
  bytes, movement flat.  Honest Ir 44.895B -> 44.700B (-0.44%);
  self-built binary reproduces the host frozen object.  Gates: report
  5430/5430 (new reducer 392-inline-trivial-leaf-budget-exempt pins
  4-in/5-out under a doubly exhausted budget), zero-fatal audit, O3
  and O0 inception lanes MATCH; pa37/README.md documents the
  exemption.  Honest Ir ratio 44.700/20.851 = 2.144.
- L10 (Phase B scratch widening REFUTED as a slice): granting R10/R11
  to values through the existing clobber-guarded caller-saved path
  (alongside R8/R9/RDI/RSI) censused well (frozen text -2.3%, MIR
  -2.5%, movement -3.3%) but produced WRONG CODE — the self-built
  compiler segfaulted on the frozen TU and the verify gate caught it
  before any landing.  Root cause: R10/R11 are the backend's
  encoder-level scratch, used by phi parallel moves, spill/reload
  address materialization, global addressing (scalar_memory), and
  compare lowering — all BELOW the LowIR instruction clobber masks
  (allocator-inserted spill sequences belong to no LowIR instruction),
  so crosses_register_clobber cannot see them.  RCX/RDX are likewise
  not quick wins: they are unmanaged argument-staging registers with
  dense clobber masks (every narrow load declares RCX).  The scratch
  item therefore requires making encoder scratch explicit in planning
  — it merges into Phase C's allocation redesign instead of being a
  standalone slice.  The census delta (-3.3% movement) is the PRIZE
  measurement for that redesign.  Tree reverted; frozen output
  byte-identical to eb619ca2.
- L11 (Phase B disposition): leaf frame/rbp elision requires zero
  stack adjustment and no callee-saved pushes, but the E5 census
  stands — 96% of functions carry frames because the walk frame-homes
  values — so the elidable population is ~4% of bodies until
  placement improves.  Every remaining Phase B item (scratch widening
  per L10, frame elision, the frame-traffic delta of L8) converges on
  Phase C: liveness-based value placement for the post-inline class
  with encoder scratch modeled explicitly, loop-carried phis in
  registers first (E6), prototyped behind --stats-functions and
  enabled class by class.  Phase C is the program's single open
  front; its prize measurements are L4's N ~ 2.9 multiplier, L8's
  6.92B ceremony bound and 34.7% frame-operand share, and L10's
  -3.3% movement census.
- L12 (Phase C slice 1: loop-carried phi register homes + backedge
  coalescing).  Mechanism: DefinePhi frame-homed every phi — E6's
  per-iteration frame traffic.  plan_value_registers now plans
  loop-carried phi destinations: interval [earliest predecessor
  terminator, span-extended last use], exact per-register clobber
  queries over the full interval (FunctionFacts::clobber_positions,
  newly exported — live_across_clobbers cannot see the pre-definition
  transfer segment or the span extension), phi-first claiming from
  the R15 end of the callee-saved pool (one phi per register; pool
  reservations do not nest), gated on the unavoidable header (no
  layout-forward edge from before the header to after it).  DefinePhi
  claims at construction with frame fallback (contested reservation,
  or constrained_wide_pressure whose binary path takes R15 as a fixed
  destination below the reservation discipline).  EmitPhiMove handles
  register destinations; the parallel-transfer solver orders
  deref-source reads against register destinations and breaks
  register cycles through the frame scratch.  consume/value_is_live/
  can_reuse treat a claimed phi as outliving its counted uses (the
  backedge keeps writing the register), and the backedge chain
  computes the next iteration's value destructively in the phi
  register (can_reuse takeover) when the block feeds the phi and the
  chain dies by the terminator.  Dose-response in honest Ir
  (self-built compiling the frozen TU; baseline 44.700B): ALL phis
  45.097B (+0.89% — merge phis in hot leaves buy ~one load but the
  claimed register adds push/pop on every call; IsIdentifierBody
  +19%); loop-carried only 44.824B; caller-saved routing REJECTED at
  44.928B (starving R8/R9, the reactive pool's first choices, pushes
  every loop temporary into fresh callee-saved registers); header
  gate 44.715B; + backedge coalescing 44.628B; early-return gate arm
  dropped (it forfeited rewrite_promoted_slots-class wins without
  avoiding losses) → **44.625B, -0.17%**.  Census at the landing
  point vs L9: MIR -210, movement -219, frozen text -142 bytes;
  143 phis planned / 136 claimed on the frozen TU; per-function:
  NamePath -16.4%, FindChild -6.5%, FunctionTemplateTypeIsDependent
  -9.5%.  Honest ratio 44.625/20.851 = 2.140 (from 2.144).  Wall
  A/B not measurable at this dose (-0.17% is under the P25c ~1%
  noise floor; L5: wall converts at ~half the Ir delta).  Residual
  regressors documented for the next slices: eliminate_dead_slot_
  stores +7.8% and DumpArena::Add +17.8% — ceremony now binds
  through reactive TEMPS in claimed loops (cursor loads and RBX/R12
  overflow when loop demand exceeds the caller-saved supply); the
  successors are load-path coalescing (mov (R),R for cursor
  advances) and the managed-pool widening that L10/L11 already
  scope.  Gates: report 5430/5430, zero-fatal audit, O3+O0
  inception lanes MATCH, frozen self-reproduction byte-for-byte at
  every measured point.  No pa37 reducer: no LowIR-level policy
  changed.
- L13 (Phase C slice 2: load-path cursor takeover).  A scalar load
  whose address value resolves to a claimed phi-home register (the
  phi itself or a chain alias of it, e.g. the copy the frontend
  emits for `p = p->next`) loads into that register directly —
  `mov (R), R` — under the same takeover conditions as the backedge
  chain; the read precedes the write.  find()'s loop body drops to
  7 instructions with the gcc-shape advance.  Effect on the frozen
  workload is real but small: honest Ir 44.6249B -> 44.6244B, MIR
  -15, movement -14, text -44 — the compiler's own hot cursors
  mostly live in object fields, not local phis; the shape matters
  more as deeper inlining merges cursor towers into locals.  Gates:
  report 5430/5430, zero-fatal audit, O3+O0 inception lanes MATCH,
  frozen self-reproduction byte-for-byte.
- L14 (Phase C slice 3: loop-invariant register residency).  P24's
  strategy lock conditioned the parked LICM stash on "placement can
  hold hoisted values in registers", and P25b showed the planner's
  VF_LOOP_INVARIANT exclusion was correct only under the old claiming
  discipline.  The slice admits loop-invariant values (including
  VF_ONLY_STORAGE_ADDRESS bases — E6's "invariant bases reloaded per
  iteration") as planner candidates when their use range overlaps an
  UNAVOIDABLE backedge span (the phi pass's forward-edge gate applied
  per span), claiming callee-saved registers from the R15 end between
  the phi pass and the ordinary pass; grants flow through the
  existing definition-time machinery, so intervals share registers
  normally, and consume may now release a planned invariant once its
  extended interval is over.  Guard added: a same-instruction
  duplicate may not destructively take any planned resident's
  register outside the phi takeover conditions.  Census on the
  frozen TU: 275 invariants planned; text -504 bytes; MIR/movement
  +100/+63 (fallback shapes shift).  Honest Ir 44.6244B ->
  44.5976B (-27M); cumulative from the L9 baseline 44.700B ->
  44.598B = -0.23%, ratio 2.139.  Per-function: TranslationCursor::
  Next -1.9% (the lexer tower's first movement), FindChild -8%
  cumulative, rewrite_promoted_slots -22.8M; the planner's own
  compile cost +15.6M (R3-class, honestly counted).  One pa38
  fixture regenerated in place per the standing rule:
  410-cyclic-edge-register-pressure — the pressure test's invariants
  (a/b/c/d/base) now hold callee-saved registers with no frame
  temps, program behavior identical, reference program 424 -> 408
  bytes (precedent: 108fb8d8 regenerated the same fixture for
  caller-saved residency).  Gates: report 5430/5430, zero-fatal
  audit, O3+O0 inception lanes MATCH, frozen self-reproduction
  byte-for-byte.  Final tree honest Ir (with the L15 fix included)
  44.5818B — cumulative Phase C 44.700B -> 44.582B = -0.26%, ratio
  2.138.  The LICM stash re-test remains open: hoisted values now
  have a residency path, but only inside unavoidable loops —
  re-measure after the next placement slice widens coverage.
- L15 (frontend use-after-free, found by the slice-3 O0 lane and
  fixed).  The lane's inception stage failed: every SELF-BUILT
  compiler (O0/O1/O3-built alike) errored "binary expression is
  missing its PA12 operand type" compiling lowir_native_session.cpp
  at -O0, while the g++-built host compiled it fine — and the same
  content PASSED from a different file path.  Slice 3's one-line
  Stats-header shift exposed it; the trigger predates the slice.
  Memcheck on the HOST binary found the mechanism: BuildBinary-
  Expression received `display_operation` as a const reference into
  the syntax arena's InternedStringTable texts_ vector; the nested
  overload analysis inside can intern new spellings, reallocating
  texts_ and dangling the reference BEFORE it is interned into the
  name table.  Hashing the freed bytes yields the right NameId only
  while the freed block still holds the old bytes — allocator-reuse
  differences between binaries made g++-built pass and self-built
  fail, with file-path/header-size sensitivity through heap layout.
  A wrong NameId misclassifies operation_kind, PA15's consistency
  check catches the missing operand type.  Fix: display_operation
  passes BY VALUE (the copy is taken while the reference is still
  valid); memcheck 0 errors (was 18/10 contexts), the O0 lane
  MATCHes again, and the host frozen output is byte-identical (the
  frozen compile never hit the dangle).  Toolkit note for R2: the
  hybrid-link bisection must mix OUR-compiled objects only (g++
  objects fail to link against ours — [abi:cxx11] tag mismatch on
  std::string-returning symbols), and "same content, different
  path" flipping an outcome is the signature of allocator-layout-
  dependent latent bugs, not codegen regressions.
- L16 (I1 re-entry at h48 after Phase C slices 1-3, REFUTED again).
  Dosed h48-b768 selfhost at the improved backend: constant-workload
  Ir 43.791B vs the landed default's 44.582B = -1.77% incremental —
  statistically UNCHANGED from the old backend's increment (L3 grid:
  43.080B -> 42.288B = -1.84%).  Census at the dose: movement share
  still rises (68.5% -> 71.2%), frozen text 549,996 = +51% over the
  pre-Phase-A base (R4's +25% envelope still blown), phi/invariant
  claims stay ~430 TU-wide against 106K movement instructions.  The
  slices fixed the loop-carried class E6 identified, but the dosed
  IR's movement mass lives in the merged straight-line epochs the
  planner still can't reach (P26a's uses-per-epoch ~1 invariant).
  Deeper doses remain parked; the program's open front is unchanged:
  whole-function placement with the managed pool widened past
  {RDI,RSI,R8,R9}+callee-saved — L10's R10/R11 audit (the -3.3%
  movement prize) is the entry point.
- L17 (Phase C slice 4: the r10 frame-carry, the first pool-widening
  landing).  The L10 lesson inverted: instead of teaching the
  LowIR-time planner about encoder scratch, the widening runs at the
  MIR/encode layer where every instruction is visible.  The frame-
  forwarding pass's rejected windows (source register not preserved)
  now ride r10: the store becomes `mov r10, src`, the load forwards
  from r10, when the window passes the carry oracle — machine_opt's
  instruction_definition_mask (implicit EH/i128/bulk/call clobbers)
  plus the encode-time rewriter shapes (div/mul magic burns r10/r11;
  OP_GLOBAL/OP_SYMBOL addressing takes the scalar-memory scratch
  pick; out-of-int32 immediates materialize through scratch) plus
  explicit r10 operand mentions.  Two wrong-code hazards found by
  the verify gate and review, both closed at plan time: (1) an
  MI_MOV immediately before the store lets the encode-time mov/store
  fold consume the store and orphan the carry — such windows are
  refused; (2) overlapping carried windows would share the single
  r10 — a greedy non-overlapping subset per block is kept.  R11 is
  NOT recruited (the address-folding machinery owns it).  Census on
  the frozen TU: 1,095 carried reloads, text -7,792 bytes (-1.7%).
  THE METRIC LESSON: honest Ir is FLAT (+6M — each carry swaps
  store+load for two register moves, same instruction count); the
  win lives in the terms Ir cannot see — full cache-sim D refs
  27.123B -> 26.744B (-380M, -1.4%), I1 misses -3.9M (-0.9%), and
  PSI-gated 5-block ABBA wall -1.34% / user -1.40% with the carry
  binary faster in all ten pairings.  For store/load-to-move
  conversions the go/no-go is Dr/Dw + wall, not Ir.  Honest wall
  pair after landing: self-O1 ~10.71 s vs the previous ~10.86 s;
  the Ir ratio stays 2.138.  Gates: report 5430/5430, zero-fatal
  audit, O3+O0 inception lanes MATCH, frozen self-reproduction
  byte-for-byte.  Next widening steps recorded: cross-block
  carries, r11 recruitment behind an address-folding interlock, and
  multi-load slots (count>2) — each rides the same oracle.
- L18 (Phase C slice 5: multi-load slots + the float oracle hole).
  Frame forwarding generalizes to slots with one store and N
  same-block loads (count == 1+N exactly): the preserved-source and
  r10-carry paths forward every load; single-load behavior is
  byte-identical to L17 (verified during the bisect).  The verify
  gate caught wrong code again, and the hybrid-link bisection
  (our-O1-objects mix, then pa21_constant_evaluator.o pinpointed by
  swap) plus the kept-store discriminator experiment localized it:
  the carry oracle was BLIND TO FLOATING-POINT EMISSION — float
  immediates (even 0.0), fneg's sign mask, and x87 staging all
  materialize through r10 at encode time with no operand or
  definition-mask evidence.  The oracle now blocks float-typed
  instructions and any OP_XMM operand.  This hole ALSO existed in
  landed L17: the compiler TU is float-sparse so every gate passed,
  but float-heavy user code could have miscompiled — the fix
  retroactively hardens slice 4, and the oracle lesson generalizes:
  ENUMERATE ENCODER SCRATCH BY EMISSION SITE, not by operand shape
  alone.  Census: 1,232 carries (from 1,095), text 437,096 (-158
  further); full cache-sim vs the pre-carry baseline: D refs
  -435M (-1.6%), I1 misses -13.0M (-3.0%); the slice-5 increment
  over slice 4 (D refs -55M, I1 -9.1M) is counter-visible but
  below the wall noise floor — combined 4+5 ABBA wall -1.07% /
  user -1.11%.  Gates: report 5430/5430, zero-fatal audit, O3+O0
  inception lanes MATCH, frozen self-reproduction byte-for-byte.
- L19 (Phase C slice 6: r11 as the second carry register).  The
  oracle is parameterized by scratch register and hardened by the
  L18 lesson — the by-emission-site sweep of r11 burners added two
  blockers the definition mask misses: the EH marker family
  (MI_EH_FILTER/MI_EH_CLEANUP_CLAUSE/MI_RESUME stage handler
  records through r11) and MI_COPY_BYTES (its rsi/rdi argument
  shuffle swaps through r11); both now block BOTH carry registers.
  The address-folding interlock is automatic: r11 windows exclude
  every MIR-visible r11 mention, and the folds' own r11 writes only
  occur at OP_GLOBAL instructions the oracle already blocks.
  Carried windows on the same register stay non-overlapping; r10
  and r11 windows interleave freely (each oracle excludes its own
  register's mentions).  Census: 1,504 carries (+272), text 435,026
  (cumulative -12,002 / -2.7% vs pre-carry); full cache-sim D refs
  26.622B (cumulative -502M / -1.85%), Ir flat, I1 misses layout-
  noisy (+9.6M this build, still -3.4M vs pre-carry); increment
  ABBA user ~-0.4% (at the noise floor, counter-backed).  Gates:
  report 5430/5430, zero-fatal audit, O3+O0 inception lanes MATCH,
  frozen self-reproduction byte-for-byte; the refreshed gcc-O1
  reference at this revision emits byte-identical frozen output.
  HONEST PAIR REFRESH at 0c296b7a (P24 protocol, PSI-gated 5-block
  ABBA, gcc-O1 reference rebuilt from the same revision): self-O1
  10.702 s vs gcc-O1 5.831 s = **1.835x wall** (from 1.891x at L7),
  user 10.205 vs 5.369 = 1.901x; Ir 44.638B vs 20.865B = **2.139x**
  (from 2.153x).  The Phase C program has moved the wall ratio
  -3.0% relative while Ir moved -0.7% — the carry family's
  D-ref/text reductions convert to wall that Ir cannot see, exactly
  as the L17 metric lesson predicted.  The carry family's remaining
  recorded step is cross-block carries; the residual same-block
  population is thinning.
- L20 (cross-block carries, MEASURED and REFUTED; Phase D pruning
  measured LOW-VALUE; L18 correction).  Cross-block: the frozen TU
  holds 2,442 cross-block single-store slots with 7,679 loads — a
  ceiling nearly twice the same-block carry population — and the
  full machinery was built and verified (normal-edge MIR CFG with
  fallthrough, landing pads excluded as normally-unreachable,
  bitset dominators, on-path region = forward-reach(store) ∩
  reaches(loads) with whole-block oracle cleanliness, block-set
  claims layered under the same-block windows; FROZEN_MATCH held).
  The filters collapse the ceiling to 70 realized windows: the
  clean-path condition dies on call/global-dense paths — the P26
  epoch wall again, now at block granularity.  Honest trade: -11M
  D refs against +79M Ir of pass self-cost (allocation and def-mask
  traffic, confirmed by profile; gating CFG construction on real
  candidates barely moved it).  Machinery reverted; this row is the
  re-arm recipe if a later stage thins calls on paths.  Phase D
  pruning null: crediting .eh_frame FDE self-references, the frozen
  TU has 214 unreferenced local bodies = 11.4KB (2.6% of text), all
  never-executed cold code — no icache value, pruning deferred.
  L18 CORRECTION: the preserved-multi path landed inert (a bisect
  cap of 0 survived into the commit); enabling it at the shared
  cap-40 measures -5 carries / -16 bytes (the preserved whitelist
  rarely holds over long windows) and is landed here so the code
  matches the ledger.  THE CARRY FAMILY IS CLOSED: same-block r10 +
  r11, multi-load, all measured; wall banked ~-1.4%; the residual
  frame traffic is call-crossing (caller-saved carries cannot reach
  it) — the callee-saved side of that residual belongs to the
  placement redesign, not to encode-time carries.
- L21 (callee-saved carries, MEASURED and REFUTED — the fourth and
  final carry direction).  Idea: a preserved callee-saved register
  survives calls and the encoder never stages through it, so a
  window whose resident value is DEAD could carry call-crossing
  reloads — the 586-window population L20 identified.  Built in
  full and FROZEN_MATCH-verified: mention-level physical-register
  liveness over the MIR (plain mov/load/lea destinations count as
  writes, everything else reads), normal-edge successors plus
  every-block-to-every-pad unwind edges (MI_EH_PUSH's label operand
  names the pad in BOTH EH flavors — landing pads read planned
  callee-saved residents, so unwind flow must be modeled), the EH
  marker range blocked in-window (the compact-EH record rolls
  callee-saved back to push-time values on unwind), indirect-jump
  functions excluded, recruitment only from function.callee_saved_
  regs (the preserve set is fixed before encode time).  A naive
  probe said 860 windows; liveness+conflicts recruited 186.  The
  honest counters refute it twice over: Ir +564M of liveness
  self-cost against Dw -3M — the recruited windows are COLD
  (EH-adjacent), and the structural finding is that PRESERVED-AND-
  DEAD callee-saved capacity barely exists: where the walk pushes a
  callee-saved register it keeps it live.  All four carry
  directions are now measured (same-block caller-saved: LANDED,
  -1.4% wall; cross-block: cost-negative; callee-saved: cost-
  negative and cold).  Machinery reverted; the row is the re-arm
  recipe (efficient mask-based liveness sketched) should idle
  callee-saved capacity ever appear.  The call-crossing frame
  residual is confirmed to live in LIVE callee-saved registers —
  reachable only by the walk-time placement redesign that decides
  WHICH values deserve them, not by encode-time scavenging.

- L22 (I7 census + Phase C slice 7: deserving-based callee-saved
  placement).  I7 instrumented the plan-to-grant funnel and the call
  boundary (counters land with the slice, all --stats-guarded).  At the
  L21 baseline: 15,458 planned values -> only 3,876 walk grants; failures
  are 6,819 busy vs 0 clobber, and the busy holders are mostly values the
  planner never put there (reactive scratch, eager parameter homes; the
  caller-pool R8/R9 plans dominate raw counts — the callee subset is 594).
  Of 3,232 frame-sourced GPR argument-staging loads, 2,358 (73%) are
  VF_ONLY_CALL_ARGUMENT values (1,153 crossing / 1,000 edge-live / 151
  call-results) — the class the planner EXCLUDED from candidacy; the
  edge-live demotion path frame-homes them at definition (EH functions
  retain nothing).  Hot-body dissection vs REAL g++ -O1 (NOTE:
  ~/i1-roots/gcc-frozen.o is OUR e325cd7e compiler's output, not g++ —
  a provenance trap; true-gcc object regenerated fresh): the
  select_constructor::$_0 lambda pays 437 frame loads / 453 calls vs
  gcc's 217 / 381 — 2.0x the reloads at 1.19x the calls, from 82
  distinct slots vs 30; at this call density gcc spills its hottest
  values too (top slot reloaded 30x), it just keeps ~50 more values out
  of the frame.  Whole frozen TU at HEAD: ours 123,921 insns / 39,043
  frame operands (31.5%) vs g++ 89,993 / 21,213 (23.6%).  THE SLICE
  (three components, each census-checked): (1) admit crossing
  VF_ONLY_CALL_ARGUMENT values as ordinary crossing candidates; (2)
  assign the crossing callee-saved pool in descending use-count order
  over per-register claimed-interval sets (replacing first-fit-by-
  definition watermarks — gap-filling packs better: OCA assignments
  287 -> 454, total failures 4,372 -> 3,980); (3) the reactive
  callee-saved allocator and eager parameter homes prefer registers
  with no overlapping planned span (two-pass — a conflict never turns
  an allocation success into failure; caller pool R8/R9 untouched per
  the L12 starvation lesson).  Census: grants 3,876 -> 4,098, spills
  219 -> 205, MIR 122,598 -> 122,538, call-boundary loads 8,317 ->
  8,130, hot-lambda frame loads 437 -> 422, TU frame operands -366.
  HONEST NUMBERS (selfhost, frozen TU): Ir 44.638B -> 44.610B (-0.06%),
  D refs 26.622B -> 26.578B (-44M, -0.17%), I1 misses 433.0M -> 427.0M
  (-1.4%) — counter-visible, below the wall noise floor, same magnitude
  class as the landed L18 increment.  Two pa38 fixtures regenerated in
  place (o2/400-loop-invariant-call-crossing-placement: a frame temp
  and its store vanish, stack 32 -> 16; behavior/o1/405-deferred-
  compare-across-call: pure rbx/r12 renaming from the weighted order),
  program behavior identical.  Gates: report 5430/5430, zero-fatal
  audit (lowir_native.cpp held at its 3,000-line limit by moving the
  census recorders and reactive-avoidance helpers into PlannedResidency
  and reclaim_dead_parameter_register into SpillSelection — all
  byte-neutral, FROZEN_MATCH), O3+O0 inception lanes MATCH, frozen
  self-reproduction byte-for-byte.  RESIDUAL: walk grants still lag
  plans (planned values landing in frame 4,009 -> 4,472 as plans grew);
  the 422 -> 217 gap vs gcc is value-materialization structure (gcc
  collapses derived pointers into one reloaded base), LowIR-level
  material, not allocation order.
- L23 (corrected I1 re-grid + honest-pair arithmetic: the dose makes
  BETTER CODEGEN but still cannot land).  L16 judged h48 on constant-
  workload Ir alone; re-measured on the L22 tree with the L17-corrected
  gates (Ir AND full-sim D refs AND PSI-gated ABBA wall), all points
  self-reproducing byte-for-byte:
    h48-b768:  Ir -1.85%, D -1.51%, I1 +9.4%,  text +13.9%, WALL -0.74%
               real / -0.68% user, dosed faster in 10/10 pairings;
    h48-b1536: Ir -2.34%, D -1.87%, I1 +10.0%, text +16.4%, wall +0.17%
               (flat — the extra budget's D gains don't convert);
    h96-b1536: Ir -0.87% (the cap-96 EH wall again), D -2.20%,
               I1 +22.1%, text +29.8%, wall +5.73% (catastrophic).
  The knee is h48-b768 and the wall-positive text envelope is ~+14%,
  well under gcc's +25% (E2 does not transfer to our icache shape).
  L16's Ir-only refutation is SUPERSEDED: the carry family + placement
  slices made the dose's codegen genuinely wall-positive.  BUT the
  honest-pair arithmetic still refuses the landing.  Simulated landing
  at 4fc1679f (both binaries run the h48-b768 policy via flags;
  DOSED_POLICY_MATCH byte-verified; gcc-O1 reference worktree rebuilt
  at this revision — TRAP: the worktree dev/Makefile defaults to -O3,
  the honest reference needs CC_FLAGS forced to -O1):
    Ir: self 44.610 -> 45.591B (policy +1.805B vs codegen -0.823B),
    ref 20.873 -> 21.524B (+0.651B); ratio 2.137 -> 2.118 (IMPROVES);
    WALL: self 10.656 -> 11.009s (+3.3%), ref 5.831 -> 5.998s (+2.9%);
    ratio 1.827 -> 1.835 (WORSENS — the verdict).
  The inliner's own compile work runs at the self binary's ~2.1x
  inefficiency, so the policy cost lands asymmetrically in wall and
  swamps the -0.74% codegen gain.  RE-ARM CONDITION (quantitative):
  the dose is wall-ratio-neutral when its constant-workload wall gain
  covers policy_cost_self - ratio x policy_cost_ref (~0.13s needed vs
  ~0.08s actual today); the gap closes as parity itself improves or as
  lowir_opt gets cheaper — re-run this row's protocol after either
  moves materially.  HONEST PAIR REFRESH at 4fc1679f (P24, both
  policies default): self-O1 10.656 s vs gcc-O1 5.831 s = **1.827x
  wall** (from 1.835x at L19), user 1.897x; Ir 44.610B vs 20.873B =
  **2.137x** — the L22 slice's honest wall confirmation.- L24 (the EH inlining wall, SIZED and first slice REFUTED; the
  profile refreshed at 4fc1679f).  Fresh callgrind: the lexer tower is
  ~20% of the honest self run (Peek 7.2%, Run 5.2%, TC::Next 2.7%,
  PC::Next 2.7%, Take 2.1%), the lowir_opt family ~9-12%, allocator
  and libc string ops ~6%.  Reject census by callee (throwaway
  instrumentation, removed): 884 EH rejects on the frozen TU decompose
  into 635 RESUME-bearing (string ctor cleanup-rethrow shapes), 138
  cleanup, and only 87 terminate-only — the implicit-noexcept wrapper
  around destructor bodies (Scoped* RAII guards, Rb_tree dtors); the
  hot lexer tower is entirely SIZE-rejected, not EH.  Two slices were
  built, gated (5431/5431 with a new o1/499 reducer, audit, O3+O0
  lanes MATCH, byte self-reproduction), measured honestly, and
  REVERTED: (1) marking synthesized TLS wrappers nonthrowing (the body
  is emit_tls_address+ret; the missing unwind=no kept terminate
  regions alive around every destructor touching thread-locals) —
  honest Ir +20M; (2) terminate-only EH inlining (admit bodies with no
  resume/throw/cleanup whose pad-side calls are unbodied-or-noreturn;
  clone machinery splices regions verbatim, verified correct) —
  combined honest Ir +31M, I1 +4.9M (text +0.30%), D +24M, ABBA wall
  -0.14% (noise); the dosed h48-b768 point on the slice tree is also
  flat (Ir +40M, I1 -1.3M vs the L23 dose) — the enabling-at-depth
  hypothesis fails because the EH wall's mass is the resume class.
  MECHANISM: splicing regions into EH-free callers flips has_eh, and
  the backend's blanket penalties (should_retain_edge_register returns
  false for ANY EH function; edge-live values demote to frame) tax the
  whole caller — the destructor-call savings drown in it.  SEQUENCING
  LESSON: the backend must price EH by REGION, not by function
  (retention and placement inside uncovered spans of EH functions),
  BEFORE any EH inlining can pay; then the resume-capable design
  (retarget resume to the call site's enclosing pad) unlocks the 635
  string-ctor class and the deep-dose EH wall (L3: 3,269 rejects at
  cap 96).  Recipe preserved in this row; the o1/499 fixture text
  lives in git history at the reverted change.
- L25 (region-granular EH retention, MEASURED and REFUTED — the third
  flat placement/EH micro-slice; E10 re-confirmed from a third
  direction).  Built in full: analysis records unwind sources (calls
  and throws inside linearly-nested open regions, calls inside landing
  pad blocks; non-linear nesting falls back to the blanket), the
  planner exports its extension spans, and stabilize_edge_live_result
  prices the hazard over the value's span-extended interval (grown in
  BOTH directions over overlapping spans — layout position is not
  execution order) instead of function-wide has_eh.  Verified
  FROZEN_MATCH-clean and self-reproducing.  Frozen-TU census: retains
  146 -> 161, edge-live demotions 770 -> 676, planned residencies 445
  -> 581, movement +20 loads / +63 stores (the belt-and-braces backup
  stores).  Honest: Ir +36M, I1 +4.9M, D refs +13M (reads -7M, writes
  +21M) — the backup-store cost exceeds the reload savings; REVERTED.
  Structural reason the reach is small: the demotion mass is crossing
  values sitting in CALLER-saved registers at definition — retention
  cannot keep those across calls regardless of EH; they need planned
  callee-saved homes, which the planner already attempts (the L22
  grant-rate residual).  VERDICT across L24-L25: placement and EH
  micro-slices are exhausted at the current inline depth — E10's
  "uses-per-epoch ~1" bites from every direction.  The program's
  binding constraint is L23's dose arithmetic: the optimizer's own
  execution cost (~9-12% of self wall per the L24 profile) is both a
  direct ratio lever (it runs at self's 2.1x inefficiency in the
  numerator) and the dose blocker.  Next front: lowir_opt self-cost
  reduction under a byte-identical-output gate (FROZEN_MATCH makes
  iteration safe and fast), starting from the profile's top passes:
  simplify_values 534M, rewrite_promoted_slots 505M,
  eliminate_dead_slot_stores 527M, resolve_instruction_operands 274M,
  inliner 232M, cleanup_cfg 228M.
- L26 (optimizer self-cost batch 1: the pass-cost front OPENS and
  pays immediately).  Three FROZEN_MATCH-verified algorithmic fixes,
  byte-identical output throughout: (1) eliminate_dead_slot_stores'
  dense per-slot byte-vector liveness became word-packed bitsets with
  hoisted scratch (the per-block-visit merge/compare/allocation was
  the pass); (2) rewrite_promoted_slots' has_alias map was rebuilt
  per BLOCK — O(values x blocks) — and is block-invariant: hoisted;
  (3) local_slot_forward allocated four vectors per block (two sized
  by value count) and paid O(slots) std::fill at every call/EH/bulk
  instruction — epoch-stamped maps make block entry and bulk
  invalidation one counter bump.  Host-side (g++-built, same source):
  Ir 17.389 -> 16.986B (-403M, -2.32%).  HONEST self-side: Ir 44.610
  -> 43.217B (-1.393B, -3.12%), D refs 26.578 -> 25.785B (-793M,
  -2.98%), I1 flat; ABBA wall -3.04% real / -3.05% user — 1:1 with
  Ir (E3 holds within a policy).  THE MULTIPLIER LESSON: the removed
  work cost the self binary 3.46x what it costs the g++ build — dense
  byte loops and allocator traffic are our codegen's worst case, so
  optimizer-algorithmic cuts move the honest numerator
  disproportionately; conversely allocator/libc-side work is SHARED
  code and near-ratio-neutral (cutting it helps both walls equally).
  Gates: report 5430/5430, zero-fatal audit, O3+O0 lanes MATCH,
  frozen self-reproduction byte-for-byte (codegen untouched).
  NEXT IN THIS ARC: pass-invocation volume is the remaining structure
  — simplify_values runs 14,063x / dce 17,813x / cleanup_cfg 19,344x
  on the frozen TU, each rebuilding value-count-sized scratch; a
  reusable scratch context threaded through the pipeline is the next
  slice, then the L23 dose re-arm check (each optimizer-second saved
  shrinks the dose's self-side policy cost directly).
  HONEST PAIR REFRESH at 342e1bfc (P24, gcc-O1 reference rebuilt
  same-revision, REF_L26_MATCH byte-identical): self-O1 10.386 s vs
  gcc-O1 5.799 s = **1.791x wall** (below 1.8 for the first time; from
  1.827x at L23), user 1.857x; Ir 43.217B vs 20.359B = **2.123x**
  (the reference gained -514M from the same fixes — g++-O1 vectorizes
  the removed loops less than the O3 host build).
- L27 (dose re-arm re-check at L26, still parked — but converging).
  h48-b768 rebuilt on the L26 tree (H48_L26_REPRODUCES,
  DOSED_L26_MATCH): honest dosed pair 10.785/5.970 = 1.807x wall vs
  the undosed 1.791x — the dose still worsens the honest ratio.  The
  re-arm gap narrowed: needs codegen gain >= policy_cost_self -
  ratio x policy_cost_ref = 0.399 - 1.79x0.171 ~ 0.093 s against the
  ~0.08 s measured constant-workload gain (was ~0.13 vs 0.08 at L23).
  The dose's incremental policy cost (self +0.399 s) is inliner +
  pipeline work on the cloned IR — precisely the pass-invocation
  volume the next optimizer-cost slice targets (simplify 14,063 runs
  / dce 17,813 / cleanup_cfg 19,344 per frozen TU at DEFAULT policy;
  more at the dose).  Each optimizer-cost slice both lowers the
  standing numerator AND shrinks this gap.
- L28 (the E6 compare-fold, MEASURED and REFUTED by silicon against
  every counter).  Census: 1,328 single-use loads feed a compare's
  FIRST operand vs 490 the (already-folded) second.  The extension —
  defer the load into cmp [mem], reg/imm through the existing
  deferred-address carrier machinery, one gate condition — measured
  the biggest static win of the program (frozen MIR -1,588 / -1.30%,
  text -6,172B, binary text -0.78%) and improved every honest counter
  (Ir -464M / -1.07%, I1 misses -2.0%, D refs -200M, D1 misses -6.4%)
  — and LOST the wall: +1.15% real / +1.24% user, slower in 9/10
  pairings.  Mechanism: cmp-with-memory + jcc does not macro-fuse and
  puts the load latency on the branch's critical path; a separate
  load issues early and cmp reg + jcc fuses.  Restricting the fold to
  non-branch-feeding compares keeps only ~6% of the population (-93
  MIR) — noise-class; REVERTED entirely, fixtures restored.  THE
  METRIC LESSON (the L17 lesson's mirror): counters and wall can
  disagree in BOTH directions — store/load conversions win wall
  invisibly to Ir, and instruction-eliminating compare folds lose
  wall visibly to fusion and latency.  Wall A/B is the only final
  arbiter for selection-shape changes; and gcc's use of this shape
  is paired with its SCHEDULER hoisting the load off the critical
  path — shape parity without scheduling is not parity.
- L29 (the dose-response VALLEY: h32-b512 measured and refuted on
  both gates).  The unprobed intermediate point between the landed
  h24-b384 and the L23 knee: text +4.5%, H32_REPRODUCES and
  H32_DOSED_MATCH verified — but constant-workload wall is +1.16%
  real / +1.23% user WORSE than undosed (h48-b768 was -0.74%), and
  the honest dosed pair is 1.816x (vs 1.791x undosed).  The
  dose-response is a VALLEY, not a slope: caps 25-47 admit mid-size
  bodies that pay text and icache without the specific tower merges
  that only trigger at cap >= 48; the h48 gain is those merges, not
  depth per se.  Consequence: the only dose worth landing is
  h48-b768 itself, and the L27 arithmetic (0.093 s needed vs 0.08 s
  covered) closes by cheapening the dose's incremental policy cost
  or improving merged-region codegen (E10's parked placement
  material), not by shrinking the dose.
- L30 (span-free edge-live release: the register-release starvation
  slice, the largest static win of the program).  The E10 re-aim:
  at the h48 dose the planner's candidates scale +24% but walk
  grants collapse to 20.5% — registers stay busy because edge-live
  values NEVER release (value_outlives_counted_uses holds them for
  potential backedge re-reads even when none exist).  The slice: a
  final counted use lying outside every backedge and backward
  exception span cannot be re-executed, so an unplanned edge-live
  non-parameter register releases right there (parameters excluded:
  promoted-slot forwarding replays their location beyond counted
  uses; the planner re-exports its extension spans for the
  membership test).  Frozen TU census: MIR 122,406 -> 118,687
  (-3,719 / -3.04%), spills 205 -> 86, grants 4,098 -> 5,082 (+24%),
  call-boundary loads 8,140 -> 7,516 (-7.7%), 743 releases; at the
  h48 dose: MIR -3.7%, grants +34% — the effect GROWS with merged
  epochs, as E10 predicted.  Honest: Ir 43.217 -> 42.365B (-852M /
  -1.97%), I1 misses -5.7%, D refs -604M (-2.34%), D1 misses -6.8%;
  ABBA wall -0.63% real / -0.72% user.  Gates: report 5430/5430
  with ZERO fixture movement, zero-fatal audit (file held at the
  3,000-line limit), O3+O0 lanes MATCH, frozen self-reproduction.
  Also in this landing: ControlFlowQueries reachability/dominance
  fills became epoch stamps (byte-identical, host-flat, removes the
  per-block-change O(blocks) fills and a per-query allocation).
- L31 (PHASE A STEP 2: h48-b768 LANDED as the default policy — the
  L23/L27 refutations superseded by L30's grant scaling).  On the L30
  tree the dose's honest pair measured 10.501/5.921 = **1.774x wall**
  against the undosed 10.336/5.775 = 1.790x — ratio-positive for the
  first time (span-free release raised dosed grants +34% and cut
  dosed MIR -3.7%, supplying the gain the L27 arithmetic lacked).
  Constants: hinted late cap 24 -> 48, ordinary caller budget 384 ->
  768 (once-cap 512 and once-caller-budget 1024 unchanged); the
  new-default host output is byte-identical to the flagged
  configuration (NEW_DEFAULTS_MATCH_FLAGS).  Reducers resized to the
  new boundaries: 391 and 392 to 50 piece calls (24 early + 24 late
  + 2 residual pins budget 768; the trivial-leaf exemption again
  runs under a doubly exhausted budget), 490's over_cap to 49
  observe calls (post-cleanup 48/49 boundary); pa37/README.md
  updated to forty-eight/768.  Gates: report 5430/5430 with zero
  pa38 movement, zero-fatal audit, O3+O0 lanes MATCH,
  NEWDEF_SELF_REPRODUCES.  The E1 chain is now open: deeper merged
  epochs give the placement machinery real material (E10), and the
  h96/EH walls (L3: 3,269 EH rejects at cap 96) are the next depth
  frontier — blocked on resume-capable EH inlining, which is blocked
  on region-granular EH pricing (the L24 sequencing).
  HONEST PAIR AT 678c5091 (P24, reference rebuilt same-revision,
  REF_L31_MATCH): wall 10.501 s vs 5.921 s = **1.774x** (from 1.835x
  at the session open), user 1.882x; Ir 42.933B vs 20.881B =
  **2.056x** (from 2.139x).  The compounding chain that produced it:
  L26 optimizer self-cost -> L30 span-free release -> the L31 dose
  landing each unlocked the next.
- L32 (release-widening follow-ups, REFUTED — one by population, one
  by wrong code).  (a) Pad-aware release inside exception-region
  spans (VF_READ_IN_PAD_REACHABLE analysis + span-kind export):
  measured +5 releases — backward-pad regions are rare and
  forward-pad regions never created spans, so L30 already covered
  the real population.  (b) Slot-alias-safe PARAMETER release
  (whitelisting parameters without promoted/forwarded-slot replay):
  delivered releases 908 -> 1,301 and grants +233 at the new default
  — and WRONG CODE: pa29 behavior test loop-invariant-temporary-home
  exits 1 (bisect: the parameter arm alone).  Parameters have AT
  LEAST three uncounted location-replay channels — promoted-slot
  forwarding, forwarded-parameter staging, and direct storage-base
  reservations whose [param_reg + offset] reads carry SLOT operands,
  not parameter uses — so facts-side whitelisting cannot be sound.
  A safe parameter release needs walk-side accounting of every
  replay (a per-register replay pin, cleared when the last replaying
  consumer passes).  Both arms reverted; tree byte-identical to the
  L31 landing.  The 709-1,020 parameter busy-holders remain the
  measured prize for that future walk-side design.
- L33 (SPAN-END RELEASE SCHEDULE LANDED — the walk now reclaims
  edge-live registers when their re-execution envelope closes, not
  only at a lucky final-use position).  Three cooperating pieces, all
  inside PlannedResidency + RegisterPool (lowir_native.cpp stays at
  2,999 lines):  (1) A static schedule of (plan_end, value) for every
  planned value, flushed at block entry — a planned loop resident
  whose final use sits INSIDE a span previously held its callee-saved
  register to function end, because planned_interval_over was only
  evaluated at the final consume.  (2) A deferred min-heap of
  (extended-span-end, value) for UNPLANNED edge-live values whose
  final counted use lies inside a backedge/EH span — the population
  L30's span-free rule had to refuse.  The release point is the
  fixpoint extension of the use position over extension_spans_ —
  exactly the envelope the planner already trusts for plan_end, so
  the soundness argument is L30's, deferred.  Parameters, fixed
  homes, phi homes, RAX, deferred carriers, and aliased locations
  all keep their exclusions (the L32 lesson stands: parameters stay
  untouched).  (3) RegisterPool plan-holds: a released register with
  a future planned claim becomes last-resort for reactive allocation
  (two-pass try_allocate + a third pass in the preserved helper), so
  the freed register survives until its claim's grant instead of
  being re-pinned by scratch.  Two instructive failures on the way:
  the release-done marker must accompany the index REMOVAL, not the
  register release (an interval_over removal whose release-if was
  vetoed by alias/retained left a phantom holder → live-location
  index inconsistency); and excluding planned values from span-free
  release to dodge that bug cost grants −11%/spills 2x — the marker
  makes the exclusion unnecessary.  Frozen census vs L31: MIR
  −1.03%, mov_loads −2.10%, mov_stores −2.98%, grants 5,452→5,652
  (+3.67%), spills 84→76, frame_homes −1.31%, releases 642→970;
  busy-fails 8,074→7,844 with planned-holder 180→76.  Gates: audit
  zero-fatal; 5430/5430; O3+O0 inception lanes MATCH; REF_PREL_MATCH
  (gcc-O1 reference rebuilt same-revision, byte-identical frozen
  object); PREL_REPRODUCES (self byte-identical).  Honest counters:
  self Ir 42.853B (−0.19%), ref Ir 20.880B (flat — the schedule's
  policy cost is free).  ABBA wall: self 10.595 s vs ref 5.990 s =
  **1.769x** (from 1.774x), user 1.835x, Ir **2.052x** (from
  2.056x).  Remaining busy-holder prizes after this landing: 6,588
  unplanned value-holders (the flush only helps claims that start
  after the holder's extended end; the rest need eviction or plan
  coverage) and 1,019 parameter-holders (walk-side replay pins).
- L34 (post-L33 allocation probes, REFUTED — three flat censuses that
  jointly map the remaining busy-holder structure).  (a) Extended-end
  reactive avoidance (reactive_lifetime_end span-extended for
  edge-live values; parameters declared full-range): grants +0, MIR
  -0.01% — pass-0 avoidance already covers the reachable cases, and
  in dense functions every preserved register carries claims, so
  avoidance has nowhere to steer.  (b) Grant-time eviction of the
  planned register's lone occupant: eager placement won grants
  +3.03% but MIR +0.17%/loads +0.39% (victim reload traffic exceeds
  the granted savings — the occupants are not squatters); last-resort
  placement fired ~8 times.  (c) Parameter release schedule (the L32
  walk-side follow-up): all three replay channels statically bounded
  per parameter — extended last counted use, extended position of
  every reference to an aliased/promoted/forwarded slot, extended
  last use of every forwarding-load destination — pushed into the
  L33 schedule with per-value sanction bits, callee-saved homes
  only, EH/va_start functions excluded.  Sound (census exit 0) but
  only +50 releases and parameter-holder busy-fails 1,019 -> 1,014.
  The diagnostic census explains the ceiling: 1,382 parameters
  sanctioned and 821 of them replay-free in the FIRST half of their
  function, but parameters are rarely callee-saved-register-resident
  outside exception functions — and 291 EH functions holding 884
  parameters sit behind the has_eh exclusion that every release
  mechanism (L30 span-free, L33 schedule, spill_candidate) shares.
  STRUCTURAL READING: after L33 the busy-fail population is (1) EH
  functions, where no release machinery operates at all, and (2)
  claim/holder lifetime overlaps in register-saturated regions,
  which no release timing can fix.  The 4,730 fresh-binding frame
  homes of planned values (2,536 of them single-use non-crossing
  loads) fail allocation ENTIRELY at definition — every managed
  register held by unspillable residents — confirming saturation,
  not scheduling, as the wall.  Next lever by this census: the EH
  front (region-granular EH pricing -> resume-capable inlining, the
  L24 sequencing), which simultaneously gates h96 depth, the 3,269
  EH inline rejects, and the 291-function EH release exclusion.
- L35 (THE EH INLINING FRONT, BUILT END TO END AND REFUTED BY SILICON
  — the L24-sequencing hypothesis is closed).  The full resume-capable
  program was implemented: (1) EH-bearing callees splice VERBATIM at
  call sites whose handler-stack depth is zero — under the dynamic
  handler-stack model a throw inside the cloned body reaches the
  cloned pads first and a cloned frame-exit `resume` continues
  unwinding out of the caller exactly as the original call did, so no
  clause merging or resume retargeting is needed at depth zero;
  (2) a depth-based EH-context analysis (saturating counter,
  propagated through landing-pad edges with pads receiving the
  pre-push depth) replaced the binary active-bit whose two holes this
  slice exposed — pad-only-reachable code read as region-free (the
  pa34 lifecycle test aborted: holder destructors spliced INSIDE
  main's try), and eh_end-pops-all blinded nesting; (3) splicing
  refreshes the caller context and restarts the scan; (4) direct-throw
  bodies stay outlined (the 486 pin); (5) callee-saved edge-live
  retention enabled in EH functions (the FDE records every
  callee-saved save, so pads read unwinder-restored registers — the
  same guarantee planned residents already rely on); (6) two latent
  bugs fixed on the way: the specializer's folded-null reference
  argument (integer literal into pass=reference — now lowered as the
  pointer value) and the depth pump on unbalanced-region cycles
  (saturation).  All gates GREEN at both doses: 5431/5431 (a new
  401 depth-gate reducer), zero-fatal audit, O3+O0 lanes MATCH,
  REF/SELF byte-identical.  Census at h48-b768: 286 splices, MIR
  +2.11%, grants -0.87%, spills 76->238.  Census at h96-b1536: 1,018
  splices, MIR +49%, grants +22%, optimizer +53%.  HONEST VERDICTS:
  undosed pair 10.650/5.990 = **1.778x** (from 1.769x, self Ir
  +0.81% vs ref +0.86% — the policy cost is SYMMETRIC and the
  codegen gain is ZERO in Ir); h96-b1536 dosed pair 11.815/6.625 =
  **1.783x**.  THE CLOSING FACT: even with the 3,269-reject class
  admitted (1,018 splices at depth), grants +22%, and the register
  machinery region-aware, the deep dose loses — the h96 wall was
  never the EH rejects; it is the post-inline body quality itself
  (the movement the census shows: mov_stores +64% at h96).  The
  ablation-reversal target (gcc-shaped inlining at ~70-GIMPLE caps)
  is not reachable by policy admission alone under this backend's
  merged-body codegen.  Everything reverted; tree byte-identical to
  L33 (TREE_BACK_AT_L33).  Recipes preserved in this row and in git
  history at this commit's parent working state.
- P29 (THE PLACEMENT-QUALITY PROGRAM — opened after L35 closed the
  policy fronts).  CEILING ARITHMETIC from the honest counters at
  L33: self D refs 25.72B (Dr 14.41B / Dw 11.31B) vs reference
  10.42B (5.76B/4.66B) — ratios 2.50x/2.43x against an Ir ratio of
  2.05x at IPC parity.  If whole-function placement brought the
  D-ref ratio down to ~1.25x of the reference, the removed
  loads/stores would take roughly 8-10B instructions off the self
  numerator: ~42.9B -> ~33-35B = a wall ratio around 1.58-1.67x.
  CONSEQUENCE: allocation quality is the largest single remaining
  chunk but does NOT reach 1.5x alone — the rest is midend value
  redundancy (the P26 abstraction-collapse half).  The frozen-TU
  movement census that scopes Phase 1: movement by reason =
  scalar_temporary 27.4k loads + 16.0k stores, call_boundary 8.2k
  loads, address_materialization 12.3k, source_slot 2.3k; frame-home
  creations by reason = edge_live 838, scalar_value 622 (serving
  9,756 staging requests), call_result 107; the L34 saturation
  census (2,536 planned single-use loads failing allocation
  entirely) marks the dense regions where the reactive walk starves.
  PHASES (census-gated, wall-arbitered as always): (1) planner
  admission widened from call-crossing/phi/invariant values to all
  multi-use scalar temporaries, with claimed-interval SPLITTING so a
  value can hold different registers in different regions (the
  missing capability every prior probe circled); (2) spill placement
  by interval — stores at region boundaries instead of definition
  sites, killing the belt-and-braces staging pattern; (3) the
  call-boundary channel: argument staging directly from planned
  homes (8.2k loads); (4) pure-address rematerialization (frame/RIP
  lea on demand) for the address_materialization residue.
- L36 (P29 Phase 1 probe: BINDING CLAIMS, REFUTED — and the
  busy-holder population finally decomposed).  Making planned claims
  binding on reactive allocation (preserved pool: conflict-or-held
  means frame fallback, no take-anything pass; caller-saved pool:
  claim-conflict check added) measured grants +41 (+0.73%), busy
  fails 7,844 -> 7,806, but MIR +0.28% and mov_loads +0.78% — the
  displaced scratch stages through frames at more cost than the
  unblocked grants return.  THE DECOMPOSITION that matters: with
  planned-holder busy-fails at 76 (L33's schedule works), the 6.5k
  value-holder busy-fails are dominated by the planner's own 4,673
  ASSIGNMENT FAILURES — candidates that fit no register in the
  weighted-crossing assignment and then acquire registers reactively,
  overlapping the claims that did fit.  Steering their acquisition
  (L34a), extending their declared lifetimes (L34a), evicting them
  (L34b), releasing them earlier (L33 — landed, small), and now
  refusing them claimed registers (L36) all measure flat-to-negative
  because the conflict is REAL DEMAND: ~24.5k planned-or-failed
  candidates competing for 9 allocatable registers across 5,139
  functions.  Register-count arithmetic closes this front: RCX/RDX
  are encoder scratch, RDI/RSI/R8/R9 already serve, RAX is the
  result carrier.  What remains of P29 is the part that changes the
  DEMAND side or the SUPPLY GRANULARITY: interval splitting (one
  register serves two half-lifetimes), spill placement by region,
  and the midend collapse that removes values outright.  Probe
  reverted; tree byte-identical to L33 (TREE_AT_L33_AGAIN).
- L37 (P29 PHASE 4 LANDED: UNION-SAFE ADDRESS DEFERRAL — the first
  wall-positive slice of the placement program, and the cheapest
  landing of the campaign: one analysis flag plus one whitelist
  line).  The ADDRC instrumentation sized the target exactly: 1,659
  pure slot addresses per frozen TU frame-homed (one store + up to
  6,235 reloads) and 1,255 more burning registers, all because the
  deferral whitelist demanded single-CLASS uses — VF_ONLY_STORAGE
  _ADDRESS wants every use to be storage, address_is_call_argument
  wants every use an argument, and the dominant semantic shape
  (&local passed to calls AND dereferenced) fell through both.
  VF_ADDRESS_UNION_SAFE marks values whose every use lies in the
  UNION {load/store/index/bulk address, call argument} — each member
  already consumes deferred addresses (argument staging emits its
  own lea), so no consumer changes at all.  The flag is separate
  from VF_ONLY_STORAGE_ADDRESS, whose meaning planner admission
  keys on.  Census: MIR -2.29%, mov_loads -7.83%, mov_stores
  -4.37%, frame_homes 1,654 -> 1,315 (-20.5%), copies -2.2%.
  One pa38 fixture regenerated (405-deferred-compare-across-call:
  preserves 3 -> 1, stack 48 -> 32 — the improvement, pinned).
  Gates: zero-fatal audit (analyze_function's marking loops
  extracted to mark_use_class_flags), 5430/5430, O3+O0 lanes MATCH,
  REF_REMAT_MATCH, REMAT_REPRODUCES.  Honest: self Ir 42.853 ->
  42.378B (-1.11%), D refs -1.15%, ref Ir -0.10% (the policy SAVES
  compiler work: fewer homes allocated and stores emitted); Ir
  ratio 2.052x -> **2.032x**.  ABBA wall: self 10.445 s vs ref
  5.920 s = **1.764x real / 1.824x user** (from 1.769x/1.835x).
  NEXT IN P29: the same union argument applies to the remaining
  address classes — field-of-slot index chains (8.4k static, the
  frame_provenance machinery already tracks constant offsets) and
  the reg-path population now freed; then re-examine the register
  competition census with 1,255 fewer contenders.
- L38 (P29: THE INDEX GATE JOINS THE UNION — constant-offset chains
  from pure frame bases defer under VF_ADDRESS_UNION_SAFE).  The
  first attempt widened both encodable arms and pa29 refuted it at
  once (three behavior tests, SIGSEGV): register-carried [reg+off]
  forms need their carrier alive at every consumer, a guarantee only
  the all-storage analysis provides — the union flag admits call
  arguments whose staging happens at positions the carrier analysis
  never priced.  The landed form restricts the union to the FRAME
  arm: a pure rbp-relative combined offset replays anywhere.
  Census on top of L37: MIR -0.48%, mov_loads -0.80%, mov_stores
  -0.98%, mov_copies -1.05%, spills 80 -> 75.  Gates: zero-fatal
  audit, 5430/5430, O3+O0 lanes MATCH, REF_IX_MATCH,
  IXD_REPRODUCES.  Honest: self Ir -0.04% (the chains are
  dynamically cool), ratio 2.032x -> 2.031x — but ABBA wall self
  10.350 s vs ref 5.895 s = **1.756x real / 1.819x user** (from
  1.764x/1.824x): the D-side pressure relief outruns the Ir story,
  the same lesson as L17 in the other direction.  Session
  trajectory: 1.835x -> 1.756x.
- L39 (LOOP-GATED INLINE CAPS, EXPLORED AND PARKED — the per-function
  profile finally names the remaining hot mass, and the probe
  surfaced a latent wrong-LowIR landmine that must be fixed before
  any depth experiment can measure).  Fresh matched callgrind at L38:
  the LEXER CLUSTER (Peek 3.21B, Run 2.92B, PhysicalCursor::Next
  1.09B, TranslationCursor::Next 1.04B, Token-move 0.72B,
  IsIdentifierBody 0.40B, AppendUTF8 0.78B) totals ~10.2B self vs
  ~4.6B ref — 26% of the WHOLE remaining gap — and the mechanism is
  visible in the ref profile: g++ has no Peek/PC::Next/Token-move
  symbols at all (inlined into their towers), while our cap-48
  policy keeps Peek (72 instrs, cold 64) outlined at every site.
  THE PROBE: sites inside layout backedge spans get 2x hint caps,
  with an id-indexed loop map inherited by spliced blocks (the
  position-indexed map goes stale after any multi-block splice — the
  first build classified 148 of Run's Peek sites as non-loop for
  exactly that reason).  Grid: caps-x2 alone spliced 4 of 82 Peek
  sites (budget-bound); +budget exemption at loop sites: MIR +49%,
  Peek -> 24 (unbounded cascade, valley class); +budget kept at 768:
  MIR +4.7%, Peek -> 60, grants -5%.  THE LANDMINE: at the governed
  point, selfhost DIES at O1 and O3 — "missing lowered temporary"
  in SelectUsualDeallocation: an SROA-promoted vector-field phi TRIO
  ([nullptr, nullptr, %tX] per field) loses ONE member while a store
  reading it survives (t294/t298 phis intact, t290 deleted, its
  `store ptr %t290, ...` dangling; also "missing operand type for
  %<internal-1856>" in Lexer::Run at O3).  The bug is in the DEFAULT
  optimizer pipeline (promoted-slot phi bookkeeping vs dce/cleanup),
  reachable only under deeper splicing today; the probe diff is
  preserved (scratchpad loop-gated-caps.patch, 226 lines) and this
  row carries the design.  SEQUENCE FOR RESUMPTION: (1) fix the phi
  trio bug (repro: apply the patch, compile
  pa12_semantic_initialization.cpp at -O1); (2) re-run the governed
  grid point's honest pair; (3) only then consider the budget
  dimension.  Tree reverted; byte-identical to L38
  (TREE_AT_L38_AGAIN).
- L40 (THE PROMOTED-PHI CROSS-SLOT FIX LANDED — the L39 landmine is
  defused).  promote_slots gated phi INSERTION on `complete &&
  promoted[the phi's own slot]`, but the sparse dataflow forwards one
  slot's merged value into ANOTHER slot (`store %phiA_dest, $slotB`),
  so a promoted slot's load replacement can name the planned phi of
  an UNPROMOTED slot — the rewrite then references a value that never
  materializes ("missing lowered temporary"; the L39 vector-field
  trio with one member missing).  The fix computes the NEEDED set:
  phis of promoted slots, phis named by promoted load replacements,
  closed transitively over phi-argument dependencies — and inserts
  every needed complete phi.  Needed phis are provably complete (an
  incomplete phi poisons its dependents, and textual availability
  already required a definition), and a phi of an unpromoted slot is
  still a correct merge of that slot's stored values, so extra
  insertions are at worst dce fodder.  At the DEFAULT policy the
  triggering shape never occurs: PHIFIX_FROZEN_MATCH (byte-identical
  frozen object), 5430/5430, zero-fatal audit, O3+O0 lanes MATCH.
  The honest pair therefore carries over from L38 unchanged (1.756x
  wall, 2.031x Ir).  The L39 repro (loop-gated caps +
  pa12_semantic_initialization.cpp) now compiles clean at O1.
- L41 (LOOP-GATED CAPS MEASURED AND REFUTED — the depth front closes
  from its most favorable angle).  With the L40 fix unblocking the
  selfhost, the governed grid point (hint caps x2 at backedge-span
  sites, id-inherited loop map, budget 768) ran the full honest
  pair: 5430/5430, O0 lane MATCH, REF_LGC_MATCH, LGC_REPRODUCES.
  Counters: ref Ir +2.03% (the dosed policy cost), self Ir +2.66% —
  ratio 2.031x -> 2.043x; ABBA wall self 10.715 s vs ref 5.995 s =
  **1.787x** (from 1.756x, -1.8% WORSE), user 1.857x.  Even 22
  hot-loop Peek splices — the single most favorable population the
  matched profile could name — lose on wall: the spliced bodies'
  movement under our allocator exceeds the call overhead they
  remove, the L35 verdict reproduced at surgical selectivity.  The
  O3 lane also exposed a SECOND latent depth-only landmine, still
  open: a deeply nested throw-path block loses its defs
  ("missing operand type for %<internal-1856>" in Lexer::Run at
  -O3; logic_error ctor + cxa_throw args with no defs — a whole
  callee block dropped by some O3-path transform).  VERDICT: the
  26%-of-gap lexer cluster is NOT reachable by inline policy under
  this backend; it is reachable only by making merged bodies CHEAP
  (P29 interval splitting / spill placement) or by not needing the
  splice (midend value collapse).  Probe reverted; both trees
  byte-verified at L40 (TREE_AT_L40, REF_AT_L40).
- L42 (GVN/PRE RE-ARM AND THE LAST MICRO-CLASSES, CLOSED BY CENSUS
  AND BY REASONING — the slice space at O1 scope is exhausted).
  (a) GVN at O1 (post-L26 cheap infrastructure): loads -1.4%, copies
  -8.2%, BUT stores +5.1%, grants -22.8%, spills +36%, frame_homes
  +12% — eliminating a redundant load merges two short lifetimes
  into one edge-live crossing lifetime, and the allocator demotes
  it; the 3f-era verdict reproduces exactly, PRE contributes nothing
  either way.  REVERTED.  (b) The E10 epoch census re-run on TODAY'S
  merged bodies (offline over the frozen LowIR): mean 1.23 uses per
  value-epoch, 85.2% of value-epochs single-use, only 7.4% of values
  span multiple epochs — interval SPLITTING is refuted by census
  before construction; P29 phase 1 dies with phase-depth.  (c) The
  two-lea stride fold (m*2^k index scaling): implemented in
  emit_index and NEVER FIRED — the ring-buffer shape reaches the
  encoder as a LowIR binary-mul, strength-reduced there; the direct
  3-instruction population in the whole self binary is ~119 sites —
  sub-noise-floor; reverted.  (d) EBB-scope load dedup: closed by
  reasoning — cleanup_cfg already merges exact edges, so the only
  reachable chains cross conditional edges, which is precisely the
  GVN demotion mechanism (a).
- P30 (THE ALLOCATOR REBUILD — the only measured-open path to 1.5x;
  design record for the next session).  EVERYTHING this campaign
  measured converges on one wall: values that cross blocks, calls,
  or merges demote to frame homes because location decisions are
  made reactively at consume time against a 9-register pool with no
  liveness model.  The rebuild replaces the DECISION layer while
  keeping the walk as the EMITTER: (1) a whole-function pass
  computes, per value, a location TIMELINE (register R over
  [a,b], frame over [b,c], ...) from the existing facts
  (uses/positions/clobber index/spans) with proper interval SPLITTING
  at pressure points and spill placement at region boundaries — the
  planner's claimed-interval machinery, plan_end extension, and the
  L33 release schedule are the seed of exactly this; (2) the walk
  consults the timeline instead of the reactive pool: definition
  emits into the assigned location, transitions emit the scheduled
  move/spill/reload, consume-time release logic disappears; (3) the
  reactive path survives only as the fallback for values the
  timeline declines.  MIGRATION GATES, in order: (i) timeline covers
  planned values only, byte-identical output required (the timeline
  reproduces today's grant/release decisions — pure refactor,
  FROZEN_MATCH); (ii) timeline admits all multi-use scalars with
  splitting, census-gated (grants/mov must move, E10 does NOT bind
  here because splitting serves PRESSURE, not epoch amortization —
  the value gets a register exactly where its uses are, frame
  elsewhere); (iii) spill placement by span boundaries replaces
  stabilize's definition-time backup stores (the 3.3k edge_live
  staging events); (iv) then and only then re-arm GVN@O1 (its
  harvest was real: copies -8% — it was the DEMOTION that refuted
  it) and re-run the L41 loop-cap point (its splices failed on
  movement, not on the splice).  The instrumented populations to
  re-check at each gate live in L34/L36/L37/L39/L41/L42.  Honest
  state carried into the program: **1.756x wall / 2.031x Ir** at
  L40.
- P30a (GATE (i) GROUNDWORK: the decision-surface catalog).  Every
  register/xmm pool mutation the timeline must subsume — 35 sites,
  eight kinds: (1) FIXED reservations: i128-atomic RBX (l.126),
  mixed-boundary R8 scratch (l.369), ABI argument registers during
  staging (l.526), R9 conversion fallback (l.611-612), all in
  lowir_native.cpp; (2) PLANNED GRANTS: try_planned_grant's
  try_reserve (planning.h) + the parameter-home path (l.323); (3)
  REACTIVE RESULTS: caller-saved probe (l.1137), preserved pool via
  try_allocate_preserved_avoiding_plans (planning.h), wide/other
  allocate calls (l.641/649/2711/2885), XMM (l.1190); (4)
  CONSUME-TIME RELEASES: the l.976/987 release block and the
  set_value transitions (l.1115/1120/1209/1213/1296/1299); (5) THE
  L33 SCHEDULE: flush_planned_releases + span-end heap (planning.h);
  (6) SPILLS/RECLAIMS: spill_one + reclaim_dead_parameter_register
  (spill_selection.h); (7) CARRIERS: deferred-address index
  reservations (l.1030) and the parameter-move release (l.660,
  l.2578 argument staging); (8) INCOMING-REGISTER reuse
  (memory_lowering.h promoted-parameter reserve).  Gate (i)'s
  record/replay seam wraps exactly these: a per-function decision
  log (position, kind, value, register) recorded by a dry-run of
  the CURRENT logic and replayed by the emitter; byte-identity of
  record->replay is the gate.  After the seam exists, gates
  (ii)-(iv) replace the log producer, never the emitter.
- L43 (THREE P30 PROBES CLOSED IN ONE SITTING, AND THE DECOMPOSITION
  THAT CONSTRAINS THE REBUILD).  (a) Truncated prefix claims (the
  planner's assignment failures get the register with the
  latest-starting conflict for a prefix of the interval, retention
  fallback store keeps the home valid, the release schedule demotes
  at a span-free boundary; span-snap refined so def-containing spans
  permit in-loop boundaries): 100 of 4,673 failures convert, grants
  +24, movement FLAT — the failures are SIMULTANEOUS overlap beyond
  the register file, not sequential scheduling, so splitting has
  nothing to harvest; P30 gate (ii)'s premise is measured false at
  this geometry.  (b) RCX/RDX pool widening: the clobber model
  already prices them, but 66 emitter sites use them as free
  transient scratch, and declaring those clobbers honestly would
  make both registers unallocatable in exactly the dense regions
  that are short — structurally capped, not attempted.  (c)
  Distance-capped GVN at O1 (same-block, same-call-epoch reuse via
  alias classes — the lifetime-safe subset): 85 loads eliminated on
  the frozen TU, noise elsewhere — block dedup already owns the
  local population; GVN's only real harvest is the cross-block kind
  that L42 showed the allocator punishes.  All reverted; tree
  byte-identical to L40.  THE DECOMPOSITION (from the L38 honest
  counters): self 42.36B Ir = 14.3B loads + 11.2B stores + 16.9B
  other; ref 20.86B = 5.8 + 4.7 + 10.4.  Non-memory operations are
  only 1.63x — the 21.5B gap is ~70% MOVEMENT, split roughly evenly
  between an op-count-proportional share (1.63x more operations
  needing operands) and a RATE share (0.84 loads per non-memory op
  vs gcc's 0.56).  CONSEQUENCE FOR P30: closing the rate half alone
  lands ~1.67x; closing the op-count half alone lands ~1.55x; 1.5x
  requires BOTH a midend that removes operations AND placement that
  serves the survivors at gcc's staging rate — with the register
  file measured saturated at simultaneous-overlap level, the
  placement half must come from cheaper spills (region placement,
  rematerialization) rather than more residency.  The next session
  starts here.
- L44 (LAZY FALLBACK-HOME WIN; VALIDATION LADDER RESET).
  Deferring `stabilize_edge_live_result`'s fallback-home allocation
  and store until an actual spill self-hosts correctly at O1 and O3,
  and the instrumented objects reproduce their uninstrumented SHA-256
  exactly.  On the frozen TU it removes 86 temporary homes (-6.6%),
  145 generated scalar stores (-0.92%), and 736 object bytes, but full
  Cachegrind moves only 42.363B to 42.246B Ir (-0.278%) and 25.412B to
  25.291B data references (-0.475%).  It also changes two PA38 pinned
  MIR fixtures (`400-loop-invariant-call-crossing-placement` and
  `410-cyclic-edge-register-pressure`), both strictly improving by
  removing the unused fallback slot/store while preserving behavior.
  ACCEPTED as the first spill-cost reduction feeding P30 gate (iii);
  it is not itself the planned region-boundary placement.  The two
  owning refs were regenerated in place; PA38 is 40/40, the through-PA38
  report is 5,430/5,430, the file audit has zero fatal findings, and
  isolated O3 and O0 inception lanes both MATCH byte-for-byte.  No stale
  Valgrind job survived the paired run.  Lackey basic counting was
  stopped after 10m43s because it was not faster than full Cachegrind.
  Future probes use exact object/MIR/stats census first, Cachegrind with
  cache and branch simulation disabled for Ir-only survivors, and full
  Cachegrind only when the hypothesis needs data references or cache
  behavior; every allocator survivor still runs verified-output
  self-host and the full owning suites.
- L45 (P30a RECORD/REPLAY SEAM LANDED; GATE (i) PRODUCER
  HANDOFF READY).  Every GPR/XMM pool mutation now passes through a
  per-function allocation-decision log carrying operation, instruction
  position, destination value, requested/selected register, call-crossing
  class, and success.  A non-emitting-stats first walk records the CURRENT
  reactive/planned decisions; the emitting walk replays the sequence and
  rejects any context or outcome divergence, so this is a strict oracle
  for replacing the producer without silently changing the emitter.
  Coverage includes fixed and planned reservations, reactive allocation,
  releases/discards, planned holds, and XMM allocation/release.  The gate
  is a pure refactor: the frozen instrumented object remains exactly
  `6a183b784f03134f1323c649a054506ac644fbbfbfbf0977a978fe1629708b34`,
  PA38 is 40/40, the through-PA38 report is 5,430/5,430, the file audit has
  zero fatal findings (36 pre-existing warnings), and isolated O3 and O0
  inception lanes both MATCH byte-for-byte.  The extra first walk is
  transitional: the whole-function location timeline replaces this
  recorded producer incrementally; it is not the final allocator cost
  model.
- L46 (P30 GATE (i) LOCATION TIMELINE LANDED; BYTE IDENTITY HOLDS).
  The planner's parallel `value_register_plan`/`value_plan_end` arrays are
  replaced by an explicit whole-function, per-value sequence of location
  segments.  A segment records `[begin,end]`, location kind (GPR, XMM,
  frame, or rematerialized), and physical index; today's producer emits
  one GPR segment for exactly the previously planned population, including
  phi occupancy from function entry.  Every planned grant, interval-end
  release, release schedule entry, conflict query, and stats classifier now
  reads this representation.  This closes the pure-refactor migration gate
  without reviving L43's disproven multi-use/splitting expansion: the frozen
  object is byte-identical at
  `6a183b784f03134f1323c649a054506ac644fbbfbfbf0977a978fe1629708b34`,
  PA38 is 40/40, the through-PA38 report is 5,430/5,430, the audit has zero
  fatal findings (36 warnings), and isolated O3/O0 inception lanes both
  MATCH.  The open timeline work is now cheaper-spill placement and
  rematerialized gaps, not additional residency.
- L47 (P30 SAFE FRAME-ADDRESS REMATERIALIZATION LANDED; FIRST
  TIMELINE-DRIVEN PLACEMENT WIN).  Fixed stack-slot addresses whose
  transitive copy consumers remain address-safe now receive a
  `rematerialized` timeline segment instead of a GPR lifetime and fallback
  home.  The analysis rejects integer/arithmetic escape through an explicit
  backward copy-use closure; address lowering, call/storage staging, copies,
  and returns consume the frame-relative fact directly.  A PA38 behavior
  reducer covers safe copied call/store/return uses plus an unsafe
  pointer-to-integer arithmetic round trip.  On the frozen TU, against a
  fair L46/current-P30 control (including the record/replay first walk), this
  admits 7,035 addresses, removes all 120 slot-address edge-staging events,
  lowers total edge staging 472 -> 354 (-25.0%), temporary homes 1,225 ->
  863 (-29.6%), scalar temporary movement 48,245 -> 44,813 (-7.1%), MIR
  instructions 137,974 -> 129,570 (-6.1%), and object size 1,461,616 ->
  1,429,720 bytes (-31,896).  Exact Ir-only Cachegrind improves
  44,618,163,231 -> 44,051,899,088 (-1.269%); comparing to L44's 42.246B
  would be invalid because L45's transitional record walk lies between the
  two.  The final object/self-host hash is
  `472b716ccb90649d780c77ae396aa9f58b462450cc1c2fe81f6df66b5b627871`.
  PA38 is 41/41, the through-PA38 report is 5,431/5,431, the audit has zero
  fatal findings (36 warnings), and isolated O1/O3/O0 inception lanes all
  MATCH byte-for-byte.  There is no stale profiler process.  Native hardware
  counters are unavailable here; software QEMU instruction callbacks could
  be an earlier directional gate but are not a Cachegrind Dref substitute.
  Future inception lanes use `INCEPTION_BUILD_JOBS=32`.
- L48 (P30 SHORT REGION-BOUNDARY STAGING PROBE REJECTED).  The first
  post-L47 transition producer deferred an unplanned edge-live GPR's backup
  store from its definition to the next throwing-call/control boundary,
  capped at eight LowIR positions and falling back to the ordinary reactive
  pressure spill.  It exercised the intended timeline/emitter seam and an
  isolated 32-way O1 inception lane matched every object and final binary.
  Static shape improved slightly on the frozen TU (object -208 bytes, MIR
  -54, scalar loads -45, scalar stores -7, frame-home count flat), but ten
  additional pressure spills made the exact Ir-only profile regress
  44,051,899,088 -> 44,069,480,106 (+17,581,018, +0.040%).  A four-position
  cap avoided the extra spills but recovered only 29 MIR instructions and
  18 loads.  Therefore definition-to-first-boundary retention is not the
  missing region placement: useful transitions must start around use
  clusters or rematerialize values without extending residency.  The probe
  was fully reverted and no profiler process remains.
- L49 (P30 SAFE GLOBAL-ADDRESS REMATERIALIZATION LANDED).  The L47
  transitive consumer proof now also identifies non-TLS global addresses
  whose complete copy/use closure remains safe for symbolic
  rematerialization.  At O1+ address lowering keeps those values as direct
  symbol operands instead of manufacturing a GPR lifetime and fallback
  home; TLS addresses and pointer-to-integer arithmetic escapes retain the
  old materialized path.  The PA38 reducer now covers copied global
  call/store/return consumers and an unsafe integer-arithmetic round trip.
  A per-function edge-staging census landed with the change and localized
  the principal win: `Lexer::Run`, which accounts for 2.948B baseline Ir,
  falls from 13 global-address staging events to zero, 31 frame homes to 18,
  688 scalar loads to 671, 670 stores to 647, and 3,610 MIR instructions to
  3,565.  Across the frozen TU, 1,693 global addresses rematerialize; MIR
  falls 129,570 -> 129,466, temporary homes 863 -> 860, scalar loads
  23,741 -> 23,737, scalar stores 15,038 -> 15,025, and object size
  1,429,720 -> 1,429,680 bytes.  Total scalar movement rises by 134 and one
  unrelated edge-staging event moves into the final allocation, so the
  candidate was accepted on the exact dynamic gate rather than static
  appearance: Ir-only Cachegrind improves 44,051,899,088 -> 44,031,791,881
  (-20,107,207, -0.0456%).  The final object/self-host hash is
  `686b87763e6f87b5eea7168df05be57e32933e1427febedc3f7675ac176cb3f8`.
  PA38 is 41/41, the through-PA38 report is 5,431/5,431 with no fixture
  movement beyond the new reducer reference, the audit has zero fatal
  findings (36 warnings), and isolated 32-way O1/O3/O0 inception lanes all
  MATCH byte-for-byte.  No profiler process remains.  This reinforces L48's
  result: profitable boundary work removes rematerializable lifetimes near
  their consumers instead of retaining ordinary values longer.
- L50 (P30 CONSTANT-INDEX ADDRESS REPLAY LANDED).  At O1+, an `INDEX`
  whose index is constant and whose complete use closure remains a storage
  address now records a semantic base-plus-offset recipe instead of
  materializing another pointer lifetime.  The recipe composes through
  nested constant indexes and is replayed at each memory consumer from a
  stable frame, symbol, global, or earlier replayed base.  The first
  self-host exposed the important lifetime constraint directly: stage 1
  linked, but stage 2 crashed because the base frame binding could be reused
  before a replayed consumer.  Extending the analysis lifetime through every
  replay consumer fixed the real cause; the resulting O1/O3/O0 self-hosts
  all match.  On the frozen TU, 1,750 constant-index recipes reduce MIR
  instructions 129,466 -> 126,021 (-3,445, -2.66%), temporary homes 860 ->
  818 (-42), scalar stores 15,025 -> 14,985 (-40), edge staging 355 -> 332
  (-23), and constant-index staging 80 -> 59 (-21).  Scalar loads rise
  23,737 -> 24,067 and total scalar movement rises 44,947 -> 45,738, so the
  exact dynamic gate again decides the survivor: Ir-only Cachegrind improves
  44,031,791,881 -> 43,469,998,821 (-561,793,060, -1.2759%), while object
  size falls 1,429,680 -> 1,427,248 bytes.  The final object/self-host hash
  is `600a404d2994699b39eef17a4432351eef950eb2f5d6ec07f8415431b7f70bcb`.
  The sole existing fixture movement is a strict MIR improvement in PA38's
  cyclic-pressure case (`mov` + `lea` + `load` becomes `mov` + displaced
  `load`); behavior remains identical.  PA38 is 41/41, the through-PA38
  report is 5,431/5,431, the audit has zero fatal findings (34 warnings),
  and isolated 32-way O1/O3/O0 inception lanes all MATCH byte-for-byte.  No
  profiler process remains.  The address replay machinery is separated from
  the emitter so subsequent rematerialized timeline kinds can share the same
  lifetime-safe path.
- L51 (P30 GVN@O1 RE-ARM REJECTED; PLACEMENT IMPROVED BUT IS NOT YET
  SUFFICIENT).  Enabling the existing memory GVN at O1 eliminates 776 loads
  and reduces optimized LowIR 108,170 -> 105,813 (-2,357).  The post-L50
  allocator response is less uniformly bad than L42: scalar temporary
  movement falls 45,738 -> 45,283 (-455), including loads 24,067 -> 23,058
  (-1,009), and edge staging falls 332 -> 330.  But the merged cross-block
  lifetimes still reduce planned grants 5,590 -> 4,170, raise temporary homes
  818 -> 960, spills 51 -> 80, and scalar stores 14,985 -> 15,533; final MIR
  rises 126,021 -> 126,531 and object size rises 1,427,248 -> 1,427,688 bytes.
  The exact apples-to-apples self-host Ir-only profile therefore regresses
  43,469,998,821 -> 44,095,917,884 (+625,919,063, +1.4399%).  The candidate's
  frozen output hash was
  `bff1820939843a68ccae0a11042d04fe863b83271d62bf3c6c5b561105310d7f`;
  PA38 remained 41/41 and its 32-way O1 inception lane matched every object
  and the final binary.  The one-line enablement is reverted, no fixtures
  moved, and no profiler process remains.  This closes the unconditional
  GVN re-arm at the current placement point: a future retry needs either a
  profitability gate that excludes the demoting cross-block cases or another
  material reduction in the home/spill cost of their merged lifetimes.
- L52 (P30 LOOP-GATED INLINE POINT RE-RUN AND REJECTED BY THE FAST
  CENSUS).  The preserved L41 policy was replayed exactly on top of L50:
  calls inside layout backedge spans receive twice the hinted size/nonleaf
  caps, with inherited loop membership for blocks created by a splice.  PA38
  remains 41/41, but the frozen TU fails the pre-Cachegrind survivor gate by a
  wide margin: optimized LowIR rises 108,170 -> 110,141, MIR 126,021 ->
  133,055 (+7,034, +5.58%), scalar temporary movement 45,738 -> 50,139
  (+4,401, +9.62%), loads 24,067 -> 26,116, stores 14,985 -> 16,607,
  temporary homes 818 -> 969, edge staging 332 -> 371, and object size
  1,427,248 -> 1,455,216 bytes (+27,968).  The candidate hash was
  `8315adcb195f0c963f09042bdbec7d52073353b7e75d93ec461360f3b00d1a09`.
  Because every backend cost proxy moved substantially in the wrong
  direction, no Cachegrind or inception run was warranted; the 81-line probe
  is fully reverted and no profiler process exists.  This reproduces L41's
  structural verdict after the P30 placement wins: the current
  rematerialization classes help address lifetimes, but do not make deeper
  merged inline bodies cheap enough.  Broader inlining remains closed until
  non-address scalar placement changes materially.
- L53 (P30 EDGE-LIVE IDENTITY-COPY HOMES LANDED).  The first remaining
  non-address scalar slice removes a redundant register round trip for
  integer/pointer identity copies in EH functions.  When the destination is
  an unplanned edge-live value that must be stabilized, lowering now stores
  the already-normalized source register directly into the destination's
  frame home instead of allocating a transient destination register and
  immediately spilling it.  A prior attempt to alias an existing spill home
  had zero population on the frozen TU and was reverted byte-identically;
  direct placement fires 16 times.  Against L50, frozen MIR falls 126,021 ->
  125,866 (-155), scalar temporary movement 45,738 -> 45,624 (-114), loads
  24,067 -> 24,017 (-50), stores 14,985 -> 14,933 (-52), register copies
  1,946 -> 1,934 (-12), edge staging 332 -> 328 (-4), and object size
  1,427,248 -> 1,426,888 bytes (-360).  Temporary homes and spills remain
  flat at 818 and 51.  The exact self-hosted Ir-only profile, including the
  identity-representation guard, confirms the small strict win,
  43,469,998,821 -> 43,469,739,908 (-258,913, -0.000596%); the frozen and
  self-hosted objects both hash to
  `ac93da5fe2a18932fd592da4591ebe5a24f6596ed8a72a80b9a10ede1343403d`.
  PA38 is 41/41 with no fixture changes, the through-PA38 report is
  5,431/5,431, the audit has zero fatal findings (34 warnings), and isolated
  32-way O1/O3/O0 inception lanes all MATCH byte-for-byte.  No profiler
  process remains.  The result validates the fast census as a rejection gate
  while retaining exact Ir-only Cachegrind as the acceptance gate for small
  scalar-placement survivors.
- L54 (P30 THREE-OPERAND 64-BIT ADD SELECTION LANDED).  An O1+ integer or
  pointer add whose destination cannot take over its left source now folds
  the lowering-created `mov destination, left; add destination, right` into
  one `lea destination, [left+right]` when the right source is a GPR or a
  signed-32-bit displacement.  This removes work without retaining either
  source longer or changing PA29's O0 MIR contract.  The frozen TU selects
  14 such adds: MIR falls 125,866 -> 125,852, scalar-temporary register
  copies fall 1,934 -> 1,920, and object size falls 1,426,888 -> 1,426,808
  bytes; scalar movement, loads, stores, edge staging, homes, and spills are
  otherwise flat.  The small static population is hot: exact self-hosted
  Ir-only Cachegrind improves 43,469,739,908 -> 43,444,897,460
  (-24,842,448, -0.057149%).  Host and self-host frozen objects are
  byte-identical at
  `7891f9f2a5a4c36d1d2fd73b10ca8dfd1fa78c3074e29447624ad927a17ffb27`.
  The two PA38 fixture changes are strict MIR improvements, replacing the
  same `mov`+`add` pairs with `lea`; both generated programs remain exact.
  PA38 is 41/41, the through-PA38 report is 5,431/5,431, the audit has zero
  fatal findings (36 warnings), and isolated 32-way O1/O3/O0 inception lanes
  all MATCH byte-for-byte.  No profiler process remains.  Validation timing
  establishes the forward loop: the frozen compile/census takes about 5.4 s
  and rejects non-survivors before profiling, while the exact Cachegrind run
  took 404.84 s.  Count-only Callgrind was slower (only 11.73B Ir after
  105.18 s) and was stopped.  QEMU is not installed here; full-system TCG
  does not provide useful host retired-instruction counters, though a future
  user-mode count-only TCG plugin could serve as a directional, not final,
  gate.
- L55 (P30 STAGED SCALAR CALL RESULTS LAND DIRECTLY FROM RAX).  An
  unplanned scalar call result in an EH function previously took the path
  `RAX -> transient allocated register -> edge-live frame home`, even when
  the post-instruction stabilizer was certain to create that home.  Lowering
  now performs the same allocation attempt to preserve the allocator's
  pressure and record/replay decisions, but releases a successful transient
  reservation and leaves eligible results in RAX for the existing stabilizer
  to store.  Planned-register and exact-forward results retain their prior
  placement, and the stabilizer no longer records no-op releases for
  unmanaged fixed registers.  The frozen TU takes this path 25 times:
  call-boundary register copies and machine-optimizer input each fall by 25,
  final MIR falls 125,852 -> 125,832, and object size falls 1,426,808 ->
  1,426,744 bytes.  Temporary homes, spills, edge staging, and scalar
  temporary movement remain exactly flat at 818, 51, 328, and 45,624.
  Exact self-hosted Ir-only Cachegrind improves 43,444,897,460 ->
  43,440,018,508 (-4,878,952, -0.011230%).  Host and self-host frozen objects
  are byte-identical at
  `a5ebb16c106c4e46daf87b6c26b2cc79d2830f435cb10035513597c9dfd51caf`.
  PA38 is 41/41 with no fixture changes, the through-PA38 report is
  5,431/5,431, the audit has zero fatal findings (36 warnings), and isolated
  32-way O1/O3/O0 inception lanes all MATCH byte-for-byte.  No profiler
  process remains.  A temporary census also closes cheap rematerialization
  of the remaining 30 staged binary values as the next slice: they comprise
  only four adds and one subtract, versus three multiplies, eleven
  divide/remainder operations, and eleven shifts; the census was fully
  reverted.
- L56 (P30 FINAL-USE LOAD ADDRESSES BECOME THEIR RESULTS).  A census of
  the 127 remaining staged scalar loads found 124 temporary addresses,
  but none consumed within four instructions and only 39 within sixteen;
  broad load folding therefore has little adjacent population.  Seventeen
  loads do, however, consume a temporary pointer for the final time while
  producing an unplanned edge-live result.  At O1+ in EH functions, those
  loads now use the dead address register as their destination: x86 reads
  the effective address before replacing that register, after which the
  existing stabilizer places the result.  The source must not be edge-live,
  must have no aliases, and must pass the existing destructive-reuse test;
  planned and exact-forward results are excluded.  The older phi-home
  address takeover retains its original predicate and is deliberately not
  included in the new counter.  An earlier unconditional generalization
  fired 786 times but raised homes 818 -> 826 and edge staging 328 -> 392;
  the fast census rejected that allocation disruption before profiling.
  The narrow survivor fires 17 times.  Frozen MIR stays 125,832, scalar
  temporary movement stays 45,624 (24,017 loads, 14,933 stores, and 1,920
  register copies), and homes/spills stay 818/51.  Edge-staging
  classification moves 328 -> 330 without adding MIR, while real generated
  sequences such as `lea address; load result; store result` become
  `load address-as-result; store result`; object size falls 1,426,744 ->
  1,426,720 bytes.  Exact self-hosted Ir-only Cachegrind improves
  43,440,018,508 -> 43,437,250,755 (-2,767,753, -0.006371%).  Host,
  self-host, and profiled frozen objects are byte-identical at
  `f496c227be1db4007a24af5bd5d437804cc7a1c19b117cfd3c0bb1ba6e919693`.
  PA38 is 41/41 with no fixture movement, the through-PA38 report is
  5,431/5,431, the audit has zero fatal findings (36 warnings), and isolated
  32-way O1/O3/O0 inception lanes all MATCH byte-for-byte.  No profiler
  process remains.  The fast frozen compile/census again selected the sole
  small survivor in about 5.5 s; exact profiling remained the final
  profitability gate rather than a search loop.
- L57 (P30 STAGED NONDESTRUCTIVE COMPARE PROBE REJECTED BY THE FAST
  GATE).  Six of the eleven staged compare results can compare their
  original left register directly instead of first copying it into the
  register that receives `setcc`.  The probe reduces machine-optimizer
  input and scalar-temporary register-copy counts by six, but the existing
  optimizer already removes all six copies: final MIR remains 125,832,
  object size remains 1,426,720 bytes, and the frozen object remains exactly
  `f496c227be1db4007a24af5bd5d437804cc7a1c19b117cfd3c0bb1ba6e919693`.
  Because the added selection predicate would execute across the full
  compare population for zero generated-code change, the probe is fully
  reverted without Cachegrind or inception.  This closes the compare class
  as an emitter-level P30 target; a future win there must improve the final
  machine optimizer, not merely its input.
- L58 (P30 STAGED INTEGER-CONVERSION SOURCE REUSE REJECTED BY THE EXACT
  GATE).  Of seven staged conversion results, one final-use, non-edge-live
  source register can safely hold its own converted result.  The probe makes
  the normal allocation attempt and releases its successful transient
  reservation before converting in place, preserving pressure/replay shape.
  It removes a real `movzbl` in `note_function_call_source_event`: final MIR
  falls 125,832 -> 125,830 and object size falls 1,426,720 -> 1,426,704 bytes,
  while homes, spills, grants, and movement stay flat.  PA38 remains 41/41
  and the 32-way O1 inception lane MATCHES, but exact self-hosted Ir-only
  Cachegrind regresses 43,437,250,755 -> 43,439,419,512 (+2,168,757,
  +0.004993%): the full compare-and-facts predicate costs more in the
  compiler than the single emitted instruction saves.  The measured object
  was verified at
  `880e89455a86d1291ed96c985516a6d4d5cbd0c6f04374a72facee7ee432de6c`.
  The implementation and counter are fully reverted, no further inception
  lanes are warranted, and no profiler process remains.  This closes the
  current staged-conversion population unless a materially cheaper shared
  placement predicate appears.
- L59 (P30 FAILED-PLAN CALL-RESULT RAX RETENTION REJECTED BY THE EXACT
  GATE).  A temporary register census split the 62 staged call results into
  25 already in RAX (the L55 population) and 37 in another GPR.  Most of the
  latter have a timeline register whose grant failed: L55 excluded them
  merely because a plan existed, even though the allocated fallback was not
  the planned register.  Preserving only allocations that actually equal the
  plan recovers 35 more direct-RAX results (60 total), reduces call-boundary
  copies 5,795 -> 5,760 and machine-optimizer input 145,864 -> 145,829,
  reduces final MIR 125,832 -> 125,818, and shrinks the object 1,426,720 ->
  1,426,688 bytes with homes/spills/grants flat.  PA38 remains 41/41 and the
  32-way O1 inception lane MATCHES.  Nevertheless exact self-hosted Ir-only
  Cachegrind regresses 43,437,250,755 -> 43,441,601,216 (+4,350,461,
  +0.010016%); changing these fallback locations costs more in the compiler
  itself than the frozen target-copy reduction saves.  The measured object
  was verified at
  `778192a56765564e348569e0025c4d7cd30f48308c0095e1613f7090855ac229`.
  The four-line generalization and temporary census are fully reverted, no
  further inception lanes are warranted, and no profiler process remains.
  L55's stricter no-plan gate therefore remains the measured optimum.
- L60 (P30 FIXED DIVIDE/REMAINDER RESULT STAGING REJECTED BY THE EXACT
  GATE).  Nine of the eleven staged divide/remainder values can leave the
  quotient or remainder in its architectural RAX/RDX result register for the
  existing stabilizer, instead of copying through the transient destination
  selected for dividend setup.  The narrow no-plan probe reduces
  machine-optimizer input and scalar-temporary register copies by nine,
  leaves final MIR flat at 125,832, and shrinks the frozen object 1,426,720
  -> 1,426,592 bytes; homes, spills, and grants remain flat.  PA38 remains
  41/41 and the 32-way O1 inception lane MATCHES.  Exact self-hosted Ir-only
  Cachegrind nevertheless regresses 43,437,250,755 -> 43,438,369,171
  (+1,118,416, +0.002575%), so the fixed-register ownership/allocation change
  is dynamically worse despite the smaller target object.  The measured
  object was verified at
  `515e3df3617b8400b9447660d985c7427fc57b20228c3c9c10a80f20bab37544`.
  The implementation and counter are fully reverted, no further inception
  lanes are warranted, and no profiler process remains.  Fixed-result
  staging is closed at the current division lowering; a future division win
  must simplify setup itself rather than only its final copy.
- L61 (P30 RESIDUAL STAGED COPY CLASS CLOSED BY CENSUS).  All 19 copy-defined
  values that still reach edge stabilization preserve representation exactly.
  Fifteen already alias their stable source register through copy lowering;
  stabilization stores that register directly, so they contain no transient
  result copy to remove.  The other four read a frame-resident source and
  necessarily use a register between the load and destination-home store
  because x86 has no general memory-to-memory move.  This population is
  complementary to L53's 16 direct-home copies, which are register sources
  that could not remain aliases across the destination lifetime.  The
  temporary five-counter census reproduced the exact L56 frozen object at
  `f496c227be1db4007a24af5bd5d437804cc7a1c19b117cfd3c0bb1ba6e919693`
  and was fully reverted.  No Cachegrind or inception run is warranted and
  no profiler process exists.  Copy-result staging is therefore exhausted
  at the emitter level.
- L62 (P30 SPILL-SAFETY CYCLE QUERIES PRECOMPUTED).  Native spill selection
  asked whether the current block was cyclic from several hot paths.  The
  answer was previously obtained by building and caching the complete set of
  blocks reachable from each selected block, even when no later spill-use
  query needed that set.  `ControlFlowQueries` now classifies cyclic blocks
  once with strongly-connected components, including singleton self-edges,
  and answers the exact same predicate in constant time.  The independent
  reachability cache remains available for the prior-use safety test, while
  layout-backedge coverage is rejected before either graph query.  The fast
  gate reproduced the accepted frozen object byte-for-byte at
  `f496c227be1db4007a24af5bd5d437804cc7a1c19b117cfd3c0bb1ba6e919693`
  (1,426,720 bytes), with MIR and all movement/allocation counters unchanged.
  Four alternating native self-host runs reduced mean user time 10.385 s ->
  9.930 s (-4.38%).  Exact self-hosted Ir-only Cachegrind improves
  43,437,250,755 -> 41,954,137,863 (-1,483,112,892, -3.414380%): attributed
  `CurrentBlockReaches` work falls from 1,017,977,702 to 2,815,420 Ir, while
  the one-time component traversal costs 12,489,122 Ir.  PA38 is 41/41, the
  through-PA38 report is 5,431/5,431, the audit has zero fatal findings
  (36 warnings), and isolated 32-way O1/O3/O0 inception lanes all MATCH
  byte-for-byte.  No profiler process remains.  The native alternating-run
  gate is retained as the sub-minute directional check before exact
  Cachegrind for future survivors.
- L63 (P30 TRANSITIONAL ALLOCATION REPLAY WALK RETIRED).  Gate (i)'s
  allocation-decision record/replay seam had completed its migration role,
  but production lowering still constructed and walked every function twice:
  a stats-free first walk recorded the current deterministic allocator, then
  an identical second walk replayed it and supplied the emitted MIR.  Normal
  lowering now emits the single deterministic walk directly with decision
  logging disabled; the optional log hooks remain available for focused
  validation.  This changes no location, allocation, MIR, movement, or object
  counter: the frozen object remains 1,426,720 bytes and byte-identical at
  `f496c227be1db4007a24af5bd5d437804cc7a1c19b117cfd3c0bb1ba6e919693`.
  Four alternating native self-host runs reduce mean user time 9.930 s ->
  9.588 s (-3.44%).  Exact self-hosted Ir-only Cachegrind improves
  41,954,137,863 -> 40,512,780,696 (-1,441,357,167, -3.435554%);
  `analyze_function` falls 361,229,362 -> 180,614,681 Ir and control-flow
  construction falls 43,778,542 -> 21,889,271 Ir.  PA38 is 41/41, the
  through-PA38 report is 5,431/5,431, the audit has zero fatal findings
  (36 warnings), and isolated 32-way O1/O3/O0 inception lanes all MATCH
  byte-for-byte.  No profiler process remains.
- L64 (P30 RESIDUAL LOAD-HOME AND EH-RETENTION PROBES CLOSED BY THE FAST
  GATE).  A temporary source-kind census split the 129 remaining staged
  loads into 126 loads through temporary pointer values and only three
  direct slot loads; there is no stable source-frame home to alias in the
  dominant population, and the census was fully reverted.  Separately,
  allowing call-free edge values to remain in registers merely because
  their EH-bearing function's exception machinery lies elsewhere retained
  ten more values, but displaced nine planned grants, added one frame home,
  and raised final MIR 125,832 -> 125,842 despite reducing loads by 15 and
  object size by 96 bytes.  Restricting the relaxation to single-use,
  non-loop values had zero final population and reproduced every L63 metric
  and the frozen hash exactly.  Both retention forms were reverted without
  Cachegrind or further inception; the accepted tree and object are restored
  byte-for-byte, and no profiler process exists.
- L65 (P30 SIMPLIFIER CENSUS FUSION REJECTED; THE FAST GATE GETS FASTER).
  `simplify_values_with_analysis` separately counts instructions, classifies
  storage-address temporaries/EH, and indexes blocks/phi uses.  Fusing those
  whole-function censuses preserved every optimizer and native counter, and
  reproduced the accepted 1,426,720-byte frozen object at
  `f496c227be1db4007a24af5bd5d437804cc7a1c19b117cfd3c0bb1ba6e919693`, but
  it did not reduce host-side simplifier time (164.101 ms -> 164.365 ms) and
  four ABBA native samples were slightly worse at 9.555 s -> 9.605 s mean
  user time (+0.52%).  The change was therefore reverted without Cachegrind
  or inception.  For future one-source candidates, PA39's `probe-self-link`
  now supplies the native A/B compiler by rebuilding and substituting only
  the touched self-host object: this candidate linked in 3.7 s instead of
  rebuilding the full checkpoint.  Full inception remains a survivor gate,
  with `INCEPTION_BUILD_JOBS=32` required on every run.  No profiler process
  remains.
- L66 (P30 ACTIVE MIR ALIAS FACTS LANDED; THE ISOLATED BACKEND GATE PAYS).
  PA38's local MIR rewrite invalidated aliases by scanning all 16 GPR and
  eight XMM fact records after every instruction, although only a small sparse
  subset is live at once.  `LocalFacts` now maintains compact active-index
  arrays, compacts those arrays under the identical destination/source
  invalidation predicates, and records facts through bounded `set_gpr` and
  `set_xmm` paths.  MIR, allocation/movement counters, function census, and
  the frozen 1,426,720-byte object remain byte-identical at
  `f496c227be1db4007a24af5bd5d437804cc7a1c19b117cfd3c0bb1ba6e919693`.
  Host-side machine optimization falls 35.549 ms -> 31.088 ms (-12.6%) and
  total lowering 210.775 ms -> 205.946 ms.  More usefully, a 9.1-MiB
  unoptimized serialized-LowIR snapshot isolates the optimizer/backend from
  frontend noise: four ABBA samples improve 3.385 s -> 3.345 s mean user time
  (-1.18%), with all eight objects identical.  This is the new sub-minute
  native direction gate after `probe-self-link`.  Exact self-hosted Ir-only
  Cachegrind improves 40,512,780,696 -> 40,401,605,545 (-111,175,151,
  -0.274420%); `rewrite_local_operands` itself falls 189,049,950 ->
  77,740,269 Ir, accounting for the result.  PA38 is 41/41, the through-PA38
  report is 5,431/5,431, the audit has zero fatal findings (36 warnings), and
  isolated O1/O3/O0 self/inception lanes all MATCH byte-for-byte.  Every
  inception build used both outer `-j32` and `INCEPTION_BUILD_JOBS=32`.  No
  profiler process remains.
- L67 (P30 DOMINATED POST-CALL USE TAILS LAND; GATE (iii) GAINS A REAL
  REGION TRANSITION).  An edge-live scalar that has already been staged to
  its frame home may now acquire a second, use-local GPR timeline segment
  after its final call.  The first tail-use block must dominate every later
  tail use, every such block must be acyclic, the segment must avoid LowIR
  clobbers and existing R8/R9 plans, and a busy boundary register simply
  keeps the old frame path.  The emitter therefore pays one boundary reload
  and retains the value through multiple dominated uses without extending
  residency across a call, loop, or unwind edge.  The frozen TU finds seven
  candidates, assigns four, promotes all four, and records zero busy
  fallbacks.  Against L66, final MIR falls 125,832 -> 125,829, machine input
  145,864 -> 145,861, scalar loads 24,017 -> 24,014, total scalar movement
  45,624 -> 45,623, and the object shrinks 1,426,720 -> 1,426,688 bytes;
  homes, spills, stores, and copies are unchanged.  The first correct form
  repeatedly scanned all timeline segments and cost +282,025 exact Ir even
  though its generated object improved.  A lazy two-register conflict index
  preserves the object at
  `f3073bb15df14b8b4280d23e1445ffdec40616b5962d0a47b5d1ad971092779c`
  while recovering 3,132,651 Ir from that implementation; final exact
  self-hosted Ir-only Cachegrind is 40,398,754,919 versus L66's
  40,401,605,545 (-2,850,626, -0.007056%).  No fixture changed: PA38 is
  41/41, the through-PA38 report is 5,431/5,431, and the audit has zero
  fatal findings (36 warnings).  O1/O3/O0 self/inception lanes all MATCH;
  every build used both outer `-j32` and `INCEPTION_BUILD_JOBS=32`.  No stale
  profiler remains.  Future iteration uses the serialized LowIR compile
  (about 1.9 s natively) for deterministic object/MIR/stats checks and native
  alternating timing for direction; full-TU Ir-only Cachegrind (about 388 s)
  is reserved for milestone survivors.  The isolated Cachegrind total is
  faster (about 38 s/sample) but too variable across separately built host
  binaries to reject a deterministic codegen improvement at the 0.001%
  scale.  This is concrete progress on gate (iii), not closure: the original
  definition-time backup store remains, while the newly supported second
  timeline segment eliminates repeated reloads inside a proven use region.
- L68 (P30 USE-TAIL CALLER POOL EXHAUSTED; ADJACENT REGION CLASSES
  CLOSED).  Post-call tails cannot cross another call, so the same indexed
  conflict test can safely consider all four ordinary caller registers used
  by native allocation: R9, R8, RDI, and RSI.  Existing clobber and timeline
  facts still price each choice, and a register busy at the promotion
  boundary still falls back to the frame path.  On the frozen source TU the
  wider pool assigns all seven candidates, promotes six, and encounters one
  busy boundary, versus four assignments/promotions with the two-register
  pool.  Final MIR falls 125,829 -> 125,826 and the object shrinks 1,426,688
  -> 1,426,656 bytes.  Scalar loads fall 24,014 -> 24,013 while register
  copies rise 1,920 -> 1,921, leaving total scalar movement flat at 45,623;
  homes, spills, and stores remain 818, 51, and 14,933, while planned grants
  rise 5,585 -> 5,587.  Host and rebuilt O1 self-host outputs agree at
  `200cee5bc5a0e50297875289038b6a706fe2fe131df0540306237d2f463120f6`.
  The serialized-LowIR gate independently gains three MIR instructions and
  16 object bytes; four native ABBA samples improve mean user time 1.6025 s
  -> 1.5900 s (-0.78%), with stable output hashes in every lane.  Two nearby
  extensions are now closed by reverted fast probes: admitting acyclic
  loop-exit tails found zero new candidates, while dominated regions between
  calls found only two qualifying values, too little population to justify a
  demotion schedule and alias-release state.  No fixture changed: PA38 is
  41/41, the through-PA38 report is 5,431/5,431, and the audit has zero fatal
  findings (36 warnings).  O1/O3/O0 self/inception lanes all MATCH, and every
  build used both outer `-j32` and `INCEPTION_BUILD_JOBS=32`.  No profiler
  process remains.  Per L67's gate policy, another full-source Cachegrind is
  deferred until the next combined milestone: this bounded pool extension is
  accepted on reproducible generated-code improvement, the isolated backend
  check, native direction, and the full correctness/inception matrix rather
  than spending about 388 s to adjudicate a sub-basis-point compiler effect.
- L69 (P30 LIFETIME-BOUNDED GVN@O1 RE-ARM REJECTED; GATE (iv) REMAINS
  CLOSED).  L51's unconditional GVN merged short loads into long edge-live
  lifetimes.  The narrow retry therefore accepted a redundant load only when
  its dominating value already had an original layout use at least as late as
  every use of the removed load, so the replacement could not lengthen that
  layout lifetime.  This is a real generated-code win, not fixture noise: it
  eliminates 47 loads on the frozen source TU, reduces final MIR 125,826 ->
  125,664, shrinks the object 1,426,656 -> 1,425,904 bytes, reduces spills
  51 -> 46 and homes 818 -> 815, and lowers scalar loads/stores/copies
  24,013/14,933/1,921 -> 23,957/14,896/1,909.  The candidate object is stable
  at `e9e0b8cb6cc78043266166e60617432dc980c6b20fe4c0114cc844783a0749e3`.
  PA38 remains 41/41, the through-PA38 report remains 5,431/5,431, the audit
  has zero fatal findings (36 warnings), and O1/O3/O0 self/inception lanes all
  MATCH; every inception build used both outer `-j32` and
  `INCEPTION_BUILD_JOBS=32`.  But repeated native backend ABBA initially
  regresses about 1.4%, and the combined L68 + retry full-source Ir-only
  profile is 40,448,238,854 versus L67's exact 40,398,754,919 (+49,483,935,
  +0.12250%).  Unlike a sub-0.001% fluctuation, this is material and agrees
  with native direction.  A final rescue prefiltered functions before EH,
  dominance, and memory-SSA construction and required multiple predicted
  non-extending pairs.  It retained 24 source-TU eliminations, five fewer
  spills, five fewer homes, 107 fewer MIR instructions, and a 336-byte object
  reduction.  Even that bounded form remains slower in eight native ABBA
  pairs, 1.59875 s -> 1.60750 s mean user time (+0.55%), and a paired isolated
  backend Cachegrind measures 4,570,797,619 -> 4,583,402,108 Ir (+12,604,489,
  +0.27576%), far outside the observed sub-million isolated-run noise.  The
  implementation and every census hook are fully reverted; the L68 source
  object is restored exactly at
  `200cee5bc5a0e50297875289038b6a706fe2fe131df0540306237d2f463120f6`,
  and no profiler remains.  GVN's harvest is genuine, but the current full
  memory-SSA engine cannot amortize an O1 invocation even after placement
  protects the selected lifetimes.  A future retry needs a genuinely lighter
  cross-block load-forwarder, not another profitability wrapper around this
  pass.
- L70 (P30 DOMINATED PRE-CALL USE REGIONS REJECTED; THE DEMOTION SEAM IS
  PROVEN BUT DOES NOT AMORTIZE).  L67's post-final-call tail has a symmetric
  candidate before the first call: retain the mandatory definition-time frame
  backup, reload once at the first dominated acyclic use, keep the value in a
  caller GPR through the local use region, then return compiler location state
  to the still-valid frame home without another store.  The broad census finds
  31 source-TU and 44 serialized-LowIR regions.  Its correct implementation
  assigns all 31 source regions, promotes and demotes 28, and takes three busy
  fallbacks.  It is a genuine target improvement: final MIR falls 125,826 ->
  125,802, scalar loads fall 24,013 -> 23,986, total scalar movement falls
  45,623 -> 45,615, and the object shrinks 1,426,656 -> 1,426,336 bytes at
  `dff3147d3ba44f5883853d9921f19c552270935f5b8c70c00d81bc6ece14519e`.
  The trade is 19 extra register copies and two extra stores.  A PA36 hosted
  `vector<bool>` reducer exposed the necessary completed-release guard: a
  prefix whose final counted use had already released its register must not be
  demoted a second time.  With that semantic fix, PA38 is 41/41, the through
  report is 5,431/5,431, the audit has zero fatal findings (36 warnings), and
  both O3 and O1 self/inception lanes MATCH object-by-object and at the final
  compiler.  Every inception build used outer `-j32` and
  `INCEPTION_BUILD_JOBS=32`; no fixture changed.  Native timing is correctly
  treated as inconclusive: one eight-sample ABBA set improves 1.6075 s ->
  1.5900 s, while a second set containing a 1.69 s outlier reverses direction
  to 1.60875 s -> 1.62625 s.  Paired isolated backend Cachegrind is decisive at
  4,570,651,256 -> 4,572,041,769 Ir (+1,390,513, +0.030423%), and the combined
  L68 + prefix exact self-source milestone is 40,407,746,805 versus L67's
  40,398,754,919 (+8,991,886, +0.022258%).  A profitability rescue requiring
  three pre-call uses keeps 23 source regions, promotes every one, removes the
  two stores, reduces final MIR by 25, loads by 24, and the object by 288 bytes,
  but is still worse in the isolated gate at 4,572,637,618 Ir (+1,986,362,
  +0.043459%).  The no-inline packaging rescue is independently 90,172 Ir
  worse in a one-object self-compiler comparison.  The implementation,
  demotion state, generalized query, and counters are therefore fully
  reverted; the L68 source object is restored exactly at
  `200cee5bc5a0e50297875289038b6a706fe2fe131df0540306237d2f463120f6`,
  and no profiler remains.  Pre-first-call residency needs either a transition
  mechanism shared with a larger allocator change or a materially larger use
  population; this standalone region class is closed.
- L71 (P30 VARIABLE-INDEX FINAL-USE TAKEOVER REJECTED; TARGET WIN IS TOO
  SMALL TO AMORTIZE).  The residual staging census contains nine variable
  `INDEX` results.  Sixteen unplanned edge-live definitions reach the
  materializer, but only nine are actually staged; four of those nine have a
  safe final-use operand register that x86 `lea` may overwrite (three bases,
  two indexes, with one overlap), and all four are predictably in EH
  functions.  An EH-only implementation therefore leaves the seven
  reactively retained definitions untouched.  It is a real but tiny target
  improvement: the source fixture falls 125,826 -> 125,824 MIR instructions
  and 1,426,656 -> 1,426,632 object bytes; the isolated serialized fixture
  falls 1,540,192 -> 1,540,152 bytes.  Two pressure-homed results move into
  the ordinary edge-staging path, so the staging census rises 330 -> 332 and
  its scalar-temporary store classification rises 14,933 -> 14,935 even
  though the net instruction count falls.  The deterministic isolated backend
  gate rejects the added lowering predicates: Cachegrind rises from L68's
  4,570,651,256 to 4,571,298,015 Ir (+646,759, +0.01415%).  This is precisely
  why P30 does not accept a locally cleaner MIR fixture by itself: the
  self-hosted compiler executes the lowering machinery on every applicable
  index, while only four sites benefit.  The implementation and census hooks
  are fully reverted, the L68 source object is restored exactly at
  `200cee5bc5a0e50297875289038b6a706fe2fe131df0540306237d2f463120f6`,
  and no profiler remains.  Variable-index takeover is closed unless it can
  share an already-paid destination-selection predicate.
- L72 (P30 FIXED-OPERAND RESOLUTION DISPATCH LANDED; THE FAST GATE FINDS A
  SHARED COST WIN).  L68's exact profile placed
  `resolve_instruction_operands` at 281,166,504 Ir: the simplifier built a
  three-pointer array and ran a counted loop for every instruction merely to
  visit the fixed `first`, `second`, and `third` operands.  The fixed visits
  are now explicit.  The first and second arms preserve exactly the existing
  load/store address rule (a forwarded literal may not replace a temporary
  storage address), the third remains an ordinary resolution, and the
  variable argument loop is unchanged.  This is semantics-neutral compiler
  work removal: source and serialized-LowIR MIR, movement, allocation, and
  object metrics remain identical, at hashes
  `200cee5bc5a0e50297875289038b6a706fe2fe131df0540306237d2f463120f6`
  and `ba6231e2a3260262e1d165637cb700e1a0a8b550f9623f5098f56d0fc6ca3130`.
  The sub-minute isolated backend gate improves 4,570,651,256 ->
  4,549,247,208 Ir (-21,404,048, -0.46829%).  The exact O1 self-hosted source
  gate confirms 40,398,754,919 -> 40,279,693,240 Ir (-119,061,679,
  -0.294716%): the resolver itself falls to 161,467,981 Ir (-119,698,523,
  -42.57%), while `resolve_operand` remains 76,987,804 and the simplifier's
  semantic body remains 566,732,995.  Eight-per-side native ABBA is correctly
  treated as inconclusive at 1.6025 s -> 1.60625 s mean user time (+0.23%),
  inside the established wall noise floor.  PA38 is 41/41, the through-PA38
  report is 5,431/5,431, and the audit has zero fatal findings (36 warnings).
  O3, O1, and O0 self/inception lanes all MATCH object-by-object and at the
  final compiler; every lane used outer `-j32` and
  `INCEPTION_BUILD_JOBS=32`.  No fixture changed and no profiler remains.
  This does not claim another gate-(iii) target-code reduction; it removes a
  shared optimizer tax that made small P30 placement candidates harder to
  amortize, and demonstrates the intended frozen-LowIR-first validation loop.
- L73 (P30 NATIVE FIXED-USE CLASSIFICATION DISPATCH LANDED; THE FAST GATE
  EXTENDS BEYOND THE OPTIMIZER).  Native whole-function analysis visits each
  instruction's fixed `first`, `second`, and `third` operands after ordinary
  liveness to classify edge/use roles and again to classify storage-address
  roles.  Both classification visits used three-pointer arrays and counted
  loops.  They are now explicit fixed visits while the variable argument
  loops and every role predicate remain unchanged.  Source and serialized
  LowIR outputs remain byte-identical at
  `200cee5bc5a0e50297875289038b6a706fe2fe131df0540306237d2f463120f6`
  and `ba6231e2a3260262e1d165637cb700e1a0a8b550f9623f5098f56d0fc6ca3130`.
  The isolated serialized gate improves 4,549,247,208 -> 4,546,884,565 Ir
  (-2,362,643, -0.051935%), with `analyze_function` falling 50,700,058 ->
  47,758,932 Ir (-2,941,126, -5.80%).  The exact O1 self-hosted source gate
  confirms 40,279,693,240 -> 40,267,653,423 Ir (-12,039,817, -0.029891%);
  `analyze_function` falls 180,614,681 -> 167,159,737 Ir (-13,454,944,
  -7.45%).  Eight-per-side serialized native ABBA is correctly inconclusive
  at 1.61625 s -> 1.62125 s mean user time (+0.31%), inside the established
  wall noise floor.  Unrolling adjacent fixed-slot loops in `analyze_storage`
  was also tested, but the combined gate rose 33,012 Ir relative to the
  smaller candidate, so that extension was reverted.  PA38 is 41/41, the
  through-PA38 report is 5,431/5,431, and the audit has zero fatal findings
  (36 warnings).  O3, O1, and O0 self/inception lanes all MATCH every object
  and the final compiler; every run used outer `-j32` and
  `INCEPTION_BUILD_JOBS=32`.  No fixture changed and no profiler remains.
- L74 (P30 ONE-BLOCK CFG SHORTCUT REJECTED; LOCAL PASS SAVINGS DO NOT PAY
  THE SELF-COMPILER LAYOUT TAX).  `cleanup_cfg` probes two cross-block folds
  before its existing one-block exit even though neither can fire with one
  block.  Three byte-identical forms tested that fact against L73's isolated
  4,546,884,565-Ir control.  Extracting terminal folding and moving the full
  one-block exit forward reduced `cleanup_cfg` 114,671,406 -> 112,445,382 Ir,
  but total work rose to 4,548,034,526 (+1,149,961, +0.025291%).  A minimal
  caller guard rose to 4,548,372,529 (+1,487,964, +0.032725%), and one-block
  guards inside the two helpers rose to 4,548,596,027 (+1,711,462,
  +0.037640%).  Every form reproduced the serialized object at
  `ba6231e2a3260262e1d165637cb700e1a0a8b550f9623f5098f56d0fc6ca3130`.
  All code was reverted; the hosted compiler is byte-identical to the L73
  control.  The fast gate therefore closes this shortcut without an exact
  source run, correctness rerun, or inception, and no profiler remains.
- L75 (P30 PROMOTED-SLOT FIXED RESOLUTION REJECTED BY THE EXACT GATE).
  `rewrite_promoted_slots` used the same three-pointer counted-loop shape that
  L72 profitably removed from the main simplifier.  Explicit fixed `first`,
  `second`, and `third` resolution reproduced both frozen outputs exactly and
  improved the isolated serialized gate 4,546,884,565 -> 4,542,983,925 Ir
  (-3,900,640, -0.085787%); the rewrite body itself fell 20,262,177 ->
  16,507,953 Ir (-18.5%).  The exact O1 source gate reversed the verdict:
  40,267,653,423 -> 40,269,922,059 Ir (+2,268,636, +0.005634%), even though
  the exact rewrite body fell 46,363,337 -> 45,405,722 Ir.  The smaller
  source-side population does not amortize the changed self-compiler object
  layout.  The code was reverted and the hosted compiler is byte-identical
  to L73.  No correctness or inception run was warranted, no fixture changed,
  and no profiler remains.
- L76 (P30 SAME-BLOCK DOMINANCE BYPASS REJECTED BY THE FAST GATE).
  `resolve_operand` asks the dominator tree whether each known replacement's
  defining block dominates the current block even when those block indices
  are equal, a case where `DominatorTree::dominates` immediately returns true.
  Bypassing the call only for equal blocks preserved the frozen serialized
  object exactly at
  `ba6231e2a3260262e1d165637cb700e1a0a8b550f9623f5098f56d0fc6ca3130`
  and reduced the isolated `resolve_operand` body to 34,315,394 Ir and its
  `resolve_instruction_operands` caller to 70,525,341 Ir.  Nevertheless the
  whole isolated gate regressed 4,546,884,565 -> 4,575,440,493 Ir
  (+28,555,928, +0.62799%): the changed self-compiler layout overwhelms the
  local saving.  The candidate was therefore rejected without an exact source
  run, correctness rerun, or inception.  The code was reverted, the hosted
  compiler is byte-identical to L73 at
  `88fa096a7cdbbc17ffeb08caa103f10a411e1d49fe74377923966053f86ead4d`,
  no fixture changed, and no profiler remains.
- L77 (P30 REUSABLE SIMPLIFIER SCRATCH LANDED; THE ORIGINAL PASS-VOLUME
  SLICE CLOSES).  The simplifier runs roughly 14.3k times on the frozen TU
  and previously rebuilt its value/block-sized fact, definition, traversal,
  phi, and expression vectors on every invocation.  `optimize` now owns one
  explicit, reentrant `SimplifyScratch` and threads it through every scheduled
  simplifier call, retaining vector capacity and the expression arena between
  completed passes.  `PassArena::reset` reuses its geometric blocks only after
  the prior map has been destroyed; public standalone `simplify_values` keeps
  a call-local context.  An arena-only form was insufficient at
  4,551,573,453 Ir, so the landed change deliberately covers the actual
  value/block scratch population.  Source and serialized LowIR outputs remain
  byte-identical at
  `200cee5bc5a0e50297875289038b6a706fe2fe131df0540306237d2f463120f6`
  and `ba6231e2a3260262e1d165637cb700e1a0a8b550f9623f5098f56d0fc6ca3130`.
  The isolated serialized gate improves 4,546,884,565 -> 4,513,564,029 Ir
  (-33,320,536, -0.73282%); its simplifier body falls 185,281,315 ->
  176,316,608 Ir, `free` falls 193,387,546 -> 186,732,588, and `operator new`
  falls 144,524,134 -> 139,631,326.  The final audit-safe helper packaging
  improves the exact O1 self-hosted source gate 40,267,653,423 ->
  40,121,352,390 Ir (-146,301,033, -0.363321%).  PA38 is 41/41, the
  through-PA38 report is 5,431/5,431, and the audit has zero fatal findings
  (36 warnings).  O3, O1, and O0 self/inception lanes all MATCH every object
  and the final compiler; every lane used outer `-j32` and
  `INCEPTION_BUILD_JOBS=32`.  No fixture changed and no profiler remains.
- L78 (P30 REUSABLE DCE SCRATCH LANDED; ANOTHER HIGH-VOLUME ALLOCATION TAX
  CLOSES).  Dead-code elimination runs roughly 18.1k times on the frozen TU
  and previously rebuilt its value-liveness vector, per-block dead masks, and
  work queue for every invocation.  `optimize` now owns one explicit,
  reentrant `DceScratch` and threads it through every scheduled DCE call,
  retaining the value and block capacities and each block mask's capacity
  between completed passes.  Public standalone behavior remains call-local.
  Source and serialized LowIR outputs remain byte-identical at
  `200cee5bc5a0e50297875289038b6a706fe2fe131df0540306237d2f463120f6`
  and `ba6231e2a3260262e1d165637cb700e1a0a8b550f9623f5098f56d0fc6ca3130`.
  The isolated serialized gate improves 4,513,564,029 -> 4,474,624,945 Ir
  (-38,939,084, -0.862713%); although the isolated DCE body rises 61,543,371
  -> 72,303,360 Ir, the avoided allocation work lowers `free` 186,732,588 ->
  179,044,207 and `operator new` 139,631,326 -> 133,875,730.  The exact O1
  self-hosted source gate confirms 40,121,352,390 -> 40,072,784,649 Ir
  (-48,567,741, -0.121052%): there the DCE body itself falls 196,105,619 ->
  176,163,684 Ir, `free` falls 528,380,391 -> 520,988,921, and `operator new`
  falls 396,048,828 -> 390,495,940.  PA38 is 41/41, the through-PA38 report is
  5,431/5,431, and the audit has zero fatal findings (36 warnings).  O3, O1,
  and O0 self/inception lanes all MATCH every object and the final compiler;
  every lane used outer `-j32` and `INCEPTION_BUILD_JOBS=32`.  No fixture
  changed and no profiler remains.
- L79 (P30 REUSABLE CFG CANDIDATE SCRATCH LANDED; THE FAST NEGATIVE PATH
  STOPS ALLOCATING).  CFG cleanup runs roughly 19.4k times on the frozen TU,
  and its first edge-known-branch census previously allocated and zeroed a
  value-sized `seen` vector on every call, including the overwhelmingly common
  no-candidate path.  `optimize` now owns one explicit, reentrant
  `CleanupCfgScratch` and threads it through scheduled cleanup plus the final
  edge-known-branch sweep; standalone public calls retain a call-local
  context.  Audit-safe terminal-folding and phi-presence helpers keep the
  large legacy cleanup body within the current function-size gate.  Source
  and serialized LowIR outputs remain byte-identical at
  `200cee5bc5a0e50297875289038b6a706fe2fe131df0540306237d2f463120f6`
  and `ba6231e2a3260262e1d165637cb700e1a0a8b550f9623f5098f56d0fc6ca3130`.
  The isolated serialized gate improves 4,474,624,945 -> 4,473,401,874 Ir
  (-1,223,071, -0.027333%), with `cleanup_cfg` falling 114,671,922 ->
  112,274,741 Ir, `free` falling 179,044,207 -> 178,521,313, and
  `operator new` falling 133,875,730 -> 133,488,007.  The exact O1
  self-hosted source gate confirms 40,072,784,649 -> 40,054,636,360 Ir
  (-18,148,289, -0.045288%); `cleanup_cfg` falls 285,316,138 -> 261,676,501
  Ir, `free` falls 520,988,921 -> 520,471,564, and `operator new` falls
  390,495,940 -> 390,111,497.  A broader form also retained bypass,
  reachability, EH, and merge scratch, but decisively regressed the isolated
  gate to 4,487,701,167 Ir (+13,076,222, +0.29223%) and was reverted before
  exact measurement.  PA38 is 41/41, the through-PA38 report is 5,431/5,431,
  and the audit has zero fatal findings (36 warnings).  O3, O1, and O0
  self/inception lanes all MATCH every object and the final compiler; every
  lane used outer `-j32` and `INCEPTION_BUILD_JOBS=32`.  No fixture changed
  and no profiler remains.
- L80 (P30 REUSABLE DEAD-SLOT SCRATCH REJECTED BY THE FAST GATE).
  `remove_dead_slots` runs roughly 6.7k times on the frozen TU and rebuilds
  slot-sized load-count, escape, and dead masks on every nonempty call.  An
  explicit per-`optimize` scratch retained all three capacities, covered the
  late-inline direct call and every timed scheduling site, preserved
  standalone call-local behavior, passed the file audit, and reproduced the
  serialized output exactly at
  `ba6231e2a3260262e1d165637cb700e1a0a8b550f9623f5098f56d0fc6ca3130`.
  Nevertheless the isolated gate regressed 4,473,401,874 -> 4,479,246,943 Ir
  (+5,845,069, +0.130663%): the additional pass plumbing and changed compiler
  layout cost more than the avoided allocations.  The candidate was reverted
  without an exact source run, correctness rerun, or inception; the L79
  serialized object is restored byte-identically, no fixture changed, and no
  profiler remains.
- L81 (P30 CALL-FREE EH LOAD RESIDENCY LANDED; THE FUNCTION-WIDE EH FALLBACK
  NARROWS).  A definition-time edge-staging census on the frozen source found
  330 events: 318 cross a call, 299 occur in an EH function, and 208 are loop
  invariant.  The exact overlap is 287 EH/call-crossing, 31 non-EH/call-
  crossing, and 12 EH/call-free, with no ordinary non-EH/call-free remainder.
  Unwinding can only leave a call, so a call-free value cannot be live into a
  landing pad; the existing fixed-clobber, narrow-alias, loop-headroom, and
  backward-span checks remain responsible for every other retention hazard.
  Removing the EH fallback for all 12 call-free values was too broad: on the
  serialized input it retained five extra values and removed five loads, but
  added six stores, seven final MIR instructions, and one temporary home.
  Restricting the exception to index results was also a loss: three retained
  values added ten final MIR instructions, five scalar movements, one home,
  and 16 object bytes.  Both forms were reverted.  The profitable residual is
  load-only.  On the frozen serialized input it retains two loads and changes
  final MIR 135,469 -> 135,466, machine input 156,288 -> 156,284, scalar
  movement 49,544 -> 49,540, loads 26,067 -> 26,065, and stores 16,437 ->
  16,435; copies, spills, and the 933 temporary homes are unchanged.  The
  object shrinks 1,540,192 -> 1,540,144 bytes at
  `e6a4672cd52794f93eb8abe595b7521af6b442c884e2c17c1ad248a30658b899`.
  On the frozen source it retains three loads and changes final MIR 125,826 ->
  125,821, machine input 145,859 -> 145,853, scalar movement 45,623 ->
  45,617, loads 24,013 -> 24,010, and stores 14,933 -> 14,930; copies,
  spills, and the 818 temporary homes are unchanged.  That object shrinks
  1,426,656 -> 1,426,592 bytes at
  `f5f3a11c079a07da2ab4b891828ade8a4332f32ac67c77417e46f25b20ba4753`.
  The isolated serialized compiler-work gate is 4,473,401,874 ->
  4,473,425,375 Ir (+23,501, +0.000525%), below the established 0.001%
  cross-build noise threshold; per the accepted L67/L68 policy, that
  sub-basis-point drift does not warrant a roughly 388-second exact source
  Cachegrind when both target-code improvements are deterministic.  PA38 is
  41/41, the through-PA38 report is 5,431/5,431, and the audit has zero fatal
  findings (36 warnings).  O3, O1, and O0 self/inception lanes all MATCH every
  object and the final compiler, each with outer `-j32` and
  `INCEPTION_BUILD_JOBS=32`.  No fixture changed and no profiler remains.
  This lands a proven residual rather than claiming gate (iii) complete: 318
  call-crossing staging events, predominantly in EH functions, remain.
- L82 (P30 CROSS-CALL EH LOAD RESIDENCY REJECTED; REACTIVE PRESERVED
  CAPACITY IS SATURATED).  Planned callee-saved residents already rely on the
  function's unwind metadata, so the next narrow probe gave the same treatment
  to an unplanned EH load result that happened to land in a callee-saved
  register and cross a call.  The class is large, but retaining it disrupts
  later allocation.  On the frozen source, 79 call-crossing staging events
  disappear and temporary homes fall 818 -> 801, while planned edge-register
  retains rise 132 -> 196.  The downstream cost is nine additional spills,
  final MIR 125,821 -> 126,084 (+263), machine input 145,853 -> 146,104,
  scalar movement 45,617 -> 45,770 (loads +94, stores +65, copies -8), and
  object size 1,426,592 -> 1,427,152 bytes.  The serialized input confirms
  the same shape: staging 362 -> 283, homes 933 -> 906, and retains 145 ->
  224, but spills rise 55 -> 64, final MIR 135,466 -> 135,861 (+395), machine
  input 156,284 -> 156,655, scalar movement 49,540 -> 49,796 (loads +150,
  stores +111, copies -8), and the object grows 1,540,144 -> 1,540,608 bytes.
  This is the L43 simultaneous-overlap result seen from the residual side:
  preserving reactive values longer steals capacity from more profitable
  planned lifetimes.  The one-condition probe is fully reverted before
  profiling or correctness/inception reruns; the L81 source is restored
  exactly, no fixture changed, and no profiler remains.
- L83 (P30 ALLOCATOR-REBUILD PROGRAM CLOSED; THE 1.5X TARGET IS NOT MET).
  The ordered migration is now fully adjudicated.  Gate (i)'s explicit
  location timeline landed at L46 and its transitional record/replay walk was
  retired at L63.  Gate (ii)'s general pressure splitting premise was measured
  false at L43: simultaneous overlap, rather than sequential scheduling,
  dominates the failures.  Gate (iii) landed every profitable cheaper-spill
  class found by the timeline and producer audits: lazy homes (L44), frame,
  global, and constant-index rematerialization (L47/L49/L50), direct copy and
  call-result homes plus final-use load takeover (L53/L55/L56), post-call use
  regions (L67/L68), and call-free EH loads (L81).  Its remaining source-TU
  staging population is exactly 318 call-crossing definitions: 126 loads, 62
  call results, 61 indexes, 30 binary results, 17 copies, 11 compares, seven
  conversions, and four addresses.  Each producer class has a landed direct
  form or a measured negative result; delaying an ordinary backup to the first
  boundary (L48), adding pre-call regions (L70), and retaining the final
  cross-call callee-saved population (L82) all lose to pressure or compiler
  work.  Those residual caller-saved values require a backup before the call
  under the current saturated geometry.  Gate (iv) was re-armed twice: full
  GVN at L51 improved LowIR but regressed placement by 1.44% exact Ir, while
  the lifetime-bounded retry at L69 improved every target-code counter but
  still lost 0.12% exact Ir because the current memory-SSA engine does not
  amortize at O1.  Another wrapper around that engine is therefore closed.
  A fresh same-implementation native endpoint used the validated L81 self-O1
  compiler and a gcc-O1 reference built from `686c61af`; both reproduced the
  frozen object at
  `f5f3a11c079a07da2ab4b891828ade8a4332f32ac67c77417e46f25b20ba4753`.
  Five ABBA blocks (ten samples per side) measured 9.941 s vs 5.867 s mean
  wall = **1.694x**, and 9.465 s vs 5.405 s mean user = **1.751x**.  This is
  real progress from P30's L40 1.756x wall starting point, but not the stated
  1.5x objective.  The result closes P30 honestly rather than extending a
  disproven allocator seam: the next program must remove the midend operation-
  count half of the gap and/or provide a genuinely lightweight cross-block
  forwarder integrated with a different global allocation geometry.  L81's
  full PA38, through-PA38, audit, and three 32-way inception lanes remain the
  final correctness matrix; no fixture changed and no profiler remains.

Forward operation-count work continues in
`PLAN-GUARDED-PARTIAL-INLINING.md` as the P31 guarded hot-path cloning program.
Its measured residual and P32 successor are documented in
`PLAN-HOT-LOOP-RESIDENCY.md`.

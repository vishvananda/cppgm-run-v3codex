# PLAN-INLINE-PARITY: closing the O1 gap through inlining

Objective: bring the exact self-O1 compiler within 10% of gcc-O1 on the
frozen benchmark (`~/cppgm-extended-pa39-source-layout/benchmarks/
self_compile/stable/semantic_overload.cpp`, `-std=gnu++11 -O1 -Idev/src`).
Current honest state at 678c5091 (P24 protocol, ledger L31): self-O1
10.501 s / 42.93B dynamic instructions vs gcc-O1 5.921 s / 20.88B =
**1.774x wall, 2.056x instructions**.  (At L26/342e1bfc: 1.791x wall,
2.123x Ir; at L19/0c296b7a: 1.835x, 2.139x; at the first Phase A
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

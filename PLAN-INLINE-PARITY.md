# PLAN-INLINE-PARITY: closing the O1 gap through inlining

Objective: bring the exact self-O1 compiler within 10% of gcc-O1 on the
frozen benchmark (`~/cppgm-extended-pa39-source-layout/benchmarks/
self_compile/stable/semantic_overload.cpp`, `-std=gnu++11 -O1 -Idev/src`).
Current honest state at fd019bdc: self-O1 10.8 s / 44.47B dynamic
instructions vs gcc-O1 5.69 s / 20.24B = 1.90x wall, 2.20x instructions.

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

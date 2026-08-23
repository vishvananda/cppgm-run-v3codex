# Plan: Generated-Code Parity With GCC and Clang at O1

Status: phases P0-P14 have measured dispositions; residency+release landed (P10, two soundness holes fixed), coverage expanded (P10-v11), loop-aware inline budgets rejected with a latent deferred-compare/cached-dereference wrong-code fix landed in their place (P13), and bulk file reads landed (P14) -- exact self-O1 is 10.95 s median vs 5.80 s for a current-source gcc-O1 build (ratio 1.89x, denominators refreshed to current source; the pinned f5bfd68e gcc-O1 binary still measures 5.86 s)

Date: 2026-08-22

## Objective

Close the remaining generated-code gap so that the compiler built by cppgm++
at `-O1` compiles the pinned frozen source in the same time as the same
source built by GCC or Clang at `-O1`.  The `f5bfd68e` matrix records the
starting point: the GCC-O1 executable takes 6.06 seconds and the Clang-O1
executable 5.73 seconds, while the cppgm++ self-O1 executable takes 17.11
seconds.  Even cppgm++'s best level (O2, 14.93 seconds) is 2.7x slower than
either host at O1, so most of the remaining gap is shared by every optimized
level and cannot be closed by O1-only policy tuning.

The acceptance target is a median frozen-compile wall time for the
configuration-fair cppgm++ self-O1 executable within 10% of the better
host-built O1 executable, measured by the established interleaved protocol,
with the full report, zero-fatal audit, and clean 32-worker self/inception
lanes intact.  Compile-time and RSS gates from `PLAN-OPT-PASS-IMPROVEMENTS.md`
continue to apply to every phase.

## Measured decomposition of the 2.8x

All numbers are from cpu-clock profiles and object census of the immutable
matrix executables compiling the pinned `semantic_overload.cpp`, plus
`--stats` frozen compiles at the current tree.

1. **Configuration skew, about 1.5 seconds.**  cppgm++ never defines
   `__OPTIMIZE__`; its host-configuration snapshot is taken without `-O`
   (`dev/gen_builtin_host_config.pl`).  GCC 15's Ubuntu libstdc++ enables
   `_GLIBCXX_ASSERTIONS` whenever `__OPTIMIZE__` is absent
   (`bits/c++config.h:597`), and assertions also force
   `_GLIBCXX_EXTERN_TEMPLATE -1`.  Every cppgm++ compile therefore builds
   hardened STL code with bounds checks in each accessor and instantiates
   string bodies per translation unit, while the GCC/Clang matrix builds do
   not.  A self-O1 rebuild with `_GLIBCXX_NO_ASSERTIONS` compiles the frozen
   source in 15.43/14.91 seconds wall/user versus 16.91/16.43 (-8.8%), with a
   byte-identical output object.  The matrix comparison is therefore partly
   configuration, not code quality, and the fix is semantic truth rather than
   a tuning trick: define `__OPTIMIZE__` when optimizing.  A GCC-O1 build of
   the compiler **with** `_GLIBCXX_ASSERTIONS` compiles the frozen source in
   6.36 seconds median versus 6.06 without: GCC absorbs the hardened
   configuration almost for free because its inliner folds each check into a
   cold branch, while the same configuration costs the generated compiler
   about 1.5 seconds.  The configuration-fair codegen gap is therefore about
   2.55x (15.43 versus 6.06) either way, and GCC's robustness to hardening
   independently confirms the cold-successor cost rationale in P2.
2. **Out-of-line tiny bodies, 40.1% of user samples.**  The self-O1 executable
   defines 19,992 functions versus GCC's 4,839.  The profile puts 40.1% of
   user-space samples in bodies GCC does not emit at all: `Token`'s move
   constructor, `SyntaxArena::IsTag`, `FindDirectChildTag`, `Lexer::Take`,
   `istreambuf_iterator` helpers, `vector`/`string` accessors, and
   `__uninitialized_*` fill loops (2.77% in the `Operand` default-fill alone).
   The current inliner's remaining rejection classes at O1 are size-driven:
   8,504 `callee_size` plus 8,087 `prepared_size` rejections, against only
   290 `callee_eh`, 12 `landing`, and 954 `no_inline` after Rank 11.
3. **Per-body code quality, 51.7% of user samples at 2--4x.**  In bodies both
   compilers emit, cppgm++ produces 2--4x the instructions: `Lexer::Peek` is
   162 instructions with 47 frame accesses versus GCC's 61 with zero;
   `Program::FindEntry` is 4.2x the bytes; `IsInRanges` 3.0x.  The concrete
   deficiencies, all visible in one page of `Peek` disassembly, are:
   - loop-invariant addresses stored to and reloaded from the frame three
     times within ten instructions (no O1 load reuse; `promote_slot_runs=0`,
     `memory_gvn_runs=0`, `pre_runs=0` at O1);
   - boolean materialization chains (`sete; movzbl; movzbl; xor; cmp; jne`)
     where GCC emits `test; je`, a consequence of the R9a canonical `i64`
     comparison plus `u8` conversion never folding back into a branch;
   - `planned_edge_register_retains=0` at O1
     (`lowir_native_location_planning.cpp:136` requires level 2) and only 439
     at O3, so nearly every cross-edge value round-trips through the frame:
     of 116,191 O1 MIR instructions, about 74,000 are movement
     (24,589 scalar-temporary, 21,573 call-boundary, 16,044 source-slot,
     11,742 address-materialization);
   - the `Operand` fill loop reloads the element pointer from the frame
     before every field store and loads `kInvalidCompactId` from memory per
     element instead of folding the constant; and
   - exact pointer-difference division by a power-of-two element size emits
     `idiv` instead of an arithmetic shift, while non-power-of-two sizes
     already use the magic-multiply path.
4. **Allocator and libc, about 4%.**  Not a current priority.

## P0: correctness gates exposed by the no-assertions configuration (complete)

The no-assertions configuration had never been self-hosted and exposed two
latent bugs, both now fixed, with reducers in their earliest owning suites:

- **Retained dereference carriers could be reallocated.**  A deferred
  storage-only address fact caches a `deref(reg)` operand
  (`lowir_native_index_lowering.h`), but when every parameter has a frame
  home the incoming register backing that operand was never reserved in the
  pool; `try_reserve_result_register`'s caller-saved fallback then handed
  `%rdi` to an ordinary load and every later replay of the cached operand
  read garbage.  The generated `PublishVariableInitializerActions` crashed on
  the frozen source.  Carrier registers of a retained dereference are now
  reserved at fact creation, following the pending-call-argument precedent.
  PA29 behavior reducer `deferred-address-parameter-carrier-reuse` fails
  (segfault) with the old backend and passes now; references were generated
  through the documented local `REF_TEST_APP=../dev/lowir2native` path
  because the pinned bundle predates the correction.  Commit `9b8a3bf9`.
- **Member templates of extern-template classes were suppressed.**  The
  explicit-instantiation-declaration mark in
  `pa32_template_preemption_semantic.cpp` covered member templates, so
  `to_string`'s `__resize_and_overwrite<lambda>` was never instantiated and
  every no-assertions link failed.  An explicit instantiation covers only
  ordinary members; a member-template specialization over a TU-local type can
  never come from the library.  The mark now skips function-template
  patterns.  PA32 course reducer
  `200-extern-template-member-template-instantiation` links and runs only
  with the fix.  Commit `348ad6c7`.

Both fixes are output-neutral on the frozen workload: the maximum-level
object remains byte-identical at SHA `2d62704d...` and deterministic across
repeated compiles.  The full report passes 5,408/5,408 and the PA39 audit has
zero fatal findings.

The first P1 inception lane exposed a third latent defect in the same
family.  `consume()` released the register of a value at its final counted
use even when the value was edge-live; lowering order is block-layout order,
so a later-layout block could reallocate that register while an
earlier-layout loop block -- re-entered through a backedge at runtime --
still addressed through it.  The generated
`ConfigureSynthesizedStoragePrefix` stored through a clobbered `%r13` and
the O3 inception crashed on six translation units.  The release now also
requires the value not be edge-live, mirroring the loop-invariant guard
(`Keep edge-live value registers across backedges`).  The full report and
every PA29/PA38 fixture pass unchanged, and the previously crashing
translation units compile.  An isolated course reducer for this exact guard
bypass is outstanding: synthetic `.t` shapes place the address value where
loop-invariance protection already applies, and the failing fact combination
arises only on the in-memory driver pipeline after inlining has rewritten
the function, which the parse-based PA29/PA38 harnesses cannot reach.  The
serialized emit path cannot currently dump the object pipeline's optimized
LowIR for a hosted translation unit (emit mode takes no `-I`, and `-E`
output is a token dump the compiler does not re-consume), which is the
tooling gap that blocks extraction; it is recorded here for follow-up.

## P1: host-configuration parity

Define `__OPTIMIZE__` (value 1, matching host GCC) in the predefined-macro
set whenever the driver's effective optimization level is at least one, for
every tool that accepts `-O`.  Audit the host snapshot for the other
level-conditioned macros: host GCC also defines `_FORTIFY_SOURCE 3` when
optimizing; decide explicitly whether to mirror it (its glibc `_chk` wrappers
mostly compile away under `__builtin_object_size` unknown) and record the
decision.  Do not hardcode either macro into the snapshot; both must follow
the compile's own level so `-O0` output is unchanged.

`_FORTIFY_SOURCE` is deliberately not mirrored: it selects glibc `_chk`
wrappers rather than libstdc++ behavior, its absence matches a valid host
configuration (`g++ -U_FORTIFY_SOURCE`), and hardening the generated
compiler is not a parity requirement.  This decision is recorded here so a
future compatibility need can revisit it explicitly.

Owning coverage: the PA15 O0 lane proves `__OPTIMIZE__` is absent at `-O0`
and the PA37 driver O1 lane proves it is present when optimizing; both are
exact serialized-LowIR course reducers.  Emit modes additionally accept
`-D`/`-U` so the PA37 object-roundtrip harness can emit its canonical O0
LowIR under the same preprocessing the direct optimized compile uses; the
roundtrip contract requires identical preprocessed input on both paths now
that hosted preprocessing is level-dependent.

Gate: the self-O1 compiler rebuilt under the new default must reproduce the
15.43-second measurement (already demonstrated with the explicit define).
The frozen maximum-level object intentionally changes -- hosted assertions
disappear from optimized output -- so the deterministic-output check, the
code-shape census, the full report, audit, clean 32-worker O3
self/inception, and the explicit O0 inception lane must all pass, and the
new frozen SHA becomes the baseline.  Re-run the twelve-way matrix afterward
so all later phases measure against configuration-fair numbers; the
GCC-O1-with-assertions lane is recorded in the decomposition above.

## Execution reordering: P4 before P2

The post-P1 profile keeps the same hot out-of-line population, and a
prototype cold-successor discount (noreturn/throw/resume-terminated blocks
charged at zero eligibility weight, with hot-leaf shape classification) left
the frozen O1 object byte-identical and moved exact-self timing only within
noise, while growing the self compiler by 8 KiB: with assertions gone, the
remaining out-of-line bodies are not guard-diamond shapes, and the hot
`Token(Token&&)`-class bodies are callful straight-line code whose real
defect is that every field copy reloads `this` and the source pointer from
frame homes.  Inlining a body that is three times its necessary size
relocates the bloat (the R10i-b lesson), so the frame-traffic work must land
first and the discount prototype is rejected pending remeasurement.  P4 is
therefore executed before P2, and the P2 sweep will be run against
frame-clean bodies.

## P2: inline the hot accessor class

After P1, re-run the frozen census and a fresh generated-self profile, and
add a call-site-frequency-weighted view (static call counts per callee from
the object, cross-checked against profile samples) so multi-use hot callees
such as the `Token` move constructor are ranked by dynamic weight rather
than by retained-definition count, which hides them.

Then revisit the size-rejection wall (8,504 + 8,087 rejections) with the
R11h single-variable protocol, extended by one narrowly scoped cost feature:
a provably cold successor -- a block whose terminator unconditionally reaches
a `noreturn`/`unreachable` call -- is charged at zero weight, so a guard
diamond (assert-style check, allocation-failure branch, growth slow path)
scores as its hot path.  This is the surviving kernel of the rejected
weighted-cost prototype, without its general weighting machinery.  Sweep the
leaf cap, then the callful/multi-block cap, then caller growth, each alone,
scored by exact interleaved generated-self timing; keep the smallest policy
with a material win.  The targets that must inline for the phase to be
accepted: `vector`/`string` element access, `Token(Token&&)`, `IsTag`,
`FindDirectChildTag`, `TypeTable::Get`, `Lexer::Take`, and the
`istreambuf_iterator` pair.

## P3: fold boolean materialization into branches

Kill the `cmp; sete; movzbl; movzbl; xor; cmp; jne` pattern: when an `i64`
comparison result's only consumers are a `u8` conversion and/or a branch,
emit the flags-consuming branch directly and materialize the boolean only
when it is genuinely stored or passed.  This may live in native selection
(PA29/PA38) as compare-branch fusion across the conversion, or as a typed O1
LowIR simplification; prefer whichever preserves serialized-LowIR semantics
without a new instruction.  Frequency makes this a top-three per-body item:
every predicate in the hot lexer/parser loops pays it.  PA29/PA38 reducers
cover fused, stored, mixed, and edge-live cases.

## P4: stop defaulting short-lived values to frame homes at O1

Four independent, individually gated pieces:

- **P4a.** Promote sparse slot promotion (and its render-once byte cost,
  about 60 ms on the frozen compile at O2) into O1, so named locals stop
  round-tripping through the frame.  This is the single policy decision
  R11g explicitly deferred; the movement census above is the evidence it
  wanted.
- **P4b.** Run region-aware memory GVN at O1 (9.9 ms at O2 on the frozen
  compile) so repeated loads of the same field/address within and across
  blocks collapse.  Budget-gate it if the O1 compile-time cost on large
  functions is measurable.
- **P4c.** Extend edge-register retention planning to O1 and remove the
  mandatory definition-time fallback store that made the R11g native
  experiment regress: allocate the fallback frame home lazily, only when an
  eviction actually occurs.  Re-run the R11g upper-bound measurement after
  P2/P3 change the code shape.
- **P4d.** Fold initialized-constant internal globals (`kInvalidCompactId`)
  into immediates at LowIR, and strength-reduce exact pointer-difference
  division by power-of-two element sizes to shifts in native lowering.

Each piece keeps the frozen-object determinism check, code-shape census,
owning PA37/PA29/PA38 reducers, and interleaved A/B gates from the
established protocol.

## P5: remeasure, then decide on deeper allocation work

After P2--P4, repeat the profile and the movement census.  If common-body
quality is still materially behind (Peek-class functions above about 1.5x
GCC's size), the remaining structural option is interval-based physical
allocation at O1/O2 within the existing `FunctionFacts` framework -- the
O(I log R) linear-scan bound the architecture section has always reserved --
replacing reactive spill-on-pressure with planned intervals.  That is a
large backend phase and must not start until the cheaper P2--P4 items have
been measured, because they change exactly the pressure patterns an
allocator would be tuned against.

## P5 implementation: planned value placement

The measured residual after P1--P5a is a flat profile with every hot body
about twice GCC's size, dominated by reactive-allocator artifacts: values
receive homes one instruction at a time, so multi-use values lose registers
to single-use scratches, edge-live values relocate to frame homes, and the
caller-saved fallback order is blind to interval length.  The replacement is
a per-function placement plan computed after `FunctionFacts`:

- **P5d. Interval construction.**  Candidate values are non-parameter
  scalar GPR temporaries with at least two uses and no fixed-register,
  deferred-address, wide, or floating constraint.  The interval is
  `[definition, last_use]` from the existing facts; an edge-live value
  extends to the end of the outermost layout loop containing one of its
  uses, honoring the backedge lesson from the edge-live release fix.
- **P5e. Linear scan.**  Sort by definition; allocate call-crossing values
  from the callee-saved pool and others from the full managed pool, always
  excluding registers in the value's `live_across_clobbers` mask; on
  exhaustion the value simply has no plan and the reactive path stands.
- **P5f. Integration.**  `try_allocate_result` consults the plan first and
  reserves the planned register when it is still free; every reactive
  mechanism remains as the fallback, so a conflict degrades to today's
  behavior instead of failing.  Exception-bearing functions are excluded
  until the non-EH measurement justifies the audit.

Each step lands separately under the established gates, with a synthetic
1x/2x/4x pressure ladder and the exact-self A/B as the acceptance signal.

### P5d/e/f measured outcome: grant bias is not the lever

P5d/e/f were implemented and measured (2026-08-22); the implementation is
preserved in this section and the measurement is in the ledger.  Three
results close this direction:

1. **Grant bias is neutral.**  With EH functions excluded, 1,056 planned
   values produced 272 grants and changed 7 of 1,335 emitted functions
   (net +4 instructions).  Including EH functions: 3,142 planned, 950
   grants, 302 functions changed, net +64, none materially smaller.  The
   reactive allocator's register *choice* was already adequate; changing
   which free register a value receives does not remove any movement.
2. **Plan-gated EH retention regresses.**  Retaining edge-live values in
   exception-bearing functions only when they sat in their planner-proved
   register (299 retains on the frozen TU) lost about 1.2% in every
   interleaved exact-self pair (base 12.51 s median wall, candidate
   12.67 s).  The retained path still writes the definition-time home
   store, so retention saves only reloads while paying callee-saved
   pinning; this reproduces the P5b conclusion under the strictest gate
   we can construct.
3. **The non-crossing EH case is already covered.**  `consume()` does not
   release edge-live values, so a value that crosses no call already stays
   in its register; `stabilize_edge_live_result`'s entry filter
   (`LOOP_INVARIANT || crosses_call`) is why widening the EH gate for
   non-crossing values changed nothing (305 vs 299 retains).

The remaining structural lever is the movement itself, measured on the
frozen TU at O1 (about 111k instructions): movement instructions are 58%
of all emitted code -- scalar-temporary traffic 28.6k (26%), call-boundary
20.6k (19%, of which 7.1k loads and 6.8k argument register copies),
address materialization 13.0k (12%).  Removing it requires values to live
in registers *without* eager home stores: the lazy-home design from the
P5c rejection, now with its constraint identified precisely --
`SpillIsSafe` dominance is required to evict a value whose home was never
written, so laziness either restricts eviction (exhaustion risk in the
reactive path) or the allocator must be driven by the plan end to end.
Incremental retention variants on the reactive allocator are exhausted:
three flavors (P5b eager, P5g plan-gated, P5h non-crossing) all measured
worse or no-coverage.

## P9: dynamic hot-path costs (the Peek decomposition)

Static instruction count does not explain the 2.14x: on the frozen TU we
emit 109.8k instructions to GCC's 90.0k (+22%), but frame movs are 28.7k
vs 15.7k (+83%) and push/pop 6.7k vs 3.0k (2.2x).  The runtime gap lives
in dynamic latency chains on hot paths.  `Lexer::Peek` (122 instructions
vs GCC's 61, top of the flat profile) decomposes the whole gap into four
recurring, separately fixable costs, each verified in the emitted LowIR
and disassembly:

- **P9a. Cold-only definitions ride the hot prologue.**  Three throw-path
  `addr` constants (string literal, RTTI, constructor) sit in `^entry`
  after inlining/LICM, are live across the loop's calls, and so receive
  callee-saved registers plus eager frame homes: three extra pushes,
  three RIP-relative loads, and three frame stores executed on every
  call, serving blocks that only run on `throw`.  Fix: sink pure
  operand-free definitions (CONST, global/function address, `index` off
  available bases) whose uses all lie in raising-cold blocks (the
  `sink_cold_blocks` classification) into those blocks.  Breadth: every
  function with an assertion or throw path -- most of the hot profile.
- **P9b. Same-block duplicate loads.**  `^land_rhs_7` loads the identical
  member address twice with no intervening store; the deque size member
  is loaded five times across the function.  Memory GVN is O2-gated; O1
  needs at least a linear per-block duplicate-load eliminator
  (invalidate on stores, calls, and fences).
- **P9c. Boolean chains reaching branches.**  `cmp eq i64` ->
  `trunc u8` -> `cmp eq u8 .., 0` -> `branch` survives to native
  `sete/movzbl/movzbl/cmp/jne` (8 instructions) where GCC emits
  `test/je`.  The P3 compare-peel must see through `trunc` of a compare
  when the outer compare is against zero.
- **P9d. Small `copyobj` staging.**  The 24-byte ring-slot `copyobj`
  lowers to a frame-staged temporary plus `rep movsb` (high fixed startup
  cost) in the lexer's refill loop; GCC emits three direct 8-byte stores.
  Bounded direct load/store lowering for small fixed-size `copyobj`
  (<= 32 bytes) through a caller-saved scratch removes the staging and the
  string-op startup from hot container loops.

Order of attack is P9a, P9d, P9b, P9c by expected dynamic leverage; each
lands separately under the established gates with the exact-self A/B as
the acceptance signal.

## P6: multi-TU emission hygiene

With the member-template fix, the extern-template configuration is usable
for the first time.  Measure the self build's defined-function count and
executable size with active `extern template` (P1 makes it the default) and
verify the 19,992-body population drops toward the host compilers' range.
If large weak populations remain, census them by category before inventing
policy, as R10f did for the frozen object.

## P7: fill and copy idioms

The `__uninitialized_default_n_a<Operand>`/`__uninitialized_fill_n_a` loops
are the largest single self-only profile entries (about 3.8% combined).
After P2 inlines element constructors into the loops, add a bounded typed
recognition of trivial fill: a loop whose body writes only
constant/zero-splat bytes to consecutive elements becomes `memset`/wide
stores.  No name recognition; the loop shape and typed stores are the only
evidence.  PA37 owns the transform with 1x/2x/4x scaling and negative
(effectful constructor) reducers.

## P8: final matrix and acceptance

Rebuild the twelve-way matrix at the end state.  Acceptance requires:

- configuration-fair self-O1 within 10% of the better host O1 lane;
- self-O2/O3 at least as fast as self-O1 (no inversion);
- matching-level cppgm++ compile time still faster than both hosts;
- full report, zero-fatal audit, clean timed 32-worker O3 and explicit O0
  self/inception lanes, byte-for-byte object and final-binary matches; and
- the ledger below completed for every phase, including rejected
  experiments and their measured reasons.


## P10: planned register residency (the placement phase)

Everything cheap is harvested.  After P9 the exact-self O1 compile is
10.32 s against gcc-O1 5.92 s (1.74x), and the remaining movement classes
-- scalar-temporary home traffic (27.6k instructions on the frozen TU) and
call-boundary reloads (7.1k loads) -- all trace to one discipline: the
reactive allocator gives a call-crossing or edge-live value an eager frame
home and reloads it at every consumer.  Three retention flavors failed
because they kept the eager store; LowIR load elimination fails (P4b, P9f
twice) because its values land in exactly those homes.  The fix must make
planned values *live in registers without homes*:

- **P10a. Planner (P5d/e revived).**  Non-parameter scalar GPR values with
  two or more uses, intervals `[definition, last_use]` extended over layout
  backedges; linear scan assigns call-crossing values callee-saved
  registers from {RBX, R12, R13} only (R14/R15 stay reactive headroom) and
  non-crossing values from {R8, R9}; registers in the value's
  `live_across_clobbers` mask are excluded.  Functions with EH, va_start,
  or dynamic stack are excluded in v1.
- **P10b. Homeless residency.**  A planned value's register is granted at
  definition (`try_reserve_result_register` consults the plan first), is
  NOT released by `consume()` until the interval ends, is skipped by
  `spill_candidate` (no home to fall back on), and is retained across
  edges by `stabilize_edge_live_result` with no fallback store, the same
  contract as `VF_EXACT_FORWARD_EDGE`.
- **P10c. Exhaustion safety.**  Planned values occupy at most 3 callee-
  saved plus 2 caller-saved registers; the reactive pool always keeps
  R14/R15 plus the remaining caller-saved set, and every planned interval
  ends with a normal release, so the reactive fallback ("reactive GPR
  allocation exhausted") can only fire where it could today.  Any such
  failure is a hard build error surfaced immediately by the suites.

Gates as always; the acceptance signal is the exact-self A/B, expecting
the first material cut into the 27.6k-instruction home-traffic class.

## Ledger

| Phase | Intended result | Fixture/contract movement | Frozen output delta | Generated-self delta | Full report/audit | Status/commit |
| --- | --- | --- | --- | --- | --- | --- |
| P0a | reserve retained dereference carriers | PA29 behavior reducer, local ref generation | none; deterministic, SHA unchanged | fixes no-assert self-compile crash | 5,408/5,408; zero fatal | complete, `9b8a3bf9` |
| P0b | instantiate member templates under extern template | PA32 course reducer | none | unblocks no-assert link | 5,408/5,408; zero fatal | complete, `348ad6c7` |
| P1 | `__OPTIMIZE__` at optimized levels | PA15 O0 and PA37 driver O1 predefine reducers; emit modes accept `-D`/`-U`; roundtrip harness preprocess parity; no other fixture movement | frozen max object 1,442,144 to 1,373,568 B (-4.8%), text 552,596 B, deterministic; O3 self compiler 11,828,888 to 10,611,520 B (-10.3%) | self-O1 vs gcc-O1 frozen medians 15.08/5.97 s wall (was 17.11/6.06); O3 inception user 1,600.9 to 1,432.1 s | 5,410/5,410; zero fatal; O3 lane self 18.56 s + inception 55.28 s and O0 lane self 20.65 s + inception 161.7 s, all 211 objects and final compiler match in both | complete, `a3d50227` |
| P1c | keep edge-live value registers across backedges (inception-exposed miscompile) | no fixture movement; isolated reducer outstanding, recorded above | none on existing fixtures | unblocks the no-assertions O3 self compiler | 5,410/5,410; zero fatal; both lanes above ran on this commit | complete, `882b7456` |
| P2 | hot accessor inlining with cold-successor discount | | | | | pending |
| P3 | compare-branch fusion through bool conversions | | | | | pending |
| P4a | scalar slot promotion and dead-slot-store elimination at O1 | 22 PA37 O1/driver/debuginfo refs and one stale O2 loop-prune ref regenerated through the documented local apps; five previously missing `.ref.stdout` artifacts added; PA29/PA38 unchanged | frozen O1 1,444,648 to 1,410,600 B, text 598,232 to 571,898 B (-4.4%); O2/O3 unchanged by promotion; corrective adds +2,704 B at max, +2,144 B at O0 | exact self-O1 frozen medians 15.05 to 13.42 s wall, 14.53 to 12.89 s user (-10.8%); host O1 compile 5.10 s unchanged | 5,410/5,410; debug lanes clean; zero fatal; O3 lane self 18.89 s + inception 54.16 s, 211 objects and final compiler match | complete, `ecf96dcc` |
| P4a-c1 | home parameters crossing register clobbers (promotion-exposed; latent at O2/O3) | no fixture movement; the slot census alone no longer proves a parameter needs no home, and wide boundaries home clobber-crossing parameters | corrective portion of the movement above | fixes the promoted `visit_store_classes` rcx-divisor miscompile | included in the P4a gates | complete, `99054828` |
| P4-note | O2 dead-loop deletion misses phi-shaped counted loops (`100-slot-loop-prune` documents current behavior); revisit with P4 loop work | | | | | recorded |
| P4b | region-aware memory GVN at O1 | rejected before fixture movement | frozen O1 object -8,976 B but text +359 B | exact self-O1 13.38 vs 13.42 s -- inside noise on top of promotion | measured on the P4a tree | rejected; revisit after placement work can harvest eliminated loads |
| P4d | power-of-two strength reduction and readonly-global constant folding at O1; exact pointer-difference shifts in source lowering | five PA37 O1 refs and one O2 IPA course ref (optimizer); three PA15, two PA17, one PA19, two PA22 exact O0 refs (frontend), all regenerated through documented local targets | frozen O1 1,410,600 to 1,406,352 B, text 571,898 to 567,624 B; `idiv` count 260 to 102 | exact self-O1 frozen medians 13.42 to 13.08 s wall, 12.89 to 12.57 s user; cumulative vs P1: -12.8% wall | 5,410/5,410; zero fatal | complete, `fe0cc35b` + `a77d5fce` |
| P3 | branch directly on boolean conversion sources (widening always; truncation of comparison results) | PA37 O1 exact reducer `385-branch-boolean-conversion` with widening, boolean-truncation, and value-truncation-negative cases; no existing fixture movement; `lowir_scalar_rules` split keeps the scheduler under the audit limit | frozen O1 1,406,352 to 1,402,496 B, text -3,856 B, `setcc` count 894 to 650 | exact self-O1 frozen medians 13.08 to 12.95 s wall -- deterministic across all interleaved rounds; cumulative vs P1: -13.7% | 5,411/5,411; zero fatal | complete, `3c069486` + split `fbe06c19` |
| P2-sweep | late nonleaf caps 8/9 | rejected before movement | compiler +110 KB | exact self-O1 medians 13.03 vs 12.95 s -- noise-to-worse; the R10i-b conclusion holds even with frame-clean bodies | measured on the P3 tree | rejected; callful-body call setup, not size, remains the boundary |
| P4c | edge register retention planning at O1 | no fixture movement (existing PA29/PA37/PA38 shapes have no eligible edges) | compiler 10,705,720 to 10,701,624 B | exact self-O1 medians about 13.02 to 12.90 s (-0.9%), candidate ahead in five of six interleaved pairs | 5,411/5,411; zero fatal | complete, `2fd09223` |
| P5a | serialize raising blocks after ordinary blocks (cold-block sinking at O1+) | PA37 O1 exact reducer `390-sink-cold-blocks` with direct, chained, and live-successor-negative cases; two existing O1 refs regenerated; `missing operand type` diagnostics gained function context | frozen O1 1,402,496 to 1,398,096 B, text 563,768 to 559,433 B | exact self-O1 user medians 12.27 to 12.09 s (-1.5%), candidate ahead in every clean interleaved pair | 5,412/5,412; zero fatal | complete, `2a882591` |
| P5 | remeasure and decide on interval allocation | none | post-P4 profile is flat: `Lexer::Peek` 8.5% then a tail below 2.5%, the general-quality signature; remaining hot bodies are within 2x of GCC with cold code interleaving removed | decision: interval-based physical allocation at O1/O2 within `FunctionFacts` is the confirmed remaining structural lever and proceeds as a dedicated follow-on phase; it must not be grown incrementally out of the reactive allocator | measured on the P5a tree | disposition recorded |
| P6 | extern-template emission census | none | self-O1 binary 12,317,088 to 10,607,568 B (-13.9%); defined functions 19,992 to 19,447 -- the body population is inliner-policy-bound, not extern-template-bound, and the cap sweeps showed the retained bodies do not cost runtime | measurement only | measured on the final tree | complete |
| P7 | trivial fill recognition | rejected before implementation | the `Operand` fill dropped from 3.16% to 0.84% of samples once promotion cleaned the loop; the remaining fill is a patterned record write, not a zero fill, so the transform is a rodata-template materialization with under 1% headroom | deferred with the P5 phase | measured on the final tree | deferred |
| P6 | extern-template emission census | | | | | pending |
| P7 | trivial fill recognition | | | | | pending |
| P5b | callee-saved edge retention inside exception regions (eager fallback) | rejected before movement | frozen O1 +4,968 B object, +5,092 B text; retains 244 to 681 | exact self-O1 behind in all four interleaved pairs (about +1%); the R11g conclusion -- eager fallback stores plus pressure outweigh saved reloads -- holds after P4 | measured on the P5a tree | rejected |
| P5c | lazy fallback stores (write the spill home only at eviction) | rejected before movement | frozen O1 near baseline with tripled retention | the generated compiler miscompiles `vector<string>::operator=` and crashes at startup: at least one consumer reads a retained value's home before any eviction writes it, so laziness needs a complete home-reader audit first | measured on the P5a tree | rejected as implemented; the reader audit belongs to the P5 interval phase |
| P5d/e/f | planned value placement: intervals, linear scan, grant hook in `try_reserve_result_register` | full suite 5,414/5,414 with grants active at O1+; PA26/PA29/PA37/PA38 owning suites clean | non-EH: 272 grants, 7/1,335 functions changed, net +4 instructions; EH included: 950 grants, 302 changed, net +64, none materially smaller | grant bias does not remove movement; the register choice was never the bottleneck | measured on the ctor-fix tree | implemented, measured, reverted; design and evidence preserved in the P5 implementation section |
| P5g | plan-gated callee-saved edge retention inside exception regions | rejected before movement | 299 retains on the frozen TU; object +1,545 B | exact self-O1 behind in 4 of 6 interleaved pairs, neutral in 2: base 12.51 s median wall vs 12.67 s; the definition-time home store remains, so only reloads are saved while pinning costs are added | measured on the ctor-fix tree | rejected; confirms P5b under a planner-proved conflict-free gate |
| P5h | non-crossing edge retention inside exception regions | not reached | 305 vs 299 retains: no coverage | `consume()` already keeps non-crossing edge-live values in registers; the stabilize entry filter (`LOOP_INVARIANT \|\| crosses_call`) makes the case vacuous | measured on the ctor-fix tree | moot; recorded to prevent re-derivation |
| ctor-EH | close the constructor cleanup region on early return (frontend); O0 self lane failed on the new `lowir_scalar_rules.cpp` TU with `host EH protected region remains active at function exit` | PA26 course reducer `210-constructor-early-return-cleanup-region` (fails on the pre-fix compiler); pa26 106/106 + course 8/8; full suite 5,414/5,414; host-EH diagnostic now names the function and block | frozen O1 object byte-identical: no benchmark impact | early `return` in a constructor body left the member-cleanup `EH_CLEANUP` region open; fixed in `LowerReturn` and the shared lexical-return terminal via `constructor_body_cleanup_active_` | O0 self+inception lanes rerun on the fix | complete, `dbff0d16` |
| P9a | sink cold-only pure definitions (constants and addresses) into their raising blocks, rematerializing per block for shared uses; landing pads excluded | PA37 O1 course reducer `392-sink-cold-only-definitions` (move, rematerialize, and hot-use-negative cases); pinned `200-eh-cleanup-addr-rematerialize` preserved by the landing-pad exclusion; full suite 5,415/5,415; zero-fatal audit; O3+O0 self/inception lanes 212/212 | `Lexer::Peek` 122 to 112 instructions with two callee-saved pushes, three RIP loads, and three frame stores off the hot prologue; frozen-TU signal weak (32 sinks, throws centralized in helpers); pp_tokenizer 54 sinks | exact self-O1 ahead in 5 of 6 interleaved pairs: wall median 12.63 to 12.45 s, user median 12.12 to 11.95 s (about -1.4%) | all gates above | complete |
| P9b/c/d | same-block duplicate-load elimination; negated boolean compare folds to the inverted comparison (integers only, one conversion deep); small `MI_COPY_BYTES` (<= 32 B) encodes as chunked direct load/store pairs instead of `rep movs` | PA37 O1 reducers `386-negated-boolean-compare` (trunc, direct, and float-negative cases) and `387-duplicate-block-loads` (call-crossing and store-intervening negatives); PA29 behavior reducer `small-copyobj-direct-stores` (7/24/33-byte spans); full suite 5,418/5,418; zero-fatal audit; O3+O0 self/inception lanes | frozen TU: `rep movs` sites 131 to 14, text -2,835 B, LowIR output -448; `Peek` boolean chain `sete/movzbl/cmp/jne` becomes `test/je`; cold sinks compound 32 to 132 | exact self-O1 ahead in all 6 interleaved pairs: wall median 12.48 to 10.55 s (-15.5%), user 11.98 to 9.86 s (-17.7%); ratio to gcc-O1 2.11x to 1.78x | all gates above | complete |
| P9e | promote small objects at O1 (was O2-gated); fix a latent promotion bug where a `copyobj` against an object passed by value produced an object-typed load or store address (reproducible at O2) | five `pa37/tests/o1` refs regenerated through the documented local flow (staging-slot chains collapse to direct stores or a phi, same precedent as the P4 enablements `ecf96dcc`/`fe0cc35b`); full suite 5,418/5,418; zero-fatal audit; O3+O0 self/inception lanes | frozen TU: 983 objects promoted pre-guard; with the object-value guard the retained delta is text -10,452 B; pass cost 9 ms per TU | exact self-O1 ahead in 4 of 6 interleaved pairs, tied 2: wall median 10.51 to 10.32 s (-1.8%) | all gates above | complete |
| P9f | memory GVN at O1, re-measured post-P9 (the P4b "revisit after placement" note) | rejected before fixture movement | 582 loads eliminated at 10.6 ms per TU, but text +7,074 B: each eliminated load becomes a cross-block live value the reactive allocator homes with a store plus reloads, costing more than the original loads | confirms P4b on the new baseline; the harvest still requires cross-block register residency | measured on the P9e tree | rejected again; folded into the P10 requirements |
| P10-v1..v6 | planned residency by hooks, six variants: crossing-value linear scan over three callee-saved registers, grant in `try_reserve_result_register`, homeless edge retention, EH eviction exemption, reactive-pool reorder, and interval-end release over layout-backedge spans | rejected on correctness | static deltas were neutral to -2.0 KB against the correct baseline (the earlier "+5 KB" reading compared against a stale pre-guard object); grants 191-397 per frozen TU | every variant MISCOMPILES the self-hosted compiler: grants-plus-homeless-retention (v2/v3) builds a binary that runs the frozen compile for the full ~10 s and then crashes without writing the object -- so interleaved timings looked plausible while the A/B outputs never existed -- and the interval-release variants (v4-v6) fail earlier with a bogus semantic diagnostic; the 5,419-test suite and owning PA suites all pass because they exercise the host-built compiler on small shapes, and only inception-scale self-hosting exposes the break | tree restored to `434593de` byte-identically | rejected; two process rules recorded: an allocator change runs the inception lanes BEFORE any A/B is read, and the A/B harness must fail loudly when the output object is missing |
| P11 | frame-operand small copies (a `MI_COPY_BYTES` side with frame provenance encodes rbp-relative chunks with no lea) and constant-multiply strength reduction (2^k -> shl; {3,5,9}*2^k -> lea+shl) | pa29 strict/course MIR refs regenerated through the documented flow with canonicalization (55 files; same precedent as `7607126f`); full suite 5,418/5,418; zero-fatal audit; O3+O0 lanes | frozen TU text -4,338 B; constant imuls 180 to 87; `Peek` ring addressing is lea+shl with direct frame loads | exact self-O1 inside noise (medians 10.37 vs 10.38 wall); retained on the static win and the specialization gap it closes for frame-staged copies | all gates above | complete |
| P12 | forward staged object copies: a staging slot written only field-wise and copied whole once becomes direct member stores to the destination (padding uncopied, member-wise contract); v1 requires one dominating store per field and a single copy site | PA37 O1 reducer `388-staged-copy-forwarding` (forwarding plus load/two-copies/conditional-store negatives); pinned `tests/driver/o2` initializer-list ref regenerated in place; full suite 5,419/5,419; zero-fatal audit; O3+O0 lanes | 30 forwards on the frozen TU, 2 in `pp_tokenizer`; `Peek`'s refill is now GCC's exact shape (three direct ring stores, no staging), body 122 at session start to 107 | exact self-O1 inside noise (medians 10.39 vs 10.36 wall); retained on the static and exemplar evidence; v1 restrictions (zeroinit prefix, conditional stores, multiple copy sites) bound the coverage | all gates above | complete |
| spill-safety | `SpillIsSafe` also refuses inside layout-backedge spans: a spill store there reads a register that later-emitted loop blocks (which execute earlier) may already have repurposed; exposed via `bind_wide_scalar_parameter_homes` treating moved 6-GPR-boundary parameters as evictable (`parameter = false`), with the P9c phi shapes triggering the sequence in `lowir_boolean_cfg.cpp:selected_target` -- the self-hosted compiler spilled the loop counter over the result pointer and crashed | PA29 behavior reducer `wide-parameter-loop-exit-spill` (segfaults on the pre-fix backend, validates the copied operand now); one pa29 behavior MIR ref regenerated; full suite 5,420/5,420; zero-fatal audit; O1 SELF-HOSTING RESTORED (frozen compile emits a valid object); O3+O0 lanes | the O1 self-host break silently invalidated the P9b/c/d through P12 A/B numbers: those binaries ran the full compile then crashed in ELF writing, so timings looked plausible while outputs never existed | honest output-verified A/B against the last good baseline (P9a era): wall median 12.45 to 11.69 s (-6.1%), user 11.95 to 11.17 s (-6.5%); the corrected cumulative ratio to gcc-O1 is 1.97x, not the phantom 1.75x | all gates above, plus the new standing gate: every self-O1 build verifies its frozen output object exists before any measurement is read | complete |
| P10-v7..v10 | residency re-evaluated on the spill-safe allocator, then the interval release debugged in two rounds | grants + homeless retention self-host correctly but are perf-neutral without release (11.61 vs 11.66 s); the release's FIRST soundness hole is FIXED: a cached dereference operand replays its carrier register beyond the carrier's counted uses, so `reserve_deferred_address_carriers` now marks carrier registers release-exempt -- with that fix the release self-hosts at O1 (verified output, 6-line `t12.cpp` enum-operator reducer passes) | localization tooling built and proven: input-space reduction (17-line then 6-line C++ reproducers), MIR edge-state checker with EH unwind edges, hybrid-link bisection | a SECOND hole remains at O3 only: `pa36 tests/link/700-hosted-self-element-vector-runtime` segfaults (vector<node> `_M_fill_insert`, -16 instructions from recycling); standalone repro: `cppgm++ -c` at default O3 plus `g++ -no-pie` link of the .t.1; release disabled passes, enabled crashes | working tree restored to HEAD `23ad9f9c`-equivalent, all suites re-verified including pa36; the full release patch set is preserved in the session scratchpad (`o1gap/release-work/`) | LANDED: the second hole's root cause was NOT in the planner -- a pool-level trace (POOL/POS instrumentation over `RegisterPool`) showed the third capped release was `%t167` (the old-end pointer in `_M_fill_insert`) released at its own final counted use, which was a register-eliding force-inline parameter copy: `consume()` removes the value from the live-location index BEFORE the alias query, and `has_live_location_alias` recomputes `value_is_live` (true forever for edge-live values), so the query expected the value still in the index and missed the genuine copy alias holding r12; the fix queries `live_locations_.has_alias(id, location, false)` -- the general accounting correction, not an exemption (the earlier `%t8`/r13/pos-164 localization was standalone-`lowir2native` numbering; in the full TU compile the third release is `%t167`/r12/pos-218); with the fix both reproducers pass with releases fully on, the full report is 5,420/5,420 (one placement-pinned pa38 o2 ref regenerated: the pinned function now holds its value in r13 across the EH region instead of a frame round-trip), audit zero-fatal, O3 and O0 restored-self inception lanes both MATCH, exact self-O1 verified-output A/B vs the spill-fix baseline: wall -0.515% / user -0.494% (11.625 vs 11.660 s, 12/12 successful); frozen-TU stats: planned=829 grants=235 residencies=138 releases=58; small but sound -- the win grows with coverage expansion (pool depth, uses>=1), which is the next step |
| P10-v11 | planner coverage expansion: R14/R15 appended to the planned pool (earlier plans keep their registers) and single-use crossing candidates admitted (`uses >= 1`) | frozen-TU stats grow planned 829 to 1123, grants 235 to 308, residencies 138 to 183, releases 58 to 91, spills 163 to 160; verified-output A/B vs the freshly landed release build: wall -0.340% / user -0.355% (12/12 successful) | full report 5,420/5,420 after regenerating one placement-pinned ref (pa38 behavior/o2 400-branch-spill-register-home: the pinned value now keeps a planned r13 residency, dropping a frame slot and its store), audit zero-fatal, O3 and O0 restored-self inception lanes MATCH | coverage increments are yielding roughly 0.3-0.5% each, so residency alone cannot close the remaining gap; the next phase needs a larger lever | landed |
| P13 | loop-aware late-wave inline budget: call sites inside layout-backward spans drew from a separate 384-instruction budget so hot loop sites stop losing the 128 caller budget to cold sites in block order (motivated by a self-vs-gcc perf-profile pairing showing the largest deltas are small leaves gcc inlines away) | frozen TU: 3,271 extra late inlines, text +14.7 KB (+3.0%); verified-output A/B vs the v11 compiler: wall +0.684% -- REJECTED: at current O1 codegen quality the marginal loop-site inline is net negative (inlined bodies keep frame-homed temporaries, so the call overhead saved is smaller than the icache cost), confirming the R10i-b conclusion from the profitability side; policy reverted | the experiment exposed a REAL pre-existing wrong-code bug, fixed and landed: a memory-RHS load defers as a cached dereference replayed at its adjacent consuming compare, but when that compare is itself a deferred BRANCH comparison the replay moves to the branch -- across intervening calls whose argument setup clobbers the caller-saved carrier register (found via a global loop-grant cap bisection to grant #649 in `abi_mangle.cpp` `collect_function_facts`: `lea r8,[rbp-536]` ... three calls ... `load rdx,[r8]` with r8=0); fix: `load_has_direct_memory_rhs` refuses when `deferred_branch_comparisons[consumer.dest]` is set | reducer `pa38/behavior/o1/405-deferred-compare-across-call.t` segfaults pre-fix (exit 139) and passes post-fix from plain LowIR with no inliner change, so the bug was reachable before P13 | full report 5,421/5,421 with no other pinned refs churned (the shape occurs nowhere else in the suites), audit zero-fatal, O3 and O0 restored-self inception lanes MATCH, O1 self output verified with the frozen object byte-identical to v11 (A/B +0.34% = noise) | fix landed, policy rejected |
| P14 | bulk file reads: `read_source_file` (driver) and `macro_processor.cpp` `ReadSource` (every include) replace per-character `istreambuf_iterator` construction with seek/tell/`read` into a presized string (the self-vs-gcc perf pairing showed `istreambuf_iterator::_M_get`/`operator!=`/string append as the largest zero-on-gcc rows: gcc inlines the iterator protocol into cheap code, cppgm++-O1 pays an out-of-line call per character over megabytes of includes) | frozen object byte-identical (host and self); verified-output A/B self-O1: wall -6.400% / user -6.649% (11.64 to 10.95 s) -- the largest single win since P9 | the acceptance denominator is refreshed to current source in the same change: a worktree gcc-O1 build of HEAD measures 5.99 s (pinned f5bfd68e matrix binary re-measures 5.86-5.88 s, so source growth cost the denominator ~1.9%), and with the bulk-read fix the gcc-O1 build measures 5.795 s (-0.5%: gcc gained little because its inlined iterator path was already cheap); the honest both-sides-current ratio moves from 1.96x to 10.945/5.795 = 1.89x | full report 5,421/5,421, audit zero-fatal, O3 and O0 restored-self inception lanes MATCH | landed |
| P15 (strategy) | two decisive experiments on the post-P14 tree | (1) memory GVN at O1, re-measured with planned residency landed (the P4b/P9f "revisit after placement" condition now satisfied): 582 loads eliminated, text +3,034 B (vs +7,074 B at P9f -- residency absorbs over half the spill bloat), interleaved A/B dead even (paired user +0.09%): the generated-code harvest now roughly cancels the added per-compile pass cost, still rejected on net; (2) an O2-BUILT exact self compiler benchmarked on the identical frozen -O1 workload is 1.4% SLOWER than the O1-built compiler (10.98 vs 11.16 s) -- at the f5bfd68e matrix the O2-built compiler was 13% FASTER than the O1-built one | the O1 pipeline has caught the backend ceiling: no remaining O1-policy lever can close the residue, because the residue is shared by every optimization level | the remaining 1.89x decomposes into backend register-allocation quality (the frozen TU still carries ~13.5k scalar-temporary loads and ~9.9k stores plus 7k call-boundary loads), inlining depth (unharvestable until the allocator can keep inlined temporaries in registers), and instruction-selection micro-shapes (profile outliers: `LookupResult()` ctor 6.3x, `ParserCursor::At` 5.7x) | reaching the 1.10x acceptance from 1.89x therefore requires replacing or fundamentally upgrading the reactive single-pass allocator with whole-function liveness-driven allocation -- an architectural build, not an incremental lever; this session landed three latent-allocator-bug fixes whose difficulty illustrates the risk profile of that work | recorded; the incremental track continues with profile-outlier shapes while the allocator decision is owned by the plan |
| P16 | direct RIP-relative global accesses: scalar loads and stores of locally bound globals encode as one `mov reg, [rip+disp32]` (new `emit_rip_load` / `emit_rip_normalized_load` / `emit_rip_store`, gated on `ADDRESS_LOCAL` because the object writer rewrites the old LEA-through-R11 shape into a GOT load for preemptible imports), and an `addr`-of-global whose every use is a storage address defers to its symbol instead of materializing a register (motivated by the `LookupResult()` 6.3x profile outlier: each global scalar read cost lea+load through R11, with the same constant re-materialized four times in one block) | frozen text size-neutral (+184 B: shapes with one access shrink, shapes with several accesses trade the shared R11 base for per-access disp32 bytes; one indexed-global shape loses a register reuse); verified-output A/B dead even (wall +0.09%) -- landed as a code-shape improvement: one instruction per access instead of two and R11 freed | six placement-pinned refs regenerated (pa29 strict copyobj/zeroinit/f80-global, two pa29 structural placements, pa38 o2 cyclic-edge-register-pressure) | full report 5,421/5,421, audit zero-fatal after comment trims keep `lowir_native.cpp` at 3,000 lines, O3 and O0 restored-self inception lanes MATCH | landed |
| P17 | planned residency for NON-crossing values: candidates without `VF_LIVE_ACROSS_CALL` ride a caller-saved pair {R9, R8} when their extended interval contains no call position (binary search over `facts.calls`, re-checked after edge-live span extension since extension can grow an interval over a call) and the function has no exception regions (landing pads never preserve caller-saved registers) | frozen TU: planned 1,123 to 4,937, grants 302 to 1,737, interval releases 91 to 206, text -2,126 B, spills 160 to 175; verified-output A/B paired wall -0.18% / user -0.24% | eight placement-pinned pa38 refs regenerated (register assignments shift onto R8/R9) | full report 5,421/5,421, audit zero-fatal, O3 and O0 restored-self inception lanes MATCH | landed |
| P17b | caller-saved planning extended to exception-bearing functions: unwinding only leaves a call, so a call-free interval is never live into a landing pad and the `has_eh` exclusion was unnecessary | frozen TU: planned 4,937 to 12,290, grants 1,737 to 3,649, text a further -2,458 B (490,176 total), spills back down to 167; verified-output A/B vs P17: paired wall -0.18% / user -0.34% (median 10.855 s) | no pinned refs churned beyond P17's | full report 5,421/5,421, audit zero-fatal, O3 and O0 restored-self inception lanes MATCH | landed |
| P18 (direction) | assessment of `~/v3codex-lowir-investigation/REPORT-LOWIR-LLVM-VALIDATION.md` against the parity plan | (1) the volatile defect (LLVM-LOWIR-001) reproduces on this tree: O0 emits ordinary stores for volatile accesses and O2 deletes them -- a correctness bug to fix before further perf work; (2) the frontend-shape finding is confirmed in production O1 LowIR: the frozen TU carries 806 short-circuit rhs blocks and 309 trivial branch diamonds (279 general, needing a `select` LowIR instruction that does not exist; 30 boolean-constant), and because the reactive backend stabilizes edge-live values at every block boundary, collapsing these diamonds attacks the same frame-traffic wall as an allocator rewrite from above at far lower risk, composing with planned residency | approach change adopted: fix volatile first, then add LowIR `select` + safe O1 if-conversion + native cmov/setcc lowering; the `sext` excess is already partially fused natively (low priority); section placement (LLVM-LOWIR-002) is recorded for PA32/PA34 ownership; the report's warning against bulk-importing Clang metadata is endorsed | the P15 backend-ceiling conclusion stands; shape reduction shrinks how often the ceiling is hit | direction recorded |
| P18a | volatile access correctness (LLVM-LOWIR-001, accept-now): `volatile_access` marker added end to end -- typed pa15 IR, pa30 adapter, `lowir_model` instruction, `load volatile`/`store volatile` text serialization and parsing, pa13 grammar (`KW_VOLATILE?` on load/store, gramparse-validated), pa13 README and lowir.md student-facing semantics; frontend sets it at every implemented access form (lvalue-to-rvalue id/subscript/member/deref/cast loads, call-reference-result deref, declaration init store, plain and compound assignment load+store, increment load+store); optimizer treats volatile as unremovable and pinning (DCE x3, dead-slot-store escape census, slot promotion census, slot-forward dead/local/single-store, duplicate-block-load elimination, readonly-global fold, memory GVN, LICM, staged copy forwarding, small-object promotion, tail-merge instruction identity+hash); native lowering pins the slot in `analyze_storage` (scalar state + dead-store flags), refuses memory-RHS deferral and direct-compare-storage for volatile, threads the flag onto MIR and guards frame store-to-load forwarding | the report witness now matches Clang at every level (O0-O3 keep `store volatile i32 0/1`, native object emits both movs); unused volatile loads survive O2 while an identical non-volatile twin folds to a constant | tests: `pa15/tests/general/300-volatile-access-markers.t` (earliest owning PA: all marker forms pinned in LowIR), `cppgm.tests/course/pa37/o2/540-volatile-access-preservation.t` (optimizer preservation with a non-volatile control that still folds), `cppgm.tests/course/pa38/o1/406-volatile-access-emission.t` (native emission); five existing volatile-bearing pinned LowIR refs regenerated with the marker (pa15 200-included-namespace-global-definition, pa16 400-zero-initialization-object-boundaries, pa19 100-function-template-cv-reference-extra-qualifier, pa25 100-volatile-auto-runtime-initializer, pa37 driver/o3 500-source-full-unroll) | residuals recorded: discarded-value volatile reads (`x;`) do not yet emit their required load; atomic access forms do not yet carry the marker; bulk `copyobj`/`zeroinit` across volatile subobjects remains outside the contract per the report | report 5,424/5,424, audit zero-fatal, O3 and O0 restored-self inception lanes MATCH, self-O1 frozen output byte-identical (perf-neutral by construction) | landed |
| P18b | LowIR `select` contract and native conditional-move lowering, plus a measured if-conversion experiment | contract landed: `IK_SELECT` (appended after `IK_PHI` to keep serialized identities stable), `%t = select <type> <cond>, <true>, <false>` serialization/parsing, pa13 grammar production and student docs, `is_pure` DCE integration, `MI_CMOV` opcode with encoder (`0F 4x /r`), MIR text render, flags-preservation entry, read-write destination classification, and an `emit_select` lowering (stage true in RDX, false in the result, test, `cmovne`, narrow renormalization) with frame-address operands taken by address | the if-conversion pass (`convert_select_diamonds`: two trivial single-predecessor arms, join with exactly the two arm edges and only two-entry leading phis, EH-target exclusion, speculatable-arm hoisting) converts 260 of the frozen TU's 309 diamonds and works end to end, but REGRESSES the benchmark: +1.60% wall at arm limit 3 (text +9.2 KB) and +1.10% at empty arms only -- well-predicted branches beat the cmov data dependency on this workload, so the pipeline hook is removed and the pass retained as unwired infrastructure | three implementation bugs found by the select-cap bisection and fixed: `instruction_clobber_mask` did not know a select clobbers RAX/RDX/R10/R11 (incoming parameter registers died), frame-address operands were loaded instead of address-taken, and machine-level optimization treated `cmovne`'s destination as write-only and rewrote through it | tests: `pa38/o1/407-select-scalar-choice.t` (native cmov emission and both runtime paths, including immediate operands and narrow renormalization), `pa37/o1/505-select-preservation.t` (optimizer keeps used selects, removes unused ones, carries selects through inlining) | report 5,426/5,426, audit zero-fatal after comment trims, O3 and O0 restored-self inception lanes MATCH; with the hook removed codegen is unchanged, so the landing is perf-neutral | contract landed, conversion rejected at current selection quality |
| P18c | `copyobj` overlap contract (LLVM-LOWIR-004, accept-now): `pa13/lowir.md` now specifies `copyobj` as a non-overlapping semantic object copy (identical addresses are the one degenerate case, elidable), matching production lowering which only emits it for semantic object transfers and native lowering which uses forward copies; overlap-capable moves stay hosted (`memmove`), and any future overlap-safe IR operation must be a distinct instruction | documentation-only: no lowering, fixture, or grammar change | native lowering already elides the identical-address case (`aliases_same_object`) | no gate movement required beyond the doc | landed |
| P19 | readonly storage for const scalar globals: `LowerGlobal` marks a statically initialized, destructor-free, non-thread-local, non-volatile const scalar global `storage=readonly`, which the existing `ReadonlyGlobalIndex`/`fold_readonly_global_loads` machinery then folds to its literal at every load site | the header-defined internal-linkage sentinel constants (`kNoScope`, `kNoBinding`, `kNoEntity`, ...) that the semantic phase compares against everywhere previously cost a RIP load per read (`LookupResult()` measured 7.4x gcc); they now fold to immediates -- the constructor becomes nine direct `movl $imm` stores, matching gcc's shape | frozen-TU text unchanged (the benchmark TU has no such globals; the win is in the compiler's own code); verified-output A/B wall -0.18% / user flat -- landed as a non-negative code-quality improvement | class objects stay default storage: mutable members and lifecycle actions can write through a const complete object | report 5,426/5,426 with zero fixture churn, audit zero-fatal, O3 and O0 restored-self inception lanes MATCH | landed |
| P20 | lexer lookahead source shape: `FixedQueue` gains an `unchecked` accessor for callers whose control flow already guarantees the bound, used by `Lexer::Peek`/`Take`/`PeekLine`/`PeekColumn` after their guarding loop or emptiness check | `Peek` drops from 82 to 69 LowIR instructions (redundant bounds branch and one inlined throw blob gone) and `Take` from 43 to 32, bringing `Take` under the 40-instruction ordinary inline cap so its hot call sites in the token loop now inline it | verified-output A/B wall -0.14% / user -0.10%; median 10.83 s (ratio ~1.87x to the 5.80 s current-source gcc-O1 denominator) | compiler-source change like P14: both producers benefit; the ratio gain is the differential | report 5,426/5,426, audit zero-fatal, O3 and O0 restored-self inception lanes MATCH | landed |
| P21a | benefit-ordered planning probe: candidates sorted by use count (hot values pick registers first) over per-register interval sets instead of the greedy definition-order linear scan | frozen TU: spills 167 to 134 (-20%), grants 3,649 to 3,532, text +2.0 KB; verified-output A/B wall +0.23% -- REJECTED and reverted: fewer reactive spills did not translate to wall time because the priority order takes registers the reactive layer would have used better elsewhere; the planner/reactive SPLIT is the bound, not the planner's ordering | fifth consecutive incremental data point at or below noise (-0.18%, -0.14%, +0.23%, +1.1..1.6% rejected, 0.0%): the incremental space at the current architecture is exhausted, exactly as the P15 strategy row predicted | the next phase is the whole-function liveness-driven allocator that OWNS all placement decisions (the reactive layer as pure executor), designed as its own plan with per-function validation against the reactive output | probe reverted cleanly; no fixture movement | rejected, direction confirmed |
| P8 | final matrix | hosts rebuilt from the exact final tree; self lanes O0-O3 rebuilt clean | frozen medians: gcc-O1 5.92 s, clang-O1 5.63 s, self-O0 35.45 s, self-O1 12.64 s, self-O2 12.60 s, self-O3 12.57 s; no level inversion; self-O1 improved 17.11 to 12.64 s (-26.1%), ratio to gcc-O1 2.87x to 2.14x | acceptance target of within-10% is not yet met; the recorded P5 follow-on phase carries the remaining 2.1x | all P-phase gates recorded above | complete; target carried to the P5 phase |

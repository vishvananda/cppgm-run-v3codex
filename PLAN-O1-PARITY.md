# Plan: Generated-Code Parity With GCC and Clang at O1

Status: phases P0-P8 have measured dispositions; the within-10% acceptance target is carried by the recorded P5 interval-allocation follow-on phase

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
| P8 | final matrix | hosts rebuilt from the exact final tree; self lanes O0-O3 rebuilt clean | frozen medians: gcc-O1 5.92 s, clang-O1 5.63 s, self-O0 35.45 s, self-O1 12.64 s, self-O2 12.60 s, self-O3 12.57 s; no level inversion; self-O1 improved 17.11 to 12.64 s (-26.1%), ratio to gcc-O1 2.87x to 2.14x | acceptance target of within-10% is not yet met; the recorded P5 follow-on phase carries the remaining 2.1x | all P-phase gates recorded above | complete; target carried to the P5 phase |

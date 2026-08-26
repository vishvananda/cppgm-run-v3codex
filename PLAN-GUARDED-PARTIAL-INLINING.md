# PLAN-GUARDED-PARTIAL-INLINING: P31 hot-path cloning and predicate collapse

Status: implementation investigated; guarded partial cloning rejected by the
whole-program gate; survivor validation remains in progress

Date: 2026-08-25

## 1. Objective

Continue the self-`-O1` parity work after the P30 allocator rebuild closed in
`PLAN-INLINE-PARITY.md`.  P30 improved the frozen native wall ratio from
1.756x to 1.694x, but its remaining placement seams were either exhausted or
measured negative.  The new same-source GCC/Clang analysis localizes the next
opportunity in operation count created by missed interprocedural collapse,
especially the lexer/cursor/accessor tower.

P31 will implement a bounded, source-independent guarded partial inliner:

1. clone a small, acyclic, side-effect-free entry path from an eligible callee
   into the caller;
2. return directly from the clone when its fast condition succeeds;
3. execute the original call on every unhandled, slow, call-bearing, looping,
   or EH path; and
4. simplify the clone using caller constants and dominating predicates before
   deciding whether to keep it.

The first measured target shape is `Lexer::Peek(offset)`: calls with a constant
offset should be able to test and read an already-populated lookahead queue in
the caller while retaining the existing outlined `Peek` call for queue fills,
translation, and exceptions.  This is a target shape, not a production symbol
special case.  Production eligibility and profitability must be expressed in
LowIR properties and must apply to ordinary user programs.

The program target is:

- recover at least 20% of the current 19.6B self-versus-GCC dynamic-instruction
  gap, bringing the frozen self compiler to at most about 36.1B Ir;
- reduce the honest same-revision self/GCC wall ratio to at most 1.55x, with
  1.50x retained as the stretch exit;
- reduce, not merely redistribute, the combined dynamic cost of the selected
  caller and callee;
- keep target-code movement and text within the admission envelopes below;
  and
- preserve PA1--PA38 behavior and byte-reproducible O0/O1/O3 inception.

P31 does not reopen global inline-cap increases, source-level restructuring of
the frozen workload, or another pass over the saturated P30 allocator seams.

## 2. Frozen protocol and current baseline

The primary workload remains:

```text
/home/vishvananda/cppgm-extended-pa39-source-layout/
  benchmarks/self_compile/stable/semantic_overload.cpp
```

Source SHA-256:

```text
ab00b2e1c3c7463baf9d8e1e7fc754b9cde2c18749568616062011f31e7daba2
```

Compile command shape:

```sh
cppgm++ -std=gnu++11 -Wall -O1 -Idev/src -c \
  -o semantic_overload.o semantic_overload.cpp
```

The starting implementation is commit `37e17155`.  The self-O1, GCC-O1, and
Clang-O1 compiler binaries were built from the same implementation.  All three
produced this exact frozen object:

```text
f5f3a11c079a07da2ab4b891828ade8a4332f32ac67c77417e46f25b20ba4753
```

Six rotating native samples per compiler measured:

| compiler | mean wall | mean user | exact Ir-only Cachegrind |
|---|---:|---:|---:|
| self O1 | 9.883 s | 9.407 s | 40,046,026,786 |
| GCC O1 | 5.830 s | 5.362 s | 20,438,817,693 |
| Clang O1 | 5.640 s | 5.173 s | 20,803,446,126 |

Current ratios:

- self/GCC: 1.695x wall, 1.754x user, 1.959x Ir;
- self/Clang: 1.752x wall, 1.818x user, 1.925x Ir; and
- Clang/GCC: Clang executes 1.78% more Ir but is about 3.3% faster in wall.

The self compiler takes less time per dynamic instruction than either host
compiler.  Its top-level deficit is therefore not an IPC or cache explanation:
it executes almost twice as many instructions.

## 3. Evidence that constrains P31

Confidence is HIGH unless marked otherwise.  All new measurements below are
from the starting implementation and the frozen command above.

### E1. Phase attribution

One `--stats` compile per compiler produced:

| phase | self | GCC | Clang | self-Clang excess |
|---|---:|---:|---:|---:|
| frontend | 7.168 s | 4.221 s | 4.161 s | 3.007 s |
| typed lowering + adaptation | 0.728 s | 0.471 s | 0.428 s | 0.300 s |
| LowIR optimization | 2.066 s | 0.945 s | 0.821 s | 1.245 s |
| native lower + machine opt + encode | 0.657 s | 0.339 s | 0.307 s | 0.351 s |

The self-versus-Clang excess is about 61% frontend, 25% LowIR optimizer, 6%
typed lowering/adaptation, and 7% native production.  Within the frontend,
semantic analysis contributes 1.814 s of the excess and preprocessing 0.987 s.
The same comparison against GCC assigns 64% to the frontend and 24% to LowIR
optimization.  P31 must therefore improve generated code used across phases;
optimizing only the native emitter cannot close the gap.

### E2. Exact hot-function differential

The largest positive self-minus-Clang Ir differences are:

| function | excess Ir |
|---|---:|
| `Lexer::Run` | 1,462,346,526 |
| `Lexer::Peek` | 1,033,749,505 |
| `Token::Token(Token&&)` | 696,546,028 |
| `lowir_model::Instruction::Instruction(Instruction&&)` | 484,081,034 |
| LowIR value simplifier | 434,010,111 |
| `IsIdentifierBody` | 407,189,994 |
| `AppendUTF8` | 301,243,888 |
| `PhysicalCursor::Next` | 247,357,284 |

The broader lexer/cursor/helper cluster is still about 5--6B of the remaining
gap, consistent with the earlier L39 estimate that it owns roughly one quarter
of the total deficit.  The optimizer-pass excess is substantial but more
distributed.  Time attributed to `new` and `free` differs by only about 3--4%,
so allocator volume is not where the missing seconds are hiding.

### E3. Static code shape

The repository ELF code-shape reporter measured:

| metric | self | GCC | Clang |
|---|---:|---:|---:|
| text bytes | 7,861,282 | 5,012,152 | 4,139,251 |
| decoded static instructions | 1,709,256 | 1,064,935 | 942,095 |
| function symbols | 17,234 | 4,981 | 4,677 |
| memory-to-register moves | 384,813 | 165,613 | 149,367 |
| register-to-memory moves | 230,121 | 72,343 | 84,371 |
| register-to-register moves | 245,883 | 125,402 | 118,875 |

The excess is both body count and per-body code quality.  A profitable P31
transformation must not trade an Ir improvement for the text and movement
explosion already observed under full splicing.

### E4. Concrete `Peek`/`Run` shape

`Lexer::Peek` is 417 bytes in self output and 232 bytes in Clang output.  The
self body:

- reloads the queue size repeatedly instead of carrying it through the loop;
- retains an underflow comparison and cold `logic_error` path after a
  dominating non-empty check;
- performs wider address arithmetic than the known capacity requires; and
- copies the three `LocatedCodePoint` fields separately where Clang combines
  the trailing fields.

`Lexer::Run` is 15,109 bytes in self output, 9,539 bytes in GCC output, and
5,104 bytes in Clang output.  Both self and Clang retain 81 static calls from
`Run` to `Peek`, so the self/Clang body gap is not explained solely by whether
`Peek` was completely inlined.  Caller-local predicate simplification and
compact code generation are independently necessary.

### E5. Refreshed no-inline causal ablation

Current same-source host builds with inlining disabled still reproduce the
frozen object and measure:

| compiler build | mean native wall |
|---|---:|
| GCC O1 | 5.830 s |
| GCC O1, no inline | 13.283 s |
| Clang O1 | 5.640 s |
| Clang O1, no inline | 12.383 s |
| self O1 | 9.883 s |

The self compiler beats both host compilers at their no-inline operating
points.  More than half of each host compiler's native time is removed by
inlining and the simplification it enables.  The causal direction from the
earlier P28 ablation therefore still holds at the P30 endpoint.

### E6. Prior negative results remain binding

P31 starts after, rather than before, these conclusions in
`PLAN-INLINE-PARITY.md`:

- L3--L5: broad depth raises reduce some Ir but sharply increase movement and
  text; cap 96 is past the useful knee.
- L39--L41: even 22 loop-selected `Peek` splices regress the honest ratio
  because the full merged bodies cost more than the calls they remove.
- L42--L43: ordinary GVN/PRE and interval splitting do not amortize under the
  current allocation geometry.
- L44--L82: the profitable P30 rematerialization, direct-placement, scratch,
  and call-free-EH slices landed; the adjacent remaining populations were
  measured negative or exhausted.
- L83: another wrapper around the current allocator or memory-SSA engine is
  not a justified next program.

The P31 distinction is that the original outlined call remains on the slow
edge.  It does not import the callee's loop, call, EH, or state-heavy region
into the caller.

### E7. Faster attribution is available

Hardware instruction/cycle events are unavailable on this host, but Linux
software events work.  Native `perf record -e task-clock -F 199` reproduced
the Cachegrind hotspot ordering in approximately one ordinary compile.  It
adds sampling overhead and is not a timing oracle, but it is suitable for
directional attribution.  Ir-only Cachegrind remains deterministic and exact,
but is reserved for finalists.

No profiler process remained after the baseline run.

## 4. Working model

The current choices are too coarse:

```text
outlined call                         full inline
-------------                         -----------
pay call on every execution           import fast + slow + loop + calls + EH
lose caller constants/predicates       expose facts, but overwhelm placement
```

P31 inserts a middle operating point:

```text
caller
  |
  +-- cloned read-only entry predicates
         |
         +-- fast terminal path ------> produce call result locally
         |
         `-- bailout -----------------> execute original call unchanged
```

For a successful clone:

- constants are substituted before its retained cost is measured;
- dominating branch facts eliminate redundant checks in the clone;
- the fast return bypasses the call boundary dynamically;
- the bailout preserves the original callee's complete semantics and EH;
- the clone introduces no side effect that the bailout would repeat; and
- the original callee remains available to every other call site.

This directly attacks operation count while bounding the merged live range and
movement cost that invalidated L41.

## 5. Scope and non-goals

P31 is an O1+ LowIR optimization owned by PA37.  It may change optimized LowIR,
MIR, native objects, and the compiler's own performance.  O0 must remain
unchanged.

In scope:

- direct, non-recursive calls with ordinary scalar or pointer results;
- inline-hinted or otherwise tightly bounded callees;
- acyclic entry regions ending in a fast return or a bailout frontier;
- constant-actual substitution;
- path predicates derived from cloned comparisons;
- affected-region simplification, DCE, slot promotion, and CFG cleanup;
- source-independent loop/callsite heuristics; and
- existing typed compact identities and existing LowIR clone infrastructure.

Out of scope:

- production checks for `Lexer`, `Peek`, filenames, benchmark hashes, or
  particular symbol spellings;
- rewriting `pp_tokenizer.cpp` to hand-split the fast path;
- PGO, persistent profiles, or runtime counters in generated programs;
- cloning stores, volatile or atomic operations, allocation, calls, invokes,
  throws, cleanup entries, or backedges into the speculative prefix;
- general PRE/GVN reactivation;
- increasing the existing full-inline caps or caller budgets;
- new P30 allocator heuristics without a new measured population; and
- fixture edits used to conceal behavior or object mismatches.

A symbol-selective debug switch is allowed only to force and inspect a probe.
It must not participate in production eligibility or a retained test result.

## 6. Required correctness properties

The transformation must establish these properties mechanically:

1. **Side-effect-free prefix.** Every instruction duplicated before the
   bailout is pure or a non-volatile, non-atomic load.  A bailout may reexecute
   such loads in the original function, but it may not repeat a store, call,
   allocation, lifetime event, or externally observable action.
2. **Acyclic prefix.** No cloned edge is a backedge.  A join requiring an
   uncloned predecessor ends the prefix unless all incoming values are
   available and the join remains within the bounded clone.
3. **Complete bailout.** Every frontier edge not resolved to a cloned terminal
   executes the original call with the original arguments and EH context.
4. **Result merge.** Cloned returns and the bailout call define one replacement
   for the original call result.  Void calls require no result merge but obey
   all other rules.
5. **EH preservation.** No protected or throwing instruction is moved out of
   its original region.  The bailout call retains the exact original EH edge.
6. **Identity preservation.** Symbols, relocations, source-owned storage, and
   address-taken reachability remain controlled by the existing program and
   reachability passes.
7. **Determinism.** Candidate order, block creation, value naming, and budget
   consumption use stable typed IDs and source order, never pointer order.
8. **Bounded complexity.** Candidate discovery and cloning are linear in the
   inspected call edges plus the capped entry regions.  There is no fixed-point
   whole-program retry.

The disabled seam must be byte-identical to the P30 endpoint before any
candidate is admitted.

## 7. Investigations and decision rules

### I1. Entry-prefix census

Add diagnostic-only counters for direct O1 call edges:

- caller/callee and inline-hint class;
- constant actual count;
- whether the call site is in a backedge span;
- entry blocks and LowIR instructions visited before each stop reason;
- number and cost of fast terminal paths;
- bailout frontier count;
- rejected calls/stores/EH/backedges/joins;
- estimated cloned instructions before and after constant substitution; and
- caller-local repetition of the same callee.

Run the census over the frozen TU and a full PA37/PA38 build.  Exit only if a
nontrivial, source-diverse population exists.  `Peek` must appear through its
shape; it is not sufficient for it to be the only candidate.

### I2. Single-shape forced probe

Use the diagnostic symbol filter to force one `Peek(0)`-shaped call edge.  The
production matcher remains disabled.  Verify:

- fast returns and bailout calls are both present;
- the frozen output remains correct and reproducible;
- the caller/callee combined MIR and x86 movement are recorded;
- the cloned prefix does not contain calls, stores, EH, or backedges; and
- the existing call executes only on the bailout path in disassembly.

Then expand the diagnostic force to all structurally equivalent `Peek` sites
inside one caller.  This is the go/no-go experiment for the mechanism, not an
accepted policy.

Decision:

- If combined `Run` + `Peek` Ir falls and movement stays bounded, proceed.
- If Ir falls but movement rises by more than 10% in the affected caller,
  shorten the prefix and complete I3 before changing policy.
- If the forced fast path cannot reduce combined Ir, close partial inlining
  and retain only any independently profitable predicate simplification.

### I3. Predicate and range collapse

Extend the existing affected-region simplifier with a deliberately narrow
fact set:

- equality/inequality to constants;
- nonzero facts from dominating branches;
- unsigned `<`, `<=`, `>`, and `>=` facts involving an unchanged scalar load
  or value;
- small constant add/subtract consequences needed to prove checked-accessor
  bounds; and
- known-capacity masks and indexes after constant-actual substitution.

Facts are local to the cloned, call-free region.  Any store, unknown call,
volatile/atomic operation, aliasing uncertainty, or merge without a common
fact kills the affected memory-derived facts.  This is not a new general
memory-SSA pass.

Measure the `Peek` redundant-underflow branch and EH block explicitly.  Land a
predicate improvement separately if it wins exact whole-program Ir without
partial inlining.

### I4. Production admission grid

Only after I2 and I3, run a small grid over:

- post-simplification prefix cap `{8, 12, 16, 24}`;
- per-caller partial-inline budget `{64, 128, 256}`; and
- site class: inline-hinted only, then inline-hinted backedge-span sites.

Do not change the full-inline caps.  Each point records:

- candidates/accepted and every rejection reason;
- LowIR and MIR delta;
- movement by reason;
- caller and whole-object text delta;
- combined caller/callee Ir in software-perf/Cachegrind attribution;
- total frozen Ir; and
- `frontend_ns` and `lowir_opt_ns`, because planner cost is paid by the
  compiler being measured.

Choose the smallest point at the Ir knee.  A point is not admissible merely
because it improves `Run`; total self Ir must fall after optimizer overhead.

### I5. Source-diversity and robustness audit

For every proposed policy, list accepted sites by source file and structural
class.  Require accepted candidates outside `pp_tokenizer.cpp`, and inspect at
least one lexer, semantic, and optimizer example when those populations exist.
Add PA37 reducers for:

- fast return plus bailout result merge;
- a void call;
- constant-actual collapse;
- a rejected store/call/EH/backedge prefix;
- multiple cloned returns;
- deterministic candidate-budget ordering; and
- no transformation at O0.

Reducers test semantics and LowIR shape.  They do not encode benchmark symbol
names or exact incidental block numbers.

### I6. Residual re-baseline

After the first landing, rebuild same-revision GCC and Clang references and
repeat E1--E5.  Decide from the residual whether to:

- widen partial-inlining shapes;
- improve predicate collapse;
- attack the distributed semantic helper bodies;
- reduce LowIR optimizer self-overhead; or
- close P31 and begin a genuinely different allocation geometry.

Do not infer the second step from the pre-P31 profile.

## 8. Implementation phases

### Phase A. Telemetry and disabled seam

Implement entry-prefix discovery, stable stop reasons, debug reporting, and
the block/value/result-merge scaffolding behind a disabled option.  Reuse the
existing inliner clone and naming infrastructure where it is correct for a
partial CFG.  Do not duplicate a second general cloner.

Exit:

- byte-identical frozen object and self reproduction while disabled;
- I1 census complete;
- no file-audit regression; and
- compile-time overhead below 0.2% when disabled.

### Phase B. Forced guarded clone

Implement the side-effect-free prefix clone and original-call bailout.  Use
the debug selector only for I2.  Fix correctness and EH issues before adding a
production policy.

Exit:

- targeted PA37 reducers pass;
- forced `Peek`-shape compile is correct at O1 and O3;
- combined target Ir falls by at least 5%; and
- affected-caller movement rises by less than 10% or falls.

### Phase C. Immediate predicate collapse

Add I3's local facts and invoke existing simplification, DCE, promotion, and
CFG cleanup on the affected caller before costing the clone.  Keep the work
bounded to the changed region and its immediate joins where possible.

Exit:

- redundant checked-accessor paths disappear in reducers and the targeted
  disassembly;
- no general GVN/PRE machinery is activated at O1;
- the forced point improves total frozen Ir by at least 1%; and
- optimizer time does not erase more than one quarter of the target-code win.

### Phase D. Production policy to the knee

Run I4, retain the smallest stable policy at the Ir knee, remove or disable the
symbol-selective force path, and add I5 reducers.

Landing threshold:

- total exact frozen Ir improves by at least 2%;
- five native A/B/B/A blocks confirm at least 1% user-time improvement;
- self/reference frozen objects match exactly;
- frozen text grows by no more than 3%, unless a larger point demonstrates a
  proportionally larger Ir win and remains below the current self text size;
- scalar movement grows by no more than half the percentage Ir improvement;
  and
- no tested non-frozen workload regresses by more than 1% exact Ir without a
  separately explained tradeoff.

### Phase E. Expand only by measured structural class

Use I5 and the post-landing profile to admit one additional structural class
at a time: for example multiple fast returns, a two-block read-only prefix, or
non-loop repeated accessor sites.  Each class gets its own census and landing
entry.  Do not widen all caps in response to one success.

Exit when the program reaches 36.1B frozen Ir or when no remaining class can
clear the landing threshold.  Reaching the Ir target triggers a full honest
ratio measurement before further implementation.

### Phase F. Close and hand off

Repeat I6, document every accepted and rejected class, remove probe-only code,
and state whether the 1.55x and 1.50x targets were reached.  If P31 closes
short, its profile—not the current one—defines the next program.

## 9. Validation ladder

The normal loop deliberately avoids Cachegrind on every edit.

### 9.1 Fast correctness and direction gate

For each implementation slice:

1. build only affected tools/objects with 32-way make where applicable;
2. run focused PA37 optimization and PA38 native tests;
3. verify the frozen self/reference object hash;
4. record the transformation census and affected MIR/movement;
5. run three rotating native samples against a same-revision host reference;
   and
6. run one `--stats` compile to check phase cost.

Reject immediately on wrong output, O0 movement, unbounded census growth,
material movement regression, or a clear native regression.

### 9.2 Fast attribution gate

For a directionally positive point, use:

```sh
perf record -q -e task-clock -F 199 -o /tmp/p31-self.data -- \
  <self-compiler> <frozen-compile-arguments>
perf report -q -i /tmp/p31-self.data --stdio --no-children --sort symbol
```

This is a hotspot sampler, not the timing result.  QEMU software PMU emulation
is not part of the loop: native software `task-clock` sampling is faster and
already available.

### 9.3 Exact finalist gate

Run Ir-only Cachegrind only after the native/stat/census gates agree:

```sh
valgrind --tool=cachegrind --cache-sim=no --branch-sim=no \
  --cachegrind-out-file=/tmp/p31-self.cg \
  <self-compiler> <frozen-compile-arguments>
```

Profile the same-revision GCC reference when the host-source change could move
its denominator.  Use `cg_annotate --diff` for function attribution.  Confirm
the generated object hash after every profiled run, and check for stale
`valgrind`, `cachegrind`, or `callgrind` processes before starting another.

### 9.4 Landing correctness gate

Every retained behavior or policy change must pass:

```sh
make test-pa37
make test-pa38
make test-report-through-pa38
perl scripts/cppgm_file_audit.pl --stage pa38 --paths dev
```

Final O0, O1, and O3 restored-self/inception lanes must match byte-for-byte.
Every inception invocation uses both outer 32-way make and the inner 32-way
setting.  A clean isolated lane has this command shape:

```sh
P31_RUN_ROOT=/tmp/v3codex-p31-COMMIT-j32

/usr/bin/time -v make -C pa39 -j32 cppgm++-self \
  INCEPTION_OBJ_ROOT_BASE="$P31_RUN_ROOT" \
  INCEPTION_BUILD_JOBS=32

/usr/bin/time -v make -C pa39 -j32 compare-cppgm++-inception \
  INCEPTION_OBJ_ROOT_BASE="$P31_RUN_ROOT" \
  INCEPTION_BUILD_JOBS=32
```

Do not start inception while another build or profiler is running.  Do not use
an inherited partial object root for a clean timing claim.

## 10. Risks and fallbacks

- **R1: structural hotness is guessed incorrectly.** The fast return may be
  statically early but dynamically rare.  The original call makes this safe,
  not profitable.  Use combined caller/callee Ir and software-perf attribution;
  reject the class rather than adding benchmark-specific weights.
- **R2: duplicated loads increase work on bailout paths.** Keep the clone
  short, require a terminal before any side effect, and measure total Ir.  A
  future shared-load handoff to the callee is out of scope until ordinary
  partial inlining wins.
- **R3: predicate facts become unsound across memory changes.** Kill
  memory-derived facts conservatively at stores, calls, volatile/atomic
  operations, and uncertain joins.  Prefer a missed fold to a stale fact.
- **R4: EH metadata or reachability drifts.** The cloned region contains no
  throwing instruction; the original bailout call retains its original EH
  context.  O1/O3 reducers and inception are mandatory before policy work.
- **R5: optimizer cost erases generated-code savings.** Bound discovery and
  cloning, reuse scratch, and record `lowir_opt_ns` at every grid point.  A
  target-only win that loses whole-compiler Ir is rejected.
- **R6: text grows without dynamic value.** Use post-simplification budgets and
  the 3% landing envelope.  Do not infer profitability from removed call count
  alone.
- **R7: the partial shape is too small to matter.** Land predicate cleanup only
  if independently profitable, close P31 honestly, and use the new residual to
  choose between semantic operation-count work and a different global
  allocation geometry.
- **R8: fixture churn obscures the result.** New semantic bugs get reducers in
  the earliest owning active suite.  Optimized-shape fixtures change only when
  the new general contract intentionally requires it, through documented
  reference regeneration.  Minor, correct MIR improvements are not a rejection
  by themselves; document and regenerate them normally.  They also do not by
  themselves prove a whole-compiler performance landing, which remains a
  separate measured decision.

## 11. Ledger

- **P31-L0 (BASELINE AND CAUSAL ATTRIBUTION COMPLETE).** At `37e17155`, six
  rotating native samples measure self/GCC/Clang at 9.883/5.830/5.640 s wall.
  Exact Ir is 40.046B/20.439B/20.803B and all outputs reproduce
  `f5f3a11c...4753`.  Phase timers put 61--64% of the time deficit in the
  frontend and 24--25% in LowIR optimization.  Static self output is 1.61x
  GCC and 1.81x Clang in decoded instructions, with 2.3--3.2x their static
  load/store movement counts.  Current no-inline GCC and Clang builds slow to
  13.283 s and 12.383 s, so inlining and enabled collapse remain causal.  The
  lexer/cursor/helper cluster owns about one quarter of the dynamic gap, while
  new/free cost is nearly flat.  Native software `task-clock` profiling
  reproduces the hotspot ordering; no stale profiler remains.
- **P31-L1 (NEXT ACTION).** Implement Phase A's disabled entry-prefix census
  and byte-identity seam.  Do not begin with a transformation, cap change, or
  allocator edit.

Append one ledger entry for every completed investigation, retained landing,
or rejected structural class.  Record the exact tree, reference tree, frozen
object hash, census/movement/text delta, native protocol, exact Ir when run,
correctness matrix, and profiler cleanup state.

Forward residual work is specified in `PLAN-HOT-LOOP-RESIDENCY.md`.  This file
remains the P31 hypothesis, protocol, and experiment record; the successor does
not reinterpret the guarded-prefix result as a landing.

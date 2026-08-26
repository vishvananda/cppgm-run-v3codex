# PLAN-HOT-LOOP-RESIDENCY: P32 residual operation-count parity

Status: active; retained-change coverage has been converted to
student-implementable property/behavior oracles; pressure-aware edge-local
loop-phi capacity is retained as a minor increment; reserved-scratch vector
chunks through naturally aligned 64-byte fixed copies are the latest
performance landing; the exact-cost 16-byte vector zero is retained as a
minor native-positive quality increment; immediate-call merge sources may
donate their frame homes as a minor MIR-quality increment; direct immediate
comparisons for large switches are the latest performance landing;
performance work continues from 1.558x GCC and 1.588x Clang

Date: 2026-08-26

## 1. Objective

Continue the self-`-O1` parity program after the guarded-partial-inlining
experiment in `PLAN-GUARDED-PARTIAL-INLINING.md`.  P31 established that a
small speculative prefix is not the missing operating point: the tested
prefixes reduced samples in `Lexer::Run` and `Lexer::Peek`, but increased or
left flat whole-compiler native time.  The useful P31 survivors instead remove
ordinary work: adjacent scalar move coalescing, a proved nonzero-underflow
fold, and potentially fully overwritten aggregate initialization.

P32 attacks the residual in two measured classes:

1. keep loop-carried and loop-invariant values resident in registers inside
   large, already-inlined hot callers, beginning with the binary-search loops
   duplicated into `Lexer::Run`; and
2. remove distributed construction, move, initialization, and helper traffic
   that remains visible across the frontend and LowIR optimizer.

The exit criterion is an honest same-revision native wall ratio of at most
**1.50x against both GCC O1 and Clang O1** on the frozen workload.  The host
references and the self compiler must be built from the same source revision.
Generated text, data, relocations, section layout, and observable behavior must
agree; compiler-dependent numeric spelling of otherwise identical local
symbols is not a semantic difference.  This target is not relaxed if one
compiler reaches it first.

P32 is not a new global inline-cap sweep, another guarded-prefix grid, or an
assumption that removing frame prologues alone will close the gap.

## 2. Frozen workload and starting points

The workload remains:

```text
/home/vishvananda/cppgm-extended-pa39-source-layout/
  benchmarks/self_compile/stable/semantic_overload.cpp
```

with:

```sh
cppgm++ -std=gnu++11 -Wall -O1 \
  -I/home/vishvananda/cppgm-extended-pa39-source-layout/dev/src \
  -c -o semantic_overload.o semantic_overload.cpp
```

### 2.1 Closed P30 baseline

At `37e17155`, six rotating native samples and exact Ir-only Cachegrind gave:

| compiler | wall | user | Ir |
|---|---:|---:|---:|
| self O1 | 9.883 s | 9.407 s | 40,046,026,786 |
| GCC O1 | 5.830 s | 5.362 s | 20,438,817,693 |
| Clang O1 | 5.640 s | 5.173 s | 20,803,446,126 |

The ratios were 1.695x self/GCC and 1.752x self/Clang in wall time.  All three
compilers emitted:

```text
f5f3a11c079a07da2ab4b891828ade8a4332f32ac67c77417e46f25b20ba4753
```

### 2.2 Provisional P31 survivor point

The adjacent scalar-copy coalescer, including the same-class copy/move
constructor no-alias proof, reduced the self compiler by about 1.5--1.7%
relative to the predicate-only build.  A preliminary three-sample
same-source comparison measured:

| compiler | wall | user |
|---|---:|---:|
| self O1 | 9.797 s | 9.317 s |
| GCC O1 | 5.817 s | 5.373 s |
| Clang O1 | 5.667 s | 5.200 s |

All three emitted:

```text
a54d2c816036ee7779f55df842f935631c92521e0186b69e30f54f87cd418f65
```

This point is provisional until the P31 survivor tree passes the full gates
in Section 8.  Its preliminary wall ratios are 1.684x against GCC and 1.729x
against Clang.  With those denominators, 1.50x would require self wall at or
below 8.726 s and 8.501 s respectively: about 1.30 s, or 13.2%, below the
provisional self time.  Host denominators must be remeasured after every
source landing; these numbers size the work but do not freeze the denominator.

### 2.3 Current source-matched P32 checkpoint

After the retained-change audit and the P31/P32 survivor cleanup, the
pre-landing six-sample medians were 9.470 s self, 5.785 s GCC, and 5.675 s
Clang.  Native software perf and final MIR then isolated repeated scalar
merge-edge traffic in the inlined punctuator classifier inside `Lexer::Run`.

The first P32 landing transfers a frame home's ownership through a single-use
acyclic phi chain.  A chain inside a cycle is admitted only when both
non-loop-carried phis belong to the same strongly connected component, which
proves that the source is refreshed before each dynamic use.  Loop-carried
phis, multi-use sources, type changes, and loop invariants feeding a repeated
merge retain independent homes.

Six rotating source-matched samples after that landing measured:

| compiler | wall median | user median | wall ratio |
|---|---:|---:|---:|
| self O1 | 9.420 s | 8.950 s | 1.000x |
| GCC O1 | 5.795 s | 5.335 s | 1.626x |
| Clang O1 | 5.700 s | 5.225 s | 1.653x |

Self and GCC emit the exact object
`10842620113e31fd83cd76e75335c26227e2e01b9b83b310f2fe9e4ad1080abb`.
Clang differs only in two compiler-created local-symbol numeric spellings;
text, data, relocations, sections, disassembly, and object size are otherwise
identical.

Against a feature-off build, four source-diverse translation units remove 126
MIR instructions, 63 loads, 63 stores, and 904 text bytes.  `Lexer::Run`
falls from 3454 to 3444 MIR instructions, loses five load/store pairs, and
shrinks from 15,261 to 15,188 bytes.  The large LowIR slot-promotion function
loses 12 MIR instructions and six load/store pairs.  The through-PA38 gate is
5452/5452, the PA38 file audit passes, and 32-way O1 inception matches.

The second P32 landing protects call-preserved register capacity for a
frequently reused call result defined and consumed within one cyclic choice
region.  The definition must dominate every use, every use must be in the same
cyclic component, and the value must be refreshed on each iteration; loop
invariants and values without that proof keep the existing placement path.
This adapts I3's original call-free-region sketch to the measured shape: the
choice inputs themselves are call results and some paths contain an optional
later call, so caller-saved-only placement cannot represent their lifetime.

Against an exact-current-source feature-off compiler, six rotating samples
measure 9.435 s wall / 8.960 s user enabled versus 9.535 s / 9.040 s disabled,
improvements of 1.05% wall and 0.88% user.  Ir-only Cachegrind measures
39,022,955,684 versus 39,200,289,897 instructions (-177,334,213, -0.452%).
`Lexer::Run` falls 2,497,059,077 to 2,332,487,627 instructions (-6.59%) and
accounts for 92.8% of the whole-process instruction reduction.  All native and
profiled A/B outputs are byte-identical at
`56cc21765910967a1bf95138dac2f4144dbb7dc265ce16ae7d5411012fb28250`.
The last fully source-matched host medians imply provisional ratios near 1.62x
GCC and 1.65x Clang; rebuild both hosts after the next cumulative landing.

A fresh production checkpoint then rebuilt both host compilers from the same
source and measured 9.435 s self, 5.830 s GCC, and 5.720 s Clang wall medians
on six rotating samples.  The honest ratios were 1.618x and 1.650x.  Native
`task-clock` attribution put the next visible gaps in `Lexer::Run`,
`std::vector<unsigned>::_M_fill_append`, `PhysicalCursor::Next`, and
`Program::EnsureEntry`; the vector helper still carried loop phis through
frame homes despite already preserving the full call-saved pool.

The LowIR pipeline now repeats ordinary scalar-slot promotion after late and
post-prune definition-removing inlining.  On the current PA11 module the late
revisit changes 15 additional functions, removes 14 LowIR instructions, and
reduces final MIR by 26 instructions and 23 frame operands beyond the
post-prune-only point.  The implementation constructs reusable CFG/dominator
state only after promotion succeeds.  Six native A/B samples still show a
small cost (9.535/9.065 s candidate wall/user versus 9.480/8.995 s before the
cleanup), so this is retained as a genuine PA37 code-quality improvement, not
claimed as a performance landing.

The third P32 native landing admits loop-carried scalar phis in a bypassable
fast arm only when the phi dies before the first call, the fast-loop header
cannot reach any call, and the sibling call-bearing path already has at least
five values live through that call.  On the frozen PA11 module the final gate
changes exactly `_M_fill_append`: its frame size and five-register preserve
set are unchanged, whole-function scalar loads/stores fall 71 to 66, and the
fast loop loses all six per-iteration frame accesses.  Against the immediately
preceding compiler, six rotating medians improve 9.595 to 9.525 s wall
(-0.73%) and 9.120 to 9.050 s user (-0.77%); three balanced native
`task-clock` samples improve 9529.32 to 9475.71 ms (-0.56%).  All twelve
native outputs are byte-identical at `13e3c638...7e9ea`.

## 3. What P31 established

### E1. Guarded partial inlining is closed for the measured shape

The disabled census and guarded clone seam were implemented.  Tested
prefix/budget points included 24/24, loop-ranked 24/128, ranked 24/128, and
24/512 variants.  They ranged from approximately flat to 1.2% slower in native
time.  Software `task-clock` samples showed local reductions in `Run` and
`Peek`, but the total compiler did not improve.  The mechanism therefore does
not clear its own whole-program keep criterion.

The diagnostic census can remain stats-only.  The production partial-clone
policy and driver knobs should not remain active merely because the clone is
correct.

### E2. Predicate collapse is correct but not a major time source

A conservative CFG fold proves that an unsigned `x - 1` underflow branch is
dead after a dominating, memory-stable `x != 0` edge.  It fires twice on the
preprocessor, removes the intended branch and cold path, and shrinks the
object, but native time is within noise.  It is a valid code-quality change;
it is not evidence for widening the partial inliner.

### E3. Adjacent scalar move coalescing is a real survivor

The staged-copy pass coalesces adjacent scalar `INDEX`/`LOAD`/`STORE` groups
to `COPYOBJ` only when the bases are the same or mechanically proven disjoint.
The same-class copy/move constructor boundary marks its two object parameters
no-alias.  On the representative macro TU this finds 3 runs, 31 groups, and
146 copied bytes.  `Token::Token(Token&&)` ends in `copyobj 61x1`, and its
native size falls from 311 to 213 bytes.  PA37 and PA38 focused suites pass.

This is the strongest measured P31 landing candidate.  Its new object hash is
intentional generated-code improvement, not a correctness rejection.  The
same-revision host and self outputs agree exactly.

### E4. Blanket loop outlining is not the answer

Rejecting every ordinary loop-bearing inline target reduced frozen text by
2.5% and made `IsInRanges` visible again.  It reduced `Lexer::Run` samples,
but newly outlined standard-library loops recovered much of the saved cost.
The broad point improved wall only about 0.7%; restricting the rule to shared
loop bodies improved about 0.4% wall and 0.15% user in a noisy three-block
comparison.

The conclusion is not that all small loop improvements should be discarded.
It is that `contains a backedge` is too coarse a production classifier.  P32
must distinguish loops whose inlining exposes useful scalar work from loops
whose cloned induction state becomes frame traffic.

### E5. Fully overwritten initialization is promising but unmeasured

A conservative pass now recognizes a bounded, nonvolatile `ZEROINIT` whose
entire byte range is overwritten by scalar stores before any observation.  On
the inheritance probe it removes four 24-byte zero-initializations and shrinks
`LookupResult::LookupResult()` from 207 to 175 native bytes.  This candidate
still requires P31 correctness, same-output, and native timing gates.  P32
must record it as provisional rather than count its hoped-for gain.

## 4. Where the remaining time is hiding

Native software `perf record -e task-clock -F 199` is the fast attribution
tool.  It reproduces the Cachegrind hotspot ordering in one ordinary compile
and avoids waiting for exact simulation on every candidate.  It is not used as
the timing oracle.

At the current source-matched point, the leading self samples include:

| self symbol | self sample share | interpretation |
|---|---:|---|
| `Lexer::Run` | 5.50% | largest single attributable frontend gap |
| `Lexer::Peek` | 3.23% | high share, but roughly equal absolute Clang time |
| `PhysicalCursor::Next` | 2.01% | lexer/cursor tower |

Approximate task-clock attribution puts `Lexer::Run` near 0.65 s in self and
0.18--0.20 s in the host builds, leaving about 0.45 s of excess.  `Peek` is
near 0.30 s in both self and Clang despite its different percentage.  Thus
`Peek` is no longer a justified primary target, while `Run` is.

Static code shape supports the same conclusion:

| `Lexer::Run` producer | native bytes |
|---|---:|
| self O1 before phi-home landing | 15,261 |
| self O1 after phi-home landing | 15,188 |
| GCC O1 | 9,539 |
| Clang O1 | 5,104 |

Self and Clang both retain 81 static `Run`-to-`Peek` calls.  The `Run` gap is
therefore in the caller body, not simply a failure to inline `Peek`.  Mapping
native software samples back to generated blocks corrected the earlier
ranking: the hottest cluster is the inlined `ScanPunctuator` switch and its
nested ternary/phi choice web around `Run+0x1600`, not the duplicated
`IsInRanges` binary searches.  GCC and Clang also inline `ScanPunctuator`, so
outlining it would not explain host parity.

Before the landing, `Run` contained about 3,108 native instructions versus
1,983 GCC and 1,283 Clang.  Its census reports 498 planned values, only four
successful grants, 311 busy-location rejections, and 298 planned values that
ultimately define in frame homes.  The landing removes five repeated
load/store pairs but leaves the much larger placement-pressure population.

The largest next plausible individual win is therefore a bounded, call-free
SCC-local placement probe for the punctuator choice web: temporarily shorten
or spill conflicting outer live ranges at the region boundary, keep only the
iteration-local values in caller-saved registers, and account for every
boundary reload.  This is narrower than the rejected broad merge-phi,
caller-saved loop-phi, and CMOV experiments.  Even eliminating the full
measured `Run` excess cannot finish P32 alone; the remaining gap is distributed
over cursor helpers, semantic tables, construction, and LowIR optimizer work.

## 5. Working model and decision tree

The first P32 question is whether the bad inlined loops can be made cheap in
place:

```text
small loop cloned into a large hot caller
                  |
          loop values resident?
             /             \
           yes              no
           |                |
      retain inline    can placement be fixed
                            /       \
                          yes        no
                          |          |
                    registerize   keep helper outlined
```

The preferred result is local register residency because Clang demonstrates
that the inlined form can be compact.  Selective outlining is the fallback,
not the first assumption.  A production choice must be based on loop shape,
live-state pressure, and resulting MIR movement; it must not mention lexer
symbols or benchmark files.

Frame/rbp elision is secondary.  Earlier P28 census found only about 4% of
functions had zero stack adjustment and no callee-saved pushes because most
values were frame-homed.  P31's overwritten-initialization cleanup may enlarge
that population, so P32 will recount it, but no frame-elision implementation
starts before the new census proves a meaningful dynamic population.

Correct minor MIR/object improvements and performance-policy landings have
different gates:

- A mechanically correct simplification may remain when it reduces generated
  work, preserves all reference behavior, and has no reproducible native
  regression.  Intentional fixture changes are regenerated and documented;
  their small size is not grounds for rejection.
- A heuristic that changes inlining, outlining, or allocation policy must show
  a reproducible whole-compiler benefit or a finalist-level exact Ir benefit.
  Locally prettier MIR alone does not establish policy profitability.

A narrow backend preference may still remain as a minor code-quality increment
when its complete safety proof is mechanical, it adds no function-wide
preserved capacity, generated movement and exact whole-program Ir both fall,
and balanced native timing shows no regression.  Such an increment is recorded
separately and does not count as clearing Phase C's 0.5% performance-policy
gate or the cumulative rebaseline threshold.

## 6. Investigations

### I1. Close the P31 survivor matrix

Build isolated self compilers for predicate-only, scalar-copy, shared-loop,
and overwritten-zero points.  Use five rotating A/B/B/A blocks for adjacent
points, then rebuild same-revision GCC and Clang references for the retained
tree.  Record:

- frozen object equality within each revision;
- PA37 and PA38 focused results;
- LowIR, final MIR, movement, and object deltas;
- wall/user means and medians; and
- one software-perf profile for the survivor.

Retain scalar-copy unless a correctness failure appears.  Retain the
overwritten-zero fold if it is correct and non-regressing even if its time gain
is below the native noise floor.  Keep the shared-loop rejection only if a
longer same-revision comparison confirms a benefit; otherwise remove it and
use its evidence to seed I3.

### I2. Hot-loop residency census

Add stats-only telemetry after final MIR planning for every natural loop:

- source function and stable loop header identity;
- block and instruction counts;
- calls, EH edges, and nested-loop depth;
- loop-carried phi/value count and scalar width;
- frame loads/stores inside the loop by value and reason;
- loop-invariant addresses reloaded in the loop;
- available, claimed, clobbered, and callee-saved register counts over the
  loop span;
- whether the loop came from an inlined source site; and
- estimated dynamic priority from native software samples, kept outside the
  production policy.

Inspect `Lexer::Run`, `IsInRanges`, `PhysicalCursor::Next`, one semantic-table
loop, and one LowIR optimizer loop.  The census must find a source-diverse
population before a general allocator change is attempted.

Decision:

- If loop values are demoted despite an actually free register, proceed to I3.
- If registers are exhausted by long outer live ranges, run I4 before changing
  allocation.
- If the `Run` loops are exceptional and no source-diverse population exists,
  skip the general placement slice and use selective outlining only if I4
  clears its whole-program gate.

Current result: the natural-loop census is implemented with CFG backedges
whose headers dominate their latches.  It reports final-MIR blocks,
instructions, calls, EH operations, nesting depth, frame operands, function
frame bindings, and callee-saved count.  The companion function census reports
planned-location contention and actual definition locations.  Across
tokenizer, macro processor, PA11 model, and LowIR optimizer sources it finds
22, 214, 156, and 297 natural loops respectively.  Large loops contain 1,448
to 2,775 frame operands, and the representative large functions have 192 to
691 busy grant failures.  The source-diverse pressure population is therefore
established; exact values are diagnostic, not test or policy thresholds.

### I3. Bounded loop-local residency probe

The broad loop-phi and invariant probes are closed: caller-saved loop phis,
all merge-phi register homes, broad segmented residency, and added
callee-saved homes either regress through save/restore work or remain flat.
The next diagnostic probe is narrower.  Select one call-free SCC subregion,
spill or end only the outer live ranges that block it, and keep its
iteration-local choice values in caller-saved registers until the region
exit.  Boundary stores/reloads, encoder scratch, EH edges, and every fixed
register effect are part of the cost.  Reuse P30's location timeline; do not
add a second allocator.

Probe the inlined `ScanPunctuator` choice web in `Lexer::Run`, then
structurally equivalent call-free SCC regions in at least two other source
areas.  Record affected-function MIR, frame movement, boundary movement,
native bytes, and software-perf attribution.  Do not retest broad CMOV
if-conversion or blanket register-homed merge phis.

Proceed to production policy only if:

- region frame movement falls by at least 25% after boundary cost;
- the affected function does not add more save/restore work than it removes;
- combined affected-symbol task-clock or exact Ir improves; and
- frozen whole-compiler native time is directionally positive.

Current result: the measured residual was a guarded call-free fast arm rather
than another broad SCC class.  A first probe that relaxed only the bypass gate
improved `_M_fill_append`, but also changed four unrelated full-save functions:
`NamePath::Push` grew by 17 MIR instructions, `LookupGraphCandidate` by 24,
and `LookupUnqualifiedCandidate` by 6 as phi homes displaced overlapping
pre-call residents.  Full callee-save use alone is therefore not a sufficient
profitability proof.  Requiring the loop header to be unable to reach a call
isolates the path-disjoint class: the PA11 census changes `_M_fill_append`
only, removes its six hot-loop frame accesses without adding save/restore
work, and clears the repeated native-user and task-clock gates above.  The
broad point is rejected; the path-disjoint point is retained.

### I4. Inline-versus-outline placement classifier

For a small loop-bearing callee, collect its cost in both legal states using a
diagnostic trial:

1. retained as an outlined function and call; and
2. inlined and fully cleaned through MIR planning.

Compare definition-removal value, call count, cloned loop count, frame
movement, native bytes, and optimizer cost.  This is a measurement probe, not
a production compile-time dual code generator.

Use the results to derive a cheap pre-MIR structural classifier.  Candidate
inputs may include shared versus single-call ownership, nested-loop creation,
callee loop-carried value count, caller live-state pressure, inline hint, and
definition removal.  `contains a backedge` by itself is prohibited as the
retained rule.

Only pursue production selective outlining if I3 cannot recover the resident
state or the classifier finds a separate source-diverse winning class.

### I5. Construction, move, and initialization residual

After I1, refresh the sample and exact top-function differential.  Investigate
the following structural classes in order of measured residual, not this
document's spelling:

1. scalar stores that completely overwrite a preceding `ZEROINIT`;
2. adjacent disjoint scalar copies not yet represented as `COPYOBJ`;
3. move constructors that copy a constant byte range plus a small state tail;
4. adjacent constant stores safely widened to one store; and
5. vector initialization/growth sequences whose immediately overwritten state
   survives cleanup.

Each transformation needs a no-alias or exact-range proof and earliest-owner
reducers.  Do not infer disjointness from parameter position except where the
typed constructor boundary mechanically proves the language relationship.

### I6. Residual re-baseline against both hosts

After each cumulative 2% self improvement, rebuild same-revision GCC and Clang
O1 compilers and repeat:

- six rotating native samples per compiler;
- one `--stats` phase attribution per compiler;
- software-perf symbol attribution;
- ELF text/movement census; and
- exact Ir-only Cachegrind for finalists.

The next slice is chosen from the new absolute self-minus-host time, not the
old percentage ranking.  In particular, do not keep targeting `Peek` if its
absolute time remains at host parity.

## 7. Implementation phases

### Phase A. P31 cleanup and honest baseline

Complete I1.  Remove or leave disabled the rejected guarded-partial policy,
retain useful diagnostic counters, and decide the shared-loop and overwritten-
zero candidates.  Rebuild all three compilers from the survivor revision.

Exit:

- same-revision object equality;
- PA37 and PA38 focused suites pass;
- no O0 transformation;
- six-sample self/GCC/Clang baseline recorded; and
- no profiler or build process remains.

### Phase B. Loop census and one-function proof

I2 is complete.  Implement the revised diagnostic part of I3 and prove the
mechanism on `Lexer::Run` without a production symbol special case.  Inspect
code and movement before timing.

Exit:

- the two target loops keep selected state in registers safely;
- `Run` native size and frame movement fall;
- `Run` plus any outlined/helper cost falls in attribution; and
- the whole frozen compile is directionally faster.

### Phase C. Source-independent loop policy

Admit one census-proven structural class.  Prefer loop-local residency; use
I4's selective outlining only for shapes that cannot be placed profitably.
Add PA37/PA38 reducers for calls, nested loops, register clobbers, phi edges,
EH rejection, and deterministic ordering.

Landing gate for an allocation/inlining policy:

- at least 0.5% exact frozen Ir improvement or a repeated 0.5% native-user
  improvement with matching attribution;
- no increase in whole-object scalar movement unless smaller dynamic work
  explains and pays for it;
- no tested workload regresses by more than 1% exact Ir without an explicit
  tradeoff; and
- host/self frozen outputs match exactly.

### Phase D. Distributed work removal

Implement I5 one proof class at a time.  Small correct simplifications may
land below the performance-policy threshold when they reduce LowIR/MIR/object
work and do not regress native time.  Maintain a separate cumulative timing
ledger so these improvements are not mistaken for achieving the P32 target.

### Phase E. Re-baseline and repeat

Run I6 whenever the cumulative self numerator improves by about 2%.  Re-rank
absolute gaps, then return to Phase B or D.  Stop expanding a mechanism when
its measured population is exhausted.

### Phase F. Exit validation

P32 completes only when the same-revision six-sample means are at most 1.50x
both hosts and the correctness matrix in Section 8 is clean.  A meaningful
improvement that remains above either ratio is a landing, not completion.

## 8. Validation and fast measurement protocol

### 8.1 Retained-change test ownership gate

No transformation or code-generation policy is considered retained until it
has an explicit entry in the parity coverage matrix.  The audit starts with
every production survivor recorded in `PLAN-O1-PARITY.md`, continues through
every retained landing in `PLAN-INLINE-PARITY.md`, and then covers the P31/P32
worktree.  The entry records:

- the student-facing README paragraph that states the high-level feature and
  its safety boundary strongly enough for an independent implementation;
- the implementation seam and the earliest assignment that owns its contract;
- at least one positive reducer that proves the documented relationship or
  behavior at the earliest observable LowIR, MIR, or native surface, while
  permitting materially different correct implementations;
- negative reducers for each material safety guard (for example aliasing,
  volatility, EH, frame-address, dynamic-stack, or successor-phi rejection);
- an O0/non-applicable-level reducer when the change is restricted to O1+;
- any intentionally changed authoritative fixture and why it moved; movement
  of a whole-output fixture is maintenance evidence only and never substitutes
  for the focused feature oracle; and
- the focused command plus the full PA37/PA38 gates that exercise the entry.

Ownership follows the first observable compiler boundary:

- LowIR simplification, inlining, alias facts consumed by LowIR, and CFG shape
  are owned by PA37.  A real LowIR improvement must be serialized and tested
  there; it must not be moved into native preparation merely to avoid updating
  a PA37 expectation.
- Baseline LowIR-to-MIR selection and native encoding are owned by PA29.  Use
  its strict or structural lane when the documented MIR shape is the contract,
  and its behavior lane when several legal register/allocation/encoding shapes
  satisfy the documented rule.
- O1+ MIR rewriting, whole-function register/frame policy,
  prologue/epilogue layout, unwind metadata, and final machine control-flow
  layout are owned by PA38.
- A change spanning both boundaries needs an owning reducer in each suite, not
  only a final generated-program behavior test.

Existing fixtures should be reused when they already isolate the behavior.
When the general contract intentionally improves their expected shape, update
the authoritative expectation in place and record the diff in the matrix.  Add
a new reducer under `cppgm.tests/course/paN/` when no existing fixture states
the positive case or a material rejection guard.  Do not weaken comparison,
special-case a test, or count broad suite passage as proof of coverage.

The feature oracle must not compare complete program contents at any compiler
boundary: not a complete generated executable, object, MIR module, or LowIR
module, and not a hash or byte-for-byte transcription of the current compiler's
answer.  Such comparisons freeze one implementation's layout, instruction
selection, allocation, scheduling, or serialization instead of specifying an
assignment students can implement from the README.  A student-facing README
must first describe the high-level capability and its safety boundary.  The
test then validates only the smallest distinguishing structural relationship
or observable behavior.  Native selection and encoding normally belong in an
earlier PA2x structural or behavior fixture: canonical MIR predicates only
when that particular MIR relationship is normative, and runtime results when
several legal MIR/allocation/encoding shapes satisfy the rule.  PA38 controls
are reserved for genuinely O1+ whole-function placement or MIR rewrite policy.

Checked-in complete outputs may still move because the ordinary assignment
harness records them, but they are compatibility gates, not ownership proof.
Likewise, exact output identity is useful during performance A/B validation but
is not a student feature test.  No implementation may recognize a fixture,
known compiler function, source spelling, or complete instruction sequence in
order to satisfy either kind of check.

Semantics-neutral compiler-work reductions (scratch reuse, fixed-field
dispatch, bulk input, and similar changes) cannot normally be distinguished by
an output fixture.  Their entry instead names the exercised pipeline, the
state/invariant that prevents semantic drift, an owning regression or
byte-identity check, and the representative invocation that executes the hot
path.  A broad suite is still only a gate, not the sole ownership evidence.

Before P32 timing resumes, audit every production survivor recorded by
`PLAN-O1-PARITY.md`, `PLAN-INLINE-PARITY.md`, and the P31/P32 worktree.
Uncovered survivors block further performance work until focused coverage is
backfilled and passes.  Rejected probes and default-disabled telemetry need a
test only if their code or public control surface remains in production; dead
probe controls are removed instead.

The earlier audit disposition is reopened wherever it cited only broad suite
passage, incidental complete-output movement, or an exact fixture.  Such a row
is not closed until its README contract and focused structural or behavioral
predicate have been identified and verified.

#### Historical survivor inventory

The tables below record the completed audit.  `owned` means the named
checked-in test states the relevant positive shape and its important rejection
cases.  `invariant` is reserved for semantics-neutral compiler-work changes
whose output is intentionally byte-identical; those rows still name the
exercised pipeline and identity/invariant check.

`PLAN-O1-PARITY.md` survivors:

| Survivor | Earliest owner and present evidence | Audit disposition |
|---|---|---|
| P0a retained deferred-address carriers | PA29 focused pass over `deferred-address-parameter-carrier-reuse` captures the ABI parameter carrier/home, checks direct pre-call address use and post-call restoration, and executes the reducer | owned focused structural/behavioral predicate |
| P0b extern-template member-template instantiation | PA32 `200-extern-template-member-template-instantiation` | owned |
| P1 `__OPTIMIZE__` and `-D`/`-U` preprocessing parity | PA15 O0 `510-no-optimize-predefine`; PA37 driver O1 `455-source-optimize-predefine` | owned |
| P1c edge-live register lifetime across backedges | PA38 survivor-property check over `420-loop-and-eh-placement` compares unavoidable and guarded loop homes and executes both levels | owned focused structural/behavioral predicate |
| P4a O1 scalar-slot promotion/dead stores | PA37 control `525-historical-lowir-contracts` checks the O0 slot/store baseline, O1 removal/forwarding, and an escaped-slot negative, then executes optimized output | owned focused LowIR/behavioral predicate |
| P4a-c1 homes for promoted/direct parameters crossing clobbers | PA38 survivor-property check over O2 `400-loop-invariant-call-crossing-placement` requires loss of the O0 frame home and positive call-safe capacity without naming a register | owned focused structural/behavioral predicate |
| P4d strength reduction, readonly-global folding, and exact pointer-difference shifts | PA15 control `530-pointer-difference-strength-reduction` checks power-of-two shift and non-power-of-two division at source-to-LowIR; PA37 control `525-historical-lowir-contracts` checks readonly folding and loop strength reduction against O0/non-applicable shapes | owned focused structural/behavioral predicates; complete-output fixture movement is compatibility evidence only |
| P3 boolean-conversion branch forwarding | PA37 survivor-property check over `385` requires removal of redundant Boolean conversions and retention of a value-changing truncation | owned focused structural predicate |
| P4c O1 edge-register retention | PA38 survivor-property check over `420` requires the unavoidable-loop values to lose O0 homes while the guarded-loop controls retain them; behavior runs | owned focused structural/behavioral predicate |
| P5a raising-block serialization | PA37 survivor-property check over `390` compares only hot/raising block order in the guarded and chained cases | owned focused structural predicate |
| noreturn continuation truncation retained during P5 | PA37 survivor-property check over `395` requires the post-noreturn jump to disappear while validating the reachable control's phi predecessors | owned focused structural predicate |
| constructor early-return EH closure | PA26 `210-constructor-early-return-cleanup-region` | owned |
| P9a cold-only pure-definition sinking | PA37 survivor-property check over `392-sink-cold-only-definitions` checks cold rematerialization and the hot-use negative | owned focused structural predicate |
| P9b negated boolean compares | PA37 survivor-property check over `386` requires one inverted integer compare and retains the float pair | owned focused structural predicate |
| P9c same-block duplicate loads | PA37 survivor-property check over `387` checks the local load-count reduction and store/call barriers | owned focused structural predicate |
| P9d small direct `copyobj` encoding | PA29 control `905-small-copy-boundary` retains the 32-byte MIR operation, rejects string-operation encoding in the entry body, and executes it | owned focused MIR/native/behavior predicate |
| P9e small-object promotion and object-valued-address guard | PA37 control `525-historical-lowir-contracts` checks O1 removal of an eligible complete scalar object and retention of a `copyobj` whose opposite operand is an object value; optimized behavior also runs | owned focused LowIR/behavioral predicate |
| P11 frame-operand small copies and constant-multiply selection | PA29 controls `900`-`902` require shift/indexed-add instruction families and two direct frame operands, then execute each reducer | owned focused MIR/native/behavior predicates |
| P12 staged object-copy forwarding | PA37 survivor-property check over `388` requires eligible staging/copy removal and retains the conditional-field home | owned focused structural predicate |
| layout-backedge spill safety | PA29 `wide-parameter-loop-exit-spill` behavior reducer | owned |
| P10 v7-v10 planned residency, sound interval release, and alias accounting | PA38 survivor-property checks over `420`, `426`, and `428` inspect only local home, release/reuse, and live-address relationships and execute both levels | owned focused structural/behavioral predicates |
| P10 v11 call-preserved capacity and single-use plan coverage | PA38 control `445-historical-placement-contracts` checks an eligible single-use call-crossing value loses its O0 frame home and receives call-safe capacity | owned focused structural/behavioral predicate; no register name or complete MIR oracle |
| P13 deferred compare across clobbering calls | PA38 survivor-property check over behavior reducer `405` requires the operand loads before, and compare after, the call with call-safe capacity; behavior runs | owned focused structural/behavioral predicate |
| P14 bulk source/include reads | all driver and preprocessor inputs exercise the path; frozen host/self objects were byte-identical | invariant |
| P16 direct local-global access and symbol-address deferral | PA29 control `903-direct-global-storage` checks direct symbol storage operands versus an observed-address materialization and executes the reducer | owned focused structural/behavioral predicate |
| P17 call-free caller-saved planning | PA38 control `445-historical-placement-contracts` checks a call-free branch adds no frame home or preserved capacity, a call-crossing twin remains protected, and two post-call tails stay frameless under five-value preserved pressure | owned focused structural/behavioral predicate; no caller-register names |
| P17b call-free planning inside EH functions | PA38 survivor-property check over `420` compares the call-free/call-crossing EH pair's homes and preserved capacity, then executes it | owned focused structural/behavioral predicate |
| P18a volatile LowIR contract and preservation | PA15 control `541` checks access markers and an ordinary-member negative; PA37 property `540` checks O0/O2 preservation versus ordinary elimination; PA38 property `406` checks all four accesses reach MIR and runs behavior | owned focused cross-boundary structural/behavioral predicates |
| P18b LowIR `select` plus MIR `cmov` contract | PA37 property `505` retains one live typed choice while removing its unused sibling; PA38 property `407` requires one local conditional choice per function and runs behavior | owned focused cross-boundary structural/behavioral predicates |
| P18c non-overlapping `copyobj` contract | PA13 specification only; implementation already matched | documentation contract |
| P19 readonly const-scalar global lowering | PA15 control `540` checks readonly scalar storage plus volatile, TLS, and class-object negatives; PA37 control `525` checks optimizer consumption | owned focused source/optimizer predicates |
| P20 unchecked lexer lookahead after proven guards | compiler-source-only specialization; tokenizer suites plus inception exercise it | invariant; run PA3 and inception |
| P22e dead cloned-phi filtering and moved-edge phi repair | PA37 survivor-property check over `511` validates every phi input/predecessor relationship, dead-input removal, and the cross-slot join values | owned focused structural predicate |
| P25a `--stats-functions` placement census | PA38 driver `440-function-census` requires records for the expected source functions and the documented numeric fields, without fixing values or timing | owned structural diagnostic |
| P27a aggregate SROA and its escape/overlap guards | PA37 survivor-property checks over `389`/`388` require component/copy collapse and retain escaping/conditional controls | owned focused structural predicates |
| P27a cold-path-discounted inlining | PA37 survivor-property check over `510` compares call presence only between nonreturning-cold and equivalent hot work | owned focused structural predicate |
| P27b callee-first collapse convergence | PA37 control `524-post-prune-inline-slot-promotion` checks the retained O1 post-inline cleanup at its distinguishing call/slot/phi boundary; existing `360-inline-after-callee-simplify` and O3 `560` remain compatibility reducers | owned focused O1 structural/behavioral predicate |

`PLAN-INLINE-PARITY.md` survivors:

| Survivor | Earliest owner and present evidence | Audit disposition |
|---|---|---|
| L1 `--inline-limit` diagnostic controls | PA37 control `520-inline-limit-once-cap` proves the default/overridden structural call-retention distinction and rejects invalid values through `lowiropt`; `522-driver-inline-limit` exercises all repeatable names and the same rejection rules through `cppgm++` | owned structural diagnostic |
| L6/L31 default h48-b768 inline operating point | PA37 survivor-property checks over `391`, `475`, `476`, and `490` use only call-presence/count inequalities and policy guards | owned focused structural predicates |
| L9 trivial-leaf budget exemption | PA37 survivor-property check over `392-inline-trivial-leaf-budget-exempt` distinguishes the exhausted-budget tiny and larger calls | owned focused structural predicate |
| L12 phi homes/backedge coalescing and L13 direct load into a phi home | PA38 survivor-property check over `420` compares only unavoidable/guarded local frame-home relationships and executes the reducers | owned focused structural/behavioral predicate |
| L14 unavoidable-loop invariant residency | PA38 survivor-property checks over `420` and O2 `400` cover unavoidable/guarded and call-crossing capacity relationships | owned focused structural/behavioral predicates |
| L15 nested binary-display semantic-analysis lifetime fix | PA17 control `533-enclosing-temporary-lifetime` checks selected operator use, reverse destruction, unwind cleanup, and generated behavior | owned focused LowIR/behavioral predicate |
| L17-L19 R10/R11 bounded frame-load carries and float exclusion | PA29 `scratch-carried-frame-reloads` now requires a positive `scratch_carried_reloads` fact and runs integer/float-pressure behavior; the predicate names no register and fixes no count | owned focused diagnostic/behavioral predicate |
| L22 deserving call-crossing callee-saved placement | PA38 O2 survivor property `400` requires the O0 home to disappear with positive call-safe capacity and retains a floating control | owned focused structural/behavioral predicate |
| L26 bitset/hoist/epoch optimizer self-cost reduction | PA37 optimizer pipeline is byte-identical | invariant |
| L30 final-use edge-live release and L33 span-end planned release | PA38 survivor properties `428`/`426` compare dominated-tail reload counts and dead/live address takeover locally | owned focused structural/behavioral predicates |
| L37 union-safe address deferral and L38 pure-frame constant-index deferral | PA38 survivor property `427` checks direct frame/global/constant-index operands versus variable-index materialization | owned focused structural/behavioral predicate |
| L40 cross-slot-needed promoted-phi insertion | PA37 survivor property `511` requires destination-slot removal and a join phi containing both edge values, then validates predecessors | owned focused structural predicate |
| L44 lazy fallback homes | PA38 control `445-historical-placement-contracts` checks that a completely resident loop value loses its O0 eager fallback home while call/pressure behavior remains correct | owned focused structural/behavioral predicate |
| L46 explicit location timelines | byte-identical representation migration exercised by all planned-placement tests | invariant |
| L47/L49 frame/global address rematerialization | PA38 survivor property `427` checks only direct-address relationships and the variable-index consumer guard; behavior runs | owned focused structural/behavioral predicate |
| L50 constant-index address replay and lifetime extension | PA38 survivor property `427` checks the constant/variable-index pair | owned focused structural/behavioral predicate |
| L53 edge-live identity copies directly into homes | PA38 survivor property `426` captures the ABI parameter location and frame home, then checks their direct transfer without fixing a register | owned focused structural/behavioral predicate |
| L54 three-operand 64-bit add via LEA | PA38 survivor property `426` requires one indexed two-source add relationship without fixing registers | owned focused structural/behavioral predicate |
| L55 staged scalar call results directly from RAX | PA38 survivor property `426` checks the ABI result-to-existing-home adjacency and O0 baseline | owned focused structural/behavioral predicate |
| L56 final-use address-register takeover by loads | PA38 survivor property `426` compares dead/live address pairs and permits any carrier register | owned focused structural/behavioral predicate |
| L62 precomputed cyclic-block spill queries | native output byte-identical; spill-safety and loop-placement reducers exercise the query | invariant |
| L63 removal of the transitional record/replay walk | no public surface or semantic change remains | invariant/removal |
| L66 sparse active-MIR alias facts | final MIR byte-identical; select/copy/native optimization fixtures execute the pass | invariant |
| L67/L68 dominated post-call use tails and full caller-register pool | PA38 survivor property `428` requires loss of the O0 crossing home, unchanged five-register pressure, and fewer tail reloads | owned focused structural/behavioral predicate |
| L72 fixed-operand simplifier dispatch | serialized LowIR and final MIR byte-identical; PA37 optimizer corpus executes it | invariant |
| L73 fixed-use native-analysis dispatch | final MIR byte-identical; PA38 corpus executes it | invariant |
| L77 reusable simplifier scratch | byte-identical PA37 optimizer output, reentrant public entry remains call-local | invariant |
| L78 reusable DCE scratch | byte-identical PA37 optimizer output, reentrant public entry remains call-local | invariant |
| L79 reusable CFG candidate scratch | byte-identical PA37 optimizer output, standalone calls remain call-local | invariant |
| L81 call-free EH load residency | PA38 survivor property `420` compares call-free and call-crossing EH homes/capacity and executes both levels | owned focused structural/behavioral predicate |

P31/P32 worktree survivors:

| Survivor | Earliest owner and present evidence | Audit disposition |
|---|---|---|
| nonzero-underflow predicate fold | PA37 survivor property `506` checks O0 presence, O1 removal, and mutation/volatile guards | owned focused structural predicate |
| adjacent noalias scalar-copy coalescing | PA37 survivor property `507` checks one contiguous copy plus alias/volatile scalar guards; PA17 control `532` and PA37 driver reducers cover source-fact propagation | owned focused structural/behavioral predicates |
| same-class copy/move-constructor boundary noalias facts | PA17 control `532-constructor-alias-boundaries` checks both constructor parameters and the conservative assignment negative, then executes; PA37 driver `460`/`461` prove propagation | owned focused LowIR/behavioral predicate |
| fully overwritten `ZEROINIT` removal | PA37 survivor property `508` checks O0/O1 removal plus partial, observed, volatile, and size guards | owned focused structural predicate |
| shared ordinary loop-body inline rejection | PA37 survivor property `509` compares call presence for shared, single-use, and explicitly hinted loops | owned focused structural predicate |
| guarded partial-clone controls/mechanism | production transform and controls removed; PA37 control `521-partial-inline-census` structurally tests the retained observational `--stats` census without enabling cloning | owned diagnostic/removal |
| O1+ conservative frameless policy and unwind metadata | PA29 control `904` establishes the O0 policy; PA38 survivor property `425` checks eligible leaf/call/return cases and frame, dynamic-stack, float-scratch, and host-EH guards | owned focused policy/behavioral predicates |
| O1+ direct/unshared epilogues | PA29 control `904` establishes shared O0 epilogues; PA38 survivor property `425` requires direct O1 epilogues including the multiple-return reducer | owned focused policy/behavioral predicates |
| single-use acyclic phi frame-home ownership transfer | PA38 control `442-acyclic-phi-frame-home` checks a one-shot chain and an iteration-local same-SCC chain; multi-use, loop-carried, and repeated loop-invariant controls retain independent homes, while runtime behavior catches destructive reuse | owned structural/behavioral predicate; no complete MIR or executable snapshot |
| immediate-call merge-phi source-home donation | PA38 control `442-acyclic-phi-frame-home` checks that an eligible same-SCC source is written directly to the merge home with no identity edge transfer; a non-immediate consumer retains independent homes and the ordinary transfer, while a repeated loop invariant retains a distinct home and behavior catches destructive reuse | owned focused structural/behavioral predicate; no physical register, complete MIR, executable, object, hash, or compiler-source match |
| large-switch literal immediate comparisons | PA29 control `910-large-switch-immediate-cases` requires literal cases to remain MIR immediates and encodable literals to become local register/immediate comparisons, retains a dynamic register-comparison fallback, and executes the reducer | owned focused structural/behavioral predicate; the local decoder accepts short or full-width immediates and any selector register, with no complete MIR, executable, object, hash, or compiler-source match |
| cyclic choice-region call-result residency | PA38 control `443-cyclic-choice-region-residency` checks that O1 keeps a refreshed, repeatedly compared call result available across an intervening call while O0 retains the frame-home baseline; runtime results exercise per-iteration refresh and a loop-invariant guard | owned focused structural/behavioral predicate; no physical register, complete MIR, or executable snapshot |
| post-late/post-prune inline scalar-slot promotion | PA37 control `524-post-prune-inline-slot-promotion` checks that O0 retains the call and scalar slot while O1 removes both, forms the required pointer phi, and preserves generated behavior; a helper-call mutation makes the control fail | owned LowIR structural/behavioral predicate; no complete LowIR or executable snapshot |
| path-disjoint guarded fast-loop phi residency | PA38 control `444-call-free-fast-loop-phi-residency` checks O1 removal of two fast-loop frame homes, the O0 frame-home baseline, unchanged five-register preserved capacity, and a call-reaching negative that must retain both homes; the pre-landing backend fails the control | owned focused structural/behavioral predicate; no physical register, complete MIR, or executable snapshot |
| exact-liveness callee-save recoloring | PA38 control `446-call-free-callee-save-recoloring` checks a call-free multi-store carrier uses caller-clobbered capacity while a call-crossing twin remains call-safe, and executes the reducer | owned focused structural/behavioral predicate; no complete MIR, object, or compiler-source match |
| adjacent single-use frame-compare forwarding | PA38 control `447-adjacent-frame-compare-forwarding` checks the direct register comparison, O0 frame baseline, multi-use guard, and generated behavior | owned focused structural/behavioral predicate; no complete MIR or instruction-sequence match |
| post-inline hinted-definition load reuse | PA37 control `526-late-inline-hint-load-reuse` checks O0/O1 load-count relationships, the ordinary-dose negative, store/writing-call barriers, readonly-call preservation, and behavior | owned focused LowIR/behavioral predicate; no complete module or compiler-source match |

#### Coverage closure map

Every row above is closed with a student-contract test rather than a snapshot
of the current compiler.  The map records the focused property controls; full
checked-in outputs remain independent compatibility gates:

| Contract group | Student-facing contract | Focused evidence and oracle |
|---|---|---|
| readonly scalar storage | PA15 **Assignment Boundary** | PA15 control `540-readonly-scalar-storage`: LowIR structural positive plus volatile, TLS, and class-object negatives |
| constructor alias boundaries and nested temporary lifetime | PA17 copy/move boundary and nested operand/full-expression paragraphs | PA17 controls `532`/`533` distinguish constructor versus assignment parameter facts and validate operator-use/destruction/unwind relationships plus behavior; PA37 driver `460`/`461` verifies propagation |
| pointer-difference lowering | PA15 pointer-arithmetic Assignment Boundary paragraph | PA15 control `530-pointer-difference-strength-reduction`; focused function-body predicates require arithmetic shift for an eight-byte element and signed division for a three-byte element, without comparing the complete generated LowIR |
| native copy, constant multiply, large-switch, and scratch-carried reload encoding | PA29 native goals 3, 8, and 15 plus its compact-MIR and large-switch implementation rules | PA29 controls `900`-`902`/`905`-`910` inspect only instruction families and local MIR operands, including fixed-copy size/alignment boundaries and immediate-versus-dynamic switch cases; the historical scratch reducer requires a positive diagnostic fact and integer/float-pressure behavior |
| local-global and deferred parameter-address selection | PA29 required native behavior item 15 | controls `903` and the focused historical deferred-address pass check direct-storage/materialized-address and pre-/post-call carrier relationships plus behavior |
| O1 LowIR survivor transforms and guards | PA37 O1 optimization-level bullets for underflow, adjacent noalias copies, full overwrite, shared loops, cold-path pricing, and phi repair | survivor-property pass over `506`-`511` plus O0 baselines and PA17/driver alias-source controls; only local relationships are inspected |
| historical readonly, strength, and small-object survivors | PA37 O1/O2 readonly, scalar-object, and counted-loop bullets | control `525-historical-lowir-contracts`; focused O0/O1/O2 predicates plus generated behavior, including the object-valued-copy guard and no complete LowIR comparison |
| diagnostic inliner controls and observational census | PA37 **Command Line** paragraphs for `--inline-limit` and `--stats` | controls `520`-`522`; predicates require only call presence/absence, repeatable-name acceptance, invalid-value rejection, record fields, and a nonzero eligible guarded-prefix contribution—not full output or exact counts |
| loop/EH placement and lifetime release | PA38 O2 interval paragraphs | survivor-property pass over `400`, `410`, `420`, and `428` checks only documented homes, release/reuse, call, and EH relationships; behavior is additional |
| staged homes, LEA, call-result stores, and load takeover | PA38 O1 rewrite bullets | survivor-property pass over `426` uses local predicates and its live-address negative |
| rematerialized storage addresses | PA38 O1 rematerialization bullet | survivor-property pass over `427` checks frame/global/constant-index versus variable materialization and runs behavior |
| volatile and typed conditional-choice boundaries | PA15 volatile-access bullet; PA37 preservation bullets; PA38 O1 scalar-choice bullet | PA15 `541`, PA37 properties `505`/`540`, and PA38 properties `406`/`407` validate the smallest relationship at each boundary plus behavior |
| frame-pointer and epilogue policy | PA29 serialized MIR policy plus PA38 O1 frame/epilogue bullets | PA29 `904` and PA38 property `425` inspect only policy fields and documented guards, never complete MIR |
| per-function native census | PA38 compile-only driver diagnostic paragraph | driver `440-function-census`; structural field/function coverage only, never exact values or timing |
| merge-phi frame-home ownership | PA38 O1 merge-home bullet | control `442-acyclic-phi-frame-home`; focused frame-home/edge-movement predicates plus runtime results, allowing a register-resident implementation and never comparing complete MIR or executable contents |
| immediate-call merge source-home donation | PA38 O1 immediate-call merge-home bullet | control `442-acyclic-phi-frame-home`; an eligible source-to-merge home relationship is contrasted with non-immediate and loop-invariant controls, and generated behavior validates repeated alternate edges without prescribing a register or complete MIR/program |
| cyclic choice-region residency | PA38 O1 cyclic choice-region bullet | control `443-cyclic-choice-region-residency`; focused O1-versus-O0 home predicate plus runtime refresh/invariant results, allowing any call-preserved register and never comparing complete MIR or executable contents |
| post-inline scalar-slot promotion | PA37 O1 late/post-prune cleanup paragraph | control `524-post-prune-inline-slot-promotion`; focused O0-versus-O1 call/slot/phi predicates plus O1 behavior, with no complete LowIR or executable comparison |
| guarded fast-loop phi residency | PA38 O1 guarded-fast-arm bullet | control `444-call-free-fast-loop-phi-residency`; focused O0-versus-O1 frame-home and preserved-capacity predicates, a call-reaching negative, and runtime behavior, allowing any physical registers |
| edge-local loop-phi activation | PA38 O1 bypassable call-free-loop bullet | control `448-local-loop-phi-activation`; focused O0-versus-O1 per-iteration frame traffic, a pressure-matched no-added-preserve comparison, a call-crossing negative, and runtime loop/bypass behavior, allowing any physical registers |
| historical planned capacity and fallback ownership | PA38 O2 single-use, call-free, call-safe, and lazy-home bullets | control `445-historical-placement-contracts`; focused home/capacity relationships plus O0/O1 runtime behavior, never naming a physical register or comparing complete MIR |

One focused control may cover several rows when each function has one named
purpose and each row has its own local property or behavior predicate.  A
complete expected MIR/LowIR body is never the feature oracle.  The final
matrix records mutation or historical-parent evidence for correctness bugs
where practical, so a reducer is not accepted merely because it passes the
current compiler.

### 8.2 Per-edit loop

1. Build affected development tools with `make -C dev -j32`.
2. Run the earliest-owner property control plus focused PA37/PA38 tests.
3. Verify the frozen output hash against a same-revision host build.
4. Inspect transformation census, affected LowIR/MIR, movement, and object
   size.
5. Run three rotating native samples for direction.
6. Use one native software `task-clock` profile for attribution when needed.

Every accepted increment is an atomic checkpoint.  Before committing it:

1. update its student-facing README contract, focused property/behavior test,
   and this ledger;
2. run the owning `make test-paN`, PA37/PA38 when affected, and
   `git diff --check`;
3. confirm that only intended source, test, README, fixture, and plan files are
   staged--never `.tmp`, profiler data, or timing objects; checked-in reference
   programs are allowed only when produced by the documented `ref-test` path;
4. commit the increment with its fast-verification result in the message or
   ledger; and
5. record the commit id in the next ledger entry.

Do not accumulate more than three retained local commits or end a work session
with an unpushed retained commit.  Before each push, run
`make test-report-through-pa38` and the PA38 file audit, then push the current
`v3opt` checkpoint to `origin`.  Run fresh 32-way inception before that push
whenever the batch changes a compiler boundary or native policy, reaches about
2% cumulative native improvement, or is a finalist; otherwise inception may
be amortized across the next push batch.  Record the through count, audit,
inception disposition, commit id, remote branch, and push result in the
ledger.  Rejected experiments leave no production code; their evidence may be
committed with the next retained checkpoint or as a small plan-only checkpoint.
A documentation-only ledger follow-up may cite the immediately preceding gate
and push; it does not trigger another compiler gate solely to record that
already-completed checkpoint.

QEMU software PMU emulation is not used: it is likely slower and less faithful
for this native x86_64 compiler than the already-working native software-event
sampler.  Cachegrind is reserved for finalists.

Before starting a profiler, check that no earlier instance remains:

```sh
pgrep -af 'valgrind|cachegrind|callgrind|perf record'
```

Do not treat the `pgrep` command itself as a stale profiler.

### 8.3 Exact finalist gate

For a native-positive finalist:

```sh
valgrind --tool=cachegrind --cache-sim=no --branch-sim=no \
  --cachegrind-out-file=/tmp/p32-self.cg \
  <self-compiler> <frozen-compile-arguments>
```

Run the same-revision GCC and Clang references when denominator or attribution
may have moved.  Confirm the generated object hash after each profiler run and
remove no profiler output until its result has been recorded.

### 8.4 Correctness and inception gate

Every retained behavior or code-generation change must pass:

```sh
make test-pa37
make test-pa38
make test-report-through-pa38
perl scripts/cppgm_file_audit.pl --stage pa38 --paths dev
```

Final PA39 validation uses isolated roots.  **Every inception invocation uses
outer `-j32`, `INCEPTION_BUILD_JOBS=32`, and
`INCEPTION_OBJECT_BUILD_JOBS=32`; `INCEPTION_SELFHOST_OPT_LEVEL=1` is explicit
even though some make targets have a different default:**

```sh
P32_RUN_ROOT=/tmp/v3codex-p32-COMMIT-j32

/usr/bin/time -v make -C pa39 -j32 cppgm++-self \
  CXX=../dev/cppgm++ CPPGM_HOST_CXX=g++ \
  INCEPTION_OBJ_ROOT_BASE="$P32_RUN_ROOT/obj" \
  INCEPTION_BIN_ROOT_BASE="$P32_RUN_ROOT/bin" \
  INCEPTION_SELFHOST_OPT_LEVEL=1 \
  INCEPTION_BUILD_JOBS=32 \
  INCEPTION_OBJECT_BUILD_JOBS=32

/usr/bin/time -v make -C pa39 -j32 compare-cppgm++-inception \
  CXX=../dev/cppgm++ CPPGM_HOST_CXX=g++ \
  INCEPTION_OBJ_ROOT_BASE="$P32_RUN_ROOT/obj" \
  INCEPTION_BIN_ROOT_BASE="$P32_RUN_ROOT/bin" \
  INCEPTION_SELFHOST_OPT_LEVEL=1 \
  INCEPTION_BUILD_JOBS=32 \
  INCEPTION_OBJECT_BUILD_JOBS=32
```

Repeat restored-self O0 and O3 lanes at final landing.  Do not reuse a partial
object root for a clean timing or reproducibility claim, and do not start an
inception lane while another build or profiler is active.

## 9. Risks and fallbacks

- **R1: register residency adds callee-save cost.** Include prologue/epilogue
  cost in the keep test; a loop-local load/store reduction does not pay if the
  function adds an equally hot save/restore sequence.
- **R2: a large caller has no truly free register.** Use I4 to keep the bad
  loop outlined, or shorten unrelated live ranges using existing P30 facts.
  Do not silently reserve encoder scratch.
- **R3: trial dual lowering is too expensive.** Keep it diagnostic-only and
  derive a cheap structural production classifier from its results.
- **R4: selective outlining repeats the broad-loop failure.** Require combined
  caller/helper attribution and whole-compiler improvement.  Reject the class,
  not all loop work, if cost merely moves between symbols.
- **R5: no-alias construction rules become too broad.** Admit only exact typed
  language relationships or same-base proofs.  Uncertain overlap blocks the
  transformation.
- **R6: native samples are noisy.** Use rotating blocks, user time, matching
  attribution, and exact Ir for finalists.  Do not use a single wall result to
  reject a correct minor simplification or accept a global policy.
- **R7: `Run` improves but the 1.50x target remains distant.** This is expected
  from the current bound.  Re-profile the distributed residual and continue
  Phase D; do not declare the program complete after one hot-function win.
- **R8: profiler/build overlap corrupts results.** Check processes before each
  long run, use isolated roots, and record cleanup state in the ledger.

## 10. Ledger seed

- **P32-L0 (RESIDUAL ANALYSIS).** P30 closed at 9.883/5.830/5.640 s
  self/GCC/Clang wall and 40.046B/20.439B/20.803B Ir.  The provisional P31
  copy-coalesced point is 9.797/5.817/5.667 s in three samples, with exact
  same-revision output `a54d2c...f65`.  Guarded partial prefixes are neutral or
  negative; adjacent scalar copy coalescing improves self by about 1.5--1.7%;
  broad/shared loop rejection is at most a small provisional win; overwritten
  zero initialization is structurally positive but not yet timed.  Native
  software perf puts about 0.45 s of host-relative excess in `Lexer::Run`,
  whose self body is 15,109 bytes versus 9,539 GCC and 5,104 Clang.  `Peek` is
  approximately at Clang absolute-time parity.  No stale profiler remained.
- **P32-L1 (NEXT ACTION).** Complete I1.  Time and validate the overwritten-
  zero candidate, decide the shared-loop rule with a longer comparison, remove
  rejected active policy, and establish the six-sample same-revision
  self/GCC/Clang survivor baseline before changing loop placement.
- **P32-L2 (COVERAGE AUDIT).** Audited every production survivor in
  `PLAN-O1-PARITY.md`, `PLAN-INLINE-PARITY.md`, and the P31/P32 tree.  README
  contracts and earliest-owner reducers now cover the retained LowIR and
  native features.  Native predicates use focused PA29/PA38 structure or
  runtime behavior; no test compares a complete executable image.  The
  pre-landing root gate passed 5452/5452.
- **P32-L3 (ATTRIBUTION CORRECTION).** Native task-clock samples mapped through
  temporary block offsets place the hottest `Lexer::Run` cluster in the
  inlined `ScanPunctuator` switch/ternary phi web, not the previously suspected
  binary searches.  Function census is 498 planned, four grants, 311 busy
  rejections, and 298 frame definitions.  GCC and Clang also inline the helper.
- **P32-L4 (UNSAFE PHI-HOME PROBE).** Unrestricted static-single-use home
  sharing improved native time but corrupted a loop invariant feeding a
  repeated acyclic merge.  In self-hosting this changed FDEs from the
  personality CIE to the basic CIE.  PA38 control `442` reduces the bug: a
  zero choice in one iteration overwrites the invariant needed by the next.
  A one-shot-only guard was correct but lost the hot `Run` and LowIR optimizer
  improvements.
- **P32-L5 (SCC-LOCAL PHI-HOME LANDING).** Retained ownership transfer for
  one-shot chains and iteration-local non-loop-carried chains in the same SCC.
  Feature-off comparison across four source areas is -126 MIR, -63 loads,
  -63 stores, and -904 text bytes.  Six rotating medians are
  9.420/5.795/5.700 s self/GCC/Clang, ratios 1.626x/1.653x.  Self/GCC output is
  exact hash `108426...0abb`; Clang differs only in two local-symbol numeric
  spellings.  PA29 is 290/290, PA38 is 46/46, through-PA38 is 5452/5452, the
  file audit passes, and 32-way O1 inception matches.  No stale profiler
  remained.
- **P32-L6 (CYCLIC CHOICE-REGION LANDING).** The measured choice inputs are
  call results and can cross an optional later call, so the probe adapted the
  call-free/caller-saved sketch to bounded call-preserved capacity.  A call
  result with at least three uses qualifies only when its definition dominates
  every use and all uses share its cyclic component; overlapping reactive
  edge-live lifetimes use the same backedge envelope as planned intervals.
  On `pp_tokenizer.cpp`, `Lexer::Run` falls 3235 -> 2944 MIR, scalar loads
  606 -> 480, scalar stores 576 -> 426, call stores 78 -> 34, and text by
  914 bytes.  A census of all 213 compiler modules finds 111 candidates in 44
  modules, 79 capacity assignments, and 29 actual residencies in 27 functions
  across tokenizer, parser, semantic, LowIR optimizer, and object/backend
  sources.  Six-sample same-source A/B is 9.435/8.960 enabled versus
  9.535/9.040 disabled wall/user.  Exact Ir is 39.023B versus 39.200B;
  `Lexer::Run` supplies 164.57M of the 177.33M reduction.  A forward register
  order did not improve `Run` and slightly worsened `ScanRawLiteral`, so it was
  reverted.  PA29 is 78/78, PA38 is 46/46 with focused control `443`, 32-way
  O1 inception matched on the measured mechanism, both A/B outputs match, and
  no profiler remains.  The final production tree passes 5453/5453 through
  PA38, the PA38 file audit, and fresh 32-way O1 inception byte-for-byte in
  52.68 s.
- **P32-L7 (NEXT ACTION).** Re-rank the distributed residual against freshly
  rebuilt same-revision GCC and Clang O1 compilers.  The cyclic choice class is exhausted:
  lowering the use threshold or dropping the direct-branch-source filter adds
  no population, and the remaining 45 busy candidates are a separate general
  pressure problem.  Prefer I5 work-removal classes over widening this policy;
  rebuild both host denominators after the next cumulative landing.
- **P32-L8 (FRESH HOST RE-BASELINE).** Fresh production binaries under
  `/dev/shm/v3codex-p32-region-production-*` measured six-sample wall/user
  medians of 9.435/8.960 s self, 5.830/5.360 s GCC, and 5.720/5.255 s Clang.
  Ratios are 1.618x and 1.650x.  Self/GCC output is exact hash
  `56cc2176...8250`; Clang differs only in compiler-local numeric spelling.
  Native task-clock profiles rank the residual in `Lexer::Run`, vector growth,
  cursor movement, and entry construction.  No stale Cachegrind or perf
  process was present before the next measurements.
- **P32-L9 (POST-INLINE LOWIR CLEANUP).** Late and post-prune inlining can
  splice scalar slots after the ordinary promotion wave.  Repeating the same
  conservative promotion on rewritten callers removes 14 additional PA11
  LowIR instructions and 26 MIR instructions.  Eager analysis construction
  regressed native time; moving it after the successful-promotion check kept
  identical optimized LowIR and narrowed the repeated cost to +0.58% wall and
  +0.78% user.  PA37 control `524` is structural/behavioral and mutation-
  proven.  This is a retained LowIR quality fix, not a numerator win.
- **P32-L10 (DESTRUCTIVE-INTERFERENCE PROBE).** Relaxing the guarded-loop phi
  gate whenever a function already used all five preserved registers improved
  `_M_fill_append` but perturbed four other functions.  Three grew by 17, 24,
  and 6 MIR instructions because the new phi claims displaced pre-call
  residents.  This broad policy is rejected.  Adding the generic proof that
  the fast-loop header cannot reach a call changes only `_M_fill_append` on
  PA11, leaves its 160-byte frame and five-register preserve count unchanged,
  removes all six per-iteration frame accesses, and reduces whole-function
  scalar loads/stores 71 to 66.  Six-sample A/B improves wall/user by
  0.73%/0.77%; three-sample task-clock improves 0.56%; output remains exact.
  PA38 control `444` checks the positive, O0 baseline, preserved-capacity
  invariant, call-reaching negative, and behavior without naming registers or
  comparing whole MIR/executables.  The historical parent fails the control.
- **P32-L11 (NEXT ACTION).** Run the full through-PA38 and 32-way inception
  gates for the two new retained changes, rebuild same-revision GCC and Clang
  denominators, and remeasure the cumulative ratio.  If either ratio remains
  above 1.50x, use fresh task-clock attribution to choose the next distributed
  work-removal or pressure class; do not widen the rejected guarded-phi rule.
- **P32-L12 (FINAL GATES AND RE-BASELINE).** The cumulative tree passes PA37
  187/187, PA38 46/46, the through-PA38 report 5453/5453, the file audit, and
  fresh O1 inception.  The inception lane used outer `-j32`, both inner
  32-way build settings, matched byte-for-byte, and finished in 52.02 s.  Six
  rotating source-matched frozen samples measure 9.530/9.050 s self wall/user,
  5.900/5.440 s GCC, and 5.805/5.315 s Clang.  Honest wall ratios are 1.615x
  and 1.642x, so P32 remains active.  Self/GCC output hash is
  `13e3c638...7e9ea`; Clang's stable compiler-local spelling produces
  `9792e8e...4406`.  No Cachegrind, Valgrind, or perf recording remained.
- **P32-L13 (FRESH DISTRIBUTED PROFILE).** Native task-clock profiles put the
  largest absolute self excess in `Lexer::Run` (about 301 ms versus the slower
  host), `Token::Token(Token&&)` (about 194 ms, with host inlining changing
  attribution), `Lexer::Peek` (about 82 ms versus Clang),
  `PhysicalCursor::Next` (about 74 ms), vector fill/append (about 66 ms), and
  `strlen`/identifier classification.  The current tokenizer MIR census is
  2,996 instructions with 643/612 movement loads/stores and only seven planned
  grants; the outer `Run` loop still contains 1,099 frame operands.
- **P32-L14 (PRIVATE TABLE HOIST REJECTED).** A diagnostic hoisted the thirteen
  immutable named-operator pointer stores out of the inlined `Lexer::Run`
  loop.  It preserved output but grew `Run` from 0x34ca to 0x3606 bytes and
  regressed six-sample wall/user medians by 0.74%/0.89%.  The source-specific
  probe is rejected; no table-content recognizer or production change remains.
- **P32-L15 (SELECTIVE OUTLINE REJECTED).** Marking only
  `IsNamedOperator` non-inline shrank net text by 147 bytes and preserved exact
  behavior, but six balanced samples improved wall by only 0.26% with user
  time flat.  This is below the reproducible keep threshold and is rejected.
- **P32-L16 (FOCUSED COVERAGE RE-AUDIT).** Reopened every audit row whose
  evidence was described only as exact fixture movement.  PA15 control `530`
  now validates power-of-two versus general pointer-difference lowering;
  PA37 control `525` validates readonly folding, strength reduction, small-
  object promotion, and the object-valued-copy guard plus behavior; PA38
  control `445` validates single-use call-safe capacity, call-free tails, and
  lazy fallback ownership plus O0/O1 behavior.  These controls compare no
  complete program, LowIR, MIR, object, executable, hash, or physical-register
  spelling.  The student READMEs state each high-level feature and guard.
  PA15 is 121/121, PA37 187/187, PA38 46/46, and the required through-PA38
  report is 5453/5453.
- **P32-L17 (NEXT ACTION).** Diagnose the 213-byte out-of-line `Token` move
  constructor.  Its dynamic small-string `memcpy` call makes five derived
  addresses live across a call and forces five callee saves before a fixed
  61-byte copy.  First measure a semantics-preserving generic operation, not a
  `Token` or frozen-input special case: either serialize a dynamic byte-copy
  MIR operation at PA29 and inline its native implementation, or rematerialize
  stable constant-field addresses after the call.  Any retained encoding has
  a PA29 README contract and focused structural/behavioral reducer; any LowIR
  rewrite is serialized and owned by PA37.
- **P32-L18 (FIXED-COPY OVERCOPY REJECTED).** A diagnostic native lowering
  replaced the variable small-string copy in the measured move constructor
  with a fixed 16-byte copy where the source and destination object extents
  made the extra reads and writes safe.  Six rotating samples moved wall/user
  medians by only -0.515%/-0.326%, an optimistic source-specific result below
  the keep threshold.  The candidate is rejected; no encoding policy or
  feature test remains.  In particular, no test may recognize `Token`, the
  frozen input, or a complete MIR/native instruction sequence.
- **P32-L19 (PARAMETER-ADDRESS REPLAY REJECTED).** A generic probe replayed a
  constant-index address derived from a call-stable parameter instead of
  preserving each derived pointer.  It reduced the measured move
  constructor's preserve set from five registers to three, but grew the body
  from 213 to 229 bytes.  A source-independent temporary control also reduced
  its preserve set from four to two.  Two separate 32-way O1 inception builds
  exposed unsoundness: the first corrupted a local-member address during call
  staging; after restricting the provenance to parameters and restoring that
  staging path, the self compiler still corrupted a parameter-derived
  semantic-table address.  The experiment was removed before timing, and no
  README or feature test was retained for rejected behavior.  The restored
  development compiler passes PA38 46/46; no Cachegrind, Valgrind, perf, or
  inception process remains.
- **P32-L20 (NEXT ACTION).** Return to the fresh absolute-time ranking rather
  than widening address replay.  Inspect the source-independent inlining and
  placement reasons for `Lexer::Run`, `Lexer::Peek`, and
  `PhysicalCursor::Next`, beginning with small hot helpers that host compilers
  inline but self leaves out of line.  A candidate must be stated as a general
  PA37 inlining rule or PA29/PA38 placement rule, documented for students at
  its earliest owner, and tested with focused structural or behavioral
  predicates rather than complete program contents.  Use native task-clock
  for fast attribution, rotating native samples for direction, and reserve
  Cachegrind for a finalist.
- **P32-L21 (CONVERTED BOOLEAN-PHI THREADING REJECTED).** A generic PA37
  probe extended the retained loop-local Boolean-phi rewrite through a
  Boolean-preserving conversion and a private single-predecessor jump bridge.
  Truncation required every incoming value to be mechanically proved as a
  comparison result or integer zero/one.  On `pp_tokenizer`, the probe reduced
  `Lexer::Run` from 1,612 to 1,600 LowIR instructions, final MIR from 4,435 to
  4,154 instructions, movement loads/stores from 643/612 to 563/504, and the
  native body from `0x34ca` to `0x320f` bytes.  PA37 remained 187/187, PA38
  remained 46/46, and a fresh 32-way O1 inception matched every object and the
  final compiler.  The required rotating native A/B did not validate a speed
  win: four-sample medians were 9.500/9.065 s old versus 9.530/9.050 s
  candidate wall/user.  Each lane was internally deterministic at hashes
  `13e3c638...7e9ea` and `f67c0b62...e985`, respectively.  The transformation
  is therefore removed as performance-neutral.  It acquires no README rule or
  feature test.  Had it survived, PA37 would have owned a high-level CFG rule
  plus focused positive and safety-guard predicates; neither a complete LowIR
  program match nor recognition of the tokenizer shape would have been
  acceptable.  No profiler or inception process remains.
- **P32-L22 (LATE NONZERO-UNDERFLOW FOLD REJECTED).** Comparing the retained
  self compiler with Clang showed that `Lexer::Peek` still executed an inlined
  unsigned `size - 1 >= size` check after a predecessor had proved the same
  stable load nonzero.  The existing PA37 fold removes the check when run
  again, so a no-extra-pass probe moved that fold behind the local rewrites
  that expose it.  This is the documented general predicate rule, not a lexer
  recognizer.  The standalone tokenizer's `Peek` body fell from 447 to 347
  bytes and total text fell 177 bytes; the frozen generated object remained
  byte-identical.  PA37 was 187/187, PA38 was 46/46, and a fresh 32-way O1
  inception matched every object and compiler in 51.60 s.  Eight rotating
  samples all emitted hash `13e3c638...7e9ea`, but old and candidate medians
  were respectively 9.500/9.020 and 9.505/9.010 s wall/user.  The scheduling
  move is therefore reverted as performance-neutral, with no new README rule
  or test.  A retained version would extend PA37's existing focused predicate
  reducer with the exposing rewrite; it would not compare a full LowIR module
  or identify the tokenizer.  No profiler or inception process remains.
- **P32-L23 (UNUSED BUILTIN MEMCPY INLINE REJECTED).** A generic PA29 probe
  recognized the canonical builtin-object metadata for a direct, three-
  argument `memcpy` whose result had no uses, and lowered it to an explicit
  dynamic byte-copy MIR operation.  Its native definition/use masks modeled
  only the SysV string-operation carriers rather than a full call clobber.
  This removed the external call and one callee save from the hot `Token` move
  constructor, shrinking that body from 213 to 198 bytes and the macro object
  text by 78 bytes.  The canonical symbol was indexed once per program, not
  searched per function.  PA29 remained 291/291, the host-built and freshly
  rebuilt 32-way O1 self compiler emitted identical candidate objects at hash
  `8b76ecac...708069`, and the build completed in 17.84 s.  Eight balanced
  samples rejected the change: retained medians were 9.435/8.940 s versus
  candidate 9.490/9.020 s wall/user, regressions of 0.58%/0.89%.  The complete
  probe, including its MIR opcode and narrow clobber analysis, was removed.
  The restored compiler emits the retained `13e3c638...7e9ea` object and
  passes PA29 291/291 and PA38 46/46.  No README or feature test remains for
  rejected behavior; had it survived, PA29 would have described the general
  unused-result builtin rule and tested bounded dynamic-copy behavior plus a
  focused operation/call guard, never a complete program or MIR match.  No
  Cachegrind, Valgrind, perf, or inception process remains.
- **P32-L24 (MIR FRAME-FORWARDING INTERFERENCE REJECTED).** A generic PA38
  probe rewrote an adjacent 64-bit or pointer frame store/reload into a direct
  register move before local machine cleanup.  `Lexer::Run` alone contained
  217 such pairs; the probe reduced its optimized MIR from 2,996 to 2,812
  instructions and its loop frame operands from 1,099 to 885.  Native output
  immediately disproved the MIR-only signal.  The retained encoder's existing
  single-use frame-reload plan sees the original pair and drops both the store
  and reload.  The early rewrite hid that relationship, stranded the store,
  and grew the native body from `0x34ca` to `0x398c` and tokenizer text from
  32,853 to 34,215 bytes.  This is confirmed destructive interference between
  two otherwise plausible layout optimizations, so the probe was removed
  before inception or timing.  PA38 stayed 46/46 during the probe.  Any future
  movement candidate must compare final native instructions/text as well as
  MIR movement and must preserve or subsume the encoder's frame-forwarding
  oracle.  No README or feature test was added for rejected behavior, and no
  profiler or inception process remains.
- **P32-L25 (GLOBAL LATE-INLINE DOSE REJECTED).** The retained compiler's
  diagnostic `hint-late-cap` control was used to test whether the remaining
  tokenizer gap was principally an inlining-dose problem.  Raising the cap
  globally from the default to 72 reduced `Lexer::Run` from `0x34ca` to
  `0x2978` bytes and its calls to `Lexer::Peek` from 81 to 30, but grew the
  tokenizer object's text from 32,853 to 57,285 bytes.  A fresh isolated
  32-way O1 inception compiler at
  `/dev/shm/v3codex-p32-hint72/bin/selfhost/cppgm++-self` still emitted the
  retained frozen object hash `13e3c638...7e9ea`.  Four rotating samples gave
  retained medians of 9.435/8.960 s wall/user and candidate medians of
  9.575/9.100 s, regressions of 1.48%/1.56%.  Normalized software task-clock
  profiles showed the mechanism: although combined `Run`/`Peek` time fell,
  the global budget outlined or displaced profitable code including
  `ScanWhitespaceAndComments`, `AddSourceToken`, `ScanPunctuator`, and
  `SpellingTable::Intern`.  This agrees with the earlier selective inlining
  probes in `PLAN-INLINE-PARITY.md`: the 81 `Run` to `Peek` calls are not the
  dominant remaining cause, and widening the dose creates destructive
  interference between call sites.  No source change, README rule, or feature
  test is retained.  The completed profile is
  `/dev/shm/p32-hint72.perf`; no profiler or inception process remains.
- **P32-L26 (EXACT-LIVENESS CALLEE-SAVE RECOLOR LANDING).** Completed MIR
  now uses a call's exact annotated argument set for liveness and may recolor
  one whole callee-saved physical color to a noninterfering caller-saved color
  when every occurrence is explicit and the source crosses no replacement
  clobber.  The O1 profitability gate is a call function with an even save
  count: removing one save also removes the SysV alignment pad; leaf and
  odd-save cases are unchanged.  Debug ranges, implicit source uses,
  call-crossing values, boundary interference, and same-instruction conflicts
  remain guards.  On the tokenizer, `Lexer::Peek` shrinks 447 -> 417 bytes,
  `TranslationCursor::PullUCN` 1,977 -> 1,942 bytes, and total text falls
  32,853 -> 32,765 bytes.  The complete self compiler loses about 30 KiB of
  text.  Four rotating frozen samples give wall medians of 9.605/9.560 s and
  user medians of 9.080/9.075 s old/new: a 0.47% wall improvement with user
  time flat,
  retained as an unambiguous native code-quality improvement rather than a
  parity breakthrough.  Host-built and self-built candidate compilers emit
  the exact object hash `1fb84a23...fa56`; the prior lane remains internally
  deterministic at `13e3c638...7e9ea`.  PA38 is 46/46.  Its new control `446`
  validates the general caller-saved multi-store carrier, exact call-argument
  premise, call-crossing safety negative, and generated behavior through
  focused predicates; it compares no complete MIR, object, hash, program
  content, or compiler fixture.  The full through-PA38 report is 5,453/5,453.
  Fresh 32-way O1 inception matches every object and the final compiler in
  51.63 s wall (`1,338.83` s aggregate user, 229,588 KiB maximum RSS).  No
  profiler or inception process remains.
- **P32-L27 (POST-RECOLOR PROFILE).** A fresh software task-clock profile of
  the inception-matched recolored compiler records 4,772 samples over about
  9.56 s of user task-clock.  `Lexer::Run` remains the largest named body at
  5.51% (about 527 ms), followed by `Lexer::Peek` at 3.77%,
  `PhysicalCursor::Next` at 1.61%, vector fill/append at 1.38%, `FindChild` at
  1.38%, and the `Token` move constructor at 1.32%.  The comparable fresh GCC
  and Clang profiles put only about 176/220 ms in `Run`, so removal of work in
  that body remains the largest absolute opportunity.  No stale Cachegrind,
  Valgrind, perf, or inception process was present.
- **P32-L28 (ADJACENT SINGLE-USE COMPARE FORWARDING LANDING).** The PA38
  machine cleanup now forwards a scalar call result stored in a temporary
  frame home into an immediately following integer comparison when that
  comparison is its only annotated use, both operands have the same machine
  type, and the other operand remains encodable.  Volatile, debug-visible,
  unannotated, multi-use, and nonadjacent homes remain unchanged.  Tokenizer
  text falls 32,765 -> 31,981 bytes, `Lexer::Run` falls `0x34ca` -> `0x31fb`,
  and complete compiler text falls by 25,164 bytes.  Eight balanced frozen
  samples give 9.525/9.055 s old versus 9.465/8.975 s new wall/user, gains of
  0.63%/0.88%; all lanes emit hash `fce4d3d2...c49f`.  Fresh task-clock puts
  `Run` at 4.62%, about 85 ms below the immediately prior profile.  PA38
  control `447` validates the pressure-positive case, O0 baseline, multi-use
  safety negative, and generated behavior with focused predicates.  PA38 is
  46/46 and the through-PA38 report is 5,453/5,453.  The fresh cumulative
  32-way inception gate is recorded in P32-L35.
- **P32-L29 (DELAYED COMPARE FORWARDING REJECTED).** Extending the same
  machine rule beyond adjacency while proving that the source register stayed
  unchanged saved only another ten bytes in the measured tokenizer/`Run`
  output.  That result does not justify the broader intervening-instruction
  proof, so the extension was removed before the retained tests and timing.
- **P32-L30 (PRE-INLINER HOT MEMORY-GVN REJECTED).** Admitting PA37's existing
  dominance-based memory GVN at O1 for `inline_hint=yes` definitions removes
  38 loads and 1,080 tokenizer text bytes at a measured pass cost of about
  0.54 ms.  Its normal pipeline position is before late and post-prune
  inlining, however, so the smaller callee bodies admit extra cloning:
  complete compiler text grows 31,811 bytes, including +10.6 KiB in
  `macro_processor.o`.  Balanced medians regress from 9.715/9.135 to
  9.815/9.255 s wall/user while every generated object remains hash
  `96036482...7654`.  The early dose is rejected and no contract or test is
  retained for it.
- **P32-L31 (POST-INLINE HOT MEMORY-GVN LANDING).** The same conservative GVN
  now runs at O1 for explicit `inline_hint=yes` definitions only after every
  inlining and reachability-pruning wave.  This preserves all inliner size
  decisions while reusing dominating nonvolatile typed loads across blocks;
  stores, writing calls, atomics, EH boundaries, and debug-visible accesses
  remain barriers, while nonthrowing `readnone`/`readonly` calls preserve
  facts.  On the tokenizer it removes 119 loads, shrinks `Run` by 361 bytes
  and `PhysicalCursor::Next` by 133 bytes, and reduces text 30,934 -> 30,382
  bytes.  Complete compiler text falls 7,996 bytes with no growing module in
  the object census.  Eight balanced samples improve 9.665/9.100 ->
  9.610/9.040 s wall/user (0.57%/0.66%) at identical hash
  `96036482...7654`.  PA37 control `526` checks the O0 baseline, hot O1
  positive, ordinary-dose negative, store/writing-call barriers,
  readonly-call preservation, and behavior; disabling only the late pass
  fails it.  PA37 is 187/187.  The fresh cumulative through gate and 32-way
  inception are recorded in P32-L35.
- **P32-L32 (EXACT-VALUE UNDERFLOW FOLD REJECTED).** Late load reuse exposes
  an exact SSA value that a predecessor proves nonzero before computing
  unsigned `x - 1 >= x`.  Extending the existing PA37 fold from stable-load
  identity to exact SSA identity removes two tokenizer branches and shrinks
  `Lexer::Peek` by 118 bytes, but the generic implementation grows complete
  compiler text by 1,244 bytes.  Incremental balanced timing is flat at 9.615
  s wall and regresses 9.045 -> 9.065 s user.  The extension and its extra
  scheduling call are removed; it receives no README or feature test.
- **P32-L33 (SIGN-EXTENDED IMM8 ENCODING REJECTED).** A PA29 encoder probe
  selected x86 opcode `83` for 16-, 32-, and 64-bit integer ALU operations
  and memory comparisons exactly when the operation-width immediate equals
  the sign extension of its low byte.  The rule is semantically exact and
  shrinks tokenizer text 30,382 -> 29,203 bytes (`Lexer::Run` by 604 bytes)
  and complete compiler text by 89,752 bytes.  The object census has no
  generated-code growth; only the encoder implementation grows, by 584
  bytes.  Nevertheless eight interleaved samples regress 9.430/8.940 ->
  9.490/9.020 s wall/user (0.64%/0.89%).  This is direct negative evidence
  that global code-density/layout changes can destructively interfere with
  the current frameless operating point: smaller native text is not by itself
  a performance oracle.  The encoder change is removed and receives no PA29
  contract or test.  PA29 remained 291/291 and PA38 remained 46/46 during the
  probe; no Cachegrind or Valgrind process was left running.
- **P32-L34 (COVERAGE-ORACLE CORRECTION).** Reopened every retained survivor
  from `PLAN-O1-PARITY.md`, `PLAN-INLINE-PARITY.md`, and P31/P32 whose only
  evidence was broad suite passage, incidental fixture movement, or exact
  complete-output matching.  The earliest student-visible owners now state
  high-level contracts in PA15, PA17, PA29, PA37, and PA38.  Their focused
  controls inspect only distinguishing local relationships--for example
  access markers, alias boundaries, call/slot/phi relationships, frame-home
  and capacity inequalities, instruction families, and documented policy
  guards--and run behavior where it can catch destructive reuse.  The
  predicates do not match complete source programs, LowIR modules, MIR,
  objects, executables, hashes, exact physical registers, or exact telemetry
  counts.  Complete checked-in outputs remain compatibility gates, not the
  ownership oracle.
- **P32-L35 (COVERAGE CLOSURE GATE).** Focused assignment suites pass at PA15
  121/121, PA17 247/247, PA29 291/291, PA37 187/187, and PA38 46/46.  The
  post-formatting aggregate report passes 5,453/5,453 and the PA38 file audit
  passes after behavior-neutral compaction brought `lowir_native.cpp` and
  `lowir_opt.cpp` under their size limits.  A fresh isolated O1 inception used
  both outer `-j32` and inner `INCEPTION_BUILD_JOBS=32`: self preparation took
  18.85 s wall (450.86 s user, 228,384 KiB maximum RSS), comparison took
  35.44 s wall (914.22 s user, 232,000 KiB maximum RSS), and every object plus
  the final compiler matched.  No profiler or inception process remains, so
  native performance work may resume.
- **P32-L36 (SOURCE-MATCHED RESIDUAL REFRESH).** Rebuilt current-source GCC
  and Clang O1 hosts in isolated trees and compared them with the freshly
  inception-matched self compiler.  Three rotating native samples give wall
  medians of 9.45 s self, 5.84 s GCC, and 5.83 s Clang: honest ratios of
  1.618x and 1.621x.  Self/GCC output is exact at `d70bd5cf...d1a8`; Clang's
  stable compiler-local spelling yields `c1114edf...e83f`.  `--stats` puts
  about 2.8 s of the self-minus-host gap in the frontend, including roughly
  1.6--1.8 s in semantics and 0.8--1.0 s in preprocessing, and another
  1.7--1.8 s in LowIR optimization; native lowering, machine optimization,
  and encoding together contribute only about 0.33--0.36 s.  Software
  task-clock makes `Lexer::Run` the largest isolated excess at about
  425 ms self versus 209/190 ms GCC/Clang.  `Program::EnsureEntry` is already
  at absolute parity (118 versus 114/110 ms) and is removed from the target
  list.  The hottest `Run` cluster is a general refreshed single-use merge:
  a cursor-call result or ring-buffer load is stored to one frame home and
  immediately reloaded as `AppendUTF8`'s first call argument.  The next probe
  is a source-independent PA38 merge-to-call-argument forwarding rule with
  predecessor coverage, register-clobber, debug, volatility, multi-use, and
  control-flow guards.  No production probe has been applied yet, and no
  profiler or inception process remains.
- **P32-L37 (RECOVERY CHECKPOINT AND PUSH).** The plan previously specified
  fast and full verification but omitted commit/push cadence, leaving P31/P32
  dirty and the local `v3opt` branch 167 commits ahead of `origin/v3opt`.
  Added the atomic-checkpoint and maximum-three-commit push rules above,
  fetched the remote tip to prove zero remote-only divergence, and staged no
  `.tmp`, perf, or timing artifacts.  Recovery commit `2b218e8e` contains the
  retained P31/P32 implementation, earliest-owner property/behavior coverage,
  fixture updates, and plans.  It cites the clean 5,453/5,453 through report,
  PA38 file audit, and fresh outer/inner-32-way inception recorded in P32-L35,
  and pushed successfully to `origin/v3opt` (`f5bfd68e..2b218e8e`).
- **P32-L38 (MERGE-TO-CALL-ARGUMENT FORWARDING REJECTED).** A generic PA38
  probe recognized a single-use temporary frame merge whose complete set of
  single-successor predecessor edges stored the value and whose target
  immediately reloaded it into a known direct-call argument register.  It
  required binding identity, matching types, no volatile or debug-visible
  home, no alternate entry, and no intervening use or clobber.  The tokenizer
  probe removed 21 MIR instructions and 234 text bytes; `Lexer::Run` lost ten
  MIR instructions, 30 loop frame operands, and 150 native bytes, including
  the measured identifier loop's two stores plus its `AppendUTF8` reload.
  PA38 remained 46/46.  A matched GCC-O1 host build emitted the exact retained
  frozen hash `d70bd5cf...d1a8`, cost no measurable compile time (5.81 versus
  5.83 s medians), and a fresh outer/inner-32-way candidate self build finished
  in 19.34 s wall (477.43 s aggregate user, 229,624 KiB maximum RSS).  Three
  rotating self samples nevertheless regressed 9.42/8.95 -> 9.50/9.02 s
  wall/user while preserving the same frozen hash.  Phase attribution puts
  only about 13 ms of the roughly 100 ms measured regression in machine
  optimization; most displacement appeared in preprocessing/frontend time.
  This is negative evidence that even removing the exact hottest spill/reload
  cluster destructively perturbs the current compiler layout enough to exceed
  its direct gain.  The implementation is removed and receives no README or
  feature test.  The tree is restored byte-for-byte to the retained source;
  no Cachegrind, Valgrind, perf, or inception process remains.

- **P32-L39 (EDGE-LOCAL LOOP-PHI ACTIVATION RETAINED).** Starting from pushed
  `4889a189`, the PA38 location planner now gives a bypassable call-free loop
  an exact local caller-saved interval from its first predecessor transfer
  through the extended backedge span.  It checks every fixed clobber, keeps
  local spans disjoint from other planned owners, activates without reading
  the phi's not-yet-initialized frame fallback, and falls back if the register
  is busy.  This avoids taxing an earlier call-bearing prefix with a
  function-entry callee-saved reservation.  On the retained PA11 input the
  final MIR falls 49,190 -> 49,101 lines: the allocation and old-buffer-copy
  loops in `std::vector<unsigned>::_M_fill_append` each keep a loop-carried
  value resident and lose their repeated frame transfers.  The tokenizer MIR
  is byte-identical, directly screening the destructive lexer-layout failure
  seen in L38.  The frozen object loses 354 text bytes and is deterministic at
  `8a33ad95...5de93f`, versus baseline `d70bd5cf...7ad1a8`.  Disabled-by-default
  telemetry reports 458 eligible local phis, 38 static assignments, 18
  successful edge activations, and 20 safe busy fallbacks on the frozen TU.
  Two balanced software-task-clock profiles put the dominant fill-append
  instance at about 1.46% baseline versus 1.16% candidate (roughly 28 ms less
  sampled CPU per compile); total sampled task-clock is also slightly lower.
  No exact-Ir Cachegrind run was used for this increment.

  Eight preliminary rotating A/B samples, followed by a final five-pair
  rebuild checkpoint, measured 9.39/8.93 s candidate versus 9.44/8.97 s
  baseline wall/user medians.  Fresh same-source O1 compilers at
  `/dev/shm/v3codex-p32-local-final-{self-j32,gcc-o1,clang-o1}` then measured
  9.38 s self, 5.85 s GCC, and 5.73 s Clang in three rotating samples, for
  honest wall ratios of 1.603x and 1.637x.  Self and GCC emit the exact
  `8a33ad95...5de93f` object; Clang's stable compiler-local spelling emits
  `ae6d5a73...a0b5fe3`.  PA38's README specifies the source-independent
  activation and fallback proof.  Control `448-local-loop-phi-activation`
  validates behavior plus the local structural properties: no per-iteration
  phi-home traffic after an earlier call, unchanged preserved capacity versus
  O0, and frame fallback for a call-crossing loop.  It does not match whole
  program, LowIR, MIR, object, register choice, or hash content.  PA38 passes
  46/46, the through report passes 5,453/5,453, and the PA38 file audit has no
  fatal findings.  Fresh O1 inception used outer `-j32` and inner
  `INCEPTION_BUILD_JOBS=32`; comparison took 35.54 s wall (910.58 s aggregate
  user, 230,208 KiB maximum RSS) and every object plus the final compiler
  matched.  No Cachegrind, Valgrind, perf, or inception process remains.
- **P32-L40 (LOCAL-PHI CHECKPOINT PUSH).** Atomic implementation, PA38
  contract/control, telemetry, and ledger commit `fa926453` pushed successfully
  to `origin/v3opt` (`4889a189..fa926453`).  The coverage matrix explicitly
  maps the new contract to control 448's structural and behavioral predicates.
  The worktree and remote tip agree before the next residual profile.
- **P32-L41 (LATE HINT CAP 52 REJECTED).** The residual profile attributed
  about 120 ms to the macro processor's out-of-line `Token` move constructor.
  A diagnostic-only inception build raised only `hint-late-cap` from 48 to 52,
  the narrowest threshold that admits that constructor: macro-processor LowIR
  calls fell 37 -> 1, its native calls fell 38 -> 2, and the standalone hot
  symbol disappeared.  This was not a free call-boundary win.  It cloned about
  1,596 LowIR lines into 36 callers, grew macro-processor text by 6,050 bytes,
  and grew the final compiler by 67,632 bytes.  Five balanced pairs at the
  exact retained `8a33ad95...5de93f` output were flat at 9.50 s wall and only
  9.02 -> 9.01 s user.  Software task-clock was slightly worse (9,450.821 ->
  9,472.707 ms); `perf diff` shows the removed 1.19% constructor sample
  reappearing chiefly in `MacroProcessor::AddSourceToken`, vector relocation,
  and other callers, with layout displacement elsewhere.  The experiment used
  `/dev/shm/v3codex-p32-hint52-self-j32/bin/selfhost/cppgm++-self`, prepared
  with outer and inner 32-way inception, and changed no source or fixtures.
  The cap remains 48, no contract/test is warranted, and no profiler or
  inception process remains.
- **P32-L42 (LEXER DENSITY PROBES REJECTED).** Three related diagnostics
  separated executed work from layout-only shrinkage in the residual
  `Lexer::Run` body.  First, spelling the local 13-entry named-operator table
  as static constant storage (a source-only upper-bound probe for aggregate
  globalization) shrank `Run` by 767 bytes and tokenizer text by 602 bytes,
  but five balanced pairs were flat: 9.48/9.00 s baseline versus 9.49/9.00 s
  candidate wall/user.  Second, redirecting explicit MIR edges through
  jump-only blocks and then pruning unreachable blocks shrank `Run` by 1,429
  bytes but lengthened branches throughout the compiler; final text grew
  71,564 bytes and the first two balanced pairs were slower at 9.45/8.96 s
  versus 9.40/8.92 s.  Finally, removing target redirection isolated ordinary
  unreachable-MIR pruning.  It shrank tokenizer text by 2,035 bytes and `Run`
  by 1,360 bytes, while its 2,971-byte implementation left final compiler text
  nearly neutral (+44 bytes).  The frozen output stayed exact at
  `8a33ad95...5de93f`, but five-pair medians regressed 9.39/8.91 -> 9.42/8.94
  s.  A matched 9,450.653 ms software profile attributed about 488 ms to the
  smaller `Run`, versus about 451 ms in the retained baseline.  This proves
  that removing never-executed blocks can destructively perturb the current
  layout without reducing operation count.  The static-table source edit and
  both machine probes are removed; PA38's existing 46/46 result is restored,
  no fixture or contract changes are retained, and no profiler or build
  process remains.
- **P32-L43 (PRESSURE-AWARE LOCAL-PHI CAPACITY RETAINED AS MINOR QUALITY).**
  Appending argument-capable registers after the original local pair was
  inert.  Preferring them for every local phi exposed more activations, but a
  low-pressure reducer showed that the broad rule could perturb a function's
  preserved set.  The retained PA38 rule therefore changes preference only
  when the earlier call-bearing prefix already has five values live through
  its first call: it tries the otherwise-free argument-capable caller-saved
  locations first, then the established local pair, and retains the existing
  exact span/clobber and dynamic-busy fallbacks.  Frozen telemetry changes
  458/38/18/20 candidate/assignment/promotion/busy counts to 458/38/27/11.
  An exclusive alternate-pair intermediate was rejected because its seven
  lost static assignments grew PA11 primary text by 248 bytes.

  The retained point changes the frozen object from `8a33ad95...5de93f` to
  deterministic `8eef1058...f7b52` and removes 99 text bytes.  PA11 primary
  text falls 125,065 -> 124,871 bytes; the dominant
  `std::vector<unsigned>::_M_fill_append` falls 428 -> 414 bytes and removes
  its destination's per-iteration frame update while preserving its existing
  five-register save set.  The complete compiler at
  `/dev/shm/v3codex-p32-local-argpressure-j32/bin/selfhost/cppgm++-self`
  loses 2,564 text bytes and was prepared with outer `-j32` plus inner 32-way
  compilation in 18.17 s wall (448.26 s aggregate user, 228,676 KiB RSS).
  Five balanced pairs measure 9.43/8.93 s candidate versus 9.44/8.96 s
  baseline wall/user medians.  Matching software-task-clock is 9,442.571 ms
  versus 9,450.821 ms; the fill helper falls from about 112.5 to 87.8 ms.
  Exact Ir-only Cachegrind completes in 377.68/379.20 s candidate/baseline and
  measures 39,594,329,579 versus 39,637,456,131 instructions
  (-43,126,552, -0.109%); 31,205,000 of the reduction is in the fill helper.
  Thus this is retained under the minor mechanically safe preference rule,
  not claimed as clearing Phase C's 0.5% performance-policy gate.

  PA38 control `448-local-loop-phi-activation` now checks behavior on both the
  loop and bypass arms, removal of each loop phi home's per-iteration traffic,
  equality of preserved capacity with a pressure-matched non-loop function,
  the O0 frame baseline, and the call-crossing fallback.  It never matches a
  physical register, complete LowIR/MIR/program, object, or hash.  Disabling
  the preference makes the focused check fail.  The focused control and PA38
  46/46 pass; full through/audit/inception and the atomic push are due with the
  next checkpoint.  Both Cachegrind output objects have the expected hashes,
  and no profiler remains.
- **P32-L44 (NATURALLY ALIGNED MEDIUM COPY LANDING).** The existing PA29
  encoder used scalar chunks through 32 bytes and string-operation setup above
  that boundary.  A broad 64-byte scalar point grew final compiler text by
  95,156 bytes and was nearly flat in five native pairs despite a 1.16%
  software-task-clock improvement.  Extending only byte-aligned copies grew
  text by 1,520 bytes and regressed five-pair wall time by 0.53%, proving that
  replacing the `Token` move constructor's 61-byte tail is detrimental on
  native hardware even though Cachegrind charges its repeated string
  instruction heavily.  Restricting the population first to alignment above
  one and then to at least eight bytes produced the distributed native win.

  The compiler contains 1,477 affected 40-byte, 408 affected 48-byte, 105
  affected 56-byte, and 358 affected 64-byte string copies.  A 40-byte ceiling
  grew text by 45,128 bytes but measured only 9.39/8.88 versus 9.41/8.93 s
  wall/user in three pairs.  The retained 48-byte ceiling grows compiler text
  8,567,790 -> 8,628,630 bytes (+60,840, +0.71%) and frozen text 621,799 ->
  621,955 bytes (+156).  The rejected 64-byte ceiling grew compiler text by
  85,432 bytes and frozen text by 348 bytes.  Thus 48 bytes is the measured
  performance/code-density knee: every complete extended chunk is naturally
  aligned, while larger and weaker-alignment copies retain compact string
  setup.

  Five balanced pairs at
  `/dev/shm/v3codex-p32-copy48-align8-j32/bin/selfhost/cppgm++-self` give
  9.270/8.790 s candidate versus 9.430/8.940 s retained-base wall/user medians
  (-1.70%/-1.68%).  Matching software task-clock falls 9,442.571 ->
  9,317.171 ms (-1.33%).  Exact Ir-only Cachegrind falls
  39,594,329,579 -> 38,830,309,716 instructions (-764,019,863, -1.930%);
  the rejected 64-byte upper bound reached 38,667,607,597 (-2.341%) but its
  extra static growth did not buy a larger repeatable native win.  Frozen
  output is deterministic at `261b3f42...5514`: same-revision self and GCC
  objects are byte-identical, while Clang has identical text, data, sections,
  disassembly, and size and differs only in the established two numeric local
  symbol spellings.

  PA29 now explains the size/alignment policy without naming a register.
  Controls `906`-`908` check the aligned positive, weak-alignment fallback,
  upper-size fallback, and runtime bytes using only the local `copy_bytes`
  relationship and bounded entry-function instruction families.  They do not
  compare a complete MIR, program, object, hash, or register choice.  Restoring
  the old 32-byte ceiling makes the positive property fail.  PA29 is 291/291,
  PA37 is 187/187, PA38 is 46/46, through PA38 is 5,453/5,453, and the PA38
  audit has zero fatal findings.  Fresh outer/inner 32-way O1 inception matches
  in 53.03 s wall; commit and push identities follow in the checkpoint entry.
  All measured objects have their expected hashes, and no profiler or build
  process remains.
- **P32-L45 (CHECKPOINT PUSH).** Atomic commits `eb9d8652` (pressure-aware
  local-phi capacity and its PA38 property coverage) and `23abb61b` (aligned
  medium-copy encoding and its PA29 boundary coverage) passed the combined
  checkpoint.  Root `make test-report-through-pa38` is 5,453/5,453; the PA38
  file audit has zero fatal findings and 36 existing advisory warnings; and
  fresh O1 `compare-cppgm++-inception` matches at every object and final
  compiler using outer `-j32` and `INCEPTION_BUILD_JOBS=32` (53.03 s wall,
  1,354.55 s aggregate user, 229,724 KiB RSS).  Push `8f5fe453..23abb61b`
  advanced `origin/v3opt` successfully.  This ledger-only follow-up cites
  those completed gates and does not require a duplicate compiler gate.  No
  profiler, build, or timing process remains.  Next, perform I6's clean
  same-revision self/GCC/Clang native rebaseline and rank the residual exact
  hot functions before selecting another implementation slice.
- **P32-L46 (SOURCE-MATCHED THREE-COMPILER REBASELINE).** Rebuilt O1 hosts
  from the pushed `3fc68c24` source and compared them with the outer/inner-32
  self compiler at
  `/dev/shm/v3codex-p32-copy48-align8-{j32,gcc-o1,clang-o1}`.  Six rotating
  samples put every sequence position in each lane twice.  Self measures
  9.270/8.770 s wall/user, GCC 5.885/5.445 s, and Clang 5.765/5.305 s.  The
  honest wall ratios are therefore 1.575x and 1.608x; the stricter Clang
  denominator requires self at or below 8.6475 s, about 6.7% below this
  checkpoint.  Self and GCC emit `261b3f42...5514`; Clang emits
  `05ccbff5...d74`, differing only in the two established numeric local-symbol
  spellings with otherwise matching sections and instructions.

  Exact Ir-only profiles measure 38,830,309,716 self instructions,
  20,701,878,992 GCC instructions, and 21,016,263,598 Clang instructions.
  The self-minus-host named deltas are dominated by lexer code shape: against
  GCC, out-of-line `Lexer::Peek`, `PhysicalCursor::Next`, `Lexer::Run`, and the
  macro `Token` move account for about 3.649B, 1.118B, 1.134B, and 1.000B
  instructions respectively; against Clang the corresponding excesses are
  about 1.481B, 0.274B, 0.787B, and 1.019B.  The differing inlining decisions
  mean these symbol rows are attribution, not independent work totals.  Native
  task-clock agrees on the broader result: self `Run`/`Peek`/physical-next
  together cost roughly 1.11 s, while the remainder is distributed across
  construction, semantic lookup, and optimizer data-structure work.

  Static shape explains why another global inline-dose sweep is not selected.
  Self/GCC/Clang `Run` bodies are 12,434/9,539/5,104 bytes; self retains 477
  rbp memory accesses there versus 76 total stack accesses in GCC and 36 in
  Clang.  Yet Clang also leaves `Peek` and physical-next out of line, and the
  already-tested cap-52/cap-72 points merely clone or displace work.  The next
  slice is instead a general operation-count/code-density improvement in the
  existing fixed-copy population, with native timing as the fast gate and one
  exact finalist run.  Two stale sleeping `perf annotate` processes from
  earlier diagnostics had accumulated only nine CPU seconds over several
  hours; both were terminated before this rebaseline analysis.  They were not
  executing samples and no stale profiler remained.
- **P32-L47 (RESERVED-SCRATCH VECTOR COPY LANDING).** Direct fixed copies now
  use unaligned 16-byte chunks through the encoder-reserved `xmm7`, followed
  by the existing scalar tail.  The x86-64 target guarantees the required
  instruction support, unaligned chunks do not strengthen the LowIR alignment
  precondition, and ordinary floating-point value placement already excludes
  `xmm6`/`xmm7`; therefore the change introduces no unmodeled live-value
  clobber.  The existing size policy remains unchanged at this increment:
  every copy through 32 bytes is direct, naturally eight-byte-aligned copies
  through 48 bytes are direct, and the other copies keep string setup.

  Against `/dev/shm/v3codex-p32-copy48-align8-j32`, the corrected O1 candidate
  at `/dev/shm/v3codex-p32-simd-copy-o1-j32` shrinks final compiler text
  8,628,630 -> 8,571,706 bytes (-56,924, -0.66%) and frozen text 621,955 ->
  620,844 bytes (-1,111).  Candidate host and self emit the exact
  `5b4f05a7...e548` object.  Four interleaved samples per lane measure
  9.425/8.930 s baseline versus 9.375/8.885 s candidate wall/user medians;
  one 11.16 s candidate run coincided with machine pressure and is an obvious
  outlier but does not change either median.  Exact Ir-only Cachegrind measures
  38,830,309,716 -> 38,646,948,485 instructions (-183,361,231, -0.472%).
  The reduction is distributed: simplification, inlining, slot promotion,
  instruction moves, lookup-result assignment, and syntax construction are
  the leading named beneficiaries.  This clears the minor mechanical
  code-density rule and is approximately at Phase C's 0.5% native threshold.

  An initial scratch compiler accidentally used PA39's default O3 self-host
  level and was compared briefly with the O1 baseline.  Its two candidate
  samples and text size are discarded as a mismatched-control experiment; the
  scratch was rebuilt explicitly with `INCEPTION_SELFHOST_OPT_LEVEL=1` before
  any keep decision.  PA29's README now describes reserved vector chunks and
  scalar tails.  Controls `905` and `906` require a vector load/store class in
  the bounded entry body, reject string setup, retain the local 32/48-byte MIR
  relationship, and execute the copied data.  They match no complete MIR,
  program, object, hash, or physical register.  Disabling vector chunks makes
  control `905` fail specifically on the missing class.

  PA29 passes 291/291 and the root through-PA38 report passes 5,453/5,453.
  The PA38 file audit has zero fatal findings and the same 36 advisory
  warnings.  Fresh O1 inception uses outer `-j32` and inner
  `INCEPTION_BUILD_JOBS=32`; every object and the final compiler match in
  53.63 s wall (1,399.37 s aggregate user, 229,372 KiB maximum RSS).  All
  exact and native output hashes are deterministic, and no profiler, build,
  or timing process remains.
- **P32-L48 (ALIGNED LARGE VECTOR-COPY LANDING; WEAK EXPANSION REJECTED).**
  Vector chunks changed the code-density knee measured in L44.  Extending the
  direct form from 48 through 64 bytes only for operations declaring at least
  eight-byte alignment grows final compiler text 8,571,706 -> 8,585,994 bytes
  (+14,288) and frozen text 620,844 -> 620,964 bytes (+120), while removing
  another 186,511,579 exact instructions: 38,646,948,485 -> 38,460,436,906
  (-0.483%).  The reduction is distributed through semantic type work and
  the LowIR optimizer rather than concentrated in one source spelling.

  Four interleaved native samples per lane measure 9.375/8.900 s for the L47
  compiler versus 9.295/8.810 s for the aligned-64 compiler wall/user medians
  (-0.85%/-1.01%); a clean software task-clock sample likewise falls
  9,313.14 -> 9,262.00 ms.  The candidate compiler is
  `/dev/shm/v3codex-p32-vector64-align8-o1-j32/bin/selfhost/cppgm++-self` and
  its frozen output is deterministic at `73c95999...e769` in both native and
  exact runs.  These are fast A/B results against L47, not a new three-host
  ratio; the next cumulative source-matched host checkpoint remains required
  before claiming progress against the 1.50x exit line.

  A broader diagnostic admitted weakly aligned fixed copies through 64 bytes.
  It added another 6,028 bytes of compiler text, did not change this frozen
  output, and was indistinguishable from the aligned-only policy across two
  screened native blocks (combined eight-sample wall medians 9.275 versus
  9.260 s, with user time effectively equal).  Contradictory pair orderings
  and machine-load outliers did not support a keep decision.  Weakly aligned
  medium copies therefore retain the compact string path.

  PA29 now describes the aligned 64-byte boundary as a student-visible
  encoding policy.  Control `908` executes a 56-byte aligned copy and checks
  only the local copy relation plus vector-load/store instruction classes and
  absence of string setup; new control `909` executes a 72-byte aligned copy
  and checks the compact fallback class.  Neither prescribes a physical
  register or compares a complete program, LowIR, MIR, object, or hash.
  Restoring the old 48-byte cutoff makes `908` fail specifically because the
  direct vector class disappears, and restoring the candidate passes it.
  PA29 passes 291/291 and the full through-PA38 report passes 5,453/5,453.
  The PA38 file audit has zero fatal findings and the same 36 advisory
  warnings.  Fresh O1 inception used outer `-j32` and inner
  `INCEPTION_BUILD_JOBS=32`; every object and the final compiler matched in
  54.08 s wall (1,360.50 s aggregate user, 229,168 KiB maximum RSS).  The one
  intended Cachegrind process exited normally, and no profiler or build
  process remains.  The atomic commit and push follow in the checkpoint
  entry.
- **P32-L49 (ALIGNED LARGE-COPY CHECKPOINT PUSH).** Atomic implementation,
  PA29 contract/control, boundary fixtures, and ledger commit `7d60e65d`
  passed the L48 gates and pushed successfully to `origin/v3opt`
  (`41820c99..7d60e65d`).  This ledger-only follow-up records the completed
  checkpoint and does not require a duplicate compiler gate.  No profiler,
  build, or timing process remains.  Continue with a census of fixed zeroing
  and other distributed memory traffic before selecting the next native A/B
  candidate.
- **P32-L50 (EXACT-COST 16-BYTE VECTOR ZERO RETAINED AS MINOR QUALITY).** A
  compiler-binary census found 174 fixed `rep stosb` sites: 147 at 16 bytes,
  nine at 24, three at 32, and only 15 across the remaining larger sizes.
  The retained rule changes only the dominant 16-byte class.  A cleared
  encoder-reserved vector scratch plus one unaligned store costs 8--9 target
  bytes depending on the address register, versus 9--12 bytes for count
  materialization, integer clearing, and REP setup.  It uses baseline x86-64
  instruction support, preserves integer flags, and consumes no allocatable
  XMM capacity.  The less populous 24/32-byte classes lack the same guaranteed
  byte-cost proof and remain unchanged.

  The O1 candidate is
  `/dev/shm/v3codex-p32-vector-zero16-o1-j32/bin/selfhost/cppgm++-self`.
  Its 147 sixteen-byte REP sites disappear, while every other REP-size census
  count is unchanged.  The new encoder implementation offsets the per-site
  shrink and leaves complete compiler text nearly neutral at 8,585,994 ->
  8,586,038 bytes (+44).  Four balanced native samples measure 9.290/8.820 s
  for the L48 baseline versus 9.250/8.785 s candidate wall/user medians
  (about -0.4% each).  Three balanced software task-clock samples improve
  9,256.10 -> 9,228.03 ms (-0.30%).  All native and exact output objects are
  byte-identical at `73c95999...e769`.

  Exact Ir-only Cachegrind confirms only a minor distributed reduction:
  38,460,436,906 -> 38,458,796,066 instructions (-1,640,840, -0.0043%).
  This does not clear Phase C's policy gate and is not treated as a major gap
  closer.  It is retained under the narrower mechanical backend rule because
  each affected encoding is smaller, exact work falls, and both independent
  native protocols are non-regressing.  PA29's existing
  `cost-directed-small-zeroinit` fixture now also serves as the focused
  property control: it executes all zeroed bytes and checks only the local
  16-byte MIR operation, cleared-vector/store instruction classes, and the
  absence of string setup.  It names no physical register and compares no
  complete MIR, program, object, or hash.  Disabling the new class makes the
  control fail specifically on retained REP setup; restoring it passes.
  PA29 passes 291/291.  The combined push checkpoint below records the full
  aggregate gates, and no profiler or build process remains.
- **P32-L51 (FORWARDED-ADDRESS COMPOSITION REJECTED).** The refreshed native
  profile still ranks `Lexer::Run`, `Lexer::Peek`, and the cursor chain first
  at 5.21%, 4.94%, and about 2.50% combined.  Inspection corrected an apparent
  `Peek` frame miss: the existing bounded PA29 forwarding oracle already
  replaces the private home with a proved `r10`/`r11` carry at encoding time,
  but each logical reload still becomes a move from that carry before an
  immediate memory consumer.  A source-independent probe substituted the
  carry directly into an adjacent load/store address when the consumer had no
  other dependency on the logical reload register.

  The broad point removes 16,744 compiler text bytes; tokenizer text falls 51
  bytes, `Peek` falls 9 bytes by removing its three repeated moves, and `Run`
  falls 33 bytes.  Nevertheless, four balanced pairs regress from
  9.240/8.745 to 9.270/8.785 s wall/user medians.  Restricting the same proof
  to homes with at least two forwarded reloads tests whether broad one-off
  layout displacement caused the result.  It still removes 10,608 compiler
  text bytes and all three `Peek` moves, but regresses 9.265/8.800 ->
  9.300/8.815 s across another four balanced pairs.  Both candidate outputs
  are deterministic but intentionally differ from the retained output.  This
  is another measured instance of smaller generated layout destructively
  interfering with the current native compiler; the repeated-use restriction
  rules out single-use volume as the sole cause.  Both probes are removed,
  no new contract/test is warranted, and the worktree is restored to L50.
  No profiler or build process remains.
- **P32-L52 (VECTOR-ZERO CHECKPOINT PUSH).** Atomic implementation, PA29
  README/property extension, and ledger commit `bab3b164` passed root
  `make test-report-through-pa38` at 5,453/5,453 and the PA38 file audit with
  zero fatal findings and the same 36 advisory warnings.  Fresh O1 inception
  matched every object and the final compiler using outer `-j32` and inner
  `INCEPTION_BUILD_JOBS=32` in 52.05 s wall (1,361.15 s aggregate user,
  230,088 KiB maximum RSS).  Push `f17e8c2f..bab3b164` advanced
  `origin/v3opt` successfully.  This ledger-only follow-up also records L51's
  rejected probes and does not require another compiler gate.  No profiler,
  build, or timing process remains.
- **P32-L53 (ODD-SAVE MULTI-COLOR RECOLORING REJECTED).** The exact-liveness
  callee-save reducer retained in L26 deliberately leaves odd save counts
  unchanged because removing one save merely introduces the SysV alignment
  word.  A general extension planned distinct caller-saved destinations for
  every independently safe whole color and admitted an odd-save function only
  when at least two colors could be removed together.  Each source retained
  the existing explicit-occurrence, debug, boundary-interference, and call-
  clobber proof; destinations were unique and no symbol or source shape was
  recognized.

  The broad probe initially also recolored every safe color in the already-
  profitable even-save class.  A split point restored the established
  one-color even policy while retaining only the odd-save extension.  The
  split and broad candidates produced identical tokenizer objects, and their
  complete compilers differed by only 24 text bytes, ruling out collateral
  even-save recoloring as the explanation for the measured result.  Relative
  to the retained compiler, tokenizer text fell 225 bytes,
  `Lexer::Peek` fell 20 bytes, `Lexer::Run` fell 109 bytes, and complete
  compiler text fell 2,500--2,524 bytes.  Fresh self compilers used outer
  `-j32` and inner `INCEPTION_BUILD_JOBS=32`; the split build completed in
  18.20 s wall.

  Four rotating native pairs were effectively flat with a slight regression:
  retained medians were 9.260/8.775 s versus 9.275/8.785 s candidate
  wall/user.  Three balanced software-task-clock samples, with one candidate
  outlier, gave medians of 9,280.91 ms retained and 9,254.32 ms candidate
  (-0.29%).  This does not clear the 0.5% allocation-policy gate and does not
  justify broadening a retained placement policy on static shrink alone.  The
  probe is removed and the existing PA38 control `446` again passes its
  focused behavior, caller-saved-range, exact-call-argument, and call-crossing
  safety predicates.  No new contract or test is warranted for rejected
  behavior; had the extension survived, PA38 would have described combined
  odd-save profitability and tested it through capacity relationships rather
  than a physical register or complete MIR/program match.  No profiler,
  build, or timing process remains.

- **P32-L54 (CURRENT-SOURCE GCC/CLANG O1 RE-BASELINE).** Fresh host compilers
  were built from the L50 source with outer `-j32`, inner
  `INCEPTION_BUILD_JOBS=32`, and O1 self-host settings.  GCC completed in
  30.14 s wall and Clang in 32.74 s.  Six rotations placed every lane in each
  sequence position twice.  Median wall/user times were 9.270/8.790 s for the
  retained self compiler, 5.905/5.445 s for GCC, and 5.790/5.330 s for Clang.
  The honest current gaps are therefore 1.570x GCC and 1.601x Clang.  Reaching
  1.50x requires another 0.413 s (4.45%) against GCC and 0.585 s (6.31%)
  against Clang.  Self and GCC produced the retained
  `73c95999...e769` output; Clang produced `979fe59d...c16b`, with the
  established local numeric lambda spelling as the only instruction-content
  difference.  Text, data, section, and relocation sizes all matched.

  Fresh software-task-clock profiles had no lost samples.  They attribute
  about 417 ms to `Lexer::Run`, 402 ms to `Lexer::Peek`, 166 ms to the Token
  move constructor, and 150 ms to `PhysicalCursor::Next` in the self lane.
  The corresponding host symbols are substantially redistributed by
  inlining: in particular, self `Peek` plus `TranslationCursor::Next` is
  about 522 ms versus about 639 ms in Clang, so the separate self `Peek`
  symbol is not itself an honest gap.  `Run` remains about 206--246 ms above
  the hosts, while Token movement and physical-cursor work remain plausible
  aggregate residuals.  The already rejected global inline-cap and converted-
  Boolean/threading probes cover the most direct versions of those apparent
  opportunities.  The exact retained Ir remains 38,458,796,066.  No stale
  Cachegrind, profiler, inception, or build process remains.
- **P32-L55 (ALIGNED REP-MOVSQ COPY ENCODING REJECTED).** A native-encoding
  probe replaced the general `rep movsb` fallback with `rep movsq` exactly
  when the proved object alignment was at least eight and the constant copy
  size was divisible by eight.  This was a source-independent alignment and
  extent rule, not a program-content match.  It converted 1,563 of the
  compiler's 1,812 static `rep movsb` sites to qword copies, left 249 byte
  copies, increased compiler text by 1,656 bytes, and produced deterministic
  candidate output `64cec7f1...c16b`.  The existing PA29 extended-stack copy
  behavior control passed.

  Samples taken immediately after two all-core host builds were invalidated
  when both retained and candidate times rose to 19--30 s.  After recovery,
  a reverse-order software-task-clock pair measured 9,584.01 ms retained and
  10,095.71 ms candidate: the qword form regressed 5.34%.  On this ERMS host,
  reducing the architectural iteration count is not a useful proxy for native
  performance and Cachegrind would point in the wrong direction.  The probe
  is removed, no new contract/test is warranted, and no profiler, build, or
  timing process remains.

- **P32-L56 (IMMEDIATE-CALL MERGE SOURCE HOME RETAINED AS MINOR QUALITY).**
  High-resolution 997 Hz software profiles collected 9,330 self, 6,239 GCC,
  and 5,915 Clang samples.  Absolute attribution puts `Lexer::Run` at about
  492 ms self versus 188/186 ms in the hosts, an honest 304--306 ms excess.
  Address-level inspection localized roughly 95 ms of self samples around a
  queue value entering `AppendUTF8`: LowIR already contains the right merge
  phi, but PA38 gave an incoming one-use temporary its own frame home and then
  copied it to the merge home immediately before the call.

  The retained O1+ rule lets one same-typed, one-use incoming temporary be
  defined directly in a one-use merge phi's frame home when the first
  non-phi instruction consumes that merge as a call argument.  In a cyclic
  target, the incoming definition must share the exact cyclic component with
  the merge, proving that it is refreshed before every dynamic transfer.
  Alternate incoming edges still write the merge home.  An intervening
  consumer, another use, a representation change, an already selected source
  location, or a cyclic loop invariant retains the ordinary path.  The rule
  is based only on local use/type/control-flow facts and recognizes no symbol,
  source spelling, or complete instruction sequence.

  On `pp_tokenizer.cpp`, MIR falls 2,838 -> 2,818 instructions, 599 -> 589
  loads, and 586 -> 576 stores; object text falls 31,325 -> 31,129 bytes and
  `Lexer::Run` shrinks 140 bytes.  Four balanced native samples are flat at
  9.310 s wall median; user medians improve 8.805 -> 8.785 s (-0.23%).  Four
  software task-clock samples give 9,301.88 ms retained versus 9,308.27 ms
  candidate (+0.07%).  The one already-running finalist Cachegrind completed
  normally at 38,458,796,066 -> 38,439,077,871 instructions (-19,718,195,
  -0.05127%).  This is below the major performance gate and is retained only
  as a mechanical MIR-quality increment with native non-regression, not as a
  material closer of the remaining 1.50x gap.

  Moving the planner into the existing phi-lowering module was required to
  restore `lowir_native.cpp` to its 3,000-line audit limit.  That packaging
  changed final compiler text to 8,591,410 bytes, so a fresh balanced check
  measured 9,202.81 ms retained versus 9,217.36 ms final (+0.16%), still
  inside the non-regression band.  Every native and exact output has hash
  `73c95999...e769`.  PA38's student contract and control
  `442-acyclic-phi-frame-home` now own the rule: the positive checks direct
  home definition and absence of the identity transfer when the implementation
  uses frames, while a register-resident implementation is accepted; a
  non-immediate twin and repeated loop-invariant twin guard the boundary, and
  generated behavior exercises alternate edges.  Disabling the rule restores
  the redundant source home/transfer and fails the focused positive.

  The focused control and PA38's 46/46 fast suite pass.  Root through-PA38
  passes 5,453/5,453; the PA38 file audit has zero fatal findings and the
  existing 36 advisories.  Fresh isolated O1 inception at
  `/dev/shm/v3codex-p32-mergehome-final.tQSP0o` used outer `-j32`, inner build
  jobs 32, and object jobs 32; every object and the final compiler matched,
  with comparison completing in 35.03 s wall (906.42 s user, 51.08 s system,
  229,464 KiB maximum RSS).  The atomic commit and push follow in the next
  checkpoint entry.  No profiler, inception, or build process remains.
- **P32-L57 (IMMEDIATE-CALL MERGE-HOME CHECKPOINT PUSH).** Atomic PA38
  implementation, student contract, positive/negative structural-behavioral
  control, coverage matrix, and evidence ledger commit `3048bb70` passed the
  L56 gates and pushed successfully to `origin/v3opt`
  (`c053ac72..3048bb70`).  This ledger-only follow-up records the completed
  checkpoint and does not require a duplicate compiler gate.  No profiler,
  inception, build, or timing process remains.  Continue from the remaining
  absolute `Lexer::Run`, token-movement, and physical-cursor gaps using native
  attribution and task-clock screening; reserve Cachegrind for the next
  native-positive finalist.

- **P32-L58 (BALANCED LARGE-SWITCH TREE REJECTED).** A source-independent
  LowIR probe lowered switches with at least sixteen cases as balanced compare
  trees while leaving smaller switches on the ordinary linear path.  It
  converted the 26-case punctuator switch in `Lexer::Run` and left both
  nine-case switches untouched, but increased `Lexer::Run` by 476 text bytes
  and tokenizer text from 31,129 to 31,745 bytes.  PA37 remained 187/187.
  Three balanced native software-event samples measured 9,272.09 ms retained
  versus 9,262.51 ms candidate, only -0.10%.  The extra branches and blocks do
  not clear the allocation/CFG-policy keep threshold, so the probe is removed
  and receives no student contract or feature test.

- **P32-L59 (IMMEDIATE SCALAR-RETURN PHI REJECTED).** A generic native
  placement probe selected the ABI return register for a scalar phi whose only
  use was an immediate return.  Complete compiler text fell 6,944 bytes and
  tokenizer helper `IsIdentifierBody` fell 185 to 175 bytes, but balanced
  software-event medians were 9,205.15 ms retained and 9,204.13 ms candidate
  (-0.01%), with one candidate outlier.  Two existing PA38 controls also
  exposed compatibility assumptions that would need a more permissive local
  property before this representation could land.  With no native benefit to
  justify that policy and coverage work, the implementation is removed and no
  new contract or test remains.

- **P32-L60 (BROAD SWITCH-IMMEDIATE INTERFERENCE REJECTED).** Retaining every
  literal switch case as a direct comparison immediate, regardless of switch
  size, shrank tokenizer text 31,129 to 31,079 bytes, reduced `Lexer::Run` by
  25 bytes, and removed 3,624 bytes from the complete compiler.  PA38 passed
  46/46, and three balanced software-event samples suggested -0.73%.  Repeated
  native wall/user samples contradicted that proxy: retained medians were
  about 9.215/8.730 s and the broad candidate about 9.260/8.785 s, a
  0.5--0.6% regression.  This is measured destructive interference from
  extending an otherwise useful encoding choice into smaller switches: text
  size alone and deterministic instruction count do not capture its layout
  effect.  The all-size rule is removed.  The next probe retains only the
  measured large-switch class and leaves the two nine-case tokenizer switches
  on their established lowering.

- **P32-L61 (LARGE-SWITCH IMMEDIATE COMPARISONS LANDING).** The retained PA29
  instruction-selection rule applies only to switches with at least sixteen
  case edges.  Literal case values remain MIR immediates and, when encodable,
  become direct native register/immediate comparisons; nonliteral cases keep
  the register-materialization fallback.  The rule depends only on case count
  and operand kind.  It recognizes no source spelling, symbol, case value,
  physical register, or complete MIR/program shape.

  The 26-case punctuator switch changes while both nine-case switches retain
  their established layout.  Tokenizer text falls 31,129 to 31,105 bytes and
  `Lexer::Run` falls 25 bytes.  The final complete compiler text is 8,589,538
  bytes versus 8,591,410 retained (-1,872 bytes); the compact audit form is
  128 bytes larger than the originally measured candidate but keeps
  `lowir_native.cpp` at its 3,000-line limit.  Three final-source balanced
  software-event samples measure 9,183.53 ms retained versus 9,117.79 ms
  candidate (-65.74 ms, -0.716%).  An earlier four-pair native wall/user run
  measured approximately -1.1%/-1.3%.

  Exact Ir-only Cachegrind completes normally at 38,438,972,631 instructions
  retained and 38,376,376,742 candidate, removing 62,595,889 instructions
  (-0.16284%).  The retained and candidate profile outputs have hashes
  `73c95999...e769` and `21883ca7...e639`; all final native candidate outputs
  repeat the latter hash.  No stale profiler existed before the run and none
  remains afterward.

  PA29 now gives students the high-level size/operand contract.  Control
  `910-large-switch-immediate-cases` executes the switch, requires only local
  literal-immediate and dynamic-register MIR relationships, and accepts either
  short or full-width native immediates in any register.  Raising the threshold
  above the reducer's case count makes the positive fail; restoration passes.
  PA29 is 291/291, PA38 is 46/46, and root through-PA38 is 5,453/5,453.  The
  PA38 file audit has zero fatal findings and the existing 36 advisories.
  Fresh isolated O1 inception at
  `/dev/shm/v3codex-p32-switchimm-final.1jeCam` used outer `-j32`, inner build
  jobs 32, and object jobs 32; every object and the final compiler matched,
  with comparison completing in 34.86 s wall.  No profiler, inception, or
  build process remains.

- **P32-L62 (SOURCE-MATCHED SWITCH CHECKPOINT REBASELINE).** Fresh GCC and
  Clang O1 host compilers were built from the exact landing source with outer
  and inner 32-way settings in 29.69 s and 30.51 s wall.  After one warmup per
  lane, six Latin-square rotations placed every compiler in each sequence
  position twice.  Median wall/user times are 9.185/8.700 s self,
  5.895/5.465 s GCC, and 5.785/5.310 s Clang.  The honest ratios are now
  1.558x GCC and 1.588x Clang.  Reaching 1.50x requires another 0.343 s
  (3.73%) against GCC and 0.508 s (5.53%) against Clang.

  Self and GCC outputs are byte-identical at `21883ca7...e639`.  Clang emits
  `727d616f...8d01`; text, data, sections, relocations, and disassembly match,
  with only the established compiler-created local lambda numeric spelling
  different.  No profiler or build process remains.  The next attribution
  should use this exact landing compiler and concentrate on the remaining
  absolute `Lexer::Run`, token-movement, and physical-cursor gaps rather than
  widening the rejected all-switch or balanced-tree policies.

- **P32-L63 (LARGE-SWITCH CHECKPOINT PUSH).** Atomic PA29 implementation,
  student contract, focused structural/behavioral control, coverage matrix,
  rejected-probe evidence, finalist gates, and source-matched rebaseline
  commit `e02fb136` pushed successfully to `origin/v3opt`
  (`702c067c..e02fb136`).  This ledger-only follow-up records the completed
  checkpoint and does not require a duplicate compiler gate.  No profiler,
  inception, build, or unpushed implementation commit remains.

- **P32-L64 (SOURCE-MATCHED POST-SWITCH ATTRIBUTION).** Fresh 997 Hz native
  software-task-clock profiles collected 8,795 self, 5,479 GCC, and 5,335
  Clang samples with zero lost samples.  Approximate absolute attribution
  puts `Lexer::Run` at 422 ms self versus 183/174 ms in GCC/Clang, but that
  body-only difference is not an honest gap: GCC leaves
  `IsIdentifierInitial` outlined while self and Clang inline much of it.
  Comparable tokenizer aggregates still leave a material excess, without
  assigning all 239--248 ms to `Run`.  `EnsureEntry` is already essentially
  at host parity.  `Lexer::Peek` cannot be treated as an isolated target:
  self `Peek` plus `TranslationCursor::Next` is about 474 ms, below Clang's
  redistributed 656 ms, while GCC redistributes more of the same work through
  inlining.  Token movement and `PhysicalCursor::Next` remain plausible
  aggregate residuals, but the measured `Run` body is still the largest
  source-independent target.  No profiler remains.

- **P32-L65 (CONDITIONAL-CALL PARAMETER STAGING RESULT INVALIDATED).** Address-level
  inspection found one genuine path-placement opportunity in
  `IsIdentifierBody`: its scalar input uses a callee-saved register for the
  whole function because one fallback arm calls `IsInRanges`, so common ASCII
  returns pay save, setup-move, and restore instructions.  A generic PA38
  probe kept an eligible incoming scalar argument in its caller-saved ABI
  register and saved/reloaded it only around a sole acyclic conditional call
  that received the same value.  It recognized only type, use, clobber, call,
  and CFG facts.

  The intended local shape appeared: `IsIdentifierBody` removed its `rbx`
  preserve and move, inserted an `i32` frame save/reload only on the call arm,
  and preserved behavior.  PA38 remained 46/46, and frozen outputs stayed
  byte-identical at `21883ca7...e639`.  The implementation was removed after
  an apparent 141 KiB text increase and +2.62% task-clock result.  P32-L67
  subsequently proved that candidate was accidentally built with the PA39
  default self-host level O3 and compared with the required O1 baseline.
  Those size and timing numbers are not comparable and do not reject this
  placement family.  The local shape and PA38 correctness evidence remain
  valid; the performance question is reopened for a source-matched O1 rebuild.
  No student contract or test is retained while the implementation is absent.

- **P32-L66 (LEAF FIXED-COLOR RECOLOR RESULT INVALIDATED).** The fresh profile's
  `Program::FindEntry`/`DirectLookup` aggregate is about 178 ms self versus
  90/85 ms GCC/Clang, and all three compilers preserve the same sole call
  boundary.  Exact disassembly makes the local deficiency concrete:
  self-hosted `FindEntry` is a leaf but saves all five callee-saved registers
  and occupies 420 bytes, versus 281 bytes GCC and 247 bytes Clang.  The
  retained exact-MIR-liveness recolorer deliberately excludes leaf functions
  and does not consider fixed-purpose `%rcx`.

  A bounded PA38 probe admitted leaf functions only to `%rcx`, while retaining
  every existing explicit-occurrence, boundary-interference, clobber, debug,
  and same-instruction guard.  It removed one save/restore pair from
  `FindEntry` and shrank the self-hosted body to 406 bytes.  The PA38 property
  controls remained clean; two legacy exact MIR fixtures changed under the
  initial admission, as expected for a physical placement change.  The
  implementation was removed after an apparent 129,792-byte text increase.
  P32-L67 subsequently proved that candidate was built at the PA39 default O3
  and compared with the O1 landing compiler.  The size comparison is invalid,
  so the local push/pop improvement remains open pending a source-matched O1
  rebuild.  No README or student property is retained while the policy is
  absent, and no profiler remains.

- **P32-L67 (SELF-HOST LEVEL VALIDATION CORRECTION).** Three consecutive
  probe builds omitted `INCEPTION_SELFHOST_OPT_LEVEL=1`; PA39 therefore used
  its default O3 while the landing compiler remained O1.  This caused the
  nearly identical 125--141 KiB distributed text increase across unrelated
  policies and invalidated P32-L65's timing plus P32-L65/L66's static
  comparisons.  Rebuilding the next probe with explicit O1 reversed the
  apparent growth to an 8,148-byte reduction, confirming the configuration
  error.  Section 8.4 now makes self-host level and all three 32-way settings
  mandatory in every preparation and comparison command.  P32-L65 and L66
  are reopened rather than treated as negative evidence.

- **P32-L68 (BUSY LOCAL-PHI EVICTION REJECTED).** `Program::FindEntry`'s
  common small-scope loop had one planned local phi whose activation lost to
  an unplanned R9/R8 resident, leaving three frame operands in the 15-MIR loop.
  A bounded PA38 probe evicted only an eligible occupant of the phi's already
  proven caller-saved color to a one-time spill home, then retried the existing
  activation.  It did not change the candidate population, steal parameter
  registers, or weaken spill safety.  The target loop fell to zero frame
  operands, at the cost of one spill and two register copies; `FindEntry`
  stayed essentially size-neutral at 420 -> 419 bytes.  PA38 passed 46/46.

  The correctly configured O1 self compiler reduced total text 8,589,538 to
  8,581,390 bytes and left tokenizer text unchanged at 31,105.  Because this
  is allocation behavior, a full outer/inner/object-32-way O1 inception ran
  before timing and matched every object plus the final compiler in 34.68 s.
  Six balanced software-task-clock positions then measured baseline
  9,156.90/9,134.87/9,093.33 ms (median 9,134.87) and candidate
  9,160.32/9,150.69/9,170.73 ms (median 9,160.32): +25.45 ms, +0.28%.
  The implementation is removed at the fast native gate and receives no
  student contract or retained property test.  No profiler remains.

- **P32-L69 (LEAF FIXED-COLOR RECOLOR REJECTED AT CORRECT O1).** P32-L66's
  bounded leaf-only `%rcx` admission was rebuilt with the required
  `INCEPTION_SELFHOST_OPT_LEVEL=1` and all three 32-way settings.  The target
  effect is real: `Program::FindEntry` dropped one callee-save pair and shrank
  from 420 to 408 bytes; total compiler text fell from 8,589,538 to 8,586,370
  bytes and tokenizer text from 31,105 to 31,095.  The focused PA38 structural
  and survivor properties stayed clean.  Two legacy exact-MIR compatibility
  fixtures changed, so the unlanded policy scored 44/46 without regenerating
  those snapshots.  A full O1 outer/inner/object-32-way inception nevertheless
  matched every object and the final compiler in 35.37 s.  Candidate frozen
  outputs were internally stable at `2247d6be...39fc`; landing outputs stayed
  stable at `21883ca7...e639`.

  Two independently balanced software-task-clock sets agreed that the local
  reduction does not improve the end-to-end workload.  Set one measured
  baseline 9,273.12/9,194.37/9,276.67 ms (median 9,273.12) and candidate
  9,659.45/9,275.46/9,276.54 ms (median 9,276.54), +0.04%; the first candidate
  position was a scheduling outlier.  The reversed repeat measured baseline
  9,453.58/9,194.01/9,253.86 ms (median 9,253.86) and candidate
  9,428.52/9,248.39/9,256.35 ms (median 9,256.35), +0.03%.  The implementation
  is removed, the original exact fixtures remain untouched, and restored PA38
  passes 46/46.  No student contract is retained for a rejected policy and no
  profiler remains.

- **P32-L70 (CONDITIONAL-CALL PARAMETER STAGING REJECTED AT CORRECT O1).**
  P32-L65's one-parameter, one-call acyclic placement was reconstructed with
  the narrowed `i32`, at-least-eight-use policy and mechanical type, CFG,
  liveness, clobber, and direct-call guards.  It produced the intended local
  relationship in `IsIdentifierBody`: ordinary ASCII paths retained the
  incoming argument carrier, while only the guarded range-search arm saved
  and restored the value through a typed frame home.  The frozen object stayed
  byte-identical at `21883ca7...e639`, PA38 passed 46/46, and the candidate was
  prepared at explicit self-host O1 with all three 32-way settings in 18.04 s.

  Correct O1 static evidence does not establish a win.  Removing the
  callee-save setup while adding the cold frame transfer grew the local body
  185 to 187 bytes, tokenizer text 31,105 to 31,107 bytes, and complete
  compiler text 8,589,538 to 8,596,034 bytes.  The first balanced
  software-task-clock set measured baseline 9,153.34/9,093.11/9,273.01 ms
  (median 9,153.34) and candidate 9,207.62/9,194.19/9,283.48 ms (median
  9,207.62), +0.59%.  A reversed repeat measured baseline
  9,173.39/9,196.78/9,199.50 ms (median 9,196.78) and candidate
  9,131.23/9,227.05/9,153.72 ms (median 9,153.72), -0.47%.  Across all six
  positions the medians are 9,185.09 and 9,200.91 ms, a candidate regression
  of 15.82 ms or 0.17%, rather than the reproducible 0.5% improvement required
  for a placement policy.  The implementation is removed at the fast gate,
  restored PA38 passes 46/46, and no full inception comparison, student
  contract, retained property, or profiler remains.

- **P32-L71 (CALL-FREE LOOP MEMORY GVN REJECTED).** Source-matched profiles
  located duplicated bucket-vector reloads in `Program::FindEntry` after the
  `name_index_probes` counter store.  A bounded PA37 probe extended the
  already-retained final O1 memory-GVN pass from inline-hint functions to
  call-free functions containing a CFG backedge, after all inlining had
  finished.  This was a scheduling experiment only: it did not weaken the
  optimizer's alias rules.  PA37 passed 187/187.  On the PA11 compiler the
  pass visited eight additional functions and reported 24 additional load
  eliminations (54 -> 78); PA11 text fell 146,437 -> 146,401 bytes and
  `Program::FindEntry` fell 420 -> 416 bytes.  Disassembly nevertheless
  confirmed that the motivating hot reload survived, correctly, because the
  intervening store has no LowIR alias proof separating it from the bucket
  storage.

  The candidate was prepared at explicit self-host O1 with outer, inner, and
  object build parallelism all set to 32 in 18.13 s.  Complete compiler text
  grew 8,589,538 -> 8,591,470 bytes.  Its frozen output was deterministic at
  `1a2cc6d...f4c`; the landing output remained `21883ca7...e639`.  Balanced
  software-task-clock positions measured baseline 9,235.83/9,259.55/9,198.32
  ms (median 9,235.83) and candidate 9,391.70/9,313.08/9,253.28 ms (median
  9,313.08), a 77.25 ms or 0.84% regression.  The implementation is removed
  at the fast gate, restored PA37 passes 187/187, and no full inception
  comparison, student contract, retained property, or profiler remains.

- **P32-L72 (DEFERRED FIXED-RESULT CONSTANT DIVISION REJECTED; INTERFERENCE
  MEASURED).** Source-matched disassembly found a concrete host-code gap in
  `TypeTable::Get`: self used hardware signed division by the constant 56,
  while GCC and Clang used multiply-high magic.  The existing PA29 encoder
  already implements constant division but recognizes only an adjacent result
  copy or direct return.  O1 instead left the quotient in `rax`, performed an
  independent load and extension, and consumed it in a later comparison.
  `TypeTable::Get` was 110 bytes with `idiv` versus 130 bytes with magic.

  Three source-matched variants tested whether other replacements caused
  destructive code-layout interference.  The broad sound fixed-result
  liveness recognition removed 4,578 of the complete compiler's 4,978
  hardware divides and grew text 8,589,538 -> 8,685,638 bytes.  Its explicit
  O1/all-32-way preparation took 18.03 s; balanced task-clock medians were
  9,197.99 ms baseline and 9,201.18 ms candidate, +0.03%.  Restricting the
  deferred use to comparisons removed 1,997 divides, grew text to 8,630,078
  bytes, and prepared in 17.79 s.  That policy regressed balanced medians
  9,147.88 -> 9,278.33 ms, +1.43%.  Candidate frozen hashes were respectively
  `523e400e...cb22e` and `6e538442...74567`; baseline remained
  `21883ca7...e639`.

  A final interference control admitted comparison uses only in functions
  with at most 32 final MIR instructions.  It changed just 14 compiler sites,
  including the one motivating `TypeTable::Get` site, and limited total text
  growth to 1,752 bytes (8,591,290).  Its O1/all-32-way preparation took 17.62
  s and its frozen output was byte-identical to baseline.  Two reversed
  three-position sets measured -0.13% and -0.30%; all six positions give
  baseline/candidate medians 9,161.83/9,141.89 ms, only -19.94 ms or -0.22%.
  A fresh 997 Hz profile confirms the local diagnosis—`TypeTable::Get` falls
  from 0.89% to 0.44% of task-clock—but also confirms that this is not the
  material end-to-end lever needed for the remaining 3.7--5.5% gap.

  The broad candidate passed PA29 291/291 and PA38 46/46; the narrowed forms
  passed the focused PA29 gate.  A provisional high-level PA29 contract and
  structural/behavioral instruction-family control were used during the
  experiment, without exact MIR, bytes, or program-content matching.  All
  implementation, provisional documentation, and provisional test changes
  are removed because no variant reached the 0.5% production-policy gate.
  Restored PA29 passes 291/291 and PA38 passes 46/46.  No full inception
  comparison ran for a rejected candidate, and no profiler process remains.

- **P32-L73 (LOWIR DEFINITION-POINTER REPRESENTATION REJECTED).** The fresh
  source-matched profiles attributed `simplify_values_with_analysis` about
  0.96% of self task-clock versus 0.69% GCC and 0.90% Clang.  Inspection of
  the self-hosted body found its hottest sampled offset immediately after a
  128-byte `rep movsb`: every retained instruction copied a `DefinitionFact`
  snapshot into the per-value table.  A semantics-neutral PA37 probe replaced
  those snapshots with pointers to already-compacted instructions.  The
  pointer lifetime was bounded to this pass; the block vector cannot
  reallocate during compaction, earlier retained elements are not overwritten,
  and no later mutating pass can observe the scratch table.

  The probe was based on source tree `6d9d8145`, used landing self
  `/dev/shm/v3codex-p32-switchimm-final.1jeCam/bin/selfhost/cppgm++-self`, host
  GCC `/dev/shm/v3codex-p32-switchimm-host-gcc.9RmyQV/bin/host/cppgm++-self`,
  and host Clang
  `/dev/shm/v3codex-p32-switchimm-host-clang.fp5U44/bin/host/cppgm++-self`.
  Candidate preparation used explicit self-host O1 and outer, inner, and
  object build parallelism of 32, completed in 18.06 s, and produced
  `/dev/shm/v3codex-p34-definition-pointer-o1/bin/selfhost/cppgm++-self`.
  The frozen output remained byte-identical at `21883ca7...e639`; PA37 passed
  all 187 LowIR cases (19/19 harness groups).  Candidate `lowir_opt.o` text
  fell 219,575 -> 218,061 bytes and complete compiler text fell 8,589,538 ->
  8,587,494 bytes, although the simplifier body itself grew 404 bytes from
  pointer validity branches and indirection.

  The first balanced three-position task-clock set measured baseline
  9,206.49/9,188.01/9,174.79 ms (median 9,188.01) and candidate
  9,139.44/9,059.42/9,241.37 ms (median 9,139.44), an apparent -0.53%.
  The reversed repeat measured baseline 9,235.38/9,122.51/9,188.14 ms
  (median 9,188.14) and candidate 9,190.27/9,198.81/9,209.72 ms (median
  9,198.81), +0.12%.  Across all six positions the medians are
  9,188.075/9,194.540 ms, a candidate regression of 6.465 ms or 0.07%.
  The implementation is removed at the fast native gate; restored PA37
  remains clean.  Because this was a rejected representation-only probe, no
  student contract or property is retained, no Cachegrind or full inception
  comparison ran, and no profiler process remains.

Append one entry for every census, probe, landing, rejection, and re-baseline.
Each entry records the source tree, self and host binaries, output hash,
correctness matrix, native protocol, exact Ir when run, affected movement/text,
and profiler/build cleanup state.

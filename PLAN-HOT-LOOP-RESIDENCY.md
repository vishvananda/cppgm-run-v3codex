# PLAN-HOT-LOOP-RESIDENCY: P32 residual operation-count parity

Status: complete; retained-change coverage has been converted to
student-implementable property/behavior oracles; pressure-aware edge-local
loop-phi capacity is retained as a minor increment; reserved-scratch vector
chunks through naturally aligned 64-byte fixed copies are the latest
performance landing; the exact-cost 16-byte vector zero is retained as a
minor native-positive quality increment; immediate-call merge sources may
donate their frame homes as a minor MIR-quality increment; direct immediate
comparisons for large switches are the latest performance landing;
odd-save paired recoloring is a retained performance landing; scaled-index
multiplier factoring is a retained critical-path landing; unused-result
dynamic builtin-copy lowering is retained; lifetime-bounded ordinary O1 load
reuse is the latest performance landing; source-matched exact-final full-source
O1 wall ratios are about 1.476x GCC and 1.481x Clang, so the two-host exit
target is reached; the retained implementation and evidence are pushed through
`0f6f8ece`

Date: 2026-08-27

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

The frozen translation unit remains the fast diagnostic and attribution
workload.  The exit criterion is an honest same-revision clean full-source O1
wall ratio of at most **1.50x against both GCC O1 and Clang O1**.  The host
references and the self compiler must be built from the same candidate source
revision.
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

- the primary performance quantity is the same-source ratio, not the self
  numerator alone: for each candidate compute `self(candidate) / GCC(candidate)`
  and `self(candidate) / Clang(candidate)`, then compare those ratios with the
  corresponding production ratios; candidate preparation time is never a
  denominator;
- at least 0.5% exact frozen Ir improvement or a repeated 0.5% native-user
  improvement with matching attribution may advance a candidate quickly, but
  a credible source-independent transform that changes compiler source cannot
  be rejected from self-only timing when it is close to the noise band; build
  at least the currently binding same-source host denominator and decide from
  repeated wall plus aggregate-CPU ratios;
- a source-independent compiler-wide transform may instead clear the gate with
  a repeated 0.5% improvement in either same-source full-build ratio when the
  other ratio does not regress, or by reaching the 1.50x exit ceiling against
  both hosts; require both host denominators before landing, though a clearly
  non-improving repeat against the binding host is sufficient to stop a probe;
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
| readonly indexed string-table publication and record colocation | PA37 control `527-readonly-byte-strlen` checks the O0 local-table/call baseline, a shared indexed record address for the O1 pointer and length loads, all safety rejections, and O0/O1 behavior | owned focused LowIR structural/behavioral predicate; no symbol spelling, aggregate contents, complete module, executable, or hash match |
| scaled-index multiplier factoring | PA37 control `525-historical-lowir-contracts` checks the O0 byte-index baseline, the O1 equal-displacement multiplier/stride relationship, multi-use and unfactorable negatives, and O1/O2 behavior | owned focused LowIR structural/behavioral predicate; no complete module, native instruction sequence, register, executable, or hash match |

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
| scaled index-address factoring | PA37 O1 multiplier/stride bullet | control `525-historical-lowir-contracts`; focused O0/O1 relationship, two conservative negatives, and generated behavior without complete-output matching |
| historical readonly, strength, and small-object survivors | PA37 O1/O2 readonly, scalar-object, and counted-loop bullets | control `525-historical-lowir-contracts`; focused O0/O1/O2 predicates plus generated behavior, including the object-valued-copy guard and no complete LowIR comparison |
| readonly byte-string lengths and indexed string records | PA37 O1 readonly-string bullets | control `527-readonly-byte-strlen`; direct and indexed positives, six indexed safety guards, shared-record structure, and generated O0/O1 behavior without complete-output or symbol-name matching |
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

The self-only samples in steps 5--6 are a fast screen, not the final relative
performance metric.  They may reject an unsound candidate, one with no final
native mechanism, or one whose generated work regresses well outside noise.
When a sound, general source-changing candidate has a credible static
population and its self result is close, construct host compilers from that
exact candidate source.  The primary progress score is the larger of the
repeated self/GCC and self/Clang wall ratios, because the exit criterion
requires both to be at most 1.50x.  A nonbinding lane may move backward if it
remains below 1.50x and the maximum ratio improves; aggregate CPU is supporting
work/noise evidence rather than a separate veto on a repeated wall result.
Use the binding host first, then measure the other host before retention.
Natural host cost from implementing a genuine documented compiler feature
belongs in the denominator; artificial work whose purpose is only to slow the
hosts is not an optimization and is inadmissible.

Use a clean full-source PA39 O1 build as a second native timing oracle.  This
oracle times a finished compiler binary as `CXX`; the shorter candidate
preparation that uses the GCC-built `../dev/cppgm++` is not a self-generated
runtime measurement.  Give every lane a fresh object/bin root, run lanes
sequentially, and make all three levels of parallelism explicit:

```sh
make -s -C pa39 -j32 cppgm++-self \
  CXX=<finished-compiler> CPPGM_HOST_CXX=g++ \
  INCEPTION_OBJ_ROOT_BASE=<fresh-root>/obj \
  INCEPTION_BIN_ROOT_BASE=<fresh-root>/bin \
  INCEPTION_SELFHOST_OPT_LEVEL=1 \
  INCEPTION_BUILD_JOBS=32 INCEPTION_OBJECT_BUILD_JOBS=32
```

Record wall, user, and system time.  Wall is the practical clean-build
throughput result but can move with the 32-way scheduler tail; aggregate
`user+sys` is the steadier work measure.  Require a rotated repeat before a
sub-percent wall-only result changes a frozen-fixture decision.  An archived
candidate may be compared only with its source-matched archived production
compiler; regenerating current source once does not transplant a removed
code-generation policy or a later optimization into the archived executable.
Use this broad oracle to reveal translation-unit or critical-path wins.  The
frozen task-clock gate remains the fast screen and regression guard, but it is
not a veto for a source-independent transform exercised across the complete
compiler: the repeated full-source alternative in Phase C may decide a close
case when frozen movement is below its 0.5% resolution threshold.

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

- **P32-L74 (LOOP-CALLER HINTED-INLINING DOSE REJECTED).** The post-switch
  profile still attributes about 150 ms to the macro processor's out-of-line
  `Token` move constructor.  Static comparison makes the redistribution
  concrete: the self compiler retains 37 native calls to that constructor,
  GCC retains four, and Clang retains none.  P32-L41 already showed that
  raising the late hinted cap from 48 to 52 inlined essentially every call
  but merely moved the samples into 36 callers.  This probe tested a narrower
  structural dose: admit a hinted 49--52-instruction nonleaf only when the
  caller already contains a CFG backedge.  It used typed function metadata and
  shape only, with no source or symbol recognition.

  A preliminary two-noalias-pointer boundary qualifier had zero population;
  the source constructor fact is not represented by that exact serialized
  parameter shape, so it was removed rather than made into a false contract.
  The loop-caller dose did have the intended dynamic population.  In the
  compiler it reduced `Token` move calls 37 -> 9, but expanded complete text
  8,589,538 -> 8,664,678 bytes (+75,140).  The candidate was based on
  `370cf5d0` and prepared at
  `/dev/shm/v3codex-p34-loop-hint-o1/bin/selfhost/cppgm++-self` with explicit
  self-host O1 plus outer, inner, and object parallelism of 32 in 18.26 s.
  It produced deterministic frozen output `84675522...2c2b`; the landing
  compiler remained deterministic at `21883ca7...e639`.

  A balanced six-position software-task-clock screen measured baseline
  9,384.39/9,152.31/9,269.51 ms (median 9,269.51) and candidate
  9,240.28/9,354.78/9,290.46 ms (median 9,290.46), a 20.95 ms or 0.23%
  regression.  Thus concentrating the cap-52 dose in loop-bearing callers
  does not recover the hidden constructor time; it reproduces the earlier
  sample redistribution while adding more large-caller code.

  A diagnostic frame-pointer call-chain capture completed, but generated
  unusably deep chains through frameless generated functions.  Several slow
  offline reporters launched while diagnosing it were explicitly terminated;
  the bounded flat profile remains the fast attribution method.  The inlining
  probe is removed, restored PA37 passes 187/187, no README/property is
  retained for the rejected policy, no Cachegrind or full inception comparison
  ran, and no profiler process remains.

- **P32-L75 (IMMEDIATE-CALL LOCAL-PHI MANAGED-POOL PROBE REJECTED).** Flat
  address-level sampling localized the remaining `Lexer::Run` excess around
  the merge immediately before `AppendUTF8`: the retained P32-L56 rule removed
  one incoming source home, but the merge itself still used a frame home.  A
  diagnostic planner probe treated a one-use merge consumed by the first
  direct call as a short local phi.  `Lexer::Run` contained ten eligible
  merges and the planner assigned four, but all four activations found the
  managed pool busy; there were zero promotions and the motivating body did
  not change.  Broader and argument-prioritized versions changed only other
  tokenizer functions and grew tokenizer text from 31,105 to 31,119 or
  31,111 bytes.

  The existing student-facing PA38 control caught the unsafe/ineffective
  shape before timing: `442-acyclic-phi-frame-home` reported a post-call
  identity transfer.  No control was weakened.  The managed-pool probe and
  its counters were removed, restored PA38 passed 46/46, and the result rules
  out another planner dose at this site: all ordinary caller-saved and
  call-preserved capacity is already occupied at activation.

- **P32-L76 (BOUNDED SCRATCH-CARRIED IMMEDIATE-CALL PHI REJECTED).** A second
  PA38 probe tested the otherwise-unallocated `r10` scratch only for a sole,
  non-loop-carried integer/pointer phi whose single use is the first simple
  direct call, with at most six direct scalar arguments and every incoming
  predecessor ending in an unconditional jump to the merge.  The phi-only
  form replaced its frame store/load with edge register transfers.  On
  `pp_tokenizer.cpp`, `Lexer::Run` shrank 12,269 -> 12,229 bytes, its frame
  fell by 48 bytes, and tokenizer text fell 31,105 -> 31,033 bytes.  The
  explicit O1/all-32-way self build completed in 18.47 s at
  `/dev/shm/v3codex-p34-r10-immediate-phi-o1/bin/selfhost/cppgm++-self`;
  complete compiler text grew 8,589,538 -> 8,589,674 bytes.  Its deterministic
  frozen output was `3b406338...008f`, versus landing `21883ca7...e639`.
  After one 10,042.35 ms baseline outlier, the balanced medians were 9,201.83
  ms baseline and 9,209.68 ms candidate, +7.85 ms or +0.09%.

  Disassembly then showed that the hot ring-buffer incoming value still used
  its own frame home before entering the scratch phi.  A stricter load-only
  source-donation form let that scalar load be born in the same carrier when
  clobber analysis proved the intervening edge safe; calls and all other
  producers retained ordinary transfers.  The hot load became a direct
  memory-to-register load, `Lexer::Run` shrank 12,269 -> 12,099 bytes, and
  tokenizer text fell to 30,851 bytes.  The workload object was deterministic
  at the same candidate hash and its text fell 620,818 -> 620,664 bytes.  The
  complete compiler grew to 8,590,342 text bytes and its clean O1/all-32-way
  preparation completed in 17.87 s at
  `/dev/shm/v3codex-p34-r10-load-direct-o1/bin/selfhost/cppgm++-self`.
  A reversed balanced screen measured baseline
  9,168.60/9,167.14/9,169.24 ms (median 9,168.60) and candidate
  9,204.18/9,288.67/9,195.45 ms (median 9,204.18), a 35.58 ms or 0.39%
  regression.

  Both production shapes therefore fail the reproducible 0.5% keep gate for
  a new placement policy despite the genuine local code-quality improvement.
  The implementation is removed rather than retaining 120 lines of new
  scratch-lifetime machinery.  The focused PA38 suite passed 46/46 throughout
  the final form and again after restoration.  No student contract or new
  property is retained for the rejected policy, no Cachegrind or full
  inception comparison ran, and no profiler process remains.

- **P32-L77 (NARROW LOAD-NORMALIZATION FUSION REJECTED).** The next native
  probe tried to fold a typed narrow load and its following sign/zero
  normalization, then to omit a later normalization already implied by a
  dominating extension.  The broad form shrank `PhysicalCursor::Next` from
  `0x88f` to `0x853` bytes and `pp_tokenizer` text from 30,050 to 29,944
  bytes, but the O1/all-32-way candidate rejected the frozen source with an
  incompatible-macro-redefinition diagnostic.  Bisection showed that load
  fusion alone retained the failure; same-width zero extension had been
  incorrectly treated as proving same-width sign extension.

  The mechanically safe `i8` load plus `zext i8` subset shrank
  `PhysicalCursor::Next` to `0x873` and tokenizer text to 30,012 bytes.  Its
  complete compiler instead grew 8,589,538 -> 8,590,178 text bytes and its
  deterministic frozen output was `ee15ca8c...4c8557`.  Preparation at
  `/dev/shm/v3codex-p34-loadu8-o1` took 17.73 s with explicit O1 and all three
  32-way settings.  A balanced task-clock screen measured baseline
  9,166.03/9,117.89/9,122.23 ms (median 9,122.23) and candidate
  9,185.00/9,133.03/9,159.46 ms (median 9,159.46), a 37.23 ms or 0.41%
  regression.  PA29 was 291/291 before restoration and again afterward.
  Both forms are removed; no student contract or property is retained and no
  Cachegrind, inception comparison, or stale profiler remains.

- **P32-L78 (READONLY BYTE-STRING LENGTH FOLD RETAINED).** Fresh profile and
  static attribution found 1,054 `strlen@plt` calls in the self compiler,
  versus 207 in GCC and 180 in Clang, with about 136 ms of sampled self time.
  The frozen optimized LowIR retained 84 direct `strlen` calls; 72 received
  the direct address of one of 33 structured byte globals.  The source
  lowering had not preserved the C++ string literal array's const element
  contract, so PA16 now serializes literal backing data as
  `storage=readonly`.  A PA37 O1 rule uses only that LowIR property, the
  structured byte initializer, and the ABI-designated `strlen` operation to
  replace a direct base-address call with the first-NUL length.  Writable,
  unterminated, non-byte, and dynamic-pointer inputs remain calls; no literal
  symbol spelling or workload content participates.

  On the frozen unit the 72 eligible calls disappear and the 12 dynamic calls
  remain.  In the complete self compiler, static `strlen@plt` calls fall
  1,054 -> 78 and text falls 8,589,538 -> 8,577,926 bytes (-11,612).  The
  isolated O1/all-32-way compiler at
  `/dev/shm/v3codex-p34-strlen-o1.P0AzF5/bin/selfhost/cppgm++-self` prepared in
  17.68 s.  Its frozen output is deterministic at
  `f92da5db...d9c866`; it intentionally differs from the prior
  `21883ca7...e639` because the object now records readonly literal storage.

  Two reverse-balanced task-clock sets independently pass the native keep
  gate.  The first measured baseline 9,202.76/9,285.91/9,155.25 ms (median
  9,202.76) and candidate 9,127.37/9,119.03/9,261.11 ms (median 9,127.37),
  -75.39 ms or -0.82%.  The second measured baseline
  9,194.32/9,240.20/9,259.19 ms (median 9,240.20) and candidate
  9,257.28/9,153.63/9,086.50 ms (median 9,153.63), -86.57 ms or -0.94%.
  Across all twelve positions, medians are 9,221.48 and 9,140.50 ms,
  -80.98 ms or -0.88%.  Exact Ir was not needed because both native sets
  cleared 0.5% reproducibly.

  PA16 and PA37 READMEs give students the high-level storage and fold
  contracts.  New routed property checks identify only named reducer
  functions: PA16 requires literal backing storage to be readonly while an
  ordinary writable character array stays writable; PA37 requires the call
  to disappear with the correct first-NUL result while three unsafe guards
  retain it.  They do not compare a complete LowIR program, MIR, object,
  register assignment, or hash.  PA16 is 301/301, PA37 is 187/187, PA38 is
  46/46, and root through-PA38 is 5,453/5,453.  The PA38 audit has zero fatal
  findings and the established 36 advisories; `lowir_opt.cpp` remains exactly
  3,000 lines.  Full O1 inception with explicit outer, inner, and object
  32-way settings matched every object and the final compiler in 36.89 s.
  No Cachegrind was started and no profiler or inception process remains.

- **P32-L79 (FULL-SOURCE O1 ORACLE AND REJECTED-POLICY REPLAY).** A clean
  PA39 compiler build now supplements the frozen translation unit as a broad
  timing screen.  Every lane used a fresh `/dev/shm` object/bin root, O1, and
  explicit outer, inner, and object parallelism of 32, and ran sequentially.
  This times the finished compiler as `CXX`, not the faster GCC-built
  `dev/cppgm++` used to prepare a self compiler.  Two source-matched current
  runs measured self 32.87/33.84 s wall and 891.78/898.48 s user, GCC
  20.84/21.11 and 539.18/539.03, and Clang 21.31/21.08 and
  555.48/554.90.  The mean wall gaps are therefore 1.590x GCC and 1.574x
  Clang; mean user gaps are 1.660x and 1.612x.  The broad workload confirms
  rather than explains away the residual, while its wall-time spread requires
  aggregate CPU and rotated repeats for small candidate decisions.

  Three close P32 rejections were screened against the common pre-L78 landing
  compiler on the same current full source set, avoiding a cross-version
  comparison.  The bounded `r10` immediate-phi candidate used 947.58 s total
  CPU versus the two-run landing mean 943.47 s (+0.44%); narrow `i8` load
  normalization used 949.20 s (+0.61%).  Both remain rejected.  The old
  definition-pointer binary appeared more interesting: three runs had a
  32.76 s wall median versus the landing's two-run center of 33.145 s
  (-1.16%), although mean total CPU improved only 0.35%.  That justified
  reconstructing the representation on the current source rather than
  promoting archived evidence.

  The current PA37 reconstruction stored pointers to already-compacted
  retained instructions instead of copying definition snapshots.  Pointer
  use ended with the simplification pass; block vectors could not reallocate,
  retained prefix elements could not be overwritten, and the pointer was
  installed only after an instruction reached its compacted slot.  PA37
  passed 187/187.  The true combined self compiler retained 78 static
  `strlen@plt` calls and shrank text 8,577,926 -> 8,575,882 bytes.  Three
  clean full-source runs nevertheless averaged 33.197 s wall and 944.61 s
  total CPU versus current production's 33.177 s and 944.79 s: +0.06% wall
  and -0.02% CPU, effectively identical.  A balanced frozen screen measured
  production 9,124.49/9,146.90/9,402.14 ms (median 9,146.90) and candidate
  9,214.26/9,162.58/9,120.38 ms (median 9,162.58), a 15.68 ms or 0.17%
  regression.  Every frozen object matched current hash
  `f92da5db...d9c866`.

  The current reconstruction is removed because neither oracle reaches the
  0.5% production gate.  This is measured destructive interference in the
  broad-oracle signal: the archived cohort suggested a critical-path win, but
  the combined current landing did not retain it.  Restored
  `lowir_opt.cpp` is again 3,000 lines and the restored focused PA37 gate is
  clean.  No student contract or property is retained for a removed internal
  representation, no Cachegrind or inception comparison ran, and no profiler
  or build process remains.

- **P32-L80 (FULL-SOURCE THREE-COMPILER ATTRIBUTION).** Flat native
  `task-clock` profiles now cover clean full-source O1/all-32-way builds for
  the exact current self, GCC, and Clang compilers.  They collected 187K,
  115K, and 119K samples with zero loss and about 940.50, 580.63, and
  599.08 aggregate task-clock seconds.  The corresponding CPU ratios are
  1.620x GCC and 1.570x Clang, consistent with the unprofiled clean-build
  oracle after profiler overhead.

  The self lane assigns about 108.3 s to `Lexer::Peek`, 75.0 s to
  `Lexer::Run`, 40.6 s to `PhysicalCursor::Next`, 31.6 s to
  `TranslationCursor::Next`, 26.1 s to the macro `Token` move, and 20.1 s to
  `AppendUTF8`.  Host inlining redistributes these rows, so they are not
  independent gaps.  Against the structurally closer Clang lane,
  `Peek+TranslationCursor` is about 139.9 versus 112.4 s (+27.5), `Run` is
  75.0 versus 48.6 s (+26.4), and physical-next is 40.6 versus 25.3 s
  (+15.3).  Those three aggregate excesses total about 69.2 s, nearly the
  69.6 s aggregate reduction needed to reach 1.50x GCC in this oracle.
  `simplify_values_with_analysis` is only 0.29% of the self profile, which
  independently explains why L79's representation shrink could not move the
  broad workload.

  Address-level inspection of `Run`'s hottest `+0x1000..+0x1274` cluster
  found the inlined generic pattern that initializes a 13-pointer local table
  from readonly literals, scans it, calls `strlen`, and conditionally calls
  `memcmp`.  Current optimized LowIR preserves the direct literal addresses
  but loses them through the variable indexed local load before `strlen`.
  GCC and Clang also retain one dynamic `strlen` call in their `Run` bodies,
  so deleting that source-level algorithm is neither the parity explanation
  nor an acceptable workload reshape.  Across the whole self build, libc
  `strlen` plus the PLT stub accounts for about 16.8 task-clock seconds; the
  hosts spend about 14 seconds in libc `strlen` with attribution redistributed
  around the call.

  The next diagnostic therefore remains generic: census bounded readonly
  string candidates and the loop state made call-crossing by their
  `strlen`/`memcmp` operations.  A native intrinsic or split-lifetime probe is
  justified only if structural facts give a source-independent profitability
  bound and final native movement improves; it may not recognize the named
  operator table, compiler source, or frozen workload.  Any retained native
  operation belongs in PA29 with a high-level student contract plus focused
  structural/behavioral guards; any LowIR provenance transform belongs in
  PA37.  The three profile files are
  `/dev/shm/v3codex-p34-fullprofile-{self,gcc,clang}.data`.  A bounded offline
  annotate attempt was terminated by its timeout, and no profiler or build
  process remains.

- **P32-L81 (DYNAMIC STRLEN CENSUS AND PREFIX-CALL CANDIDATE).** An
  `LD_PRELOAD` diagnostic outside the repository counted 20,242,111 dynamic
  `strlen` calls and 108,881,939 input bytes during one frozen compile.  The
  distribution is unusually short: 668,860 calls have length one, 1,620,677
  length two, 5,187,266 length three, 129,745 length four, 3,946,337 length
  five, and 6,564,733 length six; about 92% are at most six bytes.  The
  retained L78 fold leaves 78 static direct calls in the complete compiler,
  so this is a generic dynamic-call opportunity rather than another literal
  provenance special case.

  The first source-independent probe replaced eligible direct builtin calls
  with `repne scasb`.  Its O1/all-32-way self compiler had 78 scan sites and
  grew complete text 8,577,926 -> 8,580,305 bytes.  Balanced frozen
  task-clock medians were 9,140.22 ms for production and 9,899.70 ms for the
  probe, +759.48 ms or +8.31%; it is removed.  The string instruction is a
  poor short-string strategy even though it removes the external calls.

  The second probe tests one page-contained 16-byte SSE2 word, returns the
  first zero offset when present, and otherwise executes the original direct
  call.  It is selected only at O1 or higher for a direct one-pointer,
  i64-result operation carrying the existing `cppgm_builtin_strlen` object
  identity.  It does not recognize a source function, string contents, or
  workload.  Landing compiler text is 8,584,371 bytes (+6,445), with 78
  vector prefixes and 78 retained fallback calls; frozen text is 620,223
  versus 619,732 bytes (+491).  The same-revision GCC-built compiler and the
  experimental self compiler both produce frozen hash
  `6239606846a506f9eaef6ea94f92f5db54e77a7be944caa85390862ea7589d47`.

  The exploratory frozen screen was slightly negative: production measured
  9,230.69/9,131.37/9,182.64 ms (median 9,182.64) and the candidate
  9,218.99/9,219.32/9,189.80 ms (median 9,218.99), +36.35 ms or +0.40%.
  Two recorded full-source reverse pairs give a different and repeated
  result.  Production measured 33.74 s wall with 946.27 s aggregate CPU and
  32.91/943.51; the candidate measured 32.69/935.08 and 33.14/936.03.  Mean
  wall improves 33.325 -> 32.915 s (-1.23%) and mean aggregate CPU improves
  944.89 -> 935.56 s (-0.99%).  This is the first concrete case where the
  full-source oracle resolves a distributed win hidden by the frozen unit;
  Phase C now permits that repeated broad result when the frozen change stays
  below 0.5%.  After converting the probe to its final serialized form, two
  additional balanced sets measured production
  9,179.83/9,217.33/9,230.87 and 9,113.66/9,099.18/9,169.25 ms, and the
  candidate 9,173.81/9,099.47/9,118.40 and
  9,151.24/9,125.02/9,125.91.  The combined six-position medians are
  9,174.54 and 9,125.47 ms, -49.07 ms or -0.53%; the opposing individual
  sets demonstrate why both rotation and the broad oracle matter.

  The final source split gives the prefix selection/encoding its own
  responsibility-named `lowir_native_strlen` module; both backend tool source
  sets include it, `lowir_native.cpp` is exactly 3,000 lines, and
  `lowir_native_elf.cpp` is 2,994.  Its O1/all-32-way self compiler prepared
  in 18.34 s.  Two current-source reverse full-build pairs measured production
  33.55 s wall/956.78 s aggregate CPU and 34.99/955.64; the candidate measured
  34.20/944.48 and 34.02/950.69.  Mean wall improves 34.27 -> 34.11 s
  (-0.47%) and mean aggregate CPU improves 956.21 -> 947.59 s (-0.90%).  Thus
  the landing representation retains the distributed broad-workload win as
  well as narrowly clearing the combined frozen screen.

  The finalist Cachegrind lane used cache and branch simulation disabled and
  preserved both expected hashes.  Production executed 38,289,648,113 Ir in
  368.63 s wall; the candidate executed 38,242,973,679 in 367.07 s, a
  46,674,434 or 0.12% exact-Ir reduction.  Libc `strlen` falls from about
  284.5 million to 9.3 million Ir.  The inline work attributed to
  `Lexer::Run` rises about 184.4 million Ir, so the whole-program reduction is
  materially smaller than the removed library work but remains positive.
  Profiler files are under
  `/dev/shm/v3codex-p34-prefix-cachegrind.quHCzo/`; no profiler remains.

  The retained form is not a native-only preparation flag.  PA29 MIR
  serializes the selected call fact as `strlen_prefix=16`, while preserving
  ordinary call arguments, clobbers, unwind behavior, result placement, and
  the fallback target.  The PA29 README describes the O1 selection and safe
  page-boundary rule.  Focused controls independently cover a marked external
  declaration versus an ordinary call and O0, plus short-prefix and long-
  fallback behavior and the vector byte-zero instruction family; they never
  compare a complete MIR, LowIR program, executable, hash, register choice, or
  source spelling.  Three focused PA29 controls pass 3/3: the third rejects an
  object-marked but non-pointer signature.  Full PA29 passes 291/291 with
  14/14 native controls, and PA38 passes 46/46.  Root through-PA38 is clean at
  5,453/5,453.  The PA38 file audit has zero fatal findings and the established
  36 advisories.  Fresh O1 inception at explicit outer, inner, and object
  32-way settings matched every object and the final compiler in 34.57 s
  wall.  The retained implementation commit is `de969f83` and the gate-ledger
  commit is `6e5fb7c5`; both pushed successfully to `origin/v3opt` in
  `f261eadf..6e5fb7c5`.  No profiler, inception, or build process remains.

- **P32-L82 (CURRENT-SOURCE FULL-BUILD REBASELINE).** Fresh host compilers and
  the retained post-L81 self compiler were timed as finished `CXX` drivers on
  clean current-source PA39 builds with self-host level O1 and outer, inner,
  and object parallelism all explicitly 32.  GCC measured 21.23 s wall and
  585.09 aggregate CPU seconds; Clang measured 21.66/606.76; self measured
  33.36/942.05.  The honest full-source ratios are therefore 1.571x GCC and
  1.540x Clang by wall, and 1.610x GCC and 1.553x Clang by aggregate CPU.  GCC
  is the binding lane: self must remove about 64.4 aggregate CPU seconds, or
  6.8%, to reach 1.50x at the current denominator.  The binaries are under
  `/dev/shm/v3codex-p34-prefix-host-{gcc,clang}.*` and the retained self root
  is `/dev/shm/v3codex-p34-prefix-de969f83.Mo4xTZ`.

- **P32-L83 (ODD-SAVE RECOLOR REPLAY FINALIST).** L53's source-independent
  odd-save multi-color policy was reconstructed on the current L81 landing.
  It retains the established one-color even-save behavior, but when a call
  function starts with an odd saved-register count it requires at least two
  independently liveness-safe callee-saved colors and distinct caller-saved
  destinations before rewriting the pair.  This preserves the SysV alignment
  profitability proof: two saves disappear without adding a padding word.
  Every source occurrence remains explicit; each replacement remains free at
  every source-live boundary and clobber; debug and implicit-register guards
  are unchanged.  No symbol, source function, or workload is recognized.

  The O1 compiler prepared with all three 32-way settings in 17.70 s.
  `Lexer::Peek` drops from five to three callee-saved registers and 453 to 433
  bytes, `Lexer::Run` drops 109 bytes, and complete compiler text drops 1,904
  bytes.  Two clean full-source reverse pairs measured production
  32.70 s wall/935.44 aggregate CPU seconds and 33.35/937.97; the candidate
  measured 32.73/931.97 and 32.33/928.84.  Mean wall improves 33.025 to
  32.530 s (-1.50%) and mean aggregate CPU improves 936.71 to 930.41 s
  (-6.30 s, -0.67%), clearing the broad-oracle retention threshold that did
  not exist at L53.  This is measured destructive interference: the old
  blanket odd-count exclusion preserved two unnecessary function-wide saves
  in common conditional-call code.

  The frozen fast guard remains inside its resolution band: three task-clock
  samples have medians 9,163.31 ms production and 9,184.82 ms candidate
  (+0.23%).  Production outputs are deterministic at `62396068...58d47` and
  candidate outputs at `c1b20dc2...eb7a2`.  PA38's existing behavioral
  recoloring control now carries distinct even- and odd-save positives: the
  odd shape exposes two independent call-free address ranges and must finish
  with no more preserved capacity than the even shape, while a two-call live
  value remains call-preserved.  The checker uses register classes and
  capacity relationships, not a register spelling or complete MIR/program
  oracle.  The README states the paired-profitability rule.  Focused control
  and PA38 pass 1/1 and 46/46.

  The finalist Cachegrind lane disabled cache and branch simulation.  The
  exact retained baseline executed 38,243,807,255 Ir; the final candidate
  executed 37,762,578,773, a reduction of 481,228,482 or 1.258%.  Diff
  attribution assigns -395.9 million Ir to `Lexer::Peek` and -99.2 million to
  `TranslationCursor::Next`, with small distributed offsets.  Stopping the
  recolor search as soon as its one- or two-color profitability quota is met
  preserves generated output byte-for-byte and removes another 2.03 million
  Ir versus the initially timed candidate; final complete text is 2,144 bytes
  below production.  Exact files are under
  `/dev/shm/v3codex-p34-odd-cachegrind.mToHlQ`.

  Final verification passes PA38 46/46 and root through-PA38 5,453/5,453.
  The PA38 file audit has zero fatal findings and the established 36
  advisories.  A fresh final compiler prepared at explicit O1 and all three
  32-way settings in 17.84 s; inception matched every object and the final
  compiler in 33.50 s wall, 883.07 s user, and 49.52 s system.  The isolated
  root is `/dev/shm/v3codex-p34-odd-final.Tx2q0J`.  Commit and push remain
  pending; no profiler, inception, build, or timing process remains.

- **P32-L84 (ODD-SAVE LANDING CHECKPOINT).** Atomic PA38 implementation,
  student contract, behavioral/structural property, checker, and evidence
  landed in commit `393e1afa` (`pa38: recolor profitable odd save pairs`).
  Its final gates are the P32-L83 PA38 46/46, through-PA38 5,453/5,453, audit
  with zero fatal findings and 36 established advisories, and O1/all-32-way
  inception match.  Push `5fdf07b5..1759b398` advanced `origin/v3opt`
  successfully.  No profiler, inception, build, or timing process remains.

- **P32-L85 (POST-ODD-SAVE THREE-COMPILER REBASELINE).** Fresh landed-source
  O1 host compilers prepared with outer, inner, and object parallelism all 32:
  GCC in 29.88 s wall and Clang in 30.54 s.  Sequential clean full-source
  builds then measured GCC 21.31 s wall/541.03 user/44.10 system, self
  32.90/881.87/49.30, and Clang 21.33/557.71/44.62.  The honest landed ratios
  are 1.544x self/GCC and 1.542x self/Clang by wall, and 1.591x and 1.546x by
  aggregate CPU.  GCC remains binding: at its current 585.13-second
  denominator, self must remove about 53.5 aggregate CPU seconds or 5.74% to
  reach 1.50x.  Host roots are
  `/dev/shm/v3codex-p34-odd-host-{gcc,clang}.*`; full-build roots are
  `/dev/shm/v3codex-p34-odd-full-{gcc,self,clang}.*`.

  A fresh 199 Hz full-source task-clock profile recorded 186K samples with no
  loss in `/dev/shm/v3codex-p34-odd-profile.LarfQc/self.data`; the profiled
  build used 884.19 user and 49.92 system seconds.  `Lexer::Peek` is now
  11.04%, `Lexer::Run` 8.93%, `PhysicalCursor::Next` 4.71%, token move 2.81%,
  `TranslationCursor::Next` 2.79%, and `AppendUTF8` 2.24%.  Relative to the
  pre-landing profile, the combined Peek/translation cluster loses about 11
  aggregate CPU seconds.  Against the structurally closer archived Clang
  profile, the remaining approximate excesses are now 35 seconds in `Run`,
  19 seconds in physical-next, and 17 seconds in Peek plus translation; host
  attribution is approximate because inlining redistributes these rows.

- **P32-L86 (HOST-EH FRAME-VETO PROBE REJECTED STATICALLY).** The ELF writer
  already serializes frameless CFI for personality-bearing functions, while
  final frame-pointer selection separately rejects explicit EH MIR operations
  and frame operands.  A bounded diagnostic removed only the redundant early
  `host_eh_enabled` veto, leaving the dynamic-stack, scratch, debug, EH-op, and
  frame-operand guards intact.  PA38 stayed 46/46, but the resulting O1/all-
  32-way compiler changed only 16 text bytes.  `Lexer::Run`, `Peek`,
  `PhysicalCursor::Next`, and `TranslationCursor::Next` were byte-for-byte
  unchanged and remained framed: their real cold exception/frame state trips
  the later guards.  The probe is removed without consuming a timing lane and
  receives no student contract or property.  The tree is restored, clean,
  and has no profiler, build, or timing process.

- **P32-L87 (FRAMED ODD-SAVE SINGLE-COLOR PROBE REJECTED BROADLY).** The
  landed odd-save recolorer requires two independently safe colors because a
  frameless SysV call function would otherwise trade one push/pop pair for an
  alignment adjustment.  A destructive-interference probe admitted one safe
  color when the function must retain its frame pointer and already has a
  nonzero local-stack adjustment; in that shape the existing `sub`/`add`
  instructions can change immediate without adding instructions.  The rule
  was derived entirely from final frame facts and did not recognize a source
  function or workload.

  The O1 compiler prepared with explicit outer, inner, and object 32-way
  settings in 18.24 seconds under
  `/dev/shm/v3codex-p34-framed-recolor.beQ02T`.  Complete compiler text fell
  8,581,391 -> 8,579,023 bytes, but the target
  `PhysicalCursor::Next` remained byte-for-byte the same 2,191-byte function
  with all five saves: no single callee-saved color has an independently safe
  caller-saved replacement there.  The surrounding hot functions moved in
  the wrong direction: `Lexer::Run` grew 56 bytes, `Lexer::Peek` 10, and
  `TranslationCursor::Next` 25.  All 46 PA38 programs passed, while the
  structural driver correctly reported that control `444`'s historical exact
  five-save assertion had changed to four.  The semantic fast-loop properties
  still held--both loop homes were absent and the call-reaching negative kept
  its homes--but no control change is retained for a rejected optimization.

  The clean full-source O1/all-32 oracle under
  `/dev/shm/v3codex-p34-framed-recolor-full.Z0Y24H` measured 32.70 seconds
  wall, 890.50 user, and 49.21 system, or 939.71 aggregate CPU seconds.
  Against the landed source-matched 32.90/881.87/49.30 result, aggregate CPU
  regresses 8.54 seconds or 0.92%; the small wall movement is therefore only
  scheduler-tail noise.  The probe is removed.  This rejects the framed
  profitability extension as a class and establishes that the remaining
  physical-cursor save cost needs earlier lifetime/allocation work rather
  than this post-allocation policy.  The source tree is restored and clean,
  and no profiler, inception, or build process remains.

- **P32-L88 (LATE-HINT CAP-52 BROAD REPLAY REMAINS REJECTED).** L41's
  supported `--inline-limit hint-late-cap=52` diagnostic was replayed because
  it removes or distributes a still-profiled token-move call family and had
  previously been judged only by the frozen translation unit.  No source was
  changed.  The current O1 compiler was prepared in 17.74 seconds with outer,
  inner, and object parallelism all 32 under
  `/dev/shm/v3codex-p34-hint52.LCu1pE`.  As before, this is a costly dose:
  complete compiler text grows 8,581,391 -> 8,651,319 bytes and
  `Lexer::Run` grows 12,167 -> 12,616 bytes; the current token-move body also
  retains at least one out-of-line use, so the old local disappearance is no
  longer complete.

  The first clean full-source lane looked barely positive at 32.99 seconds
  wall, 876.66 user, and 49.14 system, or 925.80 aggregate CPU seconds,
  compared with L85's landed 32.90/881.87/49.30 or 931.17 seconds.  The
  required reverse repeat did not confirm it: fresh landed production under
  `/dev/shm/v3codex-p34-hint52-base-repeat.JovUFr` measured
  32.78/879.83/48.65 (928.48 total), while fresh cap-52 under
  `/dev/shm/v3codex-p34-hint52-repeat.SbvrSX` measured
  32.91/879.75/49.46 (929.21 total).  Across the two positions per lane,
  production averages 32.84 seconds wall and 929.83 aggregate CPU; cap-52
  averages 32.95 and 927.51.  The apparent CPU reduction is only 2.32 seconds
  or 0.25%, wall regresses 0.33%, and the direction changes by position.
  This remains below the repeated 0.5% broad-oracle threshold and does not
  justify the 70-KiB dose.  No implementation, contract, or test changes are
  retained, and no profiler or build process remains.

- **P32-L89 (HIGH-FAN-IN MERGE RESIDENCY PROBES REJECTED).** Final LowIR
  and MIR inspection isolated `PhysicalCursor::Next`'s decoded scalar as a
  five-input acyclic phi with two uses.  Its five predecessor transfers store
  to one frame home; the common tail compares that home and reloads it for the
  return.  This is structurally distinct from L12's rejected blanket merge-
  phi policy, which added callee-save ceremony to many one-load leaves.

  Three bounded forms were screened.  A local caller-saved phi interval could
  not claim any of R9, R8, RDI, or RSI across the complete predecessor-
  transfer span; every choice was legitimately busy, so it was inert.  A
  high-fan-in/multi-use callee-saved admission did claim a home, but displaced
  existing residents: the target grew 453 -> 456 MIR instructions, added
  three loads and three spills, and grew 2,191 -> 2,207 native bytes.  It was
  rejected statically.  Finally, retaining the frame merge but promoting only
  its dominated two-use tail into a clobber-free RSI interval succeeded.  It
  removes the second frame reload without changing transfers, saves, or
  behavior, but grows the target three bytes; complete compiler text grows
  only 120 bytes.  PA38 remained 46/46 throughout.

  The tail candidate prepared at explicit O1/all-32 settings in 18.34 seconds
  under `/dev/shm/v3codex-p34-merge-tail.ltcAvs`.  Its clean full-source
  build under `/dev/shm/v3codex-p34-merge-tail-full.JuTlLz` measured
  32.38 seconds wall, 882.17 user, and 48.73 system, or 930.90 aggregate CPU
  seconds.  That is effectively flat against L85's 931.17-second landed lane
  and worse than L88's fresh 928.48-second production repeat.  Only three
  compiler sites received the promotion, so neither static population nor
  the broad oracle supports retaining the additional placement mechanism.
  All three forms are removed.  The result closes acyclic merge residency as
  the next material physical-cursor win: the remaining gap is the much larger
  instruction/movement body, not this single common-tail reload.  The source
  tree is restored and no profiler or build process remains.

- **P32-L90 (INDEXED READONLY STRING-TABLE LENGTHS RETAINED).** Exact
  task-clock attribution placed the largest `Lexer::Run` cluster in the
  named-operator loop.  Final serialized LowIR showed the generic cause: a
  local 13-element pointer table is completely initialized by direct addresses
  of readonly NUL-terminated byte globals, then each variable-indexed pointer
  is scanned by `strlen` before the same pointer is passed to `memcmp`.  GCC
  leaves the local table but calls libc `strlen`; Clang additionally publishes
  its pointer table as static data.  The retained PA37 transform does not
  recognize either source routine or its string contents.  It proves from
  LowIR alone that a local pointer table has complete, single, dominating
  initialization by known readonly strings and no escape, mutation, volatile,
  or unmodelled address use.  For an indexed element's ABI-designated
  `strlen`, it publishes a parallel internal readonly `i64` length table and
  replaces the scan with the corresponding indexed load.  Partial tables,
  writable or unterminated elements, mutation, volatile loads, escapes, and
  incompatible builtin signatures retain the call.

  On the optimized tokenizer unit the pass fires once, removes the dynamic
  `strlen`, and shrinks `Lexer::Run` 12,167 -> 12,139 bytes.  The complete
  compiler grows 8,581,391 -> 8,619,307 text bytes because this is a new
  conservative analysis, so static size was not used as the keep oracle.
  The candidate compiler prepared with explicit outer, inner, and object
  32-way settings in 18.24 seconds under
  `/dev/shm/v3codex-p34-strlen-table.CCQ6qn`.  Its two clean full-source
  O1/all-32 lanes measured 32.59/876.27/48.23 and
  32.28/875.32/48.69 seconds wall/user/system, averaging 32.435 wall and
  924.255 aggregate CPU.  A rotated source-matched run of landed production
  under `/dev/shm/v3codex-p34-strlen-table-base-current.viYKJi` measured
  32.81/880.85/48.66, or 929.51 CPU.  The confirmed improvement is therefore
  1.14% wall and 0.57% aggregate CPU; it clears the repeated broad-oracle
  threshold even though the frozen-unit/static signal is small.  Against the
  L85 GCC and Clang references, the new measured means are 1.522x/1.521x wall
  and 1.580x/1.534x aggregate CPU, respectively.

  PA37 now documents the property at the student-facing optimizer surface.
  Control `527-readonly-byte-strlen` checks the O0 baseline, the eligible
  indexed-load relationship, a synthesized readonly length aggregate, and
  O0/O1 native behavior.  Independent partial, writable, escaped,
  unterminated, mutated, and volatile tables must retain `strlen`.  The
  control derives only these local properties and does not compare a complete
  LowIR program, string list, register choice, or hash.

  The first fresh inception gate disagreed only on `pp_tokenizer.cpp`.  A
  host/self stage census showed that the host-built optimizer recognized all
  13 initializing stores while the self-built optimizer recognized none,
  despite both seeing the same candidate and uses.  Changing constructors in
  the optimizer source was tested only as a diagnostic, did not repair the
  failure, and was reverted.  Compiling `lowir_scalar_rules.cpp` at O0 isolated
  the disagreement to native output generated for the O1 version of
  `operand_uses_candidate_slot`.

  The reduced cause was an earlier PA29 bulk-copy lowering defect, not a
  condition missing from the new transform.  When both copy addresses needed
  setup, destination materialization could overwrite the parameter register
  still needed to form an indexed source address.  Later frame-address folding
  merely exposed the invalid setup order more clearly.  The generic native
  fix now detects dependencies through known and deferred addresses, forms the
  source first when necessary, and stages it in reserved scratch if the
  destination also needs the source copy register.  PA29 documents this
  preservation rule, and control `914-copyobj-indexed-parameter-order` checks
  the reduced program's behavior at both O0 and O1 while requiring only that
  the documented bulk operation survive.  It does not prescribe frame shape,
  register assignment, address order, complete MIR, or source-program content.

  After the PA29 fix, the O1 unit probe recognizes all 13 stores and its
  optimized LowIR matches the host build.  Focused PA29 is 291/291, PA37 is
  187/187, and PA38 is 46/46.  The through-PA38 report is 5,453/5,453 and the
  PA38 development-file audit passes with the 36 pre-existing same-file
  warnings.  A fresh all-32 O1 inception under
  `/dev/shm/v3codex-p34-strlen-table-fixed-gate.ziiJKF` matches every object
  and the final compiler; it measured 50.44/1327.02/93.20 seconds
  wall/user/system.  No profiler, cachegrind, or build process remains.

- **P32-L91 (CORRECTED BROAD ORACLE AND LATE-CLEANUP INTERFERENCE).** The
  first L90 broad timings predated discovery of the PA29 miscompile and are
  retained only as directional history.  Two clean builds with the corrected,
  inception-matching compiler measured 32.37/876.27/48.90 and
  32.23/875.79/48.87 seconds wall/user/system: 32.30 wall and 924.92 aggregate
  CPU on average.  Against L85's last stable host references, the provisional
  wall ratios are 1.516x GCC and 1.514x Clang; CPU ratios are 1.580x and
  1.535x.  A same-revision GCC refresh used 580.11 CPU seconds but suffered a
  31.51-second scheduler tail, while the Clang refresh was an outlying 647.30
  CPU seconds.  Neither distorted wall lane replaces the stable denominator.

  A fresh 199 Hz all-32 profile recorded 184K samples with zero loss and
  927.38 task-clock seconds.  The transformed compiler's libc `strlen` share
  falls from about 14 seconds in the prior profile to about 0.2 seconds.
  Residual absolute time is led by `Lexer::Peek` at about 99 seconds,
  `Lexer::Run` at 78, `PhysicalCursor::Next` at 42, the macro `Token` move at
  27, `TranslationCursor::Next` at 26, and `AppendUTF8` at 21.

  The token-move comparison found four unused local stores introduced by
  late inlining.  Running the existing dead-slot cleanup callee-first shrank
  the body from 48 to 44 LowIR instructions, after which the existing hinted
  policy reduced its calls 37 -> 1.  This apparently useful cleanup was
  destructive interference with the inliner: final output grew by 743 LowIR
  instructions in `macro_processor.cpp`, complete text grew 1,920 bytes, and
  the broad lane measured 33.11 seconds wall/924.30 CPU versus production's
  32.30/924.92 mean.  Samples were redistributed into callers without
  reducing work.  The scheduling probe is removed and has no contract or
  test.

- **P32-L92 (READONLY POINTER-TABLE PUBLICATION RETAINED).** The remaining
  `Lexer::Run` cluster was the same 13-way loop after `strlen` removal: local
  pointer initialization, indexed loads, frame-resident loop state, and
  `memcmp` still consumed about 23 profile seconds.  The retained extension
  reuses L90's complete-initialization, dominance, readonly, and nonescape
  proof.  It publishes a readonly pointer aggregate alongside the length
  aggregate, redirects every modeled variable-indexed pointer load, deletes
  the now-dead local stores, and runs dead-code/slot cleanup only on changed
  functions.  It does not recognize a source function, table spelling, or
  string contents.  All L90 safety rejections remain unchanged.

  `Lexer::Run` shrinks 12,139 -> 11,770 bytes and tokenizer text falls 370
  bytes.  The complete compiler grows 13,840 text bytes because it contains
  the generic publication mechanism.  Only `pp_tokenizer`, two template-heavy
  sources, and the two pass implementation objects change in the full build.
  Three clean production lanes measured 32.37/925.17, 32.23/924.66, and
  32.72/928.38 seconds wall/aggregate CPU, averaging 32.44/926.07.  Two
  candidate lanes measured 32.10/925.10 and 32.03/927.12, averaging
  32.065/926.11: repeated wall improves 1.16% while total CPU is flat.  This is
  retained as a measured critical-path/work-removal increment, not claimed as
  an aggregate-CPU reduction.  Against the last stable host wall references,
  the provisional ratios are 1.505x GCC and 1.503x Clang.

  Attribution supports the critical-path result: a fresh 199 Hz profile has
  zero lost samples and 919.75 task-clock seconds versus production's 927.38;
  `Lexer::Run` falls about 78.36 -> 75.05 seconds.  PA37's student contract now
  states readonly pointer and length publication.  Control
  `527-readonly-byte-strlen` requires only the O0 local-store/call baseline,
  indexed readonly pointer/length relationships, absence of eligible local
  initialization, all safety fallbacks, and O0/O1 behavior.  It does not
  compare a complete LowIR program, aggregate contents, symbol spelling,
  register choice, native layout, or hash.

  PA37 passes 187/187, PA38 passes 46/46, and through-PA38 passes
  5,453/5,453.  `lowir_opt.cpp` remains exactly 3,000 lines; the PA38 audit
  passes with the 36 established warnings.  Fresh all-32 O1 inception under
  `/dev/shm/v3codex-p34-pointer-table-inception.1x3Kwp` matches every object
  and final compiler in 50.19/1328.19/93.57 seconds wall/user/system.  No
  profiler, cachegrind, or build process remains.

- **P32-L93 (READONLY STRING-RECORD COLOCATION RETAINED).** L92 still
  calculated two independent scale-eight table addresses in every
  named-operator iteration: one for the readonly pointer and another for its
  length.  The retained PA37 refinement publishes one internal readonly
  aggregate of `{ptr, i64}` records.  It computes the variable-indexed record
  address at the original pointer load and reuses that dominating address for
  the corresponding length field.  The same complete-initialization,
  dominance, readonly, nonescape, nonvolatile, and no-mutation proof decides
  eligibility; the rule does not recognize a function, source spelling,
  table contents, or symbol name.

  Final `Lexer::Run` native code replaces the two scale-eight address
  calculations with one scale-sixteen calculation and loses six bytes,
  11,770 -> 11,764.  Complete compiler text grows only 1,356 bytes.  The
  candidate prepared under
  `/dev/shm/v3codex-p34-string-record-candidate.VgZoIB` in 17.74 seconds with
  explicit O1 and outer/inner/object parallelism of 32, producing compiler
  hash `0e572071...ea98`.  A source-matched C-B-C-B full-source rotation used
  fresh roots.  Candidate lanes measured 32.22/874.75/49.70 and
  32.31/874.25/49.64 seconds wall/user/system; retained lanes measured
  32.50/879.06/49.42 and 32.44/880.03/49.60.  Means improve 32.470 -> 32.265
  wall (-0.63%) and 929.055 -> 924.170 aggregate CPU (-0.53%).  Applying that
  paired factor to L92's last stable 32.065-second wall point gives a
  provisional 31.863 seconds, or 1.4952x GCC and 1.4938x Clang.  This is a
  paired normalization, not a replacement for the next undistorted direct
  host re-baseline.

  The student-facing PA37 contract specifies shared readonly string records.
  Control `527-readonly-byte-strlen` requires an O0 local-store/call baseline,
  an O1 indexed record whose address feeds both pointer and length loads, a
  two-record readonly aggregate, six safety fallbacks, and O0/O1 behavior.
  It compares no complete LowIR program, aggregate values, symbol spelling,
  executable, object, hash, register, or source workload.  PA37 passes
  187/187, PA38 passes 46/46, through-PA38 passes 5,453/5,453, and the PA38
  audit has only the 36 established warnings.  `lowir_opt.cpp` remains 3,000
  lines.  Fresh all-32 O1 inception under
  `/dev/shm/v3codex-p34-string-record-inception.HNtzsM` matches every object
  and the final compiler in 50.23/1324.33/92.82 seconds wall/user/system.  No
  Cachegrind, profiler, or inception process remains.

- **P32-L94 (DIRECT POST-RECORD HOST REBASELINE).** Same-revision O1 host
  compilers prepared with explicit outer/inner/object parallelism of 32: GCC
  in 29.86/519.27/51.14 and Clang in 30.57/606.46/38.50 seconds
  wall/user/system.  Clean full-source lanes measured GCC
  21.09/542.88/44.25 and Clang 21.41/560.50/44.67.  Against L93's two-lane
  self mean of 32.265/924.170, the direct wall ratios are 1.530x GCC and
  1.507x Clang; aggregate-CPU ratios are 1.574x and 1.527x.  GCC remains
  binding: self needs 31.635 seconds, another 0.630 seconds or 1.95%, for the
  1.50x wall exit.  Thus L93's paired normalization was useful directional
  evidence but not an exit result.

- **P32-L95 (FINAL DEAD-SLOT CLEANUP REJECTED STATICALLY).** The earlier
  callee-first dead-slot probe changed late-inliner pricing and was destructive
  interference.  A bounded follow-up ran the existing cleanup only after all
  inlining decisions.  It correctly kept macro `Token` move calls at 37 while
  removing four unused LowIR stores and reducing optimized macro output by
  367 lines.  The native move body was nevertheless byte-for-byte unchanged:
  native dead-store planning already suppresses that traffic.  The final-only
  scheduling probe therefore has no runtime mechanism to measure and is
  reverted without a broad lane or student contract.

- **P32-L96 (SCALED-INDEX MULTIPLIER FACTORING RETAINED).** Current
  `Lexer::Peek` repeatedly lowered a 24-byte fixed-queue element as
  `index * 24`, then materialized the byte offset before each memory access.
  Clang instead keeps the legal scale eight in the address and computes only
  `index * 3`.  The retained generic PA37 transform finds a positive
  single-use `i64` multiply feeding `index`, factors the exact byte
  displacement between the multiplier and a conservative object stride of
  two, four, or eight, and changes nothing when the multiply has another use,
  no legal factor, or an unrepresentable quotient.  Modular byte displacement
  is unchanged; no source function, container, capacity, or multiplier value
  is recognized.

  Native addressing now fuses the scale-eight part.  `Lexer::Peek` shrinks
  433 -> 421 bytes, `Lexer::Run` 11,764 -> 11,668, and
  `TranslationCursor::Next` 1,503 -> 1,487.  The generic implementation grows
  complete compiler text 13,668 bytes.  The final O1/all-32 compiler prepared
  in 17.87 seconds under `/dev/shm/v3codex-p34-scaled-index-final.oh9Hwg`
  with hash `cbe8b322...c2ee`.  Three clean full-source lanes measured
  31.73/868.36/48.73, 32.04/872.22/49.25, and 32.34/871.74/49.44 seconds
  wall/user/system, averaging 32.037 wall and 919.913 aggregate CPU.  Against
  L93's stable 32.265/924.170 mean, wall improves 0.71% and CPU 0.46%.  A
  rotated retained pair at 32.66 and a scheduler-tail 34.72 seconds confirms
  direction but is not substituted for the stable denominator.  Against
  L94's immediately preceding host lanes, the provisional direct wall ratios
  are 1.519x GCC and 1.496x Clang; a source-matched host refresh remains due,
  and GCC still needs about 0.40 seconds.

  PA37's contract describes only the equal-displacement multiplier/stride
  property.  Control `525-historical-lowir-contracts` requires the O0
  `*24`/byte-index baseline, the O1 `*3`/eight-byte relationship, unchanged
  multi-use and unfactorable cases, and O1/O2 behavior.  It compares no
  complete LowIR/MIR/native program, source workload, instruction sequence,
  register, object, or hash.  PA37 passes 187/187, PA38 passes 46/46,
  through-PA38 passes 5,453/5,453, and the PA38 audit has only the 36
  established warnings; `lowir_opt.cpp` remains 3,000 lines.  Fresh all-32 O1
  inception under `/dev/shm/v3codex-p34-scaled-index-inception.1fv9ep`
  matches every object and the final compiler in 50.93/1323.78/93.81 seconds
  wall/user/system.  No Cachegrind, profiler, or inception process remains.

- **P32-L97 (SOURCE-MATCHED SCALED-INDEX REBASELINE).** Commit `58204585`
  is pushed to `origin/v3opt`.  Fresh exact-revision O1 host compilers prepared
  with outer, inner, and object parallelism all 32: GCC took
  29.87/520.84/51.77 seconds wall/user/system under
  `/dev/shm/v3codex-p34-scaled-host-gcc.2IT8TM`, and Clang took
  30.28/605.80/38.43 under
  `/dev/shm/v3codex-p34-scaled-host-clang.UdBzeL`.  Their sequential clean
  full-source lanes measured 21.04/543.30/44.40 and
  21.62/560.34/44.95 seconds, respectively.

  L96's three-lane directional average included two compiler binaries from
  immediately before the final constant-spelling repair.  Although that
  repair does not change the optimized hot native bodies, it does change the
  compiler executable, so it is not used in the exact-revision ratio.  The
  final binary's prior 32.34/871.74/49.44 lane and two fresh lanes at
  32.06/869.68/49.28 and 32.34/871.41/49.21 average 32.247 seconds wall and
  920.253 aggregate CPU.  The resulting direct ratios are 1.533x GCC and
  1.492x Clang by wall, and 1.566x GCC and 1.520x Clang by aggregate CPU.
  Clang is below the wall target; GCC remains binding.  At this GCC sample the
  self ceiling is 31.560 seconds, requiring another 0.687 seconds or 2.13%
  of self wall time.  No source workload was changed for any lane, every lane
  used a fresh object/bin root, and no profiler or inception lane overlapped.

- **P32-L98 (EXACT-FINAL FULL-SOURCE ATTRIBUTION).** A fresh 199 Hz
  `task-clock` profile of the exact-final scaled-index compiler recorded
  917.9245 aggregate event seconds, about 182K samples, and zero lost samples
  under `/dev/shm/v3codex-p34-scaled-final-profile.mlL3hL/self.data`.
  Approximate absolute costs are 97.5 seconds in `Lexer::Peek`, 74.2 in
  `Lexer::Run`, 39.3 in `PhysicalCursor::Next`, 26.0 in the macro `Token`
  move, 25.8 in `TranslationCursor::Next`, and 20.1 in `AppendUTF8`.
  Relative to the structurally closer Clang profile, the combined
  `Peek`/translation excess is only about 11 seconds, while `Run` is about
  25.6 seconds high and physical-next about 14 seconds high.  The next target
  is therefore the `Run`/physical movement body rather than isolated `Peek`.
  The completed profile remains as evidence; no recording, profiler,
  Cachegrind, or build process remains active.

- **P32-L99 (BOUNDED CALL-CROSSING LOCAL-PHI PROBE REJECTED STATICALLY).** A
  generic diagnostic admitted a local integer phi to a call-preserved
  register only when there was no caller-side assignment and the value had at
  least three uses.  On the tokenizer it found three candidates, promoted one,
  and rejected two as busy.  `Lexer::Run` gained two preserved colors, lost
  only three MIR instructions and one load/store pair, but grew 281 native
  bytes; tokenizer text grew 280 bytes.  The static result has no credible
  runtime mechanism, so this probe is rejected without timing under the
  corrected ratio protocol.  All code and telemetry are removed and no
  student contract or test is retained.

- **P32-L100 (ADJACENT FRAME-UPDATE PROBE AND RATIO-GATE CORRECTION).** A
  generic native cleanup folded an exact temporary-frame
  load/immediate-ALU/store-back chain into a memory-destination update after
  proving binding/type identity, nonvolatility, exact bridge use count, and
  scratch-register liveness.  It reached the hot `Run` induction update:
  `Run` lost eight MIR instructions and 23 native bytes, tokenizer text lost
  42 bytes, and complete compiler text lost 15,760 bytes despite the new
  implementation.  PA29 passed 291/291.  The explicit O1/all-32 candidate at
  `/dev/shm/v3codex-p34-frame-update-candidate.9m0AqE` has SHA-256
  `e978b53cef9690f6c0b6a28e107f9930fd98550e5a658a64b0fd7278aa2864f6`.

  Two candidate full-source self lanes measured 32.65/873.96/49.62 and
  32.24/871.70/49.79 seconds wall/user/system; the adjacent exact-final
  production repeat measured 32.36/872.87/49.48.  That self-only result was
  initially treated as a rejection, which was the wrong metric for a change
  that also alters the source compiled by the hosts.  A candidate-source GCC
  compiler was therefore prepared under
  `/dev/shm/v3codex-p34-frame-update-host-gcc.RoH9QF`, and its clean lane under
  `/dev/shm/v3codex-p34-frame-update-full-gcc.1AR7cY` measured
  20.78/544.01/44.31 seconds.  Candidate means are 32.445 wall and 922.535
  aggregate CPU, giving 1.561x wall and 1.568x CPU versus same-source GCC.
  The production ratios are 1.533x and 1.566x.  Thus this particular probe
  still loses under the corrected metric: GCC wall became faster, and the CPU
  ratio also worsened slightly.  Because GCC is binding and both hosts must
  pass, no Clang lane is needed.  The implementation is removed; no contract
  or property is retained, and no profiler, Cachegrind, inception, or build
  process remains.

- **P32-L101 (RATIO-AWARE REJECTION REPLAY QUEUE).** Rejections based only on
  a candidate self compiler were re-audited.  Correctness failures, probes
  with no final native change, source/workload-specific rewrites, and large
  generated-work regressions remain closed without denominator timing.  The
  highest-value replay is L21's converted Boolean-phi threading: it passed
  PA37/PA38 and inception, removed 281 `Run` MIR instructions, 80 loads, 108
  stores, and 699 native bytes, but was rejected on essentially flat frozen
  self timing without a candidate-source host denominator.  Second is
  L73/L79's definition-pointer representation, whose current full-source self
  result was flat (-0.02% aggregate CPU) and whose same-source host ratios were
  never measured.  The broad deferred fixed-result division form from L72 and
  unused-result dynamic-copy lowering from L23 are secondary replays because
  they have real compiler-wide populations but worse self/layout evidence.
  The immediate-return phi (L59), exact-value underflow scheduling (L22/L32),
  scratch-carried immediate-call phi (L76), and narrow load fusion (L77) are
  lower priority: their final mechanisms or populations are small and their
  later full-source self screens were neutral-to-negative.  L53 has already
  been superseded by the retained odd-save paired recoloring; L88 changes only
  a build-time policy knob, not candidate source, so its denominator did not
  move; L86/L95 and the new L99 had no material native mechanism; unsound L19
  and the broad L77 form remain permanently closed.

- **P32-L102 (CONVERTED BOOLEAN-PHI REPLAY REJECTED BY SAME-SOURCE RATIO).**
  L21's PA37 transform was reconstructed on the exact-final tree.  It extended
  the retained loop-local Boolean-phi threading through a private
  single-predecessor bridge containing a widening/truncating integer
  conversion and either a direct truth branch or a zero/one comparison.
  Truncation and comparisons to one required every incoming value to be a
  local comparison result or literal zero/one; shared bridges, loop-carried
  inputs, calls, EH, target phis, and additional uses retained the merge.

  Later landings reduced but did not erase the current population.
  `Lexer::Run` fell from 2,753 to 2,648 MIR instructions, scalar loads/stores
  from 453/433 to 400/374, call loads/stores from 79/62 to 69/28, and native
  size from `0x2d94` to `0x2cb2` (-226 bytes); tokenizer primary text fell
  29,227 -> 29,001 bytes.  PA37 passed 187/187 and PA38 passed 46/46.

  The first reconstruction used a whole-function comparison-definition scan.
  Its candidate compiler hash was
  `945043d603167dc999130686df21794b27f48378e762eba2e14ec831cce4de9e`.
  Two clean self lanes measured 32.33/876.18/49.44 and
  32.08/877.56/49.66 seconds wall/user/system.  After excluding one obvious
  23.08/599.16/50.23 GCC outlier, two stable same-source GCC lanes measured
  20.94/545.26/44.35 and 21.21/545.24/44.79.  Their ratios were about 1.528x
  wall, a 0.30% apparent improvement, but 1.571x aggregate CPU, a 0.32%
  regression.  The mixed signal did not clear the ratio gate.

  A cheaper form proved comparison results by scanning only the small incoming
  predecessor block, retaining identical generated code.  Its compiler hash
  was `769e3bd09066e1d3274c0eda9bf9f2b6c320a1f5ecd9dd4519a7b3fa9887e80a`.
  Self lanes at 32.02/872.76/49.06 and 32.27/874.38/49.08 average 32.145 wall
  and 922.640 aggregate CPU.  Same-source GCC lanes at
  20.78/542.57/44.25 and 21.06/542.56/44.49 average 20.920 wall and 586.935
  CPU.  The resulting 1.537x wall and 1.572x CPU ratios both worsen the
  production 1.533x/1.566x point.  GCC remains binding, so no Clang lane was
  consumed.  This replay demonstrates why both same-source wall and CPU
  ratios are required: the conclusion is no longer based on a flat self-only
  screen, but the corrected metric still rejects this candidate.  All
  implementation code is removed; no new contract/property is retained, and
  no profiler, Cachegrind, inception, or build process remains.

- **P32-L103 (DEFINITION-POINTER REPLAY REJECTED BY SAME-SOURCE RATIO).**
  L73/L79's semantics-neutral PA37 representation was reconstructed on the
  exact-final source.  The value simplifier stored pointers to instructions
  only after each survivor reached its stable compacted prefix slot.  The
  vectors cannot reallocate during the pass, later compaction cannot overwrite
  an earlier prefix element, shrinking preserves prefix addresses, and the
  pointer table expires before any later mutating pass.  PA37 passed 187/187;
  `lowir_opt.cpp` was 2,981 lines during the probe.  Generated compiler text
  fell 7,920,354 -> 7,918,850 bytes (-1,504), while the current 73 static
  `strlen@plt` calls were unchanged.

  The all-32 O1 candidate under
  `/dev/shm/v3codex-p34-definition-pointer-ratio-candidate.XbSNGO` and every
  full-source lane produced compiler SHA-256
  `02572d808a986264b489dd608b906883a717c7dbc322898f9aecc3e9cfeb6b6f`.
  Two self lanes measured 32.77/878.47/49.83 and
  31.93/869.14/49.04 seconds wall/user/system, averaging 32.350 wall and
  923.240 aggregate CPU.  The candidate-source GCC compiler prepared under
  `/dev/shm/v3codex-p34-definition-pointer-ratio-host-gcc.EjhRXL`; its two
  clean lanes measured 20.74/543.19/44.56 and 20.89/541.65/44.31, averaging
  20.815 wall and 586.855 CPU.  Candidate ratios are therefore 1.554x wall
  and 1.573x CPU, both worse than production's 1.533x and 1.566x.  GCC is
  binding, so no Clang lane was consumed.  The missing same-source denominator
  has now been supplied and does not rescue the old rejection.  All probe code
  is removed, `lowir_opt.cpp` is restored to 3,000 lines, and no profiler,
  Cachegrind, inception, or build process remains.

- **P32-L104 (EARLIER-PLAN RATIO-SENSITIVITY AUDIT).** Rejected survivors
  and probes in `PLAN-O1-PARITY.md` and `PLAN-INLINE-PARITY.md` were also
  checked for the same self-only metric error.  No earlier item outranks the
  P32 queue.  Memory-GVN, inlining-dose, residency, recoloring, compare, and
  fixed-result placement experiments were superseded by narrower retained
  P30--P32 mechanisms or retested on later placement; unsound allocator/address
  variants remain closed; source reshapes remain out of scope; and probes with
  no final native change need no denominator lane.  P30 L58--L60 are subsumed
  by P32's broader fixed-result/division work.  P30 L74--L76/L80 changed only
  compiler-work microstructure, had deterministic whole-self instruction
  regressions, and exposed no target-code population, so they remain below the
  replay threshold.

  The one additional sound lower-priority replay is O1 P22b's block-local
  frame-load forwarding: after its store-invalidation fix it removed 911
  frozen loads and 1.2 KiB of target text with self wall/user at +0.09%/0.00%,
  but it never received a candidate-source host denominator.  It remains
  behind P32 L72's compiler-wide deferred fixed-result division and L23's
  still-profiled unused-result dynamic-copy class.  Reconstruct L72 first,
  screen it on the current full-source O1 workload, and build the binding GCC
  denominator only if the current static population and self lane remain
  credible; do not replay obsolete variants merely because their historical
  numerator was close.

- **P32-L105 (DEFERRED FIXED-RESULT DIVISION REPLAY REJECTED BY
  SAME-SOURCE WALL RATIO).** L72's broad PA29 form was reconstructed from its
  archived candidate rather than approximated.  When constant division was
  not followed by an adjacent result move or return, it scanned the remainder
  of the block, tracked the two fixed division results until redefinition, and
  selected a result only when exactly one was read.  PA29 passed 291/291,
  PA38 passed 46/46, and an all-32 O1 inception comparison matched every
  object and the final compiler.  The candidate compiler under
  `/dev/shm/v3codex-p34-live-div-ratio-candidate.HD7kJD` has SHA-256
  `6ed4573654ea470576ef018e037e393da93e130ffa900e6e44c88e89df77475b`.
  Complete text grew 91,280 bytes, while static native division fell from
  4,662 to 354 signed operations and from 405 to 382 unsigned operations: a
  real 4,331-operation target-code population, not a source-specific match.

  Concurrent work elsewhere on this 32-CPU host contaminated several initial
  lanes; those runs had much higher aggregate CPU and involuntary context
  switching and are not included.  Two uncontended candidate self lanes
  measured 32.01/869.52/48.83 and 32.59/871.21/49.87 seconds
  wall/user/system, averaging 32.300 wall and 919.715 aggregate CPU.  The
  candidate-source GCC compiler prepared under
  `/dev/shm/v3codex-p34-live-div-gcc-prep.BypBwg`; two uncontended lanes
  measured 21.02/544.67/47.25 and 21.04/543.07/45.28, averaging 21.030 wall
  and 590.135 CPU.  The resulting 1.536x wall ratio is slightly worse than
  production's binding 1.533x even though the 1.559x CPU ratio is slightly
  better than 1.566x.  The target is explicitly wall-time parity, so the
  mixed result does not retain a 91 KiB implementation with no measured self
  wall benefit.  GCC remains binding and no Clang lane is needed.  All probe
  code is removed; no student contract/property is retained.  L23's
  unused-result dynamic-copy lowering is the next ratio-aware replay.

- **P32-L106 (UNUSED-RESULT DYNAMIC COPY RETAINED BY SAME-SOURCE
  RATIO).** L23's PA29 lowering was reconstructed from its archived compiler
  rather than approximated.  A direct three-argument call whose callee carries
  canonical `cppgm_builtin_memcpy` object metadata and whose returned pointer
  is unused now becomes a zero-operand `copy_bytes_dynamic` MIR operation
  after ordinary SysV argument staging.  Native emission moves the runtime
  count from `rdx` to `rcx` and emits `rep movsb`; its MIR use/definition facts
  model only the `rdi`/`rsi`/`rdx` inputs and `rdi`/`rsi`/`rcx` writes instead
  of a full call boundary.  Used results, indirect or unmarked calls, and
  other argument shapes remain calls.  The rule is native instruction
  selection at both O0 and O1.  On the existing PA16 builtin-memory fixture,
  the reconstructed O0 object was byte-for-byte identical to the archived L23
  compiler's output.

  On the exact-final full compiler, direct `memcpy@plt` calls fall from 898 to
  27 while `rep movsb` sites rise from 1,896 to 2,768.  Complete compiler text
  grows only 7,920,354 -> 7,923,410 bytes (+3,056).  The all-32 O1 compiler at
  `/dev/shm/v3codex-p34-dyncopy-final.znFOCf` has SHA-256
  `373a2118518095629a874cc1589fb6d904c54505f4a78ca7ad48a06719eca5dc`;
  a fresh inception comparison matched every object and the final compiler in
  35.26/876.76/51.02 seconds wall/user/system.  Outer, inner, and object
  parallelism were all explicitly 32.

  Two stable exact-final self lanes measured 32.16/873.87/50.18 and
  31.99/874.19/49.94 seconds, averaging 32.075 wall and 924.090 aggregate CPU.
  A 33.07/873.87/49.85 lane is excluded as a scheduler-tail outlier: aggregate
  work matched the retained lanes but involuntary context switches rose to
  78,219 versus 67,936 and 69,323.  The exact-source GCC compiler under
  `/dev/shm/v3codex-p34-dyncopy-final-gcc-prep.whHw9H` produced lanes at
  21.18/547.27/45.10 and 21.13/545.61/45.19, averaging 21.155 wall and
  591.585 CPU.  The exact-source Clang compiler under
  `/dev/shm/v3codex-p34-dyncopy-final-clang-prep.rtBlyZ` produced lanes at
  22.45/564.33/45.85 and 21.72/564.07/45.88, averaging 22.085 wall and
  610.065 CPU.  Final ratios are therefore 1.516x GCC and 1.452x Clang by
  wall, and 1.562x GCC and 1.515x Clang by aggregate CPU.  All four improve
  production's 1.533x/1.492x wall and 1.566x/1.520x CPU ratios, reversing the
  old self-only rejection.  GCC remains binding; at this denominator the
  1.50x ceiling is 31.733 seconds, another 0.343 seconds or 1.07% of self
  wall.

  PA29 now describes the high-level unused-result builtin rule and runtime
  count boundary.  Control `915-unused-result-builtin-memcpy` checks O0/O1
  behavior, the eligible dynamic-copy relationship, retained used-result and
  unmarked calls, and a sentinel immediately beyond the runtime copy count.
  It compares no complete program or MIR, register assignment, frame layout,
  instruction bytes, executable, or hash.  Focused PA29 is 291/291, PA37 is
  187/187, PA38 is 46/46, and the through-PA38 report is 5,453/5,453.  The
  PA38 file audit passes with the 36 established warnings; both touched
  3,000-line native files remain at their cap.  No profiler, Cachegrind, or
  build process remains active.  O1 P22b's block-local frame-load forwarding
  is the next strongest same-source replay.

- **P32-L107 (BLOCK-LOCAL FRAME-LOAD FORWARDING REPLAY REJECTED BY
  SAME-SOURCE WALL RATIO).** O1 P22b's corrected machine-level rule was
  reconstructed inside the existing local copy-propagation walk.  A typed,
  nonvolatile integer or pointer frame load could reuse an earlier load's
  still-live physical register within the same block.  Register definitions,
  overlapping direct frame stores, indirect stores, calls, throws, bulk
  writes, and atomics invalidated the fact; encoder-owned `r10`/`r11` were
  never carriers.  This explicitly preserved P22b's plain-`MI_STORE`
  soundness repair rather than repeating the historical bug.

  Later placement work made the current population much larger than P22b's
  old 911-load/1.2-KiB result.  Downstream local propagation compounded the
  forwarded loads, shrinking complete compiler text from 7,923,410 to
  7,868,546 bytes (-54,864).  PA38 passed 46/46.  An initial fixed-array
  invalidation implementation also completed an all-32 O1 inception
  comparison with every object and final compiler matching; its avoidable
  per-instruction scan was then replaced by a register-validity bitmask
  without changing the generated-code rule.  The final bitmask candidate at
  `/dev/shm/v3codex-p22b-candidate.h4kc3J/bin/selfhost/cppgm++-self` has
  SHA-256 `cd36f078e2917164c18521d48525f2e8ae48421b652099d1ddb62ace057542c9`.

  Two clean all-32 full-source self lanes measured
  32.70/873.22/49.96 and 32.52/870.08/49.78 seconds wall/user/system,
  averaging 32.610 wall and 921.520 aggregate CPU.  A third lane at
  34.08/911.18/53.28 was excluded because aggregate work rose by about 4.7%,
  not merely because its scheduler tail moved.  The exact-source GCC compiler
  under `/dev/shm/v3codex-p22b-gcc-prep.eoJv72` measured
  21.11/546.60/44.61 and 21.28/547.47/45.40, averaging 21.195 wall and
  592.040 CPU.  Candidate ratios are therefore 1.539x wall and 1.557x CPU.
  Aggregate CPU improves L106's 1.562x point by about 0.35%, but the binding
  wall ratio worsens from 1.516x by about 1.48%.  The explicit wall-time
  objective rejects that mixed trade; no Clang lane is warranted.

  The implementation is removed, PA38 remains clean, and no contract or
  property is retained for rejected behavior.  This closes the only
  additional high-priority rejection identified by the earlier-plan audit.
  P32 L59's immediate scalar-return phi is the sole remaining source-independent
  replay with real final native shrink and no same-source host denominator;
  it is a lower-value static screen because its later full-source self result
  was neutral and its representation requires compatibility-test updates.

- **P32-L108 (IMMEDIATE SCALAR-RETURN PHI REPLAY REJECTED BY
  SAME-SOURCE WALL RATIO).** P32-L59's source-independent placement rule was
  reconstructed on the current tree.  At O1, a non-loop-carried integer or
  pointer phi used once by the block's immediate return, optionally through
  the block's sole scalar conversion, received the ABI return register as its
  home.  The candidate reproduced L59's recorded compatibility footprint:
  PA38 reached 44/46 because the loop/EH and staged-home canonicalizers, plus
  the acyclic-frame diagnostic, assume a frame-backed phi representation.
  Those controls were not weakened for a performance probe.

  The current complete compiler text fell 7,923,410 -> 7,917,346 bytes
  (-6,064).  Its explicit O1/all-32 preparation took 17.86 seconds at
  `/dev/shm/v3codex-p33-returnphi-replay.oJCZW2/bin/selfhost/cppgm++-self`,
  SHA-256
  `27abef54a95c3e939cccae43bc4809595420f133628c5d9f863f2c17afd2aac4`.
  Two deterministic all-32 full-source self lanes measured
  32.93/870.79/50.76 and 32.18/868.48/49.77 seconds wall/user/system,
  averaging 32.555 wall and 919.900 aggregate CPU.

  The exact-source GCC compiler prepared in 30.11 seconds at
  `/dev/shm/v3codex-p33-returnphi-gcc-prep.J9F4x1/bin/host/cppgm++-self`,
  SHA-256
  `30c85fe2500a9ded7f3f339861d978f7b83c5ab46995af729dbaab81a6262864`.
  Two uncontended lanes measured 21.16/545.60/44.94 and
  21.18/545.77/44.91, averaging 21.170 wall and 590.610 aggregate CPU.  A
  22.38/586.34/49.75 lane was excluded as a 7.7% aggregate-work outlier; a
  23.24/553.08/45.81 lane confirmed ordinary aggregate work but had a large
  scheduler tail and was excluded from the wall mean.  Every lane produced
  the candidate hash above.

  The resulting same-source ratios are 1.538x wall and 1.558x aggregate CPU.
  Relative to L106's retained 1.516x/1.562x point, CPU ratio improves about
  0.29% but the binding wall ratio worsens about 1.43%.  The corrected metric
  therefore does not rescue L59.  The implementation is removed, the PA38
  compatibility controls remain unchanged, and restored PA38 is 46/46.  No
  compiler, make, profiler, Cachegrind, or Valgrind process remains active.
  This closes the ratio-sensitive rejection queue: remaining old probes were
  already denominator-tested, had no final native-code effect or deterministic
  generated-code regressions, were unsound/source-shaping, or were superseded
  by retained narrower work.

- **P32-L109 (LIFETIME-BOUNDED O1 MEMORY-GVN REPLAY REJECTED AFTER BOTH
  HOST DENOMINATORS).** The earlier P30-L69 policy was recovered from its
  archived compiler rather than approximated.  Ordinary O1 functions ran the
  conservative memory-GVN pass, but a redundant load was replaced only when
  the dominating value's final presentation-order use was at least as late as
  the removed load's final use.  This preserved the old rule's protection
  against lengthening a carrier lifetime.  PA37 passed 187/187.  The finished
  all-32 O1 compiler at
  `/dev/shm/v3codex-p34-lifetime-gvn-ratio.9Uf0zH/bin/selfhost/cppgm++-self`
  has SHA-256
  `7920566281aa2ab6cab966f551fe09f0c910721a742c48b7546e6bb6d42e2314`;
  complete text fell 8,656,879 -> 8,650,819 bytes (-6,060).  Every retained
  self and host timing lane reproduced that candidate hash.

  Three ordinary-aggregate self lanes measured 31.65/866.56/49.14,
  32.47/869.22/49.65, and 33.27/873.19/50.36 seconds wall/user/system.  The
  last wall result had 78,806 involuntary context switches and is a scheduler
  tail, while its aggregate work remains valid; a separate
  34.53/931.81/54.97 lane is excluded entirely because aggregate work rose
  7.3%.  The exact-source GCC compiler under
  `/dev/shm/v3codex-p34-lifetime-gvn-gcc-prep.kCo5zl` produced ordinary lanes
  at 21.69/546.91/45.63, 20.93/542.67/44.99, and
  21.14/544.41/44.99; its 23.31/553.94/46.46 lane is excluded because both
  wall and aggregate work rose.  The two usable self wall samples average
  32.060 seconds against GCC's 21.253, or 1.508x, while all three aggregate
  self samples average 919.373 CPU seconds against 589.867, or 1.559x.

  That marginal GCC result did not survive the required second host.  The
  exact-source Clang compiler under
  `/dev/shm/v3codex-p34-lifetime-gvn-clang-prep.7W5OvL` produced ordinary
  lanes at 21.55/563.79/44.30 and 22.10/565.31/44.96, averaging 21.825 wall
  and 609.180 CPU; a 23.32/584.05/47.12 aggregate outlier is excluded.  The
  candidate is therefore about 1.469x Clang by wall and 1.509x by aggregate
  CPU.  CPU ratios improve only 0.2--0.4%, below the 0.5% landing threshold,
  while Clang wall ratio regresses from L106's 1.452x point.  The
  implementation is removed and no student contract/property is retained.

- **P32-L110 (SIGN-EXTENDED IMM8 REPLAY STILL REJECTED BY THE CORRECTED
  METRIC).** P32-L33's PA29 encoder rule was reconstructed with the original
  exact-width guard: 16-, 32-, and 64-bit integer ALU immediates and memory
  comparisons selected opcode `83` only when the operation-width value
  equaled the sign extension of its low byte.  PA29 passed 291/291 and PA38
  passed 46/46.  The all-32 O1 compiler at
  `/dev/shm/v3codex-p34-imm8-ratio.R2zcEa/bin/selfhost/cppgm++-self` has
  SHA-256
  `b2a668eb95095980903086da1d581fa282c2854902072a0e6e8c976fcd7ddae4`;
  complete text fell 8,656,879 -> 8,558,503 bytes (-98,376) and the file
  shrank 98,304 bytes.  Thus the replay preserved the historical large,
  source-independent density effect rather than merely adding host work.

  Two ordinary-aggregate self lanes measured 32.79/868.16/49.48 and
  31.73/867.84/49.45 seconds wall/user/system, averaging 917.465 aggregate
  CPU.  The first had a 76,015-context-switch scheduler tail, so 31.73 is the
  usable wall point; a 34.15/890.21/51.99 lane is excluded because aggregate
  work also rose.  The exact-source GCC compiler under
  `/dev/shm/v3codex-p34-imm8-gcc-prep.siAioX` produced ordinary lanes at
  20.84/542.95/44.82 and 20.74/543.66/45.00, averaging 20.790 wall and
  588.215 CPU.  A 22.34/580.75/48.71 lane is excluded as a 7% aggregate-work
  outlier.  Every usable lane reproduced the candidate hash.

  The resulting 1.526x wall and 1.560x aggregate-CPU ratios do not improve
  L106's binding 1.516x/1.562x point by the required amount; the large density
  win still has no runtime payoff at the current frameless layout.  GCC is
  already non-improving, so no Clang lane is consumed.  The implementation is
  removed and no student contract/property is retained.

- **P32-L111 (FORWARDED-ADDRESS COMPOSITION REPLAY REJECTED BY
  SAME-SOURCE RATIO).** The remaining lower-priority P32-L51 candidate was
  reconstructed at the existing bounded frame-forwarding boundary.  When a
  proved delayed frame reload fed only the address of the immediately
  following scalar load or store, native emission substituted the intact
  forwarding carrier into that address and omitted the intermediate move.
  Other register dependencies, non-address consumers, and nonadjacent uses
  retained the ordinary forwarded move.  PA29 passed 291/291 and PA38 passed
  46/46.

  The all-32 O1 compiler at
  `/dev/shm/v3codex-p34-forwarded-address-ratio.S6MWAQ/bin/selfhost/cppgm++-self`
  has SHA-256
  `d275709cf84f07b6cbe34575a95edca03a846032eb4e05bf8279470c5081ffdf`;
  complete text fell 8,656,879 -> 8,640,559 bytes (-16,320), closely
  reproducing L51's historical 16,744-byte broad effect.  Two self lanes
  measured 32.29/870.27/49.59 and 32.99/870.90/50.25 seconds
  wall/user/system, averaging 32.640 wall and 920.505 aggregate CPU.  The
  exact-source GCC compiler under
  `/dev/shm/v3codex-p34-forwarded-address-gcc-prep.EL6IfX` produced lanes at
  21.13/545.29/44.79 and 21.15/545.74/45.21, averaging 21.140 wall and
  590.515 CPU.  Every lane reproduced the candidate hash.

  Candidate ratios are therefore 1.544x wall and 1.559x aggregate CPU.
  Aggregate ratio improves L106 by only about 0.2%, while the binding wall
  ratio regresses about 1.8%.  GCC is decisively non-improving, so no Clang
  lane is consumed.  The implementation is removed and no student
  contract/property is retained.  This closes the old self-only rejection
  set with material current native effects: smaller P30 residency and narrow
  normalization/compare probes either have deterministic compiler-work
  regressions below this population, were later screened on the broad oracle,
  or failed correctness and do not warrant denominator-only rescue attempts.

- **P32-L112 (DYNAMIC-COPY SMALL-BUFFER INTERFERENCE PROBES REJECTED BY
  SAME-SOURCE RATIO).** Exact-final task-clock profiles of the L106 compiler
  and its source-matched GCC and Clang hosts were compared before another
  native change.  The self profile charged about 34 seconds to the macro
  `Token` move constructor.  Address-level comparison against the pre-L106
  scaled-index compiler found the distinguishing instruction: the short-string
  arm's runtime `length + 1` copy changed from `memcpy@plt` to L106's
  `rdx`-to-`rcx`; `rep movsb` sequence.  This was a plausible destructive-
  interference hypothesis, but the symbol attribution alone could also move
  work formerly charged inside libc into the caller.

  A sound source-independent probe therefore retained the ordinary call when
  the dynamic count was formed directly as one plus a loaded value.  It made
  no claim that the value was bounded: preserving the original call is always
  conservative, and both native liveness analysis and selection used the same
  predicate.  PA29 passed 291/291.  The all-32 O1 compiler at
  `/dev/shm/v3codex-dyncopy-loaded-successor.eNiDov/bin/selfhost/cppgm++-self`
  has SHA-256
  `8ece171724231e3f7c71708ecfeb1f76c0b7f081b4c9bdca7f13bc8a91e9b6dd`.
  It restored the hot constructor call and 470 compiler-wide calls: static
  `rep movsb` sites fell 2,768 -> 2,298, `memcpy@plt` calls rose 27 -> 497,
  and complete text grew only 8,656,879 -> 8,658,103 bytes.

  Two ordinary-work self lanes measured 31.92/871.02/49.43 and
  32.25/872.33/49.15 seconds wall/user/system, averaging 32.085 wall and
  920.965 aggregate CPU.  A 33.69/903.60/52.52 lane is excluded because
  aggregate work rose 3.8%.  The exact-source GCC compiler under
  `/dev/shm/v3codex-dyncopy-loaded-gcc-prep.91gLrc` produced lanes at
  20.89/542.84/44.35 and 21.06/543.71/44.76, averaging 20.975 wall and
  587.830 CPU.  Candidate ratios are therefore 1.530x wall and 1.567x CPU,
  both worse than L106's 1.516x/1.562x binding point.  GCC rejects the broad
  hypothesis, so no Clang lane is warranted.

  A final narrow screen preserved only loaded-successor copies in functions
  explicitly marked nonthrowing.  It still restored the hot constructor but
  only 49 compiler-wide calls (`rep movsb` 2,719, `memcpy@plt` 76).  The
  all-32 compiler at
  `/dev/shm/v3codex-dyncopy-nothrow-successor.D9sNOS/bin/selfhost/cppgm++-self`,
  SHA-256
  `2ef95f1412ee765e884ba906e9825f5385b25a3e55b188cfb46c7b66ffe7598a`,
  measured 32.24/871.32/49.38 and 32.15/872.20/48.93 seconds.  Its 920.915
  mean aggregate CPU is indistinguishable from adjacent production controls;
  the production wall repeat had a scheduler tail and its second aggregate
  repeat was itself an outlier.  Narrowing the already ratio-negative broad
  population therefore supplied no measured work win that could justify
  another host build.

  Both probes are removed.  The apparent constructor-profile increase was
  attribution movement, not evidence that preserving its call improves the
  complete workload.  No student contract/property is retained for rejected
  behavior, and no compiler, make, profiler, Cachegrind, or Valgrind process
  remains active.

- **P32-L113 (ADJACENT 16-BYTE SCALAR-TRANSFER PROBE REJECTED BY
  SAME-SOURCE RATIO).** Exact-final profiles showed that self still used four
  scalar memory operations for adjacent 64-bit cursor fields where Clang used
  one unaligned vector load and store.  A source-independent native probe
  recognized two adjacent nonvolatile integer/pointer loads whose only uses
  were ordered adjacent stores, with exact liveness, alias-point, CFG, and
  reserved-scratch-clobber checks.  Separate `load_bytes16` and
  `store_bytes16` pseudos kept the read and write at their original semantic
  positions instead of moving a copy across possible aliasing work.

  A current full-source census found 5,069 adjacent load pairs in 174
  translation units and 80 conservatively proved transfers across 205 final
  MIR files.  The generated programs lost 167 MIR lines and 1,216 aggregate
  text bytes; `Lexer::Peek` changed its two-load/two-store field transfer to
  `movdqu` and shrank 12 bytes.  The implementation itself outweighed that
  density win: complete compiler text grew 8,656,879 -> 8,668,127 bytes.
  PA29 passed 291/291 and PA38 passed 46/46.  The explicit O1/all-32 candidate
  prepared in 17.97 seconds at
  `/dev/shm/v3codex-pair16-candidate.kSNPrC/bin/selfhost/cppgm++-self`,
  SHA-256
  `621db7ee5f1844547060dec45d945c538ef3ed4fc5c1dc696c897f1fd85c4dac`.

  Two successful self lanes measured 32.13/872.57/49.14 and
  32.83/873.99/49.36 seconds wall/user/system.  The second had a 79,342
  involuntary-context-switch scheduler tail, while both give ordinary
  aggregate work averaging 922.53 CPU seconds.  An earlier compile-complete
  lane is excluded entirely because the candidate had accidentally been
  inferred as `CPPGM_HOST_CXX` and native linking failed; the corrected lanes
  explicitly used `CPPGM_HOST_CXX=g++` and produced the candidate hash above.

  The exact-source GCC compiler prepared with all three job levels at 32 in
  29.57 seconds under `/dev/shm/v3codex-pair16-gcc-prep.NbS08w`.  Its first
  lane at 23.21/554.34/46.35 had both a large scheduler tail and elevated
  aggregate work and is excluded.  The clean repeat measured
  21.00/542.43/45.01 seconds and reproduced the candidate hash.  The usable
  ratios are therefore about 1.530x wall and 1.570x aggregate CPU, both worse
  than L106's retained 1.516x/1.562x GCC point.  GCC benefits more from the
  added source and the small target-code saving does not pay for the analysis,
  so no Clang lane is warranted.  The implementation is removed; no student
  contract/property is retained for rejected behavior, and no compiler,
  profiler, Cachegrind, or Valgrind process remains active.

- **P32-L114 (REPEATED COLD-TAIL INLINE BUDGET REJECTED BY SAME-SOURCE
  RATIO).** Exact-final self/GCC/Clang profiles and native symbol sizes showed
  another plausible destructive-interference mechanism.  Self had inlined
  six calls to `PhysicalCursor::Continuation` into `DecodeOne`, and then
  inlined `DecodeOne` into `PhysicalCursor::Next`; Clang retained the
  149-byte continuation as a separate function and GCC retained the larger
  decoder.  A source-independent O1 probe therefore bounded repeated cloning
  within one caller when the callee had both an ordinary path and a
  cold/nonreturning tail.  It used only call-graph multiplicity, LowIR
  instruction counts, and existing cold-path classification; it did not
  recognize a source function, symbol, or workload.

  A first broad version also blocked ordinary repeated helpers and failed one
  PA37 compatibility control, so it was narrowed before timing to callees
  whose full instruction count exceeded their cold-discounted cost.  The
  narrowed form passed PA37 187/187 and PA38 46/46.  It retained the six
  continuation calls, shrank `PhysicalCursor::Next` from 2,189 to 1,088 bytes
  and `Lexer::Run` from 13,772 to 8,998 bytes in the diagnostic object, and
  reduced complete compiler text from 8,656,879 to 8,571,980 bytes
  (-84,899).  Thus this was a real compiler-wide density change, not a
  source-specific shape match.

  The explicit O1/all-32 candidate prepared in 17.92 seconds at
  `/dev/shm/v3codex-repeat-cold-candidate.UscUDa/bin/selfhost/cppgm++-self`,
  SHA-256
  `78f0517c6f2900a2472bdac880bcdc601237ab9bf6db7f32a16532bf816910ab`.
  Its valid full-source self lane measured 32.27/873.33/49.54 seconds
  wall/user/system, or 922.87 aggregate CPU seconds.  The exact-source GCC
  compiler under `/dev/shm/v3codex-repeat-cold-gcc-prep.Cn1ViE` completed the
  same workload in 21.12/544.42/44.94 seconds, or 589.36 aggregate CPU
  seconds, and reproduced the candidate hash.  Candidate ratios are therefore
  about 1.528x wall and 1.566x aggregate CPU, both worse than L106's retained
  1.516x/1.562x binding point.  The large text reduction lowers self aggregate
  work only marginally while the added inliner analysis lowers GCC work more;
  the corrected metric rejects it and no Clang lane is warranted.

  The implementation is removed.  No student contract/property is retained
  for rejected behavior, and no compiler, profiler, Cachegrind, or Valgrind
  process remains active.  The historical ratio-sensitive replay queue stays
  closed; subsequent work returns to source-independent, profile-driven
  `Run`/cursor movement rather than re-testing obsolete self-only results.

- **P32-L115 (STABLE-PARAMETER ADDRESS REPLAY REJECTED BY CONTROLLED
  SAME-SOURCE RATIO).** The exact-final profile's next `Lexer::Run` cluster
  kept a named-operator table base in a register under Clang but repeatedly
  materialized and spilled it under self.  The older storage-only address
  deferral could not safely represent that lifetime: simply widening it
  reproduced PA29's `loop-invariant-parameter-across-call` failure by loading
  from the parameter address where the pointer value was required.

  A source-independent probe therefore proved a broader replay-safe use
  class through identity copies and admitted only storage, call-argument,
  pointer-value store, and scalar-return consumers.  Unsupported consumers
  cleared the property through copy chains.  It also treated either an
  original parameter register or a selected stable parameter home as a valid
  constant-index base, materializing the pointer value at copy, call, store,
  and return boundaries.  This repaired the historical failure without
  weakening or shaping its fixture.  PA29 passed 291/291 and PA38 passed
  46/46.

  On `pp_tokenizer.cpp`, final MIR fell 11,079 -> 11,008 lines, object text
  fell 30,266 -> 30,174 bytes, `Lexer::Run` shrank 96 bytes, and its frame
  fell 512 -> 480 bytes.  Self-generation also reduced complete compiler text
  8,659,687 -> 8,571,223 bytes while preserving the exact generated compiler
  SHA-256
  `6ba7ae4c5d89116f925e31143022c8f4b248ae8bdd7c30e3e088bcad1c565f9d`
  across self and GCC lanes.  Thus the mechanism produced real general MIR
  and density improvements; it was not a source-function recognizer.

  The first all-32 self lane measured 32.77/870.11/48.98 seconds
  wall/user/system and had lower CPU occupancy than the adjacent host lane.
  Its clean repeat measured 32.01/868.04/49.33, or 917.37 aggregate CPU
  seconds.  Exact-source GCC lanes measured 21.40/543.73/44.94 and
  21.56/544.84/44.84 seconds, putting individual wall ratios on opposite
  sides of the target and the two-run median at about 1.508x.  Aggregate work
  was only slightly favorable at about 1.558x versus L106's 1.562x.

  A stronger control then compiled the identical candidate source with L106's
  pre-change self compiler.  It measured 32.00/869.64/48.65 seconds, or
  918.29 aggregate CPU, against the candidate repeat's 32.01/917.37.  This
  removes both source-size and GCC-denominator movement from the A/B: the
  substantial density improvement is end-to-end neutral.  The implementation
  is removed and PA38 remains 46/46.  No student contract/property is retained
  for rejected behavior, and no compiler, profiler, Cachegrind, or Valgrind
  process remains active.

- **P32-L116 (CALL-CROSSING LOCAL-PHI REPLAY STILL REJECTED BY
  SAME-SOURCE RATIO).** Fresh exact-final attribution gave old L99 one reason
  for a ratio-aware replay that its static rejection lacked.  The 13-entry
  string-comparison loop accounts for roughly one third of `Lexer::Run`, and
  self carries its local loop state through the frame while leaving two
  callee-saved registers unused.  This corrects the interim statement that
  every historical candidate was already closed: L99 was the sole remaining
  sound item with a now-proved hot native target and no source-matched host
  denominator.

  The original general boundary was reproduced: a bypassable local integer
  phi that crosses a call, has at least three uses, and cannot use a
  caller-saved register received an exact-interval callee-saved home only
  when MIR liveness and clobber facts allowed it.  On `Lexer::Run` this
  changed the preserve set from three to five registers, removed three MIR
  instructions and one load/store pair, but grew the function 281 bytes and
  tokenizer text 288 bytes.  Complete compiler text grew 8,656,879 ->
  8,657,691 bytes.  PA29 passed 291/291 and PA38 passed 46/46.

  The explicit O1/all-32 candidate prepared under
  `/dev/shm/v3codex-local-phi-candidate.3wsLZF`; its pre-change-compiler
  binary has SHA-256
  `9d59edd4c6f3078f873b7924a73fe25337aae634e6f6638cdbeedc7426ec74a4`.
  The full-source self lane measured 32.28/877.12/49.75 seconds
  wall/user/system.  Its exact-source GCC compiler under
  `/dev/shm/v3codex-local-phi-gcc-prep.X9iVX4` measured the same workload at
  21.00/545.67/44.86 seconds.  Both generated the identical compiler SHA-256
  `b85d21dfa49a1274cac0d365653ab0cdc7591cb467a9847975b8bf31bbf6a705`.

  The resulting 1.537x wall and 1.570x aggregate-CPU ratios both regress
  L106's 1.516x/1.562x point.  New attribution justified measuring L99, but
  the corrected metric still rejects it.  The implementation is removed,
  restored PA38 is 46/46, and no student contract/property is retained for
  rejected behavior.  No compiler, profiler, Cachegrind, Valgrind, or build
  process remains active.  The historical ratio-sensitive queue is now
  closed including every sound item with a material current native effect;
  further work is fresh profile-driven optimization.

- **P32-L117 (WEAKLY ALIGNED 64-BYTE DIRECT COPY STILL REJECTED BY
  SAME-SOURCE RATIO).** The exact-final macro `Token` move constructor owns
  about 34 aggregate profile seconds under self and ends in a 61-byte weakly
  aligned `rep movsb`.  This is the precise population exercised by L44/L48's
  old weak-copy expansion, which had only self-side native timing.  Because
  L106's dynamic-copy interference tests did not alter this fixed tail, the
  one-line upper-bound policy received its missing candidate-source GCC lane.

  Admitting every fixed copy through 64 bytes to the existing reserved-XMM
  vector chunks removed the constructor's string operation.  The constructor
  grew 215 -> 258 bytes and `macro_processor.o` text grew 195,395 -> 196,187
  bytes, while complete compiler text changed only 8,656,879 -> 8,656,751
  bytes after distributed offsets.  The O1/all-32 pre-change-compiler
  candidate at `/dev/shm/v3codex-weak64-candidate.ulI3XF` has SHA-256
  `c53e5dd3f4f974e61a471bbf84e3f1990b285ea0723a624b56d71d6f1f5e3e80`.

  Its full-source self lane measured 32.07/876.11/49.78 seconds
  wall/user/system.  The exact-source GCC compiler under
  `/dev/shm/v3codex-weak64-gcc-prep.2q5wYs` measured
  21.01/547.88/44.97 seconds.  Both generated compiler SHA-256
  `07f26564c25918ba3b24ae5709989ddee09353a3f5e010b82cd48fdab6f8d05a`.
  The resulting ratios are about 1.526x wall and 1.5626x aggregate CPU,
  neither better than L106's 1.516x/1.5621x point.

  The implementation is removed; the existing student-visible weak-alignment
  compact fallback is unchanged, so no contract or property edit is needed.
  Restored PA38 is 46/46, and no compiler, profiler, Cachegrind, Valgrind, or
  build process remains active.  Together with L116 this supplies the last
  missing denominators for old probes that have a credible current hot native
  population.  The historical replay queue is closed; further candidates
  must come from a new source-independent mechanism or new attribution.

- **P32-L118 (BOUNDED SMALL DYNAMIC-COPY ENCODING REJECTED BY
  SAME-SOURCE RATIO).** With the historical queue closed, a fresh generic
  PA29 probe targeted the two hot `rep movsb` offsets in the macro `Token`
  move constructor.  A flag-dead `copy_bytes_dynamic` with runtime count at
  most sixteen copied exact first/last 1-, 2-, 4-, or 8-byte chunks; zero
  copied nothing, larger counts retained `rep movsb`, and a copy across live
  condition flags also retained the flag-preserving string form.  The proof
  used only the existing nonoverlapping `memcpy` contract and the MIR's
  declared `rdi`/`rsi`/`rdx` inputs plus `rcx` clobber.  It recognized no
  source function, symbol, string contents, or workload.

  The broad form changed 871 compiler sites, grew the hot constructor
  215 -> 311 bytes, and grew complete compiler text
  8,656,879 -> 8,741,303 bytes (+84,424).  Its O1/all-32 compiler at
  `/dev/shm/v3codex-small-dyncopy-candidate.5BbOOU` and every accepted
  full-source lane produced SHA-256
  `aa17eae46b7010282c9303a8d2d2df71bd6c223d713e5f964441613ad6d014e6`.
  Two clean self lanes measured 31.70/864.65/48.92 and
  31.79/863.79/49.28 seconds wall/user/system.  Two clean exact-source GCC
  lanes measured 20.89/541.72/44.48 and 20.86/542.83/44.39 seconds; their
  host compiler was prepared under
  `/dev/shm/v3codex-small-dyncopy-gcc-prep.iddLgm`.  Additional 22.21 and
  22.24 second GCC lanes had scheduler tails, with the latter also carrying
  elevated aggregate work, and are not used to make a favorable wall claim.

  The clean means are 31.745/913.320 self and 20.875/586.710 GCC wall/CPU,
  or 1.521x wall and 1.557x aggregate CPU.  Relative to L106's
  1.516x/1.562x point, CPU ratio improves only about 0.35% while binding wall
  ratio regresses about 0.3%.  An identical-candidate-source control using
  the pre-change L106 self compiler measured 32.37/917.90 wall/CPU versus the
  candidate's first 31.70/913.57, confirming a real but only 0.47% generated-
  work saving before the faster GCC denominator is applied.  The corrected
  metric therefore rejects the broad policy and no Clang lane is warranted.

  A loaded-length-plus-one restriction reproduced L112's 470-site
  population and kept the hot constructor while reducing text growth to
  49,092 bytes.  Its full-source lane at
  `/dev/shm/v3codex-loaded-dyncopy-v2.hD6pAl` measured 32.15 seconds wall and
  914.97 aggregate CPU, worse than the broad form; the excluded 401 sites
  supplied real distributed benefit, so that narrowing is also removed.
  Candidate PA29 passed 291/291.  After removal PA29 passes 291/291, PA38
  passes 46/46, the worktree again contains no production or fixture change,
  and no compiler, profiler, Cachegrind, Valgrind, or build process remains
  active.  No student contract/property is retained for either rejected
  encoding.  Historical replays remain closed; the next step requires a new
  profile-driven mechanism rather than another denominator-only rescue.

- **P32-L119 (LIFETIME-BOUNDED O1 MEMORY GVN RETAINED UNDER THE
  MAX-HOST WALL METRIC).** The L109 replay was reopened after correcting the
  decision rule: progress is the maximum same-source self/GCC and self/Clang
  wall ratio, not a requirement that every individual host ratio improve
  relative to L106.  Ordinary O1 definitions now run the existing conservative
  cross-block memory-GVN pass only when replacing a redundant load cannot
  extend the dominating value's original presentation-order lifetime.
  Explicitly hinted definitions keep their already documented broader O1
  rule; stores, writing calls, atomics, exceptional barriers, volatility, and
  typed/address alias guards are unchanged.

  The reconstructed rule emitted a byte-identical `pp_tokenizer.o` to the
  archived L109 compiler and byte-identical optimized LowIR for the focused
  reducer.  Relative to L106, the final compiler text falls
  8,656,879 -> 8,652,443 bytes and aggregate shared-object text falls
  9,321,878 -> 9,310,868 bytes.  The O1/all-32 compiler under
  `/dev/shm/v3codex-l109-candidate.pPgh9C` has SHA-256
  `153f48841b6870e07adeb8fd0726657017932c5ba671febd039efe0175bb0289`;
  formatting-only audit compaction reproduced that binary byte for byte.

  Three clean all-32 self lanes measured 32.01/871.02/49.65,
  32.26/870.11/49.39, and 31.74/868.39/48.89 seconds wall/user/system.
  Three exact-source GCC-built-compiler lanes measured 21.68/545.02/44.91,
  22.20/550.82/45.57, and 21.22/543.65/44.20; three Clang-built-compiler
  lanes measured 21.61/562.13/44.76, 21.52/560.54/44.92, and
  21.93/562.21/44.52.  Median wall/aggregate-CPU values are therefore
  32.01/919.50 self, 21.68/589.93 GCC, and 21.61/606.73 Clang.  The honest
  final ratios are **1.476x GCC and 1.481x Clang by wall**, and 1.559x and
  1.516x by aggregate CPU.  The maximum wall ratio is below 1.50x with about
  1.25% relative margin.

  An identical-candidate-source control using L106's retained self and GCC
  compilers measured 32.39/873.67/49.90 and 22.44/551.66/45.78.  It confirms
  both that the new generated compiler is absolutely faster and that natural
  implementation work moves the host denominator.  The latter is part of the
  clarified same-source metric for this genuine, documented optimization; the
  control is not used to erase that denominator effect.

  PA37 now describes the lifetime boundary.  Control
  `526-late-inline-hint-load-reuse` checks an O0 two-load baseline, an ordinary
  lifetime-bounded positive, an ordinary lifetime-extending negative, the
  existing hinted case, memory barriers, and generated behavior without
  matching a complete LowIR module.  Its control bucket now propagates an
  early script failure instead of allowing a later passing script to mask it.
  Focused PA37 passes 187/187, PA38 passes 46/46, and the through-PA38 report
  passes 5,453/5,453.  The PA38 file audit passes with 36 established warnings.
  Fresh O1 inception with outer, inner, and object parallelism all at 32
  matched every object and the final compiler at the candidate hash in
  49.99/1330.91/94.81 seconds wall/user/system.  Implementation commit is
  `764e12a3`.  No Cachegrind, Valgrind, profiler, compiler, or build process
  remains active.  L115 remains a valid fallback only if a future
  source-matched checkpoint loses the margin; it is not added after the exit
  criterion has already been reached.

- **P32-L120 (FINAL CHECKPOINT PUSH).** Implementation commit `764e12a3` and
  evidence/metric commit `0f6f8ece` were pushed together to `origin/v3opt`
  (`c6824b82..0f6f8ece`).  The immediately preceding 5,453/5,453 through gate,
  36-warning/zero-fatal audit, and byte-identical 32-way O1 inception are the
  final verification matrix; this documentation-only closure does not rerun
  those completed gates.  The working branch was clean and synchronized after
  the push, and the temporary detached production-source worktree used only
  for the timing control was removed.

- **P32-L121 (POST-CONTRACT PREDICATE INTERFERENCE RETAINED; GAP STILL
  OPEN).** After the LowIR-contract program, fresh exact-source software
  profiles put the remaining excess back in generated frontend code:
  `Lexer::Peek`, `Lexer::Run`, and `PhysicalCursor::Next`, while the LowIR
  simplifier itself remained below one percent.  `Peek` had regained an
  unsigned `size - 1 >= size` bounds branch and its cold exception body even
  though the preceding edge proved that exact SSA value nonzero.  The original
  P31 proof recognized only a second load in the comparison block.  Final O1
  load reuse and separately inlined queue accessors had changed the value into
  a dominating load, so two individually retained transformations interfered
  and disabled the proof.

  The retained repair recognizes the same unchanged SSA value across a bounded
  unique-predecessor chain, preserves the older stable-reload proof, and reruns
  predicate cleanup only for callers changed by an inlining wave after final
  O1 load reuse.  Distinct values, volatile reloads, and writing-call barriers
  remain guarded.  This is a generic LowIR property; it does not recognize the
  tokenizer or any program text.  `Lexer::Peek` shrinks 421 -> 323 bytes and
  loses the hot comparison/branch plus its first cold throw block.  Complete
  self compiler text grows only 584 bytes for the proof machinery.

  A reverse-order same-source compiler ablation measured old-self lanes at
  32.07/867.94/48.22 and 32.34/868.38/48.85 seconds wall/user/system versus
  candidate lanes at 32.44/858.27/49.34 and 31.77/855.51/48.14.  Mean wall is
  32.205 -> 32.105 (-0.31%) and aggregate CPU is 916.695 -> 905.630 (-1.21%).
  Exact-candidate-source GCC lanes measured 21.05/542.20/44.33 and
  20.91/541.95/44.20; Clang lanes measured 21.85/563.09/44.78 and
  22.35/564.56/45.12.  Candidate mean ratios are therefore **1.530x GCC** and
  **1.453x Clang** by wall, and **1.544x GCC** and **1.488x Clang** by
  aggregate CPU.  GCC remains binding and needs roughly another two percent
  self wall reduction for the 1.50x exit.

  PA37's README now describes same-value dominance and stable-reload safety.
  Control `528-nonzero-underflow-value-proof` checks the O0 comparison, its O1
  removal through an intervening block, and a distinct-value negative without
  matching a complete LowIR program.  PA37 passes 188/188 and PA38 passes
  45/45.  Candidate compiler SHA-256 is
  `43df77f52dd35aba8d1c7041abbfc62befdae5d9de02d486ccbfac414566b0ab`.
  The next target is the remaining redundant queue-size reload on `Peek`'s
  backedge: Clang carries the just-stored size to the header comparison, while
  self reloads it immediately.  Test this as generic loop-carried exact-store
  forwarding, not as a source rewrite or field-specific rule.

- **P32-L122 (HINTED LOOP-CARRIED EXACT-STORE FORWARDING RETAINED;
  WALL TARGET RESTORED).** The remaining `Lexer::Peek` header reload was the
  predicted generic loop case: every ordinary backedge stored a locally
  computed value to the exact nonvolatile scalar location and then jumped
  directly to the header, yet the header loaded that location again.  O1 now
  moves the initial load to the unique ordinary preheader and replaces the
  header load with a typed `phi` whose backedge inputs are the exact stored
  values.  The bounded rule rejects exceptional loops, volatile or mismatched
  addresses/types, missing or conditional stores, values not defined in the
  latch, and any instruction after the store other than the header jump.  It
  performs at most one rewrite per definition.

  A broad implementation was a useful destructive-interference control.  Even
  after a cheap latch-shape preflight, it added loop analysis to ordinary
  definitions, grew compiler text by about 21 KiB, and changed reverse-order
  same-source wall from a 31.885-second baseline mean to a 32.435-second
  candidate mean (+1.73%) while aggregate CPU was flat (908.295 -> 907.690
  seconds).  That policy is rejected.  The retained rule is limited to the
  existing student-visible `inline_hint=yes` contract.  It fires twice in
  `pp_tokenizer.cpp`, removes the target reload, shrinks `Lexer::Peek`
  323 -> 315 bytes, and grows complete compiler text only
  8,621,687 -> 8,640,083 bytes for the implementation.

  Reverse-order identical-source lanes using the L121 compiler measured
  32.40/858.13/49.29 and 31.61/855.94/48.94 seconds wall/user/system; retained
  candidate lanes measured 31.44/852.74/49.09 and 32.35/853.08/48.71.  Mean
  self wall/aggregate CPU therefore improves 32.005/906.150 ->
  31.895/901.810 seconds.  Exact-candidate-source GCC lanes measured
  21.17/544.37/44.91 and 21.75/545.54/44.51; Clang lanes measured
  22.53/565.47/44.10 and 21.63/564.04/43.85.  The resulting mean ratios are
  **1.486x GCC** and **1.445x Clang** by wall, and **1.529x GCC** and
  **1.481x Clang** by aggregate CPU.  The primary maximum same-source wall
  ratio is again below 1.50x.

  The all-32 candidate compiler under
  `/dev/shm/v3codex-loop-hint.ceoADi` and every retained timing lane produced
  SHA-256
  `8b6952075632e689e99e8a7f502e1f3c00b9cfa6d280e07e0e33c3653b69bbd4`.
  The exact-source GCC and Clang host preparations are respectively
  `/dev/shm/v3codex-loop-hint-gcc-prep.mx92EW` and
  `/dev/shm/v3codex-loop-hint-clang-prep.Q5SiyN`; timed workloads kept
  `CPPGM_HOST_CXX=g++` and outer, inner, and object parallelism at 32.
  PA37's README describes the constrained transformation.  Control
  `529-loop-carried-store-forwarding` checks the O0 load baseline, O1
  load-to-phi structure, ordinary-definition policy, address and post-store
  barriers, and executable result without matching a complete program.
  Focused PA37 passes 188/188 and PA38 passes 45/45.  The cumulative report,
  file audit, byte-identity inception gate, commit, and push follow as the
  checkpoint for L121-L122.

- **P32-L123 (POST-INTERFERENCE CUMULATIVE CHECKPOINT).** Predicate repair
  commit `042a8ead`, loop-carried forwarding commit `92382f8e`, and the
  token-identical optimizer-orchestration compaction in `486390e6` pass the
  post-compaction PA37 188/188 and PA38 45/45 focused suites and root
  through-PA38 at 5,465/5,465.  The PA38 development-file audit has zero fatal
  findings and the established 36 advisories; `lowir_opt.cpp` is 2,995 lines.

  Fresh isolated O1 inception under
  `/dev/shm/v3codex-final-inception.BWEjjn`, with outer, inner, and object
  parallelism all at 32, prepared self in 17.90/453.13/43.29 seconds and
  completed the inception/object comparison in 33.24/862.66/50.43 seconds
  wall/user/system.  Every object and the final compiler match.  Both compiler
  stages retain SHA-256
  `8b6952075632e689e99e8a7f502e1f3c00b9cfa6d280e07e0e33c3653b69bbd4`,
  proving that the audit-only token-preserving compaction did not change the
  measured binary.  No profiler, Cachegrind, Valgrind, compiler, or build
  process remains active.  L122's 1.486x maximum same-source wall ratio is the
  final performance result for this batch.

Append one entry for every census, probe, landing, rejection, and re-baseline.
Each entry records the source tree, self and host binaries, output hash,
correctness matrix, native protocol, exact Ir when run, affected movement/text,
and profiler/build cleanup state.

# Plan: O0 Code-Shape Parity, Second Pass

Status: complete

Date: 2026-08-20

## Objective

Continue the `-O0` code-shape work after `PLAN-O0-CLANG-PARITY.md` without
turning baseline compilation into an undocumented optimization pipeline.  The
second pass targets the two remaining large gaps demonstrated by the frozen
`semantic_overload.cpp` compile:

1. compiler-created value, address, and object-placement traffic; and
2. repeated exception-boundary, cleanup, and resume machinery.

The implementation must continue to use the typed production path:

```text
source semantics -> typed LowIR -> typed MIR -> native ELF
```

Every retained baseline change must be mandatory lowering, target selection,
or host-ABI legalization that a reasonable `-O0` compiler performs.  General
source-independent optimization remains in PA37 `-O1`/`-O2`, and general MIR
cleanup/layout remains in PA38 `-O1`/`-O2`.

The compile-time constraints from `spec.md` remain binding.  Hot-path work must
be linear or near-linear, use dense typed IDs and fixed-size target state, and
avoid string-keyed identity, rendered-operand parsing, repeated whole-function
rescans, fixed-point iteration, or a second hidden IR.  PGO is out of scope.

The frozen compile must remain below 15 seconds under a fair load window and
must retain its `-O0` compile-time lead over GCC.  Because the host is
intermittently loaded, performance decisions use deterministic output metrics
plus sequential, interleaved A/B/B/A blocks; an isolated wall-time observation
is only a screen.

## Relationship to the completed first pass

`PLAN-O0-CLANG-PARITY.md` is complete through commit `d9e97848`.  It remains
the audit record for bit-field-unit construction, readonly constant templates,
immediate and memory MIR operands, shared epilogues, the typed cleanup DAG,
source-owned zero initialization, weak-demand analysis, shared ELF strings,
and dual-entry host landing legalization.

This plan does not reopen those phases.  It starts from their final object and
uses the same earliest-owner test policy.  If a second-pass prototype conflicts
with a first-pass invariant, stop and explain the conflict rather than hiding
the change below MIR or weakening a fixture.

## Reproducible baseline

The source is:

```text
~/cppgm-extended-pa39-source-layout/benchmarks/self_compile/stable/semantic_overload.cpp
```

The comparison uses GCC 15 libstdc++ headers for every compiler:

```sh
INC=~/cppgm-extended-pa39-source-layout/dev/src
SRC=~/cppgm-extended-pa39-source-layout/benchmarks/self_compile/stable/semantic_overload.cpp

dev/cppgm++ -std=gnu++11 -O0 -I "$INC" -c "$SRC" -o cppgm.o
g++          -std=gnu++11 -O0 -I "$INC" -c "$SRC" -o gcc.o
clang++      -std=gnu++11 -O0 -stdlib=libstdc++ \
  -I "$INC" -c "$SRC" -o clang.o
```

The current objects are deterministic at:

| Compiler | Object bytes | SHA-256 |
| --- | ---: | --- |
| cppgm++ `d9e97848` | 3,246,896 | `7e5c4439998ec30af49f047f80a702f266df9ed9abcd0ff242176dd9be910a98` |
| GCC 15.2 `-O0` | 3,196,024 | `e7b6bd8d5d6fc88c52b751a346568da2f82d651b922a4ed88679e80d7d5c9b60` |
| Clang 21.1.8 `-O0`, libstdc++ | 2,477,128 | `ce641b7f4071208928869b95b7a251080c9dfe0845cbdcd5b00a2c5dcf68a212` |

Ubuntu GCC enables CET entry instructions by default in this environment.  It
emits 4,990 `endbr64` instructions occupying 19,960 text bytes.  A diagnostic
`-fcf-protection=none` build is 3,175,816 bytes with 579,715 `.text*` bytes and
150,011 decoded instructions.  Use that build when comparing instruction
efficiency, while retaining the ordinary invocation above as the host-default
object comparison.

### Current whole-object gap

| Metric | cppgm++ | GCC `-O0` | Clang `-O0` |
| --- | ---: | ---: | ---: |
| ELF object bytes | 3,246,896 | 3,196,024 | 2,477,128 |
| all `.text*` bytes | 738,654 | 599,783 | 523,269 |
| base `.text` bytes | 428,192 | 309,745 | 308,743 |
| COMDAT `.text*` bytes | 310,462 | 290,038 | 214,526 |
| `.gcc_except_table` bytes | 42,376 | 16,263 | 19,992 |
| `.eh_frame` bytes | 137,264 | 128,488 | 115,024 |
| relocation bytes | 646,464 | 523,032 | 470,328 |
| relocations | 26,936 | 21,793 | 19,597 |
| decoded instructions | 185,725 | 155,002 | 124,218 |
| defined function symbols | 5,530 | 4,980 | 3,476 |
| weak defined function symbols | 4,491 | 4,237 | 2,897 |

The total object is only 1.6% larger than host-default GCC because cppgm++ now
shares its ELF symbol and section-name storage.  That file-size result must not
hide the loaded-code result: cppgm++ still has 138,871 more text bytes than
host-default GCC, 158,939 more than GCC with CET disabled, and 215,385 more
than Clang.

The base-text gap is almost identical against GCC and Clang: 118,447 and
119,449 bytes.  Against GCC, COMDAT text differs by only 20,424 bytes.  The
primary GCC gap is therefore source/local function lowering, not another
large weak-body or string-table mistake.

### Movement and frame traffic

Decoded `mov*` byte attribution is:

| Operand class | cppgm++ | GCC | Clang |
| --- | ---: | ---: | ---: |
| memory to register | 150,456 | 125,731 | 126,516 |
| register to memory | 118,937 | 61,550 | 107,708 |
| register to register | 108,373 | 99,449 | 25,564 |
| immediate to register | 14,840 | 17,455 | 4,826 |
| immediate to memory | 13,664 | 7,708 | 13,796 |
| total `mov*` bytes | 406,270 | 311,893 | 278,410 |

Together with `lea`, cppgm++ has 108,433 gross excess movement/address bytes
against GCC and 148,656 against Clang.  These differences are not all
independently removable because the compilers retain different function sets,
but they identify the dominant instruction family.

`semantic_overload::analyze_call_expression` is the largest single example:

| Metric | cppgm++ | GCC | Clang |
| --- | ---: | ---: | ---: |
| function bytes | 68,504 | 38,411 | 40,947 |
| `mov*` bytes | 37,945 | 15,062 | 17,786 |

Extra movement accounts for 22,883 of its 30,093-byte GCC gap and 20,159 of
its 27,557-byte Clang gap.  The ten largest exactly matched positive function
deltas total 62,353 bytes against GCC and 60,502 against Clang, so the gap is
partly concentrated but not one isolated function bug.

Native telemetry reports only 172 allocator spills.  The movement gap is
therefore not primarily emergency spilling.  It includes compiler-created
temporary homes, early stores to planned homes, reloads around calls and EH
edges, direct-construction misses, repeated width normalization, and register
copies selected before their final consumer is known.

### Exception and cleanup traffic

| Operation | cppgm++ | GCC | Clang |
| --- | ---: | ---: | ---: |
| `_Unwind_Resume` calls | 821 | 467 | 292 |
| functions containing resume | 568 | 342 | 292 |
| resume calls beyond one per such function | 253 | 125 | 0 |
| `__cxa_begin_catch` calls | 482 | 43 | 48 |
| terminate-helper calls | 400 | 0 | 207 |
| string destructor calls | 1,585 | 1,160 | 1,132 |
| `shared_ptr<Type>` destructor calls | 747 | 426 | 410 |
| `ExprInfo` destructor calls | 646 | 370 | 373 |

The current terminate path calls `__cxa_begin_catch` at every terminate landing
and then calls one shared `@cppgm_call_terminate` helper.  Clang instead puts
`__cxa_begin_catch` inside its shared helper.  The frozen object also gives
terminate handling to simple nonthrowing library functions such as accessors
and iterators for which Clang emits no corresponding boundary.  This requires
two separate fixes: a truthful nonthrowing-fact audit and a compact legitimate
terminate path.

The first-pass cleanup DAG already avoided 11,631 destructor actions and 1,010
resume operations.  The residual counts are not evidence that its context keys
should simply be weakened.  Any additional sharing must first classify the
semantic distinction blocking each merge and preserve active try/handler,
handler-exit, object-lifetime, and landing-entry behavior.

### Live function bodies and inlining

The first-pass demand audit proved that almost every retained weak body is the
target of a live relocation.  Deleting those bodies is not valid demand
pruning.  The current source force-inliner is active: it sees 625 candidates
and expands 1,438 calls in the frozen compile.

The remaining function-count gap is small against GCC and large against Clang.
Clang performs more tiny inline/template simplification at `-O0`; generic
replication of that policy would be an optimization-level change, not a native
encoding fix.  This plan audits missed explicit `always_inline` facts at `-O0`
but leaves ordinary small-call inlining in PA37 `-O1` and later.

### Post-S3 residual opportunity register

The initial baseline above remains the stable comparison point for the plan,
but the instruction-family priorities must be based on the object after the
accepted S1-S3 and S5a changes.  That object is 3,221,784 bytes with 733,065
`.text*` bytes and SHA-256
`05d07eadb7398e0b91a12c258ec7287db1c0c2bad9264255d491e99ee659f54b`.
Against the same GCC build with CET disabled and the same-libstdc++ Clang
build, its gross positive byte differences are:

| Residual family | Versus GCC | Versus Clang | Disposition |
| --- | ---: | ---: | --- |
| `mov*` plus `lea` | 93,583 | 136,077 | S5 direct destination placement, then S6 bounded scalar retention |
| unconditional `jmp` | 25,501 | 12,615 | S4 semantic cleanup/continuation audit first; ordinary block layout remains PA38 |
| `movzbl` plus `movslq` | 12,649 | 16,287 | S7 typed width-state retention |
| `push` plus `pop` | 11,951 | 12,495 | S8 bounded register-cost selection |
| `call` | 10,549 | 21,133 | S4 cleanup calls and S9 truthful demand/force-inline audit; ordinary inlining remains PA37 |
| integer compare/test/set-result family | 2,139 | 8,735 | S7 direct typed predicate consumption after width work |
| multiply/divide/shift family | 3,676 | 4,627 | S7 target-selection audit, one constant/operator class at a time |

These are attribution bounds, not additive savings targets.  The compared
objects retain different function sets and make different source-level
inlining decisions.  S3 also intentionally traded repeated resume calls for
local branches, so the branch and call rows must be remeasured together after
S4.  A phase is justified by a reduced same-function cause, not by matching a
host compiler's aggregate mnemonic count.

No separate third O0 plan is needed for these families.  The first five map to
existing S4-S9 work.  S7 explicitly owns the two smaller target-selection
families so they receive a decision and earliest-owner test instead of being
silently left behind.  If a reducer requires cross-block value propagation,
general dead-code elimination, ordinary inlining, or trace layout, record it
as PA37/PA38 work rather than extending baseline compilation.

## Assignment and public-boundary ownership

| Work | Public boundary | Earliest tests | Student-facing effect |
| --- | --- | --- | --- |
| Explicit function/member `noexcept` and truthful `unwind=no` | semantic fact and LowIR metadata | PA16 course LowIR | PA16 already requires truthful direct-`noexcept` metadata; strengthen the current requirement only if the reducer exposes a missing case |
| Derived special-member nonthrowing facts | semantic fact and LowIR metadata | PA17 course LowIR | PA17 requirement/Design Notes only for a real derived-special-member gap |
| Template specialization/member-template nonthrowing propagation | semantic fact and LowIR metadata | PA19, PA22, or the first later template owner demonstrated by the reducer | edit only that owning README; do not describe frozen-header history |
| Shared terminate action | host-object exception-policy LowIR CFG | PA31 host-EH behavior/inspect | PA31 states the current helper/ABI requirement; compact implementation advice belongs in `Design Notes` |
| One physical resume terminal per compatible function | host object layout below MIR | PA31 course behavior/inspect | no LowIR/MIR contract change; at most a PA31 `Design Notes` suggestion |
| Additional context-safe cleanup-tail sharing | source-generated LowIR CFG | PA16 lexical, PA17 temporary/value, PA26 handler/unwind, plus PA31 behavior | existing sharing requirements move only when their owning fixture moves |
| Source-owned direct object construction | LowIR, only if the frontend currently creates a redundant semantic temporary | PA16 or PA17, or the later feature owner proven by the reducer | normative current LowIR behavior plus a compact destination-planning suggestion in `Design Notes` |
| Backend-created temporary/home elimination | MIR placement | PA29 strict/structural/behavior | PA29 MIR rules and scaffold comments only when the serialized operand/location contract changes |
| Known-width normalization placement | MIR placement and x86 selection | PA29 strict/structural/behavior | PA29 narrow-value rule; no hidden encoder-only value graph |
| Direct predicate consumption and fixed-constant arithmetic selection | MIR selection when the instruction/dataflow shape changes; native encoding only for a one-instruction equivalent already permitted by MIR | PA29 structural/behavior; PA38 only for cross-instruction optimized rewrites | document only the current accepted PA29 opcode/operand rule; optimization-only rewrites remain PA38 |
| Cost-aware callee-save selection | MIR register/preserve list | PA29 structural/behavior | PA29 already exposes physical registers and the final preserve list |
| Ordinary tiny-function inlining | optimized LowIR | PA37 `o1` and driver `o1`; PA38 only for downstream MIR effects | deferred from `-O0` |
| Trace block layout and post-optimization preserve cleanup | optimized MIR | PA38 `o2` and behavior/debuginfo | already a PA38 `-O2` requirement; deferred from `-O0` |

Student-facing main sections state only current behavior students must
implement to pass the tests.  Complexity advice and efficient implementation
shapes go under `Design Notes (Non-Normative)`.  Benchmark history, old output,
fixture migration, commit IDs, and maintainer audit policy stay in this plan.

## Optimization-level boundary

The following are valid at `-O0` when represented at the correct public layer:

- preserving an explicit or correctly derived nonthrowing source fact;
- omitting EH state for a call proven not to unwind;
- using one ABI-correct terminate helper instead of repeating its body;
- using one physical resume terminal when every path supplies the same current
  exception slot and no semantic cleanup is removed;
- constructing a source object directly in the destination already selected by
  source semantics;
- retaining compiler-created values in registers until an actual call, fixed
  register clobber, address escape, lifetime boundary, or CFG constraint
  requires a home;
- choosing a legal memory/immediate operand or final ABI register directly;
- avoiding a repeated sign/zero extension when typed MIR already proves the
  register contains the required normalized value; and
- choosing caller- versus callee-saved registers using a bounded target cost.

The following do not belong in baseline compilation:

- general source-slot forwarding or promotion;
- arbitrary dead-store or dead-code elimination;
- fixed-point copy propagation;
- general-purpose inlining without an explicit force-inline requirement;
- trace layout or whole-function CFG reordering; and
- post-hoc global register allocation over serialized MIR.

Those behaviors remain in PA37/PA38 at `-O1` or `-O2`.  An `-O0` prototype
that needs one of them is deferred rather than hidden below MIR.

## Test and reference protocol

Every failure receives a minimal reducer at the earliest assignment that owns
the failing behavior.  Completely new tests go directly under
`cppgm.tests/course/paN/`; there is no `proposed/` lane.

Use the documented `ref-test` target.  `test-ref`/`ref-test` may receive an
explicit clean local reference binary through `REF_TEST_APP`; the pinned
reference bundle changes only when `cppgm-extended` is officially updated and
exported.  Do not copy output from the implementation under test into a ref.

For PA29 behavior tests, retain the reference MIR as an informational example
even though behavior—not exact MIR—is the grading oracle.  Use strict or
structural coverage when the placement rule should be visible to students;
use behavior coverage when multiple correct register layouts are intended.

For each phase:

1. run the focused reducer;
2. run one keep-going owning-PA report, for example:

   ```sh
   make test-report ACTIVE_TEST_REPORT_PAS='pa16 pa17 pa26 pa29 pa31'
   ```

3. run `make test-paN` and `make test-report-through-paN` for the earliest
   owner;
4. run the full root `make test-report` before retaining the phase;
5. run `perl scripts/cppgm_file_audit.pl --stage pa39 --paths dev/src` and
   require zero fatal findings;
6. regenerate deterministic frozen objects and the complete code-shape report;
7. run sequential interleaved A/B/B/A timing blocks against an immutable
   baseline; and
8. commit and push the isolated accepted changeset before starting the next
   phase.

`make test` is not a substitute for `make test-report`: it stops at the first
failure.  Do not run two timed compilers, test reports, self builds, or
inception builds concurrently.

## Implementation sequence

### S0: make the remaining causes observable

Extend `--stats`-only telemetry before changing output.  Use dense enums and
counters that disappear from the normal path; do not retain names or construct
diagnostic strings unless `--stats` is active.

Classify:

- terminate boundaries by explicit noexcept, derived special member, template
  specialization, builtin/runtime fact, or conservative fallback;
- potentially throwing calls inside those functions by the exact typed reason
  they lack `unwind=no`;
- `mov*` and `lea` selection by parameter home, source slot,
  compiler-created scalar temporary, object temporary, call setup/result,
  cleanup/EH, width normalization, address materialization, or encoder
  fallback;
- temporary homes created versus reused and the first event requiring each
  home;
- repeated resume terminals and the semantic context distinction that kept
  them separate; and
- cleanup-state misses by action, guard, handler stack, handler-exit,
  cleanup-region-exit, or terminal-continuation mismatch.

Add a durable comparison report for instruction bytes by operand class,
function body, runtime call target, binding, and EH section.  The reporting
script may use strings because it is out-of-band analysis; production lowering
may not.

Reduce at least one simple false terminate boundary and one high-movement
source expression before selecting an implementation change.  The reducer,
not the frozen file name, determines assignment ownership.

Complexity: no output-path cost without `--stats`; one constant-time counter
update at an event already visited when stats are active.

#### S0 completion record

The production counters are typed dense enums.  Native movement is attributed
to parameter homes, source slots, scalar temporaries, object temporaries, call
boundaries, cleanup/EH, width normalization, address materialization, or an
encoder fallback.  Temporary-home requests separately record scalar/object
values, call crossing, CFG-edge liveness, register pressure, address escape,
call result, and extended representation.  The normal path only tests the
existing stats pointer; all classification and counter updates remain behind
that test.

On the frozen compile, S0 records 104,605 movement instructions:

| Cause | Instructions |
| --- | ---: |
| parameter home | 1,537 |
| source slot | 36,836 |
| scalar temporary | 18,759 |
| object temporary | 1,206 |
| call boundary | 32,976 |
| cleanup/EH | 17 |
| width normalization | 1,918 |
| address materialization | 11,356 |
| encoder fallback | 0 |

There are 6,540 typed temporary-home requests: 1,338 create a home and 5,202
reuse one.  The largest reasons are scalar values (3,990 requests), CFG-edge
liveness (1,765), call crossing (352), call results (194), and register
pressure (172).  This confirms that emergency spilling is not the dominant
cause and gives S5-S8 a stable cause census.

The source boundary census finds 327 terminate boundaries: 291 explicit and
36 template-specialization boundaries, with no derived, builtin, or
`unexpected` boundary in this translation unit.  The stats-only whole-root
scan also attributes potentially throwing operations to 9,151 ordinary calls,
7,191 special-member calls, 1,935 template calls, 62 builtin/runtime calls,
seven indirect calls, and 227 explicit throw/typeid/dynamic-cast operations.

The durable ELF report now records encoded bytes and operand classes, largest
and selected function bodies, call targets and callers, binding, relocations,
and EH sections.  It uses `objdump -drw` so wrapped instruction bytes cannot be
mistaken for instructions; this script is deliberately out-of-band and may use
text identity.

The first high-movement reducer is an empty allocator-like base initialized
from a reference-returning move helper.  In the reduced case, PA17 currently
creates an `obj<24x8>` `refcall` temporary for the complete derived object while
binding a `const` reference to its empty base.  The attempted raw copy can even
fail with `invalid PA17 copyobj span`.  The frozen `std::vector<Item>` form
survives only because the empty-base path elides enough work; it still emits a
24-byte temporary, cleanup, and terminate machinery which Clang does not emit.
This is a source/value-boundary defect, not a native peephole.  Its LowIR fix
is therefore promoted to the start of S5 and receives a PA17 reducer before
the later PA29 movement work.  S1 retains a separate audit of genuinely lost
nonthrowing facts so the downstream false EH from this materialization is not
misclassified as a `noexcept` propagation failure.

The no-stats frozen object remains byte-identical to the immutable baseline:
3,246,896 bytes with SHA-256
`7e5c4439998ec30af49f047f80a702f266df9ed9abcd0ff242176dd9be910a98`.
Three interleaved A/B/B/A blocks measured baseline/candidate median wall time
4.525/4.540 seconds, user time 4.070/4.095 seconds, and peak RSS
360,346/360,690 KiB.  The paired movement is +0.33% wall, +0.49% user, and
-0.01% RSS, below the noise guardrail.  The exact S0 tree passes all 5,276
report tests, four reporting-script unit tests, `git diff --check`, and the
PA39 file audit with zero fatal findings.

### S1: repair nonthrowing fact propagation

Start with the simplest terminate-bearing functions that host compilers treat
as nonthrowing.  Trace the selected callee binding through redeclaration,
canonical binding, template cloning/substitution, special-member synthesis,
force-inline cloning, and LowIR symbol adaptation.  Fix the earliest point
where a truthful fact is lost.

Do not infer nonthrowing from a function name, body spelling, standard-library
namespace, or the absence of a throw in one observed instantiation.  A call is
`unwind=no` only from a source exception specification, a correctly derived
special-member rule, a supported builtin/runtime contract, or an already
typed callee fact.

Route reducers independently:

- PA16 for explicit free/member `noexcept` facts;
- PA17 for implicit/defaulted special-member propagation;
- PA19/PA22 or the first later template owner for a fact lost during
  specialization; and
- PA26/PA31 for the resulting absence of an unnecessary terminate boundary.

Record how many of the 400 terminate call sites and 568 EH-resume-bearing
functions disappear.  Re-run demand analysis only to confirm that bodies made
unreachable by removed calls are safely pruned; do not introduce a new demand
policy.

Complexity: O(1) fact copying/merging at existing declaration and
specialization events; no body scan beyond the current boundary census.

#### S1 completion record

The audit found one lost typed fact rather than a general declaration or
specialization propagation failure.  Calls represented as compiler, hosted
vector, or hosted atomic intrinsics lower directly to typed LowIR operations;
they are not indirect calls and cannot unwind.  The exception-boundary census
now consumes those existing dense intrinsic kinds directly.  It performs
three enum comparisons at the call node and introduces no text identity,
body-based inference, or additional pass.

The PA34 course run/inspect reducer wraps `__atomic_load_n` in an explicit
`noexcept` function and requires the resulting native object not to define the
terminate helper.  PA34's existing run harness now includes a course run
bucket, using the same behavior and object-inspection test type as its main run
suite.  The current student requirement identifies the observable intrinsic
exception contract; the compact typed-fact suggestion is confined to `Design
Notes (Non-Normative)`.

The two residual calls reported as indirect by the frozen census were audited
to `std::_Function_base::~_Function_base` and `std::function::operator()`.
Their call operands are genuine erased-function-pointer dispatches rather than
lost callee bindings, so their conservative unwind behavior remains.  Several
other terminate-bearing libstdc++ helpers call functions whose declarations do
not carry `noexcept`; removing those boundaries would require the forbidden
O0 body analysis or force-inline inference.  Explicit, virtual, templated, and
explicit-specialization `noexcept` reducers already preserve their typed facts.

Relative to S5a, the frozen object loses 168 bytes, 49 `.text*` bytes, 48
relocation bytes, two relocations, and 15 decoded instructions.  It removes one
terminate-helper call and one `__cxa_begin_catch` call; `_Unwind_Resume` and the
EH section sizes are unchanged.  The deterministic object is 3,241,512 bytes
with SHA-256
`8b0618198bb070b045d330b9653a61fcb242138a7ad3da30f2b8c86dd412f4e5`.

Three A/B/B/A blocks against immutable commit `e3e86302` measured
baseline/candidate median wall time 4.635/4.625 seconds, user time
4.170/4.165 seconds, and peak RSS 359,930/360,502 KiB.  Paired movement is
+0.22% wall, -0.24% user, and +0.13% RSS.  The phase passes its focused test,
all 374 PA34 tests, all 4,923 report tests through PA34, the complete 5,278-test
root report, `git diff --check`, and the PA39 audit with zero fatal findings.

### S2: centralize the legitimate terminate action

For a function that truly needs a terminate boundary, call one shared helper
with the exception object.  The helper performs `__cxa_begin_catch` once and
then calls `std::terminate`, matching the compact ABI shape used by Clang.
Ordinary typed catch handlers continue to call `__cxa_begin_catch` themselves.

This changes the source-generated LowIR used by host-object compilation, not an
ELF peephole.  PA26's public `--emit-lowir -O0` path deliberately does not add
host exception-boundary policy, so the earliest observable owner is PA31 rather
than an invented PA26 test mode.  Add a PA31 host-linked behavior/inspection
reducer containing multiple legitimate `noexcept` violations.  Verify that the
helper has internal binding, is emitted only on demand, receives the current
exception object, never returns, and does not interfere with handler ownership
or `__cxa_end_catch`.

Measure begin-catch calls, helper calls, relocations, `.text*`, and LSDA.  A
candidate that merely moves repeated work into another per-function helper is
not complete.

Complexity: one helper per translation unit and O(1) work per legitimate
terminate landing.

#### S2 completion record

The translation-unit-local `cppgm_call_terminate` helper now accepts the active
exception pointer, calls the typed `eh_begin_catch` runtime once, and then calls
`std::terminate`.  Each legitimate function boundary loads its existing
exception slot and calls that helper directly.  The helper is explicitly
`no_inline`: expanding it at `-O1`/`-O2` would recreate the duplication that the
shared ABI action exists to avoid.  Ordinary source catch handlers retain their
own begin/end-catch ownership.

The PA31 course reducer contains two distinct throwing paths in explicit
`noexcept` functions, executes only their nonthrowing paths, and inspects the
object for one local helper plus exactly one call relocation each to
`__cxa_begin_catch` and `std::terminate`.  The object-expectation harness gained
an exact-count form of its existing typed relocation-class check so the old
per-landing shape cannot pass by satisfying a minimum.  PA31's current required
surface and non-normative design note describe only the student-visible rule
and an efficient typed implementation shape.

Relative to S1, the frozen object falls from 3,241,512 to 3,229,456 bytes
(-12,056).  `.text*` falls by 1,998 bytes, base text by 150 bytes,
`.gcc_except_table` by 285 bytes, relocation storage by 9,600 bytes, and
decoded instructions by 398.  Relocations fall by exactly 400.  Begin-catch
calls fall from 448 to 48 while the 401 shared-helper calls and 821 resume calls
remain; function and `.eh_frame` counts are unchanged.  The deterministic
object SHA-256 is
`757c01b035233344c725542712d2a71bce5fe850a6eda96cb8c6aa640c9f1623`.

Three A/B/B/A blocks against immutable S1 commit `6e012c47` measured
baseline/candidate median wall time 4.520/4.505 seconds, user time
4.080/4.080 seconds, and peak RSS 360,826/360,332 KiB.  Paired movement is
-0.22% wall, -0.24% user, and -0.37% RSS.  The exact phase passes its focused
test, PA31's 29 result groups, all 4,304 report tests through PA31, the full
5,279-test report, `git diff --check`, and the PA39 audit with zero fatal
findings.

### S3: share the physical resume terminal safely

After S1 and S2, recount `_Unwind_Resume`.  For each host-EH function, determine
whether every final resume can load the current exception from the same typed
frame slot and branch to one physical terminal.  Keep semantic cleanup blocks
and serialized MIR intact; this phase is PA31 host-object layout.

The implementation must account for:

- dual-entry cleanup suffixes and the landing trampoline introduced by
  `51ac844a`;
- stack-argument cleanup shims;
- nested handlers and rethrow state;
- functions in which different resume paths can carry different active
  exception objects; and
- shared epilogue fallthrough.

If one physical terminal is not valid for a function, retain distinct typed
terminal classes and report the reason.  Never merge by rendered label or
exception-slot spelling.

Add PA31 behavior cases for ordinary cleanup, cleanup after a clobbering call,
nested handler state, and stack-argument unwind.  No LowIR or MIR fixture
should change.  Record semantic resume count, physical resume count, and
relocation/LSDA movement.

Complexity: one O(MIR instructions) census during the existing native layout
walk and O(1) terminal selection per resume.

#### S3 completion record

Host-object emission now retains every semantic MIR `resume` but gives a
function with more than one such operation one physical terminal.  Each
semantic site branches to the terminal; the terminal reloads the active
exception from the function's existing typed host-EH frame slot and calls
`_Unwind_Resume`.  A function with one resume keeps its direct sequence, so the
change cannot add a branch where no sharing is possible.

The safety argument follows the existing host-EH contract.  MIR `resume` has no
operand, every landing entry stores the current exception in the one typed
per-function slot, a stack-argument landing shim restores its outgoing stack
area before entering cleanup, and the terminal reloads only after all cleanup
calls that may clobber registers.  A nested landing replaces the slot with its
own current exception before using the same terminal; no semantic cleanup
block, handler state, or exception identity is merged.  The resume count is
collected in the host-EH analysis's existing all-instruction walk, so normal
emission adds no extra scan, map, or text identity.

The existing PA31 distinct-cleanup test now requires its two functions to have
two total resume relocations rather than five.  A new PA31 behavior/inspection
fixture combines two cleanup arms, an eight-argument throwing call, stack
cleanup, and a destructor that makes a clobbering eight-argument call before
resuming; it likewise falls from five resume relocations to two and executes
both unwind arms.  Existing PA31 cases continue to cover ordinary cleanup,
nested handlers, landing-entry separation, and LSDA barriers.  The focused
reducer's serialized LowIR and MIR are byte-identical between S2 and S3.

On the frozen compile, 821 semantic resume operations become 568 physical
terminals.  The 101 functions with multiple resumes contain 354 branch sites,
removing 253 `_Unwind_Resume` calls and relocations.  Relative to S2, the object
falls from 3,229,456 to 3,221,784 bytes (-7,672), `.text*` by 1,416 bytes, base
text by 934 bytes, `.gcc_except_table` by 212 bytes, relocation storage by
6,072 bytes, and decoded instructions by 163.  `.eh_frame` and function counts
are unchanged.  The deterministic object SHA-256 is
`05d07eadb7398e0b91a12c258ec7287db1c0c2bad9264255d491e99ee659f54b`.

Three A/B/B/A blocks against immutable S2 commit `ee5f6234` measured
baseline/candidate median wall time 4.555/4.550 seconds, user time
4.095/4.095 seconds, and peak RSS 359,768/360,438 KiB.  Paired movement is
-0.11% wall, +0.24% user, and +0.25% RSS.  The exact phase passes PA31's 30
result groups, all 4,305 report tests through PA31, the full 5,280-test report,
`git diff --check`, and the PA39 audit with zero fatal findings.

### S4: re-audit cleanup-tail equivalence

Use S0 telemetry to identify the largest remaining destructor families and the
specific cleanup-key field blocking sharing.  Attempt one semantic class at a
time:

1. PA16 lexical object cleanup;
2. PA17 temporary/value cleanup; and
3. PA26 handler/unwind cleanup.

Only remove a distinction after a reducer proves that it does not affect
destruction order, conditional lifetime, handler exit, cleanup-region exit,
return staging, or the exception object resumed.  Extend the existing dense
cleanup DAG rather than adding a late LowIR optimizer or a second suffix map.

Every public change receives LowIR coverage at its earliest owner and PA31
behavior coverage when unwind is involved.  Stop this phase if the residual
call excess is explained by genuinely distinct lifetimes rather than duplicate
continuations.

Measure the three high-count destructor families, resume calls, cleanup blocks,
LSDA, relocations, text, compile time, and cleanup interner probes/hits.

Complexity: expected O(actions + cleanup edges), O(1) expected typed-state
interning, and O(unique states) storage.

#### S4 completion record: no safe baseline key weakening

The frozen S3 compile makes 18,326 cleanup-state probes, finds 15,644 exact
hits (85.4%), interns 2,682 unique states, and materializes 2,630 blocks.  The
existing typed DAG avoids 11,631 destructor actions and 1,010 semantic resume
operations before LowIR optimization.  This is already the dominant exact
sharing mechanism rather than a low-hit cache hiding an obvious missed
equivalence class.

Every remaining key dimension changes emitted semantics:

- `lifetime_object`, `object_binding`, base projection, and operand type select
  the object address and subobject passed to the destructor;
- destructor binding and the action flags select the called operation and
  handler/cleanup-region transition;
- `tail` preserves destruction order;
- `terminal` preserves return staging or the external continuation;
- exception context preserves the active try/handler destination; and
- mode distinguishes a destructor action from a landing prefix, terminal,
  conditional tail, or lexical-return transition.

As an independent upper-bound check, PA37's existing exact LowIR cleanup-tail
pass at `-O1` visits every retained function but finds only 16 shareable groups,
rewrites 18 blocks, and removes 22 instructions; its resume cleanup removes
three blocks.  The high-count destructor relocations do not materially fall:
string destruction changes from 1,585 to 1,594, `shared_ptr<Type>` from 747 to
744, and `ExprInfo` from 646 to 647.  The small increases come from ordinary
O1 inlining.  The larger O1 call-count changes coincide with 7,106 generic
inline expansions and other optimizer passes, so they are not evidence for
weakening baseline cleanup identity.

The per-caller census confirms that the remaining high counts are concentrated
in large source functions but name different object lifetimes within those
functions.  Sharing merely by destructor binding would call the right
destructor on the wrong storage.  Cross-function factoring would require a
new helper ABI carrying object and continuation state and is code-size
optimization, not PA16/PA17 semantic lowering.

S4 therefore retains no code, fixture, reference, scaffold, or student README
change.  The deterministic frozen object remains the S3 object above, and the
S3 full-report and zero-fatal audit are the validation boundary.  Remaining
temporary-object reductions continue under S5; generic suffix factoring stays
in PA37.

### S5: classify and remove destination-placement misses

Use S0 provenance to split movement before implementing a broad allocator:

- If source lowering constructs an object in a temporary and then copies it to
  an already-known final destination, fix construction at PA16, PA17, or the
  later source-feature owner.  The LowIR migration is intentional and tested
  there.
- If LowIR already names the final slot but PA29 creates an address or scalar
  temporary, fix PA29 selection and make the direct location visible in MIR.
- If MIR already names the direct location but native emission introduces a
  copy, fix the encoder without changing MIR.
- If removing the store requires arbitrary source-slot promotion, defer it to
  PA37 `-O2`.

Begin with the largest reduced family from `analyze_call_expression`, not the
whole frozen function.  Make one placement rule at a time and census every
existing PA16-PA29 LowIR fixture, PA29 strict/structural/behavior fixture, and
PA38 O1/O2 downstream fixture.

Use existing typed destination, use-count, last-use, clobber, lifetime, and
address-escape facts.  Do not key decisions by frame displacement, rendered
operand, or source name.

Complexity: O(1) placement decisions after facts already computed during the
existing O(operands + CFG edges) census.

The first S5 changeset is the S0 allocator-like empty-base reducer above.  It
must preserve the reference-returning call as an address, apply the typed base
projection directly, and bind the constructor reference parameter without
creating a complete-object `refcall` slot.  Test this at PA17 before measuring
the larger `std::vector` family.  Do not special-case empty classes, allocator
names, `std::move`, or object byte width; the rule follows the existing typed
reference category and base-projection facts.

#### S5a completion record: derived xvalue reference binding

The reduced semantic defect was the derived-to-base reference-binding
predicate: it treated every non-lvalue as requiring temporary materialization.
An xvalue is already a glvalue designating storage, so only a prvalue needs a
new materialization before the base projection.  The corrected typed
value-category check removes the complete-object `refcall` slot and projects
the existing object directly.  Clang's AST independently shows the same
`DerivedToBase` binding without a materialization node.

The new PA17 course fixture covers an empty base with a `const` reference copy
constructor and a nonempty sibling base.  The old compiler rejects it while
trying to lower the mismatched complete-object temporary; the corrected LowIR
has two direct base projections.  PA17's current requirement and its
non-normative design note now distinguish prvalue materialization from xvalue
glvalue binding without describing implementation history.

For the frozen compile this removes 5,216 object bytes, 2,126 `.text*` bytes,
528 `.gcc_except_table` bytes, 2,328 relocation bytes, 97 relocations, two
defined functions, and 512 decoded instructions.  It removes 33
`cppgm_call_terminate` and 33 `__cxa_begin_catch` calls; `_Unwind_Resume` stays
at 821.  The reduced `std::vector<Item>` object loses 456 object bytes, 192
text bytes, 48 LSDA bytes, nine relocations, and 46 decoded instructions.

Three A/B/B/A blocks measured baseline/candidate median wall time
4.555/4.540 seconds, user time 4.105/4.100 seconds, and peak RSS
360,226/361,000 KiB.  That is -0.33% wall, -0.12% user, and +0.21% RSS.  The
phase passes PA17's 244 tests, all 1,714 report tests through PA17, the full
5,277-test report, `git diff --check`, and the PA39 audit with zero fatal
findings.

#### S5b completion record: direct-register class-call destinations

PA17 now passes an already-planned class temporary address through the same
typed call-lowering interface for both indirect-result and direct-register
class ABIs.  An indirect-result call continues to receive the address as its
hidden result argument.  A direct-register call copies its returned object
once into that address and does not retain a second full-expression call
object.  Slot planning recognizes this immediate typed parent/child relation
with one additional bit in its existing byte context; it adds no string key,
map, or second traversal.

The PA17 course reducer puts direct-register class returns in both arms of a
conditional call argument with another class temporary whose destructor must
run on normal and exceptional exits.  The checked-in LowIR contains the final
argument object and one `copyobj` per selected arm, but no intermediate object
`$call` slot.  The upstream reference independently agrees on direct
placement.  The local reference records the current cleanup CFG as required by
the reference policy, and both GCC and Clang accept and run the C++11 source.
PA17's current requirements and `Design Notes` describe the public placement
contract without implementation history.

The frozen LowIR census removes all 62 generated object `$call__N` slots
(62 to zero), 62 total object slots, and 61 `copyobj` instructions.  The
lowering reports 518 direct class-call destination placements because calls
that did not need a retained slot already had a final destination too; the 62
slot avoids count is the narrower full-expression saving.  The residual eight
object slots whose names begin with `call` are source variables such as
`$callable` and `$callee`, not generated call results, so this placement family
is exhausted.

Against the byte-identical S3 baseline, the frozen object is 3,220,432 bytes
(-1,352), with 731,760 `.text*` bytes (-1,305), 426,048 base text bytes
(-854), 41,339 LSDA bytes (-12), and 184,165 decoded instructions (-247).
Relocations and `.eh_frame` are unchanged.  The movement census loses 104
address-materialization LEAs, 61 memory-to-memory moves, and 61
immediate-to-register moves.  LowIR falls by 122 instructions and MIR by 142;
object-temporary movement falls by 88 and address movement by 28.

Three deterministic A/B/B/A blocks measured baseline/candidate median wall
time 4.610/4.590 seconds, user time 4.125/4.130 seconds, and peak RSS
360,382/362,408 KiB.  Paired block medians are 0.000% wall, +0.605% user, and
+0.451% RSS, all within host noise and with no compile-speed regression.
The phase passes PA17's 245 tests, all 1,715 report tests through PA17, the full
5,281-test report, `git diff --check`, and the PA39 audit with zero fatal
findings.

### S6: retain scalar values through bounded baseline regions

After direct-construction misses are removed, extend PA29's existing
constraint-aware location tracking to compiler-created scalar values that
remain within a bounded region.  Keep a value in its selected register until
an actual fixed-register definition, call clobber, control-flow edge,
address-taking use, or representation change requires a stable home.

This is not general global register allocation.  Use the existing dense
definition/last-use and fixed 16-register state; a straight-line region may be
extended across a block boundary only when predecessor/successor identity and
edge liveness are already exact.  Do not add interval sorting, graph coloring,
per-instruction maps, or repeated active-value scans.

Serialized MIR must show the retained register and any required home transfer.
Add PA29 structural coverage for accepted placement and behavior coverage for
call pressure, loops, EH edges, indirect calls, mixed GPR/XMM calls, and
address escape.  PA38 O1/O2 inputs must continue to optimize the resulting MIR
without relying on an O0-only hidden alias.

Measure frame loads/stores, register copies, temporary homes, callee saves,
MIR instructions, native instructions, and per-phase compile time.  A reduction
that increases callee-save/restore bytes more than it removes is rejected or
deferred to the cost phase.

Complexity: O(values + instructions + CFG edges), with active-register work
bounded by the fixed target register count.

Completed in S6.  The PA29 analysis now records a dense block index, sole
successor, sole predecessor, and sole cross-block-use destination.  It marks a
compiler-created scalar only when its definition block and unique use block
form one presentation-adjacent, acyclic, exact CFG edge.  Parameters, loop
invariants, joins, branch fanout, EH fanout, nonadjacent edges, address-backed
values, and registers clobbered before the use remain on the conservative
path.  This costs O(blocks + CFG edges + uses + values), uses `ValueId` and
dense vectors throughout, and constructs neither string keys nor per-block
live-value sets.  The lowering may retain an eligible selected GPR/XMM and may
reuse it destructively at its final use; all existing fixed-register and
clobber handling remains authoritative.

The new PA29 structural/behavior fixture keeps one scalar in `rbx` across an
exact edge and a call, with no temporary frame binding, store, or reload.  The
independent upstream PA29 reference also selects a callee-saved register for
this case.  Existing PA29 loop, pressure, indirect-call, mixed GPR/XMM, and
address-escape coverage, plus PA38 EH-edge coverage, remain negative guards.
One pre-existing PA29 raw MIR sidecar was regenerated to agree with its
already-canonicalized CMIR: its immediate `or` is now represented directly
instead of through a stale temporary register.

On the frozen source, 3,452 values meet the exact-edge relation and 35 avoid
edge stabilization.  Temporary homes and edge-home creations each fall by 34
or 35, MIR falls by 67 instructions, decoded native instructions by 19,
memory-to-register movement by 39 instructions/163 bytes, and
register-to-memory movement by 34 instructions/133 bytes.  The object is
3,220,336 bytes (-96), `.text*` is 731,575 bytes (-185), and base `.text` is
426,036 bytes (-12).  LSDA and relocations are unchanged; `.eh_frame` grows by
108 bytes because 27 additional push/pop pairs preserve profitable retained
registers.  The net size gate passes, while S8 retains responsibility for the
preserve-cost audit.

Two three-block A/B/B/A measurements were run in opposite orders.  In the
baseline-first run, baseline/candidate medians were 4.775/4.750 seconds wall
and 4.275/4.270 seconds user.  In the candidate-first run, candidate/baseline
medians were 4.765/4.755 seconds wall and both were 4.270 seconds user.  Peak
RSS moved by less than 0.5% in both directions.  The reversed order therefore
confirms compile time and memory are neutral under intermittent host load.
PA29 plus PA38 pass all 289 report tests and the full report passes all 5,282.

### S7: preserve known-width normalization

Classify the residual 3,639 `movzbl`, 1,430 `movslq`, and repeated `setcc`
families.  When a typed defining instruction already establishes the required
upper-bit state and no intervening definition invalidates it, let its consumer
read that normalized register directly.  Keep explicit normalization when a
narrow frame/global reload, call return, arithmetic result, or ABI boundary
does not prove the upper bits.

The fact belongs in typed MIR location/definition state, not a string-keyed
encoder cache.  If serialized MIR currently contains a real conversion, the
fixture must migrate at PA29; do not silently erase it only while encoding.

Add positive and negative PA29 cases for signed and unsigned i8/i16/i32 values,
call returns, switch/compare consumers, fixed-register clobbers, and loops.
Reuse existing narrow-width tests where they already own the behavior.

After the width-state change is independently accepted, audit the smaller
predicate and fixed-arithmetic families with reducers from the largest
same-function deltas.  A comparison used only by a branch should feed the
branch condition directly; materialize `setcc` only when the C++ value is also
consumed as data.  For multiplication, division, and shifts by a fixed
constant, select a cheaper legal target form at `-O0` only when this is local
mandatory instruction selection with no duplicated computation, altered
overflow semantics, or new control flow.  Do not infer algebraic identities
from rendered operands or run a general strength-reduction pass.

If the accepted choice changes the MIR instruction or dataflow sequence, make
it visible in serialized MIR and add PA29 structural plus behavior coverage.
If the existing MIR contract already denotes one operation for which x86 has
multiple equivalent single-instruction encodings, keep MIR stable and add a
PA29 behavior/inspect assertion for the selected encoding.  A rewrite needing
multiple-instruction analysis or profitability across blocks belongs in PA38,
not in this baseline phase.

Complexity: one fixed-size per-register normalization state updated and
queried at instructions already visited, plus O(1) typed operator/constant
selection per instruction.  No extra whole-function scan is permitted.

S7a completes the load-owned portion of the width work.  Integer `MI_LOAD`
now defines the complete logical register value: x86 selection emits a signed
or unsigned extending memory form directly for i8/u8/i16/u16/i32/u32 and an
ordinary full-width load for i64/u64/pointers.  The MIR builder consequently
does not append a second normalization after a same-destination typed load.
Adjacent and delayed frame-reload forwarding use the same typed normalized
register transfer, including when source and destination are identical, so
forwarding cannot expose stale upper bits.  This is a constant-time decision
at operations already emitted and adds no map, value scan, or hidden encoder
dataflow graph.

The existing PA29 fixtures already provide the required reducers: signed and
unsigned frame/global loads, promoted comparisons, atomics, floating branch
inputs, direct returns, memory operands, and stack-call arguments all migrate
to the one-operation load contract.  Existing call-return fixtures retain
their explicit normalization and remain the negative boundary.  Seventeen
PA29 fixtures and five inherited PA38 fixtures changed, each only by deleting
the redundant post-load instruction; no LowIR fixture changed.

Against S6, frozen MIR falls from 171,577 to 169,530 instructions (-2,047).
Source-slot normalizations fall from 1,026 to zero, scalar-temporary
normalizations from 2,206 to 1,218, and call-boundary normalizations from 1,247
to 1,214.  The object falls from 3,220,336 to 3,215,320 bytes (-5,016),
`.text*` from 731,575 to 726,695 (-4,880), LSDA by seven bytes, and decoded
instructions from 184,146 to 182,489 (-1,657); `.eh_frame` is unchanged.
The remaining `movzx`/`movslq` counts move only from 3,659/1,431 to
3,616/1,419 because one extending load remains, correctly, in place of each
old partial-load/register-extension pair.

Three A/B/B/A timing blocks against the exact S6 compiler measured
baseline/candidate medians of 4.715/4.720 seconds wall, 4.220/4.215 seconds
user, and 360,036/360,238 KiB RSS.  Paired block medians are -0.53% wall,
-0.82% user, and -0.05% RSS, so the phase is compile-cost neutral.  PA29 plus
PA38 pass 289/289, the full report passes 5,282/5,282, and the file audit has
zero fatal findings.

S7b completes a second bounded consumer class.  A sole-use narrow call result
does not normalize in its ABI carrier when the next LowIR operation is a
same-width store, a same-width return, or an explicit integer
extension/truncation.  The store and return consume only the ABI-defined low
bits; the explicit conversion performs its own typed normalization.  Any
wider comparison, switch, unrelated instruction, multiple use, or noninteger
conversion retains the call-boundary normalization.  The decision is one
constant-time next-instruction check against the existing dense use count.

The new PA29 structural/behavior reducer covers the store, return, and explicit
conversion cases together and keeps the wider conversion visible in MIR.
No existing fixture changes.  On the frozen source, 537 call-result
normalizations disappear: MIR falls from 169,530 to 168,993 instructions,
the object from 3,215,320 to 3,213,728 bytes (-1,592), `.text*` from 726,695
to 725,075 (-1,620), LSDA by six bytes, and decoded instructions from 182,489
to 181,952 (-537).  `movzx` falls by 488 and `movslq` by 46; `.eh_frame` is
unchanged.

Three A/B/B/A timing blocks against exact S7a measured baseline/candidate
medians of 4.690/4.725 seconds wall, 4.215/4.250 seconds user, and
360,636/360,284 KiB RSS.  One candidate run was a 4.84-second user-time host
outlier; paired block medians remain +0.21% wall, +0.83% user, and -0.23% RSS,
inside the neutral gate.  PA29 plus PA38 pass 290/290, the full report passes
5,283/5,283, and the file audit has zero fatal findings.

S7c records three adjacent typed producer facts directly in the MIR builder.
A canonical in-range integer immediate, a `movzx` Boolean result, or an
identical preceding integer extension already defines the complete value and
does not receive a duplicate normalization.  Explicit source-language
extensions and truncations remain visible: the new PA29 reducer pairs an
in-range typed shift count with an explicit truncation that must survive.
Three existing PA29 course fixtures migrate only by removing a redundant
normalization after a materialized Boolean.  The check examines only the last
MIR instruction and fixed typed fields.

The frozen object loses 86 MIR and 86 decoded instructions, all residual
`movzx`, along with 306 `.text*` bytes, one LSDA byte, and 288 file bytes;
`.eh_frame` is unchanged.  Three A/B/B/A blocks against exact S7b measured
baseline/candidate medians of 4.625/4.620 seconds wall, identical 4.165-second
user medians, and 362,100/360,260 KiB RSS.  Paired block medians are +0.11%
wall, -0.24% user, and -0.54% RSS.  PA29 plus PA38 pass 291/291, the full
report passes 5,284/5,284, and the file audit has zero fatal findings.

S7d completes the local x86 selection portion without changing serialized
MIR.  An explicit register move followed by an integer `sext` or `zext` now
selects the corresponding single extending-register move.  An explicit
normalization after a canonical immediate or typed producer is omitted when
that producer already establishes exactly the required upper-bit state.  Both
checks inspect only the immediately adjacent typed MIR instruction; there is
no encoder-side value map, string identity, or extra function scan.  The new
PA29 behavior reducer exercises signed and unsigned call-result conversions
and an explicit constant truncation.  It passes both the independent PA29
reference and the local implementation while retaining the explicit
conversions in its informative MIR.

The frozen census records 765 producer-owned normalizations and 597 fused
move/normalization pairs in the final encoder.  Seventy-two of those cases
were already handled by the earlier u32-only selector; generalizing the rule
removes another 1,290 decoded instructions.  Against exact S7c the object falls
from 3,213,440 to 3,209,640 bytes, `.text*` from 724,769 to 720,821 (-3,948),
base text by 2,294 bytes, LSDA by 23 bytes, and register-to-register movement
by 1,290 instructions/3,899 bytes.  MIR, `.eh_frame`, relocations, functions,
and string tables are unchanged.  The deterministic object SHA-256 is
`6ab42f70971ea27beed42ba33d7e950ed95b2b037910c6d8fa89f9c6da48c5bc`.

Three isolated A/B/B/A blocks against exact S7c measured baseline/candidate
medians of 4.630/4.635 seconds wall, 4.145/4.160 seconds user, and
359,878/360,608 KiB RSS.  Median paired block movement is +0.32% wall, +0.36%
user, and +0.23% RSS, inside the neutral gate.  PA29 plus PA38 pass 292/292,
the full report passes 5,285/5,285, and the file audit has zero fatal findings.

S7e completes the fixed-arithmetic and predicate audit.  Forty-two frozen
source shifts used a constant count but still emitted a five-byte `rcx` setup
and a `%cl` shift.  PA29 MIR now retains a fixed count directly on `shl`,
logical `shr`, or arithmetic `sar`; variable counts retain the existing
`rcx/cl` constraint.  Native selection uses the one-count or immediate-byte
x86 form and treats a masked zero count as a no-op.  The change is a single
typed operand check at the already-visited binary instruction.  PA29 gains a
structural/behavior reducer for all three shift kinds, one existing course MIR
fixture and one PA29 strict fixture migrate, and the student contract and
non-normative design notes describe the two count forms.

The residual census finds only three `%cl` shifts, all variable.  Of 646
`imul` instructions, 557 already carry an immediate operand.  Constant
division already uses the local magic-number/power-of-two selector, and the
23 residual `div`/`idiv` instructions have no fixed legal replacement in this
translation unit.  Compare results with a sole branch consumer already use
the direct compare/jump path; the remaining 954 `setcc` instructions are
materialized data values.  No additional baseline arithmetic or predicate
rewrite is justified.

Against S7d, frozen MIR and decoded instructions each fall by 42, the object
falls from 3,209,640 to 3,209,448 bytes, `.text*` from 720,821 to 720,646
(-175), and immediate-to-register movement by 42 instructions/210 bytes.
`.eh_frame`, LSDA, relocations, functions, and strings are unchanged.  The
deterministic SHA-256 is
`ca3a24a69fa27eb00383d200b848353ebd70dc8fcedde853dfe2e5026faf62c3`.

Three isolated A/B/B/A blocks against exact S7d measured baseline/candidate
medians of 4.720/4.690 seconds wall, 4.235/4.205 seconds user, and
360,554/360,234 KiB RSS.  Median paired block movement is -0.95% wall, -0.83%
user, and -0.24% RSS.  PA29 plus PA38 pass 293/293, the full report passes
5,286/5,286, and the file audit has zero fatal findings.

### S8: make baseline register cost explicit

Recount `push`, `pop`, physical epilogues, and preserve-list entries after the
placement phases.  Choose a callee-saved register only when its expected
save/restore and unwind-description cost is lower than the required home
store/reload cost.  The decision must use already-known call crossings and
use counts with a fixed target-register loop.

Because physical registers and the final preserve list are serialized MIR,
this is a PA29 placement change.  Add structural coverage for a value that
profits from one callee-saved register and behavior/structural coverage for a
short value that should use a caller-saved register or frame home instead.

Do not perform post-hoc preserve deletion at `-O0`; PA38 already owns that
`-O2` transformation.

Complexity: O(values * fixed target-register count), using existing clobber
and use facts.

Completed as an audit-only phase.  All 35 S6 exact-edge retentions in the
frozen translation unit cross a call.  A diagnostic build that sends those
values back to frame homes removes 27 push/pop pairs (108 text bytes) and 108
`.eh_frame` bytes, but adds 39 memory-to-register instructions/163 bytes and
34 register-to-memory instructions/133 bytes.  It therefore grows `.text*`
by 185 bytes, decoded instructions by 19, MIR by 67, and the complete object
by 128 bytes.  The retained-register form spends 216 bytes on added physical
preservation plus unwind description and avoids 296 bytes of required home
traffic, a net 80-byte code/EH saving before section alignment.

A second bounded prototype preferred any previously used, currently free
callee-saved register before opening a new preserve.  It removed no preserve,
push/pop, or unwind entry on this workload and grew text/object by 16 bytes
through less compact extended-register encodings, so it was rejected.  The
fixed register-order policy already reuses the compact `rbx` opportunity when
available and opens a later preserve only under simultaneous live pressure.

The existing PA29 `single-edge-callee-saved-retention` structural/behavior
fixture is the positive cost case.  `800-single-use-call-arg-no-preserve` and
`700-call-setup-forwarding-no-preserve` are the required short-value negative
cases: the call argument remains in a caller-saved carrier and the containing
function gains no preserve list.  Because the production tree and every
fixture remain unchanged, S7e's 5,286-test full report, zero-fatal audit,
deterministic object, and timing are the S8 validation boundary.

### S9: remeasure live bodies and optimization-only opportunities

After S1-S8, repeat the weak-body and relocation census.  Remove a definition
only if its last typed demand edge disappeared as a consequence of an accepted
change.  Do not target Clang's weak count directly.

Audit explicit `always_inline` propagation through PA33 attribute semantics and
the existing PA35 native-object coverage.  A missed explicit force-inline fact
is an O0 correctness/extension issue and receives an earliest-owner reducer.
Ordinary tiny-function inlining remains PA37 `-O1`; trace layout and final
preserve/frame cleanup remain PA38 `-O2`.  Add any new optimizer tests there,
not PA29.

This phase records, rather than hides, the residual Clang-only difference.

Completed as an audit-only phase.  The final object defines 5,528 functions:
13 global, 1,024 local, and 4,491 weak.  The weak count is unchanged from S0.
The only two definitions removed since S0 are the local complete/base aliases
of `std::allocator<SourceTokenRef>::~allocator()`.  S5's direct class-result
placement removed the allocator-like temporary and therefore its last typed
destructor demand.  The relocation reduction from 26,936 to 26,184 is fully
accounted for by S1 (-2), S2 (-400), S3 (-253), and S5 (-97); no unexplained
demand edge disappeared in S6-S8.

Post-inline reachability prunes 685 functions, including 682 unreachable weak
bodies.  The retained roots are 13 external strong definitions, 39
address/relocation demands, 4,295 direct-call demands, two lifecycle demands,
one EH/runtime demand, and 224 native object-output roots (four weak and 220
internal).  There are zero required-weak exceptions, zero unreachable
internal leftovers, and zero conservative fallback roots.  Removing another
body would therefore discard a live typed root rather than clean up stale
demand.

The explicit inline fact is also intact end to end.  PA33 records
`always_inline` on both the declaration binding and its canonical binding;
redeclaration and special-member propagation copy that fact; PA15 lowers the
binding/canonical OR to typed LowIR; the PA30 adapter/object representation
preserves it; and the native session performs the mandatory rewrite before
reachability.  The frozen compile has 623 candidates and expands 1,426 calls,
5,666 blocks, and 9,446 instructions, with zero recursive candidates.  The
existing PA35 native-object value-transport reducer emits `use_one`,
`use_sixteen`, and `use`, but no `pass_one` or `pass_sixteen` body.  PA33 plus
PA35 pass 248/248.  No missing PA33 fact or new reducer is needed.

The S9-boundary host-toolchain comparison uses the same GCC 15 libstdc++
headers.  cppgm++ is 3,209,448 bytes versus GCC's 3,196,024 (+0.42%), but still has
720,646 `.text*` bytes versus GCC's 599,783 (+120,863) and Clang's 523,269
(+197,377).  The base-text gaps are 108,797 bytes to GCC and 109,799 to Clang;
the COMDAT-text gaps are only 12,066 to GCC but 87,578 to Clang.  cppgm++ has
180,534 decoded instructions versus GCC's 154,849 and Clang's 124,116.  The
remaining GCC gap is concentrated in base-function stack traffic and EH
shape, while the much larger Clang gap also reflects 1,594 fewer weak bodies
and its different template/code-generation policy.  Ordinary inlining,
source-slot promotion, trace layout, and post-hoc preserve cleanup remain in
PA37/PA38 rather than being hidden at baseline.

### S10: emit sparse physical LSDA coverage

The S9 LSDA writer emitted a null-landing call-site record for every byte gap
between protected ranges and for the trailing function suffix.  That is more
coverage than the Itanium personality contract requires.  An instruction
pointer absent from a function's call-site table terminates the search, while
an explicit record with no landing pad continues unwinding.  Consequently an
unprotected potentially throwing call must remain covered, but ordinary
prologue, arithmetic, branch, and epilogue bytes need no record.

The durable LSDA census makes the distinction explicit.  At the S9 boundary,
the frozen object has 621 LSDAs and 35,990 call-site-table bytes: 2,975
protected records occupy 18,039 bytes and 3,592 null-landing records occupy
17,951 bytes.  The comparable GCC object has 463 LSDAs and 14,006 call-site
bytes: 1,782 protected records occupy 11,192 bytes and 584 null-landing records
occupy 2,814 bytes.  The old aggregate section-size measurement hid that
almost half of cppgm++'s call-site table was unconditional gap padding.

The retained PA31 change consumes the exact typed
`unprotected_unwind_ranges` already recorded during layout.  It walks those
ranges and the sorted protected sites once.  Within each interval between
protected sites it emits one null-landing hull only when the interval contains
an unprotected potentially throwing call, then emits the protected site.  A
single reusable vector carries typed offsets, lengths, landing-pad offsets,
and action block identities to the encoder.  There is no rendered identity,
string set, lookup map, rescanning of machine bytes, or per-function sort.
The complexity is O(P + U) time with O(P + U) reusable scratch space for P
protected and U unprotected ranges.

PA31 owns this physical host-object rule; LowIR and MIR remain unchanged.  The
student-facing PA31 contract now requires sparse ordinary gaps and explicit
unprotected throwing coverage, and its Design Notes recommend the typed
layout merge without prescribing this implementation.  The new course reducer
`420-sparse-unprotected-lsda-coverage` executes one throw before a cleanup
lifetime and another during it.  Thus behavior requires the former call's
null-landing record, while normalized inspection requires an ordinary sparse
gap.  The S9 compiler still passes the program but fails the new inspection.
Nine existing PA31 inspection references move from the old
`call_site_starts_at_zero` shape to `call_site_has_sparse_gap`; they and the new
fixture were regenerated with the documented PA31 `ref-test` path using the
explicit local compiler binary.

The frozen call-site table falls from 35,990 to 26,500 bytes.  Protected shape
is byte-identical, while null-landing shape falls from 3,592 records/17,951
bytes to 1,697 records/8,461 bytes.  `.gcc_except_table` falls from 41,302 to
31,802 bytes and the object from 3,209,448 to 3,199,952 bytes; `.text*`,
`.eh_frame`, relocations, functions, and all 180,534 decoded instructions are
unchanged.  The deterministic object SHA-256 is
`3adc50057636ce1c5a343eba423fb7cd17fff3a93af9dbc95fc35fdab86d033a`.

The remaining 15,539-byte LSDA gap to GCC is now classified rather than
treated as one opaque EH problem.  Call-site tables account for 12,494 bytes:
cppgm++ still has 1,193 more protected records/6,847 bytes and 1,113 more
required null records/5,647 bytes.  The other 3,045 bytes are the aggregate
header, action, type, and alignment difference.  cppgm++ also has 158 more
LSDAs and 548 more defined functions.  Closing those differences requires
fewer live EH-bearing bodies, fewer protected cleanup boundaries, or more
proof that calls cannot throw; omitting their records at the object writer
would be incorrect.  Those are demand, source/EH lowering, and PA37/PA38
optimization questions, not another unconditional O0 padding deletion.

Two A/A calibration blocks show effectively zero wall movement and -0.24%
median user movement.  Three load-screened A/B/B/A blocks measure the S9
baseline/candidate at 4.630/4.640 seconds median wall, 4.145/4.185 seconds
median user, and 360,222/360,286 KiB median peak RSS.  Paired movement is
+0.65% wall, +1.21% user, and +0.08% RSS, within the 3% guardrail.  The exact
tree passes PA31 31/31, through-PA31 4,312/4,312, the full report
5,287/5,287, five reporting-script unit tests, and the file audit with zero
fatal findings and the same 28 advisory warnings.

## Final closeout

The completed second pass reduces the deterministic frozen object from
3,246,896 to 3,199,952 bytes (-46,944), `.text*` from 738,654 to 720,646
(-18,008), base text from 428,192 to 418,542 (-9,650), COMDAT text from
310,462 to 302,104 (-8,358), LSDA from 42,376 to 31,802 (-10,574), relocations
from 26,936 to 26,184 (-752), and decoded instructions from 185,500 to 180,534
(-4,966, using the final report script for both objects).  `.eh_frame` moves
from 137,264 to 137,300 (+36); S8 accounts for the profitable retained-register
portion of that increase.  The final object is deterministic at SHA-256
`3adc50057636ce1c5a343eba423fb7cd17fff3a93af9dbc95fc35fdab86d033a`.

The final load-screened S10 timing has a 4.640-second median wall time,
4.185-second median user time, and 360,286 KiB median peak RSS.  The final
baseline compile is therefore comfortably below the 15-second gate.

Starting with no PA39 object tree, the clean 32-worker `cppgm++-self` build
completed in 18.70 seconds wall, 425.56 seconds aggregate user, and 43.26
seconds aggregate system time, with 228,436 KiB peak RSS.  Starting with no
inception objects, the separate 8-worker comparison completed in 3:55.61 wall,
1,807.86 seconds user, and 44.36 seconds system, with 229,460 KiB peak RSS.
After moving both generated inception object and binary trees out of
`obj/pa39`, the separate 32-worker comparison completed in 1:46.59 wall,
2,901.81 seconds user, and 71.15 seconds system, with 226,780 KiB peak RSS.
Both lanes compare all 191 objects successfully and produce the exact
16,635,736-byte self compiler at SHA-256
`7a17b9111c05bb72672087b759a0afbce0dcc049bb72c54e7dde785501bff189`.

The exact closeout tree passes the full 5,287/5,287 report, five reporting
script tests, and the PA39 file audit with zero fatal findings (28 pre-existing
warnings).  Every retained phase has an owning fixture or a documented
audit-only conclusion, and the extended plan is complete.

## Performance and correctness gates

For every retained output-changing phase, record:

- deterministic object size and SHA-256;
- `.text*`, base text, COMDAT text, `.gcc_except_table`, `.eh_frame`, string,
  symbol, relocation, and section-header bytes;
- decoded instruction counts and bytes by mnemonic and operand class;
- defined functions and unique bodies by binding;
- runtime/helper and selected destructor call counts;
- LowIR, MIR, cleanup-state, temporary-home, spill, and native-layout telemetry;
- focused, owning-PA, through-PA, full-report, and file-audit results; and
- three load-screened A/B/B/A blocks with median wall/user/system time and
  peak RSS.

Treat an unexplained median user-time or RSS regression above 3% as a stop.
Treat wall-only movement under host load as noise only when user time,
deterministic output, and paired ordering support that conclusion.  Do not
combine a timed compiler run with another build or test workload.

Use `scripts/report_elf_code_shape.py` for the core deterministic report and
extend it rather than maintaining ad hoc shell parsing once S0 begins.

## Commit discipline and final PA39 lane

Each independently accepted phase is committed and pushed before the next
phase.  A phase that changes references keeps implementation, tests,
student-facing contract/scaffold, fixture migration, and the plan ledger in
the same reviewable changeset or in an explicitly documented reference-only
follow-up commit.

After all retained phases:

1. require a clean full `make test-report`;
2. run the PA39 file audit with zero fatal findings;
3. perform a timed clean PA39 `cppgm++-self` build;
4. perform a clean timed 8-worker inception comparison and record peak RSS;
5. remove only the generated inception object/binary tree;
6. perform a separate clean timed 32-worker inception comparison and record
   peak RSS;
7. require every object and the final compiler to match in both lanes;
8. rerun the full report and file audit on the exact final commit; and
9. push the completed ledger and validation record.

## Change ledger

Fill one row after each retained phase.

| Phase | Public fixture effect | Frozen code/EH effect | Compile time/RSS | Validation | Status/commit |
| --- | --- | --- | --- | --- | --- |
| S0 baseline/instrumentation | none | byte-identical 3,246,896-byte baseline; typed movement/home/EH census above | +0.49% paired median user, -0.01% RSS | 5,276/5,276 full report; four script tests; zero-fatal audit | complete; `1048f6c5` |
| S1 nonthrowing propagation | PA34 atomic `noexcept` behavior/inspect; course run bucket enabled | -49 text, -2 relocations, -1 terminate and begin-catch call | -0.24% paired median user, +0.13% RSS | PA34 374/374; through-PA34 4,923/4,923; full 5,278/5,278; zero-fatal audit | complete |
| S2 shared terminate action | PA31 behavior plus exact relocation inspection; PA26 has no host-policy output | -1,998 text, -285 LSDA, -400 relocations/begin-catch calls | -0.24% paired median user, -0.37% RSS | PA31 29/29 groups; through-PA31 4,304/4,304; full 5,279/5,279; zero-fatal audit | complete |
| S3 physical resume terminal | PA31 stack/clobber behavior and exact resume-relocation inspection; no LowIR/MIR migration | -1,416 text, -212 LSDA, -253 resume calls/relocations | +0.24% paired median user, +0.25% RSS | PA31 30/30 groups; through-PA31 4,305/4,305; full 5,280/5,280; zero-fatal audit | complete |
| S4 cleanup equivalence | none; all typed key fields remain semantically required | audit-only: frozen object remains byte-identical to S3; O1 finds only 16 exact tail groups/22 instructions and three resume blocks | none; no production change | S3 full 5,280/5,280 and zero-fatal audit remain the boundary; exact O0/O1 cleanup/call census recorded | complete; audit-only |
| S5 destination placement | PA17 direct xvalue-to-base binding and direct-register class-call destination fixtures; no PA29 migration needed | cumulative through S5b: -3,431 text, -540 LSDA, -97 relocations, -33 terminate calls; all 62 generated object call slots removed | S5b paired 0.000% wall, +0.605% user, +0.451% RSS | PA17 245/245; through-PA17 1,715/1,715; full 5,281/5,281; zero-fatal audit | complete through S5b |
| S6 bounded scalar retention | PA29 exact-forward-edge MIR/behavior; existing PA29 loop/call/escape and PA38 EH negatives | -185 text, -19 decoded instructions, -34 temporary homes; +108 `.eh_frame` pending S8 cost audit | reverse-order medians both 4.270s user; <0.5% RSS movement | PA29+PA38 289/289; full 5,282/5,282; zero-fatal audit | complete |
| S7 width/fixed arithmetic | S7a migrates 17 PA29 and five inherited PA38 MIR fixtures; S7b/S7c add two PA29 producer/consumer fixtures and migrate three course MIR fixtures; S7d adds MIR-stable behavior; S7e adds fixed shift operands and migrates two MIR fixtures | cumulative through S7e: -10,929 text, -3,612 decoded instructions, -2,712 MIR; no extra scan/map | paired user: S7a -0.82%, S7b +0.83%, S7c -0.24%, S7d +0.36%, S7e -0.83%; RSS neutral | PA29+PA38 293/293; full 5,286/5,286; zero-fatal audit | complete |
| S8 register cost | existing PA29 profitable-retention and no-preserve short-value fixtures already cover both decisions | audit-only: register retention is 80 bytes smaller than frame traffic including unwind; reuse-first prototype rejected at +16 text/object | no production change; S7e timing remains authoritative | S7e full 5,286/5,286 and zero-fatal audit; two diagnostic frozen objects compared | complete; audit-only |
| S9 demand/optimization remeasure | existing PA35 forced-inline object reducer; no missing PA33 fact and no fixture change | weak count unchanged; only two S5 allocator-destructor aliases lost their last typed demand; every remaining body has an enumerated root | no production change; S7e timing remains authoritative | PA33+PA35 248/248; zero conservative fallbacks; final GCC/Clang code-shape census recorded | complete; audit-only |
| S10 sparse LSDA coverage | PA31 behavior/inspection reducer; nine PA31 inspection refs migrate to sparse-gap fact; no LowIR/MIR change | -9,500 LSDA / -9,496 object; protected entries and all machine code byte-identical | paired +0.65% wall / +1.21% user / +0.08% RSS | PA31 31/31; through-PA31 4,312/4,312; full 5,287/5,287; five script tests; zero-fatal audit | complete; `78874ae8` |
| Final PA39 gate | no fixture change | final frozen object 3,199,952 bytes / 720,646 text / 180,534 decoded instructions | frozen median 4.64s wall; clean self 18.70s; inception j8 3:55.61 and fully clean j32 1:46.59 | full 5,287/5,287; zero-fatal audit; 191/191 objects and final compiler match in both inception lanes | complete |

## Completion criteria

This plan is complete only when:

- every retained change has an earliest-owner reducer and no test lives in a
  dormant proposed directory;
- any LowIR or MIR movement is public, documented at the owning assignment,
  and regenerated through the documented reference workflow;
- no baseline phase implements general source-slot promotion, general
  inlining, trace layout, or other PA37/PA38 optimization invisibly;
- production hot paths use typed compact identity and meet the declared
  complexity bounds;
- the nonthrowing and terminate census accounts for every retained boundary;
- the movement census accounts for the major remaining load/store/register-copy
  families rather than reporting only aggregate `mov` counts;
- cleanup/resume sharing preserves the full semantic exception context and all
  PA31 host-EH behavior;
- each accepted phase stays within the compile-time/RSS guardrail and improves
  a measured code/EH category;
- the final frozen compile remains below 15 seconds under a fair load window;
- the full root report and zero-fatal file audit pass; and
- the clean self build and separate clean 8-way and 32-way inception compares
  pass with recorded timings and peak RSS.

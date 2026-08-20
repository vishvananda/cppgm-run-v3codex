# Plan: O0 Code-Shape Parity, Second Pass

Status: active

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

## Assignment and public-boundary ownership

| Work | Public boundary | Earliest tests | Student-facing effect |
| --- | --- | --- | --- |
| Explicit function/member `noexcept` and truthful `unwind=no` | semantic fact and LowIR metadata | PA16 course LowIR | PA16 already requires truthful direct-`noexcept` metadata; strengthen the current requirement only if the reducer exposes a missing case |
| Derived special-member nonthrowing facts | semantic fact and LowIR metadata | PA17 course LowIR | PA17 requirement/Design Notes only for a real derived-special-member gap |
| Template specialization/member-template nonthrowing propagation | semantic fact and LowIR metadata | PA19, PA22, or the first later template owner demonstrated by the reducer | edit only that owning README; do not describe frozen-header history |
| Shared terminate action | source-generated LowIR CFG | PA26 course LowIR plus PA31 host-EH behavior/inspect | PA26 states the current observable helper/CFG requirement; compact implementation advice belongs in `Design Notes` |
| One physical resume terminal per compatible function | host object layout below MIR | PA31 course behavior/inspect | no LowIR/MIR contract change; at most a PA31 `Design Notes` suggestion |
| Additional context-safe cleanup-tail sharing | source-generated LowIR CFG | PA16 lexical, PA17 temporary/value, PA26 handler/unwind, plus PA31 behavior | existing sharing requirements move only when their owning fixture moves |
| Source-owned direct object construction | LowIR, only if the frontend currently creates a redundant semantic temporary | PA16 or PA17, or the later feature owner proven by the reducer | normative current LowIR behavior plus a compact destination-planning suggestion in `Design Notes` |
| Backend-created temporary/home elimination | MIR placement | PA29 strict/structural/behavior | PA29 MIR rules and scaffold comments only when the serialized operand/location contract changes |
| Known-width normalization placement | MIR placement and x86 selection | PA29 strict/structural/behavior | PA29 narrow-value rule; no hidden encoder-only value graph |
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
all 374 PA34 tests, all 4,923 report tests through PA34, the complete root
report (590/590 batched result groups), `git diff --check`, and the PA39 audit
with zero fatal findings.

### S2: centralize the legitimate terminate action

For a function that truly needs a terminate boundary, call one shared helper
with the exception object.  The helper performs `__cxa_begin_catch` once and
then calls `std::terminate`, matching the compact ABI shape used by Clang.
Ordinary typed catch handlers continue to call `__cxa_begin_catch` themselves.

This is expected to change source-generated LowIR and therefore belongs to
PA26, not an ELF peephole.  Add a PA26 LowIR reducer containing multiple
legitimate noexcept violations and a PA31 host-linked behavior/inspection
reducer.  Verify that the helper has internal binding, is emitted only on
demand, receives the current exception object, never returns, and does not
interfere with handler ownership or `__cxa_end_catch`.

Measure begin-catch calls, helper calls, relocations, `.text*`, and LSDA.  A
candidate that merely moves repeated work into another per-function helper is
not complete.

Complexity: one helper per translation unit and O(1) work per legitimate
terminate landing.

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

Complexity: one fixed-size per-register normalization state updated and queried
at instructions already visited.

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
| S1 nonthrowing propagation | PA34 atomic `noexcept` behavior/inspect; course run bucket enabled | -49 text, -2 relocations, -1 terminate and begin-catch call | -0.24% paired median user, +0.13% RSS | PA34 374/374; through-PA34 4,923/4,923; full 590/590 batched groups; zero-fatal audit | complete |
| S2 shared terminate action | PA26 LowIR; PA31 behavior/inspect | pending | pending | pending | planned |
| S3 physical resume terminal | PA31 behavior/inspect only; no LowIR/MIR migration | pending | pending | pending | planned |
| S4 cleanup equivalence | PA16/PA17/PA26 only as proven | pending | pending | pending | planned |
| S5 destination placement | PA17 direct xvalue-to-base reference binding added; later PA29 work pending | S5a: -2,126 text, -528 LSDA, -97 relocations, -33 terminate calls | -0.12% paired median user, +0.21% RSS | PA17 244/244; through-PA17 1,714/1,714; full 5,277/5,277; zero-fatal audit | S5a complete; remaining S5 planned |
| S6 bounded scalar retention | PA29 MIR/behavior; PA38 census | pending | pending | pending | planned |
| S7 width normalization | PA29 MIR/behavior; PA38 census | pending | pending | pending | planned |
| S8 register cost | PA29 MIR/behavior; PA38 census | pending | pending | pending | planned |
| S9 demand/optimization remeasure | explicit force-inline owner only at O0; ordinary work deferred to PA37/PA38 | pending | pending | pending | planned |

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

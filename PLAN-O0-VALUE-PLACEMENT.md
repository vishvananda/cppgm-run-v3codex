# Plan: O0 Native Value Placement and Address Selection

Status: complete (2026-08-20)

Date: 2026-08-17

## Objective

Reduce compiler-created register copies, frame stores/reloads, address
materialization, and callee-save traffic in the PA29 baseline backend without
adding a general optimization pass to `-O0`.  The backend should retain LowIR
values until their target constraints are known, select a final machine
location once, and serialize the same MIR facts that native emission consumes.

The implementation must remain function-local and linear or near-linear in the
LowIR and MIR it processes.  It must not add a textual round trip, a hidden
object-only IR, a whole-program allocator, repeated full-function rescans, or a
fixed-point pass to the baseline path.

## Measured cause

The frozen `semantic_overload.cpp` object currently contains 29,677 stack
loads and 24,897 stack stores, compared with GCC's 22,776 and 13,619.  The O0
`lowir_opt.cpp` comparison is more diagnostic: cppgm++ already emits fewer
stack loads than GCC, 16,276 versus 17,068, but emits 12,188 stack stores
versus 10,581.  The persistent gap is therefore not a missing global load
elimination pass.  It is early physical placement, reactive spills,
materialized address values, ABI shuffles, and callee-save selection.

The current ordinary GPR pool has only R8 and R9 before callee-saved registers.
LowIR values receive a physical register or frame home while each instruction
is lowered.  Variable indexing is expanded to multiply/add operations, and an
ordinary MIR dereference can describe only `base + displacement`.  These
decisions manufacture work that GCC and Clang avoid during mandatory O0 target
selection and register allocation.

## Optimization-level classification

The following remain valid at `-O0`:

- shortest legal instruction and displacement encodings;
- branch relaxation and omission of a branch whose target is the next encoded
  instruction;
- target selection for constant division, zeroing, tests, extensions, and
  legal x86 memory operands;
- direct selection of an ABI argument or return register;
- retaining compiler-created immediates and addresses for rematerialization;
- linear or near-linear register allocation that avoids unnecessary temporary
  frame homes; and
- bounded encoder forwarding that preserves the semantics and final physical
  state of serialized MIR.

The following belong at `-O1` or `-O2`:

- general copy propagation, dead-definition removal, or frame cleanup after
  MIR has been constructed (`-O1`);
- CFG-wide coalescing and optional address folding discovered from existing
  MIR (`-O1`);
- trace layout, whole-function frame finalization after optional rewrites, and
  more global allocation policy (`-O2`); and
- source/LowIR slot promotion, common subexpression elimination, and other
  language-independent value optimization (PA37 `-O1`/`-O2`).

The single-block encoding-only coalescer is transitional.  Its generic copy
and dead-definition work belongs in the visible PA38 optimizer, not in an O0
object-only copy.  It will be removed from baseline encoding after direct O0
placement covers its high-value cases.  The earlier narrow encoding choices
remain as safety nets until the new selector makes them unreachable; they do
not need a blanket rollback.

## Public MIR contract

MIR must be sufficient to explain the native program:

1. Call instructions will serialize the exact GPR/XMM argument-use set and
   relevant ABI/EH call properties already carried by the typed instruction.
2. Memory operands will be able to represent an optional base, optional index,
   scale, displacement, and the existing frame/global binding facts.
3. The typed `dev/src/mir_model.h` scaffold and PA29/PA38 student documentation
   will describe those fields and their textual spelling.
4. Encoding will consume those same typed operands.  There will be no richer
   encoder-only alias or address model.

Public assignment text will describe the model and required behavior directly.
It will not discuss repository migration, fixture maintenance, benchmark
history, or implementation-review policy.

## Implementation sequence

Each retained compiler behavior change is a separate commit.

### VP0: make existing call facts visible

- Serialize known call argument registers, stack argument bytes, variadic,
  no-unwind, and no-return properties.
- Update the scaffold, serializer documentation, canonicalizer, and affected
  MIR fixtures.
- Remove reliance on a nonserialized exact-call mask.

Complexity: O(call argument pieces) while the existing ABI plan is built and
O(1) per register while serializing or querying the fixed-size mask.

### VP1: symbolic sole-use address arguments

- Record an exact sole-use role in the existing function analysis.
- Keep non-TLS global and frame addresses symbolic when their only consumer is
  a call argument.
- Emit the address directly into the ABI destination register.
- Add positive and negative PA29 reducers.

Complexity: one extension to the existing O(LowIR operands) function scan and
O(1) producer queries.

### VP2: serializable indexed memory operands

- Extend MIR memory operands with an optional index register and scale.
- Encode legal x86 base/index/scale/displacement forms directly.
- Retain a sole-use index expression until an immediately consuming load or
  store instead of emitting `imul`/`add` address temporaries.
- Preserve source register lifetimes until the memory consumer and retain the
  materialized path for unsupported scales, TLS, multiple uses, calls,
  control-flow edges, atomics, and bulk operations.

Complexity: O(1) work per producer and consumer after the existing use census;
encoding remains O(MIR instructions).

### VP3: destination-directed scalar placement

- Add exact sole-use roles for return, call argument, store, compare/branch,
  and ordinary use.
- Precolor a producer to its ABI destination only when intervening clobber and
  parallel-move constraints prove it safe.
- Preserve explicit copies when a value has another use, crosses a CFG edge,
  participates in a move cycle, or requires a width normalization.

Complexity: O(LowIR operands) analysis and O(1) lowering decisions.

### VP4: bounded constraint-aware allocation

- Reuse the existing definition, last-use, call, edge-liveness, and clobber
  facts rather than constructing another CFG or value graph.
- Use every caller-saved GPR whose fixed-register clobbers do not intersect a
  value interval.
- Choose a callee-saved register only when the interval crosses calls and its
  per-function save/restore cost is preferable to a spill.
- Rematerialize cheap immediates and addresses; reuse frame spill slots after
  intervals end.
- Use compact value IDs and fixed-size register state in the hot path where the
  existing string-keyed tables would otherwise be duplicated.

Complexity: O(n log n) for interval endpoints, or O(n) when source-order
definitions permit a cursor; active-register scans are bounded by the fixed
x86-64 register count.  Storage is O(values + instructions + CFG edges).

### VP5: retire superseded compatibility rewrites

- Remove baseline single-block copy/dead-definition preparation.
- Remove a narrow encoder forwarding rule only when the visible selector or
  allocator makes it unreachable and object measurements confirm no loss.
- Keep ordinary PA38 O1/O2 cleanup operating on the serialized MIR.

## Fixture policy

For every stage, record:

- every changed existing LowIR fixture;
- every changed strict or structural MIR fixture;
- whether the change is a new public MIR fact or an instruction-selection
  shape change;
- behavior and object-inspection results;
- reference behavior where the available reference implements the same public
  contract; and
- object size, instruction-family counts, compile time, and final decision.

PA29 owns O0 selection, ABI placement, allocation correctness, and the MIR
surface.  PA38 owns generic O1/O2 machine cleanup.  New correctness reducers
go into the earliest owning course suite.  If an adopted public shape differs
from the pinned reference, update the assignment contract and authoritative
reference before activating the structural fixture.  Behavior-only PA29 tests
retain informational reference MIR without grading its exact shape.

An intentional public MIR format addition requires a coordinated fixture
migration.  It is not permission to rebaseline an unrelated behavioral or
lowering difference.

## Validation and performance gates

For each stage:

1. Run the focused reducer and the complete owning PA report.
2. Run the affected PA29/PA31/PA37/PA38 report set to collect all failures.
3. Require a clean full `make test-report` before a retained commit.
4. Run the PA39 fatal file audit on every changed implementation path.
5. Compare deterministic frozen O0 objects and disassembly counts.
6. Use immutable, sequential ABBA compiler timing; reject a median user-time
   regression above 3% unless a larger measured end-to-end improvement
   justifies it.
7. Commit and push the isolated changeset before starting the next stage.

Run the expensive self-build and inception lanes after the complete retained
batch, not after every local change.  The final gate is a timed clean PA39 self
build, clean 8-way inception comparison, clean 32-way inception comparison,
full report, zero-fatal audit, and updated frozen/compiler-size evidence.

## Change ledger

| Stage | LowIR fixtures | MIR fixtures | Size/time result | Status |
| --- | ---: | ---: | --- | --- |
| VP0 | 0 | 135 raw MIR and 93 structural sidecars; exactly 223 call lines | Frozen object byte-identical at 4,498,880 bytes; one timing screen 6.29 s wall/5.71 s user; full report 5,188/5,188; audit zero fatal | landed in `43f17b58` |
| VP1 | 0 | 5 existing MIR fixtures plus one new PA29 structural fixture | Frozen object/text -11,272 bytes; x86 instructions -1,962, including 1,895 moves; three-block ABBA medians tied at 6.295 s wall and 5.720 s user; full report 5,189/5,189; audit zero fatal | landed in `9a7e9dee` |
| VP2 | 1 PA29 structural LowIR witness | 5 existing PA29 MIR fixtures; indexed operand syntax added to the scaffold/canonicalizer | Frozen object -4,656 bytes and text -4,288 bytes; x86 instructions -1,741, including 1,848 fewer `lea`, 33 fewer `imul`, 7 fewer `add`, 215 fewer `push`, and 218 fewer `pop`; paired user +0.09%, wall +0.56%, RSS +0.24%; full report 5,189/5,189; audit zero fatal | landed in `4b36cd90` |
| VP3 | 3 PA29 structural LowIR shape witnesses | 10 existing PA29 fixtures plus the PA38 call-address fixture at O1/O2; the call-result slice changes no existing fixture; fixed-home call forwarding changes 1 behavior-exact PA29 fixture; promoted-slot interval extension changes 2 strict and 1 behavior-exact PA29 fixtures; direct comparison returns change 8 exact and 17 structural report cases; direct unary returns change no existing fixture; direct integer-conversion returns change 4 exact and 1 structural report cases | Input-lifetime slice: frozen object -2,360 bytes, text -2,272 bytes, and 662 instructions. Placement slice: object -2,704 bytes, text -1,090 bytes, and 1,178 instructions. Direct call-result consumers: object -9,048 bytes, text -8,204 bytes, and 2,749 instructions; its paired medians improve user 0.71% and wall 0.85% with RSS +0.24%. Fixed-home forwarding: object -144 bytes, text -59 bytes, and 21 moves; paired user +0.27%, wall +0.73%, RSS +0.22%. Promoted-slot intervals: object -2,416 bytes, text -2,426 bytes, and 915 instructions; paired user +0.62%, wall -0.48%, RSS -0.09%. Dense slot analysis is object-identical and improves paired user 0.27%, wall 0.40%, and RSS 0.16%. Direct comparison returns: object -576 bytes, text -607 bytes, and 111 instructions/moves; paired user -1.32%, wall -1.04%, RSS tied. Direct unary returns: object/text -16 bytes and 4 instructions/moves; paired user -0.35%, wall -0.48%, RSS +0.63%. Direct integer-conversion returns: object -32 bytes, text -20 bytes, and 6 instructions/moves; paired user -1.05%, wall -0.40%, and RSS -0.21% | complete; VP4/VP5 cover the retained broader placement cases, while the measured broad integer-binary reshaping candidate remains deferred because it increased text size |
| VP4 | 5 course LowIR correctness/shape reducers | Typed/copy slice: 12 strict, 9 structural, and 3 course-exact PA29 fixtures plus 4 PA38 O1/O2 fixtures; direct constraints: 3 strict and 1 structural PA29 fixtures; parameter retention: 3 strict, 12 structural, and 1 behavior-exact PA29 fixture, with overlap; wide spill reuse changes 2 strict and 1 structural PA29 cases (4 MIR/CMIR files); scalar spill reuse changes 1 behavior-exact PA29 fixture and adds PA29 O0 and PA38 O2 course behavior reducers; frame-address placement changes 4 strict and 4 structural PA29 fixtures plus 3 PA38 O1/O2 fixtures | Caller-saved slice: frozen object -21,824 bytes, text -18,494 bytes, and 5,800 instructions. Typed-immediate/copy slice: object -24,688 bytes, text -23,682 bytes, MIR instructions -5,926, x86 instructions -4,533, moves -3,645, and spills 476 -> 318. Direct-constraint slice: object -160 bytes, text -155 bytes, 56 x86 instructions, and 55 moves. Intact-parameter slice: object -1,648 bytes, text -1,305 bytes, and 447 instructions. Its calm ABBA medians are user +0.09%, tied wall, and RSS +0.12%. Wide spill reuse is frozen-object-identical; paired user +0.18%, wall +0.08%, RSS -0.03%. Lifetime-keyed frame forwarding is also frozen-object-identical; paired user -0.70%, wall -0.24%, RSS -0.22%. Scalar reuse removes 80 object bytes and 74 text bytes with unchanged instruction counts; final paired user +0.45%, wall +0.53%, RSS +0.18%. Frame-address placement removes 2,472 object bytes, 2,507 text bytes, 1,341 MIR instructions, and 589 x86 instructions with neutral paired timing | complete: caller-saved pool, clobber-safe reuse, typed-immediate/address rematerialization, safe copy sharing, direct constraints, intact ABI-parameter retention, all scalar spill-home reuse, and frame-forwarding lifetime identity |
| VP5 | 4 PA29 structural placement witnesses plus existing course reducers | Existing migrations are enumerated in the phase narrative below; selected parameter demand changes 9 PA29 and 2 PA38 fixture cases; stable promoted homes, direct remainder returns, and the early call-result carrier change no checked fixture; logical large ALU immediates change 1 structural PA29 pair; unread selected homes change 1 strict PA29 fixture and 1 structural raw/canonical pair; compatibility retirement changes no fixtures | Compatibility telemetry falls from 959 operand rewrites / 874 dead definitions / 3 frame corrections to 0 / 0 / 0 before the pass and is then retired; the unread-home slice removes 2,288 object bytes, 2,428 text bytes, and 1,001 x86 instructions, while removing the pass is object-identical and improves paired user/wall time 1.54%/2.01% | complete |

The VP1 call-argument input predates the public MIR placement rule and is now
active supplemental structural coverage beside
`900-symbolic-global-call-argument`.  The VP2 scaled-index witness is likewise
active structural coverage under the updated authoritative PA29 reference.

The VP4 caller-saved slice changes strict PA29 MIR for `100-copyobj`,
`100-zeroinit`, `300-atomic-add-fetch`,
`300-atomic-compare-exchange-failure`,
`300-atomic-compare-exchange-success`, `300-atomic-exchange`,
`300-unsigned-compare-predicates`, `300-unsigned-int-ops`, and
`600-atomic-i32-exchange`. It changes the raw and canonical MIR pairs for
`300-integral-float-conversions`, `400-float-width-conversions`,
`500-mixed-gpr-xmm-call-abi`, `500-ptr-index-arithmetic`,
`600-indirect-mixed-gpr-xmm-call-abi`,
`600-ptr-compare-value-materialize`, and
`700-call-setup-forwarding-no-preserve`. These cases use an unoccupied RDI or
RSI for a short-lived result and remove unnecessary callee-save and frame
traffic.

`caller-saved-binary-reuse-clobber.t` and
`caller-saved-index-reuse-clobber.t` are PA29 behavior reducers for the fixed
register hazard exposed by the wider pool. Each forces a short-lived value
into a caller-saved argument register, lengthens its interval through result
reuse, and crosses `copyobj`; the reference and current compiler agree on the
program result. Reuse now queries the already-computed per-register clobber
mask for the result interval in O(1) time.

The typed-immediate and scalar-copy slice changes strict PA29 MIR for
`100-direct-call-branch`, `100-startup-shutdown-hooks`,
`100-switch-terminator`, `200-indirect-call-six-register-args`,
`300-atomic-add-fetch`, `300-atomic-compare-exchange-failure`,
`300-atomic-compare-exchange-success`, `300-atomic-exchange`,
`300-atomic-load-store`, `600-atomic-i32-exchange`,
`600-atomic-i32-seqcst-store`, and
`600-thread-local-direct-native-runtime`. It changes the raw and canonical
PA29 structural pairs for `200-stack-arguments-beyond-six`,
`400-call-clobber-register-pressure`, `400-i64-leaf-register-chain`,
`500-const-ptr-null-direct-compare-branch`,
`600-atomic-i8-load-store`, `800-conditional-edge-liveness`,
`800-forwarded-param-identity-live-across-call`,
`800-runtime-zero-only-global-pointer-alignment`, and
`800-switch-call-case-liveness`. The course-exact
`fallthrough-jump-encoding` and `immediate-move-encoding-boundaries` fixtures
also change. PA38's O1 and O2 raw/canonical fixtures change for
`100-call-argument-immediate-rematerialize` and
`200-cross-block-copy-liveness` because PA29 now presents the already-placed
value to the optimizer.

`scalar-copy-location-sharing.t` is the active PA29 shape reducer. The course
reference and current compiler both materialize the constant once in the
arithmetic destination and emit no machine move for the same-type copy. The
current fixture additionally carries the public exact call-argument annotation
introduced by VP0. Mutable slot/global values and incoming parameter registers
are excluded from sharing; register aliases query the copied result interval's
fixed clobber mask in O(1) time. Temporary frame homes are identified through
the existing per-value spill-home table rather than a second frame index.

The direct-constraint slice changes strict PA29 MIR for
`300-unsigned-int-ops`, `600-readonly-global-extra-section-runtime`, and
`600-thread-local-direct-native-runtime`, plus the canonical structural
oracle for `800-runtime-zero-only-global-pointer-alignment`. Integer ALU
constants remain logical immediate operands, with any unencodable x86
immediate materialized only by native emission, and variable shift counts go
straight to the required `rcx` carrier. The existing fixtures already cover
both shapes, so no duplicate reducer was added. The older course reference
materializes the extra scratch moves; this intentional public MIR selection
difference is confined to those four listed oracles. Against `ad4bb3b0`, the
frozen object falls from 4,431,376 to 4,431,216 bytes and aggregate `.text*`
from 944,752 to 944,597 bytes. Two ABBA blocks give 5.620 versus 5.615 seconds
median user time, 6.185 versus 6.205 seconds wall, and 364,738 versus 364,646
KiB peak RSS for baseline versus candidate.

The intact-parameter slice changes strict PA29 MIR for
`100-object-abi-lowered`, `600-call-pass-mode-address-materialization`, and
`600-call-pass-mode-register-temp-address-materialization`; raw and canonical
structural MIR for `400-call-clobber-register-pressure`,
`500-mixed-gpr-xmm-call-abi`, `600-floating-short-circuit-branch`,
`600-indirect-mixed-gpr-xmm-call-abi`,
`700-call-pass-mode-address-materialization`,
`700-call-setup-forwarding-no-preserve`, `800-conditional-edge-liveness`,
`800-forwarded-param-identity-live-across-call`,
`800-slot-address-rematerialization`, `800-switch-call-case-liveness`,
`800-xmm-live-across-integer-call`, and
`900-symbolic-global-call-argument`; and the exact MIR sidecar for behavior
case `800-register-param-r8-home-clobber`. These existing fixtures already
exercise direct arithmetic, load, store, address, call, branch, switch, and
mixed GPR/XMM consumers, so no duplicate reducer was added. The older course
reference eagerly moves these parameters; its disagreement is limited to the
listed MIR shapes, while program behavior agrees.

The selector now retains an incoming scalar parameter only after the existing
linear interval analysis proves that its ABI register crosses no fixed
clobber. It reserves the retained register in the existing fixed-size register
state and performs no new scan or value graph construction. Against
`4b4efbd5`, the frozen object falls from 4,431,216 to 4,429,568 bytes,
aggregate `.text*` from 944,597 to 943,292 bytes, and x86 instructions from
235,164 to 234,717. The movement includes 144 fewer pushes and 311 fewer pops;
small local allocation changes add 35 moves and 21 `lea` instructions. Two
calm ABBA blocks give baseline/candidate medians of 5.670/5.675 seconds user,
6.260/6.260 seconds wall, and 364,608/365,048 KiB peak RSS.

The wide spill-home slice reuses an expired compiler temporary only within an
exact storage-size/alignment class. Twenty-five fixed buckets cover the five
power-of-two sizes and alignments through 16 bytes; each bucket is a binary
heap ordered by interval end. Acquiring or returning a home is O(log H), and
the allocator compares the earliest end with the current source position.
It reuses only i128 and f80 homes in this slice. These types cannot enter the
scalar-copy location-sharing path, so no copied-value string set or parallel
alias analysis is needed.

Strict PA29 MIR changes for `200-f80-direct-call` and
`800-atomic-i128-compare-exchange`; raw and canonical structural MIR change
for `300-integral-float-conversions`. The first and third cases reduce their
frames by 16 bytes. The atomic case reduces its frame from 96 to 64 bytes by
sharing four non-overlapping i128 values across two homes. Existing fixtures
cover f80 conversion/calls and repeated i128 atomic lifetimes, so no duplicate
shape-only reducer was added. The older course reference gives each temporary
a distinct offset.

The frozen input contains no reusable wide home: its object remains
byte-identical to `d988c874` at 4,417,272 bytes with SHA-256
`0498a0bd5ea7024e581e36f156f15e082bb8b064cfc4d04e3ddce101a378020e`.
Two ABBA blocks give baseline/candidate medians of 5.635/5.645 seconds user,
6.200/6.205 seconds wall, and 364,996/364,872 KiB peak RSS, all neutral.

An unrestricted scalar prototype was rejected for this slice. Reused offsets
combined independent lifetimes in the encoder's offset-keyed single-use reload
table, disabling existing frame forwarding and growing the frozen object by
24,232 bytes and aggregate `.text*` by 24,155 bytes. Scalar reuse therefore
remains pending until frame forwarding is keyed by instruction lifetime rather
than treating an offset as a permanent value identity.

The frame-forwarding prerequisite replaces per-instruction offset lookups with
two-byte actions indexed by block and MIR instruction. The first version tried
to divide a reused offset at each defining store. That assumption is sufficient
only while emitted block order matches LowIR definition order: PA38 O2 trace
layout can place a later lifetime's block before the earlier lifetime's block.

The completed version gives each compiler-created frame binding a compact
one-based `uint32_t` ordinal and carries that identity on typed frame operands.
The field occupies existing operand padding. Forwarding facts are one dense
vector indexed by that ordinal; one numeric offset index remains only for
unique unannotated homes. Native emission performs one dense indexed action
read. The analysis remains one MIR scan plus at most five bounded
intervening-instruction checks per candidate, with O(bindings) dense facts and
O(MIR) compact action storage only when a forwarding action exists.

Scalar reuse remains disabled in this prerequisite, so no fixture changes.
The frozen object is byte-identical to `a80ca2dd` at 4,417,272 bytes with
SHA-256 `0498a0bd5ea7024e581e36f156f15e082bb8b064cfc4d04e3ddce101a378020e`.
A sequential ABBA block gives baseline/candidate medians of 5.685/5.645
seconds user, 6.260/6.245 seconds wall, and 365,752/364,944 KiB peak RSS.
The through-PA29 report passes 4,108/4,108, the full report passes
5,192/5,192, and the PA39 file audit has zero fatal findings.

The scalar spill-home slice enables the same exact size/alignment pool for all
non-object temporaries. A reverse walk over scalar copy edges computes only the
source homes whose storage lifetime must extend beyond the source value's own
last use. This makes immutable frame-location sharing safe without updating a
heap by string identity during lowering. The copy walk is O(instructions), its
sparse extension table is O(shared-copy sources), and allocation adds one
lookup before its existing O(log H) fixed-bucket operation. Source slots,
parameter slots, and object homes remain outside the pool.

PA29 behavior-exact fixture `800-reactive-spill-bulk-storage` now reuses the
expired `%src` pointer home for `%ignored`; its stack allocation remains
alignment-equivalent, but the binding and accesses move from `rbp-128` to
`rbp-112`. Course behavior reducer `scalar-spill-home-copy-lifetime` forces a
spilled call result and its copied alias to remain live while a second result
needs a home. Prematurely reusing the first offset changes the program result.
The course reference passes the behavior but allocates a separate `%saved`
home, so the active reducer deliberately carries no exact MIR oracle.

PA38 O2 behavior reducer `300-reused-frame-home-layout-lifetime` covers the
trace-layout hazard: two disjoint values share a physical home, O2 places the
later value's block first, and a call separates that value's defining store
from its reload. Treating physical offset or emitted store order as value
identity reads the earlier value. The reference chooses a different valid
spill layout, so PA38 now uses its behavior-only lane for this correctness
case rather than imposing an implementation-specific MIR shape. The same bug
was originally reduced from PA36's hosted string-buffer link smoke test.

The scalar home is stored directly in `ValueFact`; there is no parallel
string-keyed spill-home map. Copy-lifetime analysis stays sparse and
out-of-band, while lowering and forwarding use the compact binding identity.

On the frozen input the pool creates 1,100 temporary homes and reuses them for
5,750 later bindings; 2,186 copy sources require an extended shared-storage
lifetime. Against `ea49445d`, the object falls from 4,417,272 to 4,417,192
bytes and aggregate `.text*` from 931,936 to 931,862 bytes. X86 instructions
remain 230,905 and moves, `lea`, pushes, and pops are unchanged; the text
reduction comes from shorter frame displacements. The final two-block
sequential ABBA gives baseline/candidate medians of 5.630/5.655 seconds user,
6.180/6.205 seconds wall, and 364,728/365,212 KiB peak RSS. Paired deltas are
+0.45% user, +0.53% wall, and +0.18% RSS. PA29 plus PA36 pass 299/299, PA38
passes 27/27, the full report passes 5,194/5,194, and the PA39 file audit has
zero fatal findings.

The first clean self-host gate after this slice exposed an earlier VP2 encoder
contract error. The MIR correctly represented `lea base+index; load`, and the
dead-setup fold retained the indexed address fact, but its emitter always used
the base-only load encoder. The resulting self compiler treated every simple
post-token as invalid once O1 LowIR exposed the indexed lookup shape. The fix
emits the indexed load directly, preserving the eliminated `lea` and its size
benefit. Reference-backed PA38 behavior cases cover both O1 and O2 without
requiring an exact MIR layout. The frozen O0 object remains 4,417,192 bytes
with 931,862 aggregate text bytes; a quiet confirmation run took 5.78 seconds
user and 6.37 seconds wall at 365,104 KiB peak RSS. The full report passes
5,196/5,196 and the PA39 file audit remains zero-fatal.

The logical-bulk-operand slice changes strict PA29 MIR for `100-copyobj`,
`100-zeroinit`, and `200-pass-by-value-lvalue`. `copy_bytes` and `zero_bytes`
now retain the selected logical address registers in serialized MIR; the
encoder alone performs the parallel setup required by x86 string operations.
The original assignment scaffold exposed the opcodes but did not specify the
operand-placement rule, and its three fixtures eagerly used RDI/RSI. The
student README and scaffold now state only the current rule; this migration
history remains in this maintainer ledger.

Against `5524be37`, the frozen object changes from 4,429,568 to 4,429,536
bytes while aggregate `.text*` remains 943,292 bytes. The individual function
layouts are reshaped by different legal parallel-move orders, but the totals
remain 234,717 x86 instructions, 98,327 moves, 26,788 `lea` instructions,
8,562 pushes, and 13,052 pops. Two ABBA blocks give baseline/candidate medians
of 5.665/5.665 seconds user, 6.245/6.230 seconds wall, and 365,076/364,652 KiB
peak RSS. Existing exact fixtures cover both copy and zero forms, so no
duplicate reducer was added.

The logical-scalar-return slice lets `ret` retain an already selected GPR.
The encoder performs the final transfer to RAX, just as it already does for a
non-RAX explicit return operand. Immediate, memory, and symbolic results still
materialize in RAX because `ret` accepts a register operand only. Adjacent
sole-use producers still select RAX directly under the VP3 rule; this slice
removes a separately serialized ABI-only move from other returns.

The migration changes strict PA29 MIR for `100-copyobj`,
`100-direct-call-branch`, `100-object-abi-lowered`, `100-zeroinit`,
`200-class-constructor-member-init`, `200-class-template-field`,
`200-indirect-call-six-register-args`, `200-non-type-class-specialization`,
`200-pass-by-value-lvalue`, `300-atomic-compare-exchange-failure`,
`300-atomic-compare-exchange-success`, `300-atomic-exchange`,
`300-unsigned-compare-predicates`, `300-unsigned-int-ops`,
`600-atomic-i32-exchange`, `600-readonly-global-extra-section-runtime`,
`600-thread-local-direct-native-runtime`, and
`800-atomic-i128-compare-exchange`. It changes raw and canonical structural
MIR for `200-stack-arguments-beyond-six`,
`300-integral-float-conversions`, `400-call-clobber-register-pressure`,
`400-float-width-conversions`, `400-i64-leaf-register-chain`,
`400-u32-compare-value-materialize`, `500-f64-compare-value-materialize`,
`500-i16-leaf-normalize-chain`, `500-mixed-gpr-xmm-call-abi`,
`500-ptr-index-arithmetic`, `600-indirect-mixed-gpr-xmm-call-abi`,
`600-ptr-compare-value-materialize`,
`700-call-setup-forwarding-no-preserve`, `800-conditional-edge-liveness`,
`800-forwarded-param-identity-live-across-call`,
`800-slot-address-rematerialization`, `800-switch-call-case-liveness`,
`800-xmm-live-across-integer-call`, and
`900-symbolic-global-call-argument`. Behavior-exact cases
`800-reactive-spill-bulk-storage`, `800-reactive-spill-signed-div`, and
`800-register-param-r8-home-clobber`, plus course-exact case
`scalar-copy-location-sharing`, also change. Every difference removes
`mov rax, rN` and changes the following `ret rax` to `ret rN`; existing cases
cover the contract, so no duplicate reducer was added. The older course
reference retains the explicit move and therefore does not define the new
shape.

On the frozen input, hidden baseline preparation falls from 1,314 to 959
operand rewrites and from 1,228 to 873 dead-definition removals. In particular,
MOV-to-return forwarding falls from 515 to 173 cases and MOV-to-MOV forwarding
from 508 to 495. Against `3bbffaa2`, the object falls from 4,417,304 to
4,417,272 bytes, aggregate `.text*` from 931,960 to 931,936 bytes, and x86
instructions from 230,911 to 230,905; all six removed instructions are moves.
`.eh_frame` and `.gcc_except_table` remain unchanged. Two ABBA blocks give
baseline/candidate medians of 5.635/5.585 seconds user, 6.195/6.155 seconds
wall, and 364,470/365,168 KiB peak RSS. All four objects in each arm are
byte-identical.

The direct call-result-consumer slice keeps a sole-use scalar result in RAX
when the immediately following instruction passes it as a direct-value call
argument or stores it. The existing use census proves the sole use, and the
selector inspects only the following instruction and its argument list. It
does not build another value graph or rescan the function. Indirect-result and
other address-requiring arguments retain their frame home.

No existing PA29 fixture exercises this exact placement shape.
`cppgm.tests/course/pa29/structural/direct-call-result-consumers.t` covers both consumers and runs
successfully under the current and course-reference compilers. The older
reference forwarded RAX to the consumer but retained dead intermediate result
copies; the updated authoritative reference adopts the public placement rule,
and the case is active structural course coverage. The student README and
scaffold state only the required placement rule.

Against `59e7e175`, the frozen object falls from 4,429,536 to 4,420,488 bytes,
aggregate `.text*` from 943,292 to 935,088 bytes, `.eh_frame` from 138,804 to
138,180 bytes, and `.gcc_except_table` from 46,382 to 46,359 bytes. X86
instructions fall from 234,717 to 231,968, including 2,624 fewer moves, 9
fewer `lea` instructions, 177 fewer pushes, and 376 fewer pops. The hidden
single-block preparation has 241 fewer register-copy rewrites and 233 fewer
dead register-copy definitions to remove. Two ABBA blocks give
baseline/candidate medians of 5.625/5.605 seconds user, 6.215/6.145 seconds
wall, and 364,674/365,418 KiB peak RSS.

The fixed-home call-forwarding slice changes only the exact MIR sidecar for
PA29 behavior case `800-register-param-r8-home-clobber`. A promoted parameter
slot load used only as a call argument now lets call setup read the parameter's
already-selected fixed home rather than also creating an unconsumed R9 copy.
The existing fixed-home flag, call-only use fact, and cross-call query make the
decision O(1). The course reference executes the case successfully but retains
the dead R9 copy; that historical disagreement is recorded here rather than in
student documentation. The existing case already covers the clobber hazard, so
no duplicate reducer was added.

Against `12307448`, the frozen object falls from 4,420,488 to 4,420,344 bytes
and aggregate `.text*` from 935,088 to 935,029 bytes. The instruction and move
counts both fall by 21; `.eh_frame`, `.gcc_except_table`, `lea`, push, and pop
counts are unchanged. Two ABBA blocks give baseline/candidate medians of
5.575/5.585 seconds user, 6.130/6.170 seconds wall, and 364,630/365,256 KiB
peak RSS, all within the neutral gate.

The promoted-slot interval slice changes strict PA29 MIR for
`200-class-constructor-member-init` and `200-trivial-param-slot-promotion`,
plus the exact MIR sidecar for behavior case
`800-register-param-r8-home-clobber`. A scalar parameter's register interval
now includes clobbers between its promoted slot store and load as well as the
loaded value's remaining interval. When that complete interval is intact, the
slot load retains the incoming ABI register instead of first copying the
parameter into R9. The existing strict cases cover the positive placement and
`promoted-rsi-after-object-copy.t` remains the negative clobber reducer, so no
duplicate test was added.

The storage analysis records the result as one compact mask per parameter
ordinal and tracks physical-register history in a fixed 16-entry array. Native
lowering performs one indexed mask test: it adds no string-keyed fact, value
graph, or rescan to the hot path. This follows the `spec.md` requirements for
compact identities, bounded per-function machine IR, and linear instruction
selection. The serialized MIR migration is limited to the three listed
fixtures; student documentation specifies only the current interval rule.

Against `f0698079`, the frozen object falls from 4,420,344 to 4,417,928 bytes,
aggregate `.text*` from 935,029 to 932,603 bytes, and x86 instructions from
231,947 to 231,032. This includes 691 fewer moves, 89 fewer pushes, and 89
fewer pops; `.eh_frame` falls by 48 bytes and `.gcc_except_table` is unchanged.
Two ABBA blocks give baseline/candidate medians of 5.660/5.695 seconds user,
6.280/6.250 seconds wall, and 364,406/364,094 KiB peak RSS. All changes remain
inside the neutral compile-time gate.

The follow-up storage-analysis representation slice replaces the transient
`written_slots`, `observed_slots`, and `seen_object_slots` string sets with one
byte-flags vector indexed by function slot ordinal. Scalar promotion state is
also ordinal-indexed. Each slot operand spelling is resolved once through one
function-local index; all classification and state updates after that lookup
are dense array operations. This removes repeated string copies, hashes, and
node allocations while retaining the existing string-keyed output facts that
the current native selector consumes.

The frozen object remains byte-identical at 4,417,928 bytes with SHA-256
`f4578b97f4fdb8d710b6eac77a5828a0408c594337ddf7e4a6131d4ed3757614`.
Two ABBA blocks give baseline/candidate medians of 5.660/5.645 seconds user,
6.220/6.195 seconds wall, and 365,856/365,274 KiB peak RSS.

The representation audit also found that the front-end PA15 LowIR already
uses compact `uint32_t` parameter, slot, temporary, block, and symbol IDs, but
`AdaptTypedLowIRForNative` expands those identities into owning strings in a
second backend-facing model. This is an in-memory adapter rather than a text
round trip, yet it leaves optimizers and native lowering hashing presentation
names. A complete repair must preserve compact identities for source input,
assign them once for explicit textual LowIR and decoded binary objects, and
make rendering the only spelling consumer. That cross-pipeline migration is
deferred from this O0 placement slice because it spans PA15 lowering, PA30
adaptation/object decoding, PA37/PA38 optimization, and PA29 selection; it must
not be approximated by parallel ID and string models that can diverge.  The
cross-pipeline work, complete representation audit, and performance gates are
specified in `PLAN-LOWIR-COMPACT-IDENTITY.md`.

The direct comparison-return slice extends the existing PA29 rule for
immediately returned scalar producers to ordinary integer comparisons. It
selects RAX only when the comparison result has one adjacent return use and
placing the left operand there cannot overwrite an RAX-based right operand.
The already-computed use count plus the adjacent instruction check make this
an O(1) lowering decision; it adds no analysis or pass.

The migration changes strict PA29 MIR for `300-atomic-add-fetch`,
`300-atomic-load-store`, `400-u32-bswap-and-float-conversions`,
`600-atomic-i32-seqcst-store`,
`600-call-pass-mode-address-materialization`,
`600-call-pass-mode-register-temp-address-materialization`, and
`900-slot-address-stack-call-argument`, plus the exact behavior sidecar for
`800-call-i128-literal-abi-chunks`. It changes raw and canonical structural
MIR for PA29 `500-mixed-gpr-xmm-call-abi`,
`600-atomic-i8-load-store`, `600-i8-signed-frame-load-widen`,
`600-indirect-mixed-gpr-xmm-call-abi`,
`600-u16-zero-frame-load-widen`,
`700-call-pass-mode-address-materialization`,
`700-call-setup-forwarding-no-preserve`,
`700-f64-f80-implicit-store-return-convert`,
`700-i8-signed-global-load-widen`, `700-index-chain-register-reuse`, and
`700-u16-zero-global-load-widen`. PA38's O1 and O2 raw/canonical fixtures also
change for `100-frame-address-fold`, `200-cross-block-copy-liveness`, and
course case `300-call-result-across-eh-push` because the input PA29 MIR already
places the result in RAX.

The comparison was added to `cppgm.tests/course/pa29/structural/direct-return-placement.t`. Both
compilers execute the expanded witness successfully. The older course
reference retained a temporary comparison register and final return copy; the
updated authoritative reference adopts the public shape, and the witness is
active structural course coverage.
Against `51a7243c`, the frozen object falls from 4,417,928 to 4,417,352 bytes,
aggregate `.text*` from 932,603 to 931,996 bytes, and x86 instructions from
231,032 to 230,921; all 111 removed instructions are moves. Two ABBA blocks
give baseline/candidate medians of 5.665/5.590 seconds user, 6.230/6.165
seconds wall, and 364,872/364,868 KiB peak RSS.

An immediately returned integer-binary-result prototype was rejected. It
would change 14 exact PA29 cases and 17 structural PA29/PA38 cases (48 MIR
files after raw/canonical pairs), but against `1e6cb571` the frozen object grew
from 4,417,352 to 4,417,376 bytes and aggregate `.text*` grew from 931,996 to
932,005 bytes. The reshaping removed 20 moves and 10 net instructions, plus
one push/pop pair, but wider encodings erased the nominal instruction benefit.
This candidate remains deferred rather than imposing a broad fixture migration
for a nine-byte text regression. The affected exact cases were PA29
`100-copyobj`, `100-object-abi-lowered`, `100-zeroinit`,
`200-indirect-call-six-register-args`,
`300-atomic-compare-exchange-failure`,
`300-atomic-compare-exchange-success`, `300-atomic-exchange`,
`300-unsigned-compare-predicates`, `300-unsigned-int-ops`,
`600-atomic-i32-exchange`, `800-atomic-i128-compare-exchange`, behavior cases
`800-reactive-spill-bulk-storage` and `800-reactive-spill-signed-div`, and
course case `scalar-copy-location-sharing`. The structural cases were PA29
`200-stack-arguments-beyond-six`, `300-integral-float-conversions`,
`400-call-clobber-register-pressure`, `400-float-width-conversions`,
`400-i64-leaf-register-chain`, `400-u32-compare-value-materialize`,
`500-f64-compare-value-materialize`, `500-mixed-gpr-xmm-call-abi`,
`500-ptr-index-arithmetic`, `600-indirect-mixed-gpr-xmm-call-abi`,
`600-ptr-compare-value-materialize`,
`700-call-setup-forwarding-no-preserve`,
`800-slot-address-rematerialization`, `800-xmm-live-across-integer-call`,
`900-symbolic-global-call-argument`, and PA38 O1/O2
`100-return-copy-coalesce`.

The direct unary-return slice applies the same rule to integer negation,
bitwise complement, byte swap, and logical not. The operation has a sole
adjacent return use, so moving its one input to RAX before the operation cannot
overwrite another input. The existing adjacency/use query is O(1); no new
analysis state is retained. No checked-in report fixture contains this exact
shape. The negation case was therefore added to the existing
`cppgm.tests/course/pa29/structural/direct-return-placement.t`; both compilers execute it, while the
course reference retains an intermediate register and return copy.

Against `1e6cb571`, the frozen object falls from 4,417,352 to 4,417,336 bytes,
aggregate `.text*` from 931,996 to 931,980 bytes, and x86 instructions from
230,921 to 230,917; all four removed instructions are moves. Two ABBA blocks
give baseline/candidate medians of 5.660/5.640 seconds user, 6.240/6.210
seconds wall, and 363,642/365,918 KiB peak RSS. The RSS movement is 0.63% and
inside the neutral gate.

The direct integer-conversion-return slice applies the same adjacent sole-use
rule to integer and pointer conversions that produce an integer or pointer.
It selects RAX before emitting the conversion and otherwise retains the old
allocation and pressure-home path. The decision reuses the existing use count
and next-instruction check, so it is O(1) and adds no string state or scan.

Strict or behavior-exact MIR changes for PA29
`800-reactive-spill-bulk-storage`, `800-reactive-spill-signed-div`,
`800-register-param-r8-home-clobber`, and course case
`copied-compare-result-across-call`. Raw and canonical structural MIR changes
for PA29 `800-xmm-live-across-integer-call`. Each migration replaces a
conversion destination temporary and final return copy with direct RAX
placement. The truncation case was added to
`cppgm.tests/course/pa29/structural/direct-return-placement.t`; both compilers execute it, while
the course reference retains the intermediate register and copy.

Against `31420a14`, the frozen object falls from 4,417,336 to 4,417,304 bytes,
aggregate `.text*` from 931,980 to 931,960 bytes, and x86 instructions from
230,917 to 230,911; all six removed instructions are moves. Two ABBA blocks
give baseline/candidate medians of 5.715/5.655 seconds user, 6.270/6.245
seconds wall, and 365,896/365,110 KiB peak RSS. The first baseline wall sample
was loaded at 8.36 seconds; the four-run median keeps that outlier from
determining the result.

The VP3 input-lifetime slice changes
`pa29/tests/strict/100-object-abi-lowered.ref.mir`.  The address-selection
slice changes the PA29 structural `500-ptr-diff-switch`,
`500-ptr-index-arithmetic`, `600-ptr-compare-value-materialize`, and
`800-loop-slot-compare-reload` MIR/CMIR pairs: each removes a base-register
copy and forms the address from the original base in one `lea`.  Direct return
placement changes the PA29 strict `100-ret0`, `100-structured-global-data`,
`200-pass-by-value-lvalue`, `200-pcrel-global-data-load`, and
`300-atomic-seq-cst-fence` MIR files.  It also changes the PA38
`100-call-address-cleanup` MIR/CMIR pair at both O1 and O2 because the PA29
load already targets the return register before machine optimization.

`cppgm.tests/course/pa29/structural/index-address-placement.t` and
`cppgm.tests/course/pa29/structural/direct-return-placement.t` execute successfully with both
compilers.  The updated authoritative reference adopts the public placement
rule, so both are active structural course tests.

The frame-home reuse follow-up fixes a correctness hole in loop interval
construction.  A nested loop body may be serialized after an outer loop's
textual backedge; treating that backedge block as the outer interval's end let
the later body reuse storage still needed by the next outer iteration.  The
analysis now merges overlapping source-order backedge intervals before
extending invariant lifetimes.  It visits every block and backedge a constant
number of times, adds only two dense block-indexed vectors, and adds no
string-keyed hot-path state or fixed-point rescan.

`cppgm.tests/course/pa29/noncontiguous-loop-frame-home-lifetime.t` is the
active behavior reducer.  Before the fix `%original` and `%replacement`
shared one frame offset and the program returned 1; afterward they have
separate homes and the program returns 0.  The course reference also returns
0.  No pre-existing PA29 MIR fixture changes.

The compact-identity prerequisite is complete in
`PLAN-LOWIR-COMPACT-IDENTITY.md`.  The resumed frozen baseline remains exactly
4,417,192 bytes with SHA-256
`98f77be4b76e5f097be61797fa6559d80266f1e2bb096ac76328b3aabc731283`.
An explicit `--stats` measurement now accounts for the O0 single-block
compatibility path that previously sat outside both lowering and encoding
telemetry.  It prepares 2,684 functions and reduces 19,968 input instructions
to 19,094 output instructions through 959 operand substitutions, 874 dead
definitions, and three frame corrections.  The preparation itself takes about
5.6 ms on the frozen compile.

This establishes two separate goals for the remaining VP5 work.  Direct
placement should eliminate the common substitutions and dead definitions so
serialized MIR describes the native program; deleting the bounded pass itself
will not be presented as a major compile-time optimization.  Each placement
slice must reduce the measured residual without replacing it with another
scan, map, or hidden representation.  The full report passes 5,204/5,204,
the file audit has zero fatal findings, and telemetry does not change the
frozen object.

The zero-index identity slice treats `index` with a computed displacement of
zero as the unchanged pointer value.  A register location is shared only when
the destination's complete interval crosses no fixed clobber.  Destructive
reuse now also rejects a register with another live value alias, making the
shared location safe without a second value graph or scan.  The decision uses
the existing dense use/clobber facts and the fixed per-register live-location
index, so it is O(1) in the hot path.

The first prototype exposed why alias-aware reuse is required:
`compact-memory-displacement-boundaries.t` kept the zero-derived address live
while a later nonzero index destructively reused the base register.  That
existing PA29 behavior test and the course reference both require the original
zero address to remain valid.  The corrected implementation passes it and
changes only the raw/canonical structural pair for
`600-floating-short-circuit-branch`, which now loads directly through the
unchanged incoming pointer.  The student README and MIR scaffold describe the
current zero-index placement rule directly.

On the frozen compile, MIR instructions fall from 214,493 to 213,458.  The
single-block compatibility residual falls from 959 to 342 operand rewrites and
from 874 to 250 dead definitions.  The object falls from 4,417,192 to
4,416,360 bytes, aggregate `.text*` from 931,862 to 930,659 bytes, and x86
instructions from 230,909 to 230,469, including 400 fewer moves.  Three A/B/B/A
blocks against `a22321c5` give baseline/candidate medians of 4.855/4.865
seconds user, 5.335/5.335 seconds wall, and 364,972/364,844 KiB peak RSS.
Paired medians are -0.20% user, -0.28% wall, and -0.06% RSS, all neutral.

The direct comparison-return slice keeps the compared values in their selected
source locations and reserves RAX only for the Boolean written by `setcc`.
The selector reuses the existing adjacent-return predicate and resolves each
operand once.  It adds no analysis state, collection, or pass; the additional
work is O(1) at the comparison already being lowered.  Immediate right operands
also remain immediate when the target encoding accepts them.

The public PA29 contract and typed scaffold now state that distinction.  The
change updates 19 existing PA29 cases and 6 downstream PA38 cases, or 42 raw
and canonical MIR files.  Every migration removes only the input-to-RAX copy
and, when applicable, an encodable immediate's scratch materialization.  The
existing fixtures already cover integer widths, pointer values, atomics,
mixed floating/GPR calls, frame/global loads, and PA38 O1/O2 cleanup, so a
duplicate reducer was not added.

On the frozen compile, raw MIR falls from 213,458 to 213,309 instructions.  The
single-block compatibility residual falls from 342 to 269 operand rewrites and
from 250 to 177 dead definitions.  The object falls from 4,416,360 to 4,416,056
bytes and aggregate `.text*` from 930,659 to 930,442 bytes.  Three A/B/B/A
blocks give baseline/candidate medians of 4.900/4.910 seconds user,
5.375/5.400 seconds wall, and 364,004/364,114 KiB peak RSS.  Paired medians are
+0.71% user, +0.55% wall, and -0.12% RSS, all neutral.  The affected report
passes 368/368, the through-PA29 report passes 4,116/4,116, the full report
passes 5,204/5,204, and the PA39 file audit has zero fatal findings.  The
comparison-specific logic is separated into `lowir_native_compare_lowering.h`,
leaving the main lowering owner below the fatal file-size limit.

The direct early-parameter slice distinguishes an incoming ABI location from
the selected home that may be needed after a later clobber.  A use reads the
incoming register only when fixed-register analysis, parameter setup, earlier
selected definitions, and the active call-setup sequence prove that register
is still intact.  Promoted parameter-slot loads retain the originating compact
`ValueId`, so the same O(1) query applies without a name lookup or value scan.
The lowerer stores setup clobbers and first selected definitions in fixed
16-register tables.

The first prototype exposed two existing correctness boundaries.  Reusing an
incoming register for a source-order loop invariant after a call broke
`900-loop-limit-parameter-survives-call`; loop-invariant values now keep the
stable selected home.  Extended calls that copy a stack object use RDI and RSI
as setup scratch before ordinary register arguments are placed, so
`extended-stack-object-register-source-clobber` requires those active scratch
clobbers to suppress incoming-register reads.  Both are existing PA29 behavior
reducers and need no duplicate fixture.  The intentional public shape change
is covered by `200-indirect-call-six-register-args`,
`200-stack-arguments-beyond-six`, and `800-switch-call-case-liveness`: their
early uses read intact RDI/RSI/RDX/RCX/R8 locations, while later post-call uses
still read their fixed homes.

Against `24e9f2c6`, frozen raw MIR falls from 213,309 to 213,294 instructions,
and single-block preparation falls from 269 to 260 operand rewrites while its
177 dead definitions and three frame corrections remain unchanged.  The
object falls from 4,416,056 to 4,416,024 bytes and aggregate `.text*` from
930,442 to 930,392 bytes.  Three A/B/B/A blocks give baseline/candidate
medians of 4.895/4.950 seconds user, 5.370/5.400 seconds wall, and
364,462/364,266 KiB peak RSS.  Paired medians are +0.82% user, +0.93% wall,
and +0.03% RSS, all neutral.  The affected report passes 368/368, the PA29
suite passes 224/224, the through-PA29 report passes 4,116/4,116, the full
report passes 5,204/5,204, and the PA39 file audit has zero fatal findings.
Parameter-register state is separated into
`lowir_native_parameter_lowering.h`, leaving the main lowering owner at 2,996
lines.

At commit `b8d70f5b`, a clean 32-way self build takes 18.59 seconds wall,
406.76 seconds aggregate user time, and 244,152 KiB peak RSS.  With no
inception object tree present, the separate 32-way inception compare takes
1:50.13 wall, 2,921.83 seconds aggregate user time, and 233,580 KiB peak RSS.
All 163 compared objects match.  The self and inception binaries are
byte-identical at 16,745,840 bytes with SHA-256
`8b11823875d402227b221efa70789c457ff486258fc56e44caeb9f725048cfbd`.

The call-result carrier slice keeps a scalar result in RAX for its complete
single-block interval when the existing dense clobber facts prove that no
intervening operation overwrites RAX.  The final-use instruction is obtained
directly from its recorded position; there is no use scan.  A store-address
final use is excluded because scalar-store lowering may materialize the stored
value in RAX before consuming the address.  Other supported consumers include
ordinary arithmetic, comparison, return, conversion, bulk-copy operands, and
call setup.  The implementation adds no per-value state and performs O(1)
work once per scalar call result.

The broad prototype made
`800-cross-call-param-shadow-home-freed-once` dereference the integer being
stored as an address: `%result` remained in RAX, then `store i64 77, %result`
materialized 77 in RAX before the store.  That existing PA29 behavior reducer
returned to success after the final-use role was checked.  The six structural,
three behavior-exact, and one course-exact migrated cases cover arithmetic,
direct and indirect mixed calls, conversions, comparisons, a copied result
whose derivative crosses a later call, register pressure, and symbolic call
arguments.  No duplicate input was added.

Against `b8d70f5b`, frozen raw MIR falls from 213,294 to 213,056 instructions.
Single-block preparation falls from 260 to 141 operand rewrites and from 177
to 102 dead definitions; its three frame corrections remain unchanged.  The
object falls from 4,416,024 to 4,415,688 bytes, aggregate `.text*` from 930,392
to 930,006 bytes, and x86 instructions from 230,378 to 230,256, including 118
fewer moves.  Three A/B/B/A blocks give baseline/candidate medians of
4.860/4.840 seconds user, 5.340/5.310 seconds wall, and 364,186/364,016 KiB
peak RSS.  Paired medians improve user by 0.41% and wall by 0.75%, with RSS
improving 0.02%.  The affected report passes 368/368, the PA29 suite passes
224/224, the through-PA29 report passes 4,116/4,116, the full report passes
5,204/5,204, and the PA39 file audit has zero fatal findings.

At commit `1e1b63ca`, a clean 32-way self build takes 19.39 seconds wall,
414.14 seconds aggregate user time, and 239,472 KiB peak RSS.  With no
inception object tree present, the separate clean 32-way inception comparison
takes 1:53.27 wall, 3,023.62 seconds aggregate user time, and 243,376 KiB peak
RSS.  All 163 compared objects match.  The self and inception binaries are
byte-identical at 16,746,040 bytes with SHA-256
`2982ccf4437bc3046bcc9856c76cdf856fddfca10a088840078804c991eec6b7`.

The emitted-register-state slice replaces a conservative LowIR-opcode query
for early parameter reads with the register definitions in the MIR that has
actually been appended.  It reuses the optimizer's explicit and implicit MIR
definition semantics and accumulates only the incoming GPR bits in one fixed
mask.  Functions without tracked incoming GPRs do no work, and scanning stops
once every tracked register has been defined.  There is no string key, map,
per-instruction allocation, or prior-instruction rescan.  A prototype that
only refined direct indexed load/store clobbers was raw-MIR- and
object-identical because eliminated promoted-slot loads, rather than the
eventual memory operations, supplied most of the false clobbers; that narrower
prototype was reverted.

Against `1e1b63ca`, the frozen raw MIR falls from 213,056 to 213,024
instructions.  A total of 628 early parameter-backed stores now name their
intact incoming GPR directly.  Single-block preparation falls from 141 to 78
operand rewrites; its 102 dead definitions and three frame corrections remain
unchanged.  The object falls from 4,415,688 to 4,415,656 bytes, aggregate
`.text*` from 930,006 to 929,904 bytes, and x86 instructions from 230,256 to
230,224, including 38 fewer moves.  Three A/B/B/A blocks give
baseline/candidate medians of 4.950/4.950 seconds user, 5.440/5.450 seconds
wall, and 364,468/364,962 KiB peak RSS.  Paired medians are +0.51% user,
+0.18% wall, and +0.25% RSS, all neutral.

No existing MIR fixture changes.  Both compilers execute the new
`cppgm.tests/course/pa29/structural/incoming-parameter-emitted-clobbers.t` input successfully.  The
updated authoritative reference adopts the direct placement, so the witness
is active structural coverage.  The affected report passes 368/368, the
PA29 suite passes 224/224, the through-PA29 report passes 4,116/4,116, the
full report passes 5,204/5,204, and the PA39 file audit has zero fatal
findings.

At commit `c78ed5f7`, a clean 32-way self build takes 17.97 seconds wall,
405.26 seconds aggregate user time, and 241,004 KiB peak RSS.  With no
inception object tree present, the separate clean 32-way inception comparison
takes 1:49.99 wall, 2,926.58 seconds aggregate user time, and 232,488 KiB peak
RSS.  All 163 compared objects match.  The self and inception binaries are
byte-identical at 16,754,800 bytes with SHA-256
`4ebaf46822cf80b50aefc7f7fb4f0fa506b4a6e3e73140389e02c28bccdd64a3`.

The unused-address/index slice recognizes a pure `index` result with zero uses
from the existing dense use-count vector and consumes its operands without
selecting MIR.  When its only base is an immediately preceding frame `addr`,
one next-consumer check keeps that address as logical frame storage, so neither
operation manufactures a dead register definition.  The lowering performs
constant-time checks, adds no map or per-value state, and does not run a
general dead-code pass.

On the frozen compile, raw MIR falls from 213,024 to 213,003 instructions and
single-block preparation dead definitions fall from 102 to 81.  Operand
rewrites remain 78 and frame corrections remain three.  The 4,415,656-byte
object and its 929,904 aggregate `.text*` bytes are byte-identical to the
emitted-register-state baseline, with SHA-256
`5607808daca616b51c9e3fd906040f2be7f728020fe42318fd530fedecb0e708`.
Three uncontaminated interleaved A/B/B/A blocks give baseline/candidate
medians of 4.930/4.920 seconds user, 5.410/5.395 seconds wall, and
364,752/364,676 KiB peak RSS.  Median within-block candidate ratios are
-0.41% user, -0.19% wall, and +0.05% RSS.  A prior session was discarded in
full after another checkout began a 16-way test report during its second block
and produced a 7.61-second candidate outlier.

No existing fixture changes.  Both compilers omit the operations from
`cppgm.tests/course/pa29/structural/unused-index-address-elision.t` and execute it successfully,
but their unrelated startup-call syntax originally prevented an exact course
oracle.  The explicit PA29 structural lane and updated authoritative reference
now make the supplemental input an active placement test without manufacturing
an oracle.

The affected PA29/PA31/PA37/PA38 report passes 368/368, the PA29 suite passes
224/224, the through-PA29 report passes 4,116/4,116, and the final full report
passes 5,204/5,204.  The PA39 file audit has zero fatal findings and the same
26 advisory warnings as the phase baseline; the index predicate remains an
inline typed selection operation rather than adding implementation weight to
the address-lowering owner.

At commit `324f2356`, a clean 32-way self build takes 17.97 seconds wall,
404.13 seconds aggregate user time, and 252,864 KiB peak RSS.  With no
inception object tree present, the separate clean 32-way inception comparison
takes 1:52.43 wall, 2,940.83 seconds aggregate user time, and 231,940 KiB peak
RSS.  All 163 compared objects match.  The self and inception binaries are
byte-identical at 16,754,968 bytes with SHA-256
`013307fc2e0636b22b2b20b71d01de59727aa43fc2f62f3c71f98d2f954535e6`.

The direct-division slice places the divisor in RCX and the dividend in RAX
with a bounded two-register parallel setup.  Nonoverlapping sources need only
the two required moves; a source already in the other's destination reverses
their order, and the rare RAX/RCX cycle uses division-clobbered RDX as scratch.
Direct return placement is conservatively declined if earlier divisor
materialization would overwrite the dividend. An adjacent quotient return
retains the inherent RAX result, while an adjacent remainder return retains
the inherent RDX result and lets return encoding perform the ABI transfer.
The selector reuses the existing dense adjacent-use fact and makes a bounded
decision while lowering the one instruction.  It adds no pass, map,
collection, or string-keyed state; the separated helper classifies the
existing compact `LowOperation` enum without a text comparison.

The intentional public MIR migration affects
`pa29/tests/strict/300-unsigned-int-ops` and
`pa29/tests/behavior/800-reactive-spill-signed-div`: each now materializes its
immediate divisor in RCX without a dead RDX intermediate.  The pinned reference
and current compiler both execute these cases and the extended
`cppgm.tests/course/pa29/structural/direct-return-placement.t` successfully.  The updated reference
adopts the public placement shape, so this remains part of the active
structural witness rather than adding a duplicate behavior fixture.  Existing course PA29 tests
already cover directly returned signed/unsigned quotients and remainders.

Against the unused-index baseline, frozen raw MIR falls from 213,003 to 212,642
instructions.  Single-block preparation falls from 78 to 49 operand rewrites
and from 81 to 52 dead definitions; its 18,461 output instructions and three
frame corrections remain unchanged.  The object falls from 4,415,656 to
4,415,480 bytes and aggregate `.text*` from 929,904 to 929,726 bytes.  Two
uncontaminated sequential A/B/B/A blocks with the final compact-enum candidate
give baseline/candidate medians tied at 4.920 seconds user, 5.405/5.410 seconds
wall, and 365,234/365,100 KiB peak RSS.  Median within-block candidate ratios
are +0.20% user, +0.28% wall, and -0.04% RSS, all neutral.  A block with a
5.61-second candidate spike and a replacement overlapped by another checkout's
PA-cost analysis were each discarded in full.

The affected PA29/PA31/PA37/PA38 report passes 368/368, the PA29 suite passes
224/224, the through-PA29 report passes 4,116/4,116, and the full report passes
5,204/5,204.  The PA39 file audit remains zero-fatal with the same 26 advisory
warnings as the phase baseline.

At commit `7bbbc937`, a clean 32-way self build takes 19.09 seconds wall,
408.76 seconds aggregate user time, 39.51 seconds system time, and 239,968 KiB
peak RSS.  With no inception object tree present, the separate clean 32-way
inception comparison takes 1:51.15 wall, 2,982.52 seconds aggregate user time,
70.20 seconds system time, and 237,140 KiB peak RSS.  All 163 compared objects
match.  The self and inception binaries are byte-identical at 16,751,152 bytes
with SHA-256
`2f19994d8a6088ba2b591582a8398db09369e259e7eb4dc6edaf96e57f0a8123`.

The frame-address placement slice retains a typed frame location through an
immediately consuming scalar load, store, or index.  Constant indexing adds
its displacement to that logical location once; it does not first create a
register base.  The index selector distinguishes a logical frame address from
a pointer value spilled in a frame home before applying this rule.  The
existing `800-direct-call-index-base-preserve-sret` behavior case caught that
distinction during development.  The selector adds one bounded next-consumer
predicate and otherwise uses the existing value fact, so it adds no map,
string identity, scan, or new per-value storage.

The public MIR migration changes four strict and four structural PA29 cases,
plus three PA38 O1/O2 cases whose input MIR is now already direct.  Every
difference removes a frame-address `lea`; the existing fixtures cover object
parameters and results, scalar loads and stores, narrow widening, and constant
index displacement.  The PA29 student contract and typed MIR scaffold state
the direct frame-location requirement.  No duplicate reducer is needed.

Against the post-PA11 baseline at `074bf960`, frozen raw MIR falls from 212,646
to 211,305 instructions.  Single-block preparation falls from 49 to 44 operand
rewrites and from 52 to 47 dead definitions; its three frame corrections
remain.  The object falls from 4,415,448 to 4,412,976 bytes, aggregate `.text*`
from 929,709 to 927,202 bytes, and x86 instructions from 230,155 to 229,566.
That includes 508 fewer `lea` and 92 fewer `mov` instructions, with unchanged
push/pop counts.  Two load-screened sequential A/B/B/A blocks give
baseline/candidate medians of 4.215/4.210 seconds user, tied 4.665-second wall,
and 360,620/361,012 KiB peak RSS.  Median within-block candidate ratios are
-0.12% user, +0.11% wall, and +0.07% RSS, all neutral.  All four objects in
each arm are deterministic.

The affected PA29/PA31/PA37/PA38 report passes 373/373, the full report passes
5,225/5,225, and the PA39 file audit has zero fatal findings.

The direct object-chunk slice lets extended call setup load a frame-resident
object or wide-integer chunk from its final frame operand into the assigned ABI
register.  It retains the materialized-address fallback for non-frame sources.
The decision is a bounded kind check on the existing typed value fact; it adds
no analysis, map, string identity, or instruction scan.  Moving the predicate
and the existing parallel-move safety predicate into the call-lowering owner
keeps `lowir_native.cpp` below the fatal file-audit limit.

The public MIR migration changes only the structural
`700-object-param-slot-alias` PA29 fixture, which already covers a
frame-resident direct object argument.  The PA29 contract and typed MIR
scaffold require the direct chunk load, so no duplicate reducer is needed.

Against `f96a3224`, frozen raw MIR falls from 211,305 to 210,621
instructions.  The 4,412,976-byte object, 927,202 aggregate `.text*` bytes,
and SHA-256
`53ef8c0e63657a57f2d428646c4373fcb8cdf9f12a4022caf8d5def7882039cf`
are byte-identical.  Two load-screened sequential A/B/B/A blocks give
baseline/candidate medians of 4.195/4.190 seconds user, 4.680/4.670 seconds
wall, and 360,144/360,608 KiB peak RSS.  Median within-block candidate ratios
are +0.06% user, -0.16% wall, and +0.06% RSS, all neutral.

The affected PA29/PA31/PA37/PA38 report passes 373/373, the full report passes
5,225/5,225, and the PA39 file audit has zero fatal findings.

The parameter-copy sharing slice applies the existing same-type scalar-copy
rule to an intact incoming parameter location.  It uses the copied result's
precomputed fixed-register clobber mask and the existing live-location alias
tracking; it adds no state or scan.  A copy whose result crosses a clobber
continues to receive a distinct location.

The public MIR migration changes only the structural
`800-conditional-edge-liveness` PA29 fixture: its edge-live copy now returns
the intact RSI parameter directly instead of moving it to R8.  The existing
fixture covers both the copy and control-flow lifetime, and the PA29 contract
and typed MIR scaffold state the rule, so no duplicate reducer is needed.

Against `c5971a5f`, the frozen MIR and object are byte-identical at 210,621 MIR
instructions and 4,412,976 object bytes.  Two load-screened sequential
A/B/B/A blocks give baseline/candidate medians of 4.180/4.190 seconds user,
4.650/4.675 seconds wall, and 360,360/360,818 KiB peak RSS.  Median
within-block candidate ratios are +0.54% user, +0.65% wall, and +0.04% RSS,
all neutral.  The affected report passes 373/373, the full report passes
5,225/5,225, and the PA39 file audit has zero fatal findings.

The direct object-return slice applies the same typed frame-chunk rule to both
sides of the ABI result boundary. A return loads a frame-resident object
directly into RAX/RDX, and a direct-object call result stores those registers
directly to its frame destination. Non-frame operands keep the materialized
address fallback. The selection is one bounded operand-kind check per object
boundary and adds no scan, map, or string identity.

The public MIR migration changes the strict
`600-direct-object-return-temp-padding` fixture and the structural
`700-object-call-result-slot-alias` and `700-object-param-slot-alias` pairs.
These existing cases cover a padded narrow result, direct call-result storage,
and a result subsequently passed as an object argument. The PA29 contract and
typed MIR scaffold state the direct return-transfer rule, so no duplicate
reducer is needed.

Against `ac324c90`, frozen raw MIR falls from 210,621 to 210,256
instructions. Single-block preparation falls from 44 to 32 operand rewrites;
its 47 dead definitions and three frame corrections are unchanged. The object
falls from 4,412,976 to 4,412,784 bytes, aggregate `.text*` from 927,202 to
927,022 bytes, and x86 instructions by 88. Two load-screened sequential
A/B/B/A blocks give baseline/candidate medians of 4.175/4.170 seconds user,
4.655/4.645 seconds wall, and 360,394/361,210 KiB peak RSS. Paired medians are
-0.12% user, -0.21% wall, and +0.23% RSS, all neutral. Every object is
deterministic within its arm.

The affected PA29/PA31/PA37/PA38 report passes 373/373, the full report passes
5,225/5,225, and the PA39 file audit has zero fatal findings.

The representation-preserving copy slice recognizes that `ptr` and `i64` are
the same 64-bit machine representation on the PA29 target. A bit-preserving
copy between those two tags may therefore retain a stable source location
under the same complete-interval clobber check as an exactly typed scalar
copy. The selected value records its result type; width- and sign-changing
conversions remain explicit. The decision is constant-time and adds no state.

No existing fixture changes. The supplemental
`cppgm.tests/course/pa29/structural/representation-preserving-copy-placement.t` round-trips a
pointer through `i64`; both compilers execute it successfully, but the older
reference assigns new registers to both copies. Since behavior duplicates
active pointer-copy coverage, it remains a shape witness. The PA29 contract
and typed MIR scaffold describe the representation boundary directly.

Against `5345758d`, frozen raw MIR falls from 210,256 to 210,248
instructions. Single-block preparation falls from 32 to 24 operand rewrites
and from 47 to 39 dead definitions; its three frame corrections remain. The
4,412,784-byte object, 927,022 aggregate `.text*` bytes, and SHA-256
`096bc214aecd93cebd909acd27cd197ff547b0b2aee7aeeedc4d02db9b647275`
are byte-identical. Two load-screened sequential A/B/B/A blocks give
baseline/candidate medians of 4.230/4.235 seconds user, 4.710/4.700 seconds
wall, and 360,614/360,678 KiB peak RSS. Paired medians are +0.12% user,
-0.21% wall, and +0.02% RSS, all neutral.

The affected PA29/PA31/PA37/PA38 report passes 373/373, the full report passes
5,225/5,225, and the PA39 file audit has zero fatal findings.

The nonadjacent object-result slice uses the address result's existing dense
last-use and definition positions to recognize a frame destination whose sole
consumer copies a direct-object call result. The address remains a logical
frame location from its definition through that copy, even when the call is
not adjacent. Selection performs a fixed number of indexed lookups and
instruction-kind checks; it adds no scan, map, string identity, or state.

No existing fixture changes. Both compilers execute
`cppgm.tests/course/pa29/structural/nonadjacent-object-result-frame-placement.t`, while the older
reference carries the destination in a register across the call. Active PA29
cases already cover object-result behavior, so the new input remains a shape
witness. The PA29 contract and typed MIR scaffold state the nonadjacent frame
placement requirement.

Against `e23d3969`, frozen raw MIR falls from 210,248 to 209,669
instructions. Single-block preparation input falls from 18,007 to 17,755
instructions and its operand rewrites fall from 24 to 14; its 39 dead
definitions and three frame corrections are unchanged. The object falls from
4,412,784 to 4,409,616 bytes, aggregate `.text*` from 927,022 to 924,050
bytes, and x86 instructions from 230,037 to 229,204. Two load-screened
sequential A/B/B/A blocks give baseline/candidate medians of 4.220/4.230
seconds user, tied 4.715-second wall, and 360,478/360,508 KiB peak RSS. Paired
medians are +0.24% user, tied wall, and +0.01% RSS, all neutral. Every object
is deterministic within its arm.

The affected PA29/PA31/PA37/PA38 report passes 373/373, the full report passes
5,225/5,225, and the PA39 file audit has zero fatal findings.

The direct indirect-call-target slice retains an already selected target in a
callee-saved register, or in R8/R9 when the ABI plan does not assign that
register to an argument. Other locations continue to use R10, as do targets
whose argument setup can overwrite the selected register. The decision walks
the ABI plan already built for an extended call, or the ordinary call's
parameter list once, and adds no map, string identity, MIR scan, or new state.

The intentional public MIR migration changes the strict
`100-structured-global-data` fixture and the structural
`600-indirect-mixed-gpr-xmm-call-abi` pair. They cover a zero-argument call and
a mixed GPR/XMM setup whose target remains in R8. No duplicate reducer is
needed. Call-specific selection now lives in the call-lowering owner, and the
PA39 audit remains zero-fatal.

Against `1f735ef6`, single-block preparation falls from 14 to 12 operand
rewrites and from 39 to 37 dead definitions; its three frame corrections are
unchanged. The frozen object falls from 4,409,616 to 4,409,296 bytes,
aggregate `.text*` from 924,050 to 923,719 bytes, and x86 instructions from
229,204 to 229,092. Two sequential A/B/B/A blocks give baseline/candidate
medians of 4.310/4.270 seconds user, 4.760/4.725 seconds wall, and
360,622/360,970 KiB peak RSS. Median within-block candidate ratios improve
user by 0.76% and wall by 0.89%, with RSS +0.10%. Every object is deterministic
within its arm. The affected report passes 373/373 and the full report passes
5,225/5,225.

The selected-parameter-demand slice distinguishes LowIR operand uses from the
uses that survive scalar-slot selection. During the existing storage scan it
subtracts an initial store when a promoted, forwarded, or dead slot removes
that store, and adds the uses of any promoted load result. Parameter binding
then omits homes and setup transfers when that compact per-parameter count is
zero. The analysis remains one pass over instructions plus the existing slot
finalization loops, uses dense vectors indexed by parameter and value IDs, and
adds no string key, map, or MIR scan.

The public MIR migration changes strict
`900-slot-address-stack-call-argument`; structural
`700-direct-branch-source-live-across-call`,
`800-direct-branch-call-slot-ptr-compare`,
`800-forwarded-param-identity-live-across-call`, and
`800-switch-call-case-liveness`; behavior-exact
`800-reactive-spill-bulk-storage`, `800-reactive-spill-signed-div`, and
`800-register-param-r8-home-clobber`; the course-exact
`copied-compare-result-across-call`; and PA38's O1/O2
`200-call-argument-register-copy`. These existing cases cover unused register
parameters, a still-used stack parameter, promoted aliases, call liveness,
spill pressure, and downstream machine cleanup, so no duplicate reducer is
needed. The migration comprises 17 raw/canonical MIR files.

Against `b9fed3e0`, single-block preparation input falls from 17,753 to 17,739
instructions, dead definitions fall from 37 to 25, and the 12 operand rewrites
and three frame corrections are unchanged. The frozen object falls from
4,409,296 to 4,409,272 bytes, aggregate `.text*` from 923,719 to 923,709 bytes,
and x86 instructions from 229,092 to 229,088. Two sequential A/B/B/A blocks
give baseline/candidate medians of 4.240/4.230 seconds user, 4.720/4.695
seconds wall, and 360,524/360,456 KiB peak RSS. Median within-block candidate
ratios are -0.12% user, -0.26% wall, and -0.02% RSS. Every object is
deterministic within its arm. The affected report passes 373/373, the full
report passes 5,225/5,225, and the PA39 audit has zero fatal findings.

The stable promoted-home slice removes the speculative register copy formerly
created by a promoted or forwarded parameter-slot load after the first call.
Storage and parameter selection have already chosen a home that survives the
required clobbers; consumers can therefore read that typed location directly
and apply their own fixed-register constraints. The change is one bounded
branch in the existing load selector and removes code, state mutation, and
allocation work. It adds no analysis, map, string identity, or instruction
scan.

No checked MIR fixture changes. Existing parameter, call, arithmetic, and
store cases all continue to pass, while the frozen compile's single-block
preparation falls from 12 to 3 operand rewrites, from 25 to 24 dead
definitions, and from 17,739 to 17,706 input instructions. Its three frame
corrections remain. Against `0f11751e`, the object falls from 4,409,272 to
4,409,072 bytes, aggregate `.text*` from 923,709 to 923,582 bytes, and x86
instructions from 229,088 to 229,042.

Two sequential A/B/B/A blocks give baseline/candidate medians of 4.190/4.165
seconds user, 4.680/4.655 seconds wall, and 359,896/360,844 KiB peak RSS.
Median within-block candidate ratios improve user by 0.48% and wall by 0.48%,
with RSS +0.38%, all neutral. Every object is deterministic within its arm.
The affected PA29/PA31/PA37/PA38 report passes 373/373 and the PA39 audit has
zero fatal findings. The full report passes 5,225/5,225.

The logical large-ALU-immediate slice removes an early signed-32-bit check
from integer binary selection. MIR retains the complete typed constant for
add, subtract, multiply, bitwise, shift, and division operations. The native
emitter already selects an immediate encoding when legal and otherwise uses
its fixed R11 scratch; fixed-register shift and division selectors perform
their own bounded placement. This removes duplicate target-encoding policy
from lowering and adds no state or work.

The raw and canonical structural pair for
`800-runtime-i64-large-alu-immediate` now names the large constant directly in
the add and subtract instructions. That existing test executes both forms and
is the exact owning witness, so no new reducer is needed. Against `35dd15ad`,
single-block preparation falls from 3 to 2 operand rewrites and its 24 dead
definitions and three frame corrections are unchanged. The frozen object
remains 4,409,072 bytes, while aggregate `.text*` falls by 10 bytes to 923,572
and x86 instructions fall by two to 229,040.

Two sequential A/B/B/A blocks give baseline/candidate medians of 4.190/4.210
seconds user, 4.680/4.690 seconds wall, and 360,686/360,544 KiB peak RSS.
Median within-block candidate ratios are +0.48% user, +0.43% wall, and -0.08%
RSS, all neutral. Every object is deterministic within its arm. The affected
PA29/PA31/PA37/PA38 report passes 373/373 and the PA39 audit has zero fatal
findings. The full report passes 5,225/5,225.

The direct remainder-return slice selects RDX as the destination of a
remainder whose sole adjacent consumer is `return`. Division already produces
the value there, and serialized `ret rdx` follows the existing rule that a
return may name its selected GPR while native emission performs the final RAX
transfer. The decision extends the existing constant-time adjacent-use query;
it adds no state, scan, or allocation.

No checked fixture changes. The existing course behavior cases and the
supplemental `cppgm.tests/course/pa29/structural/direct-return-placement.t` input cover signed and
unsigned directly returned remainders. Against `14149ba4`, raw MIR and
single-block preparation input each fall by one instruction. Preparation falls
from 2 to 1 operand rewrite and from 24 to 23 dead definitions; its three frame
corrections remain. The frozen object is byte-identical at 4,409,072 bytes,
with 923,572 aggregate `.text*` bytes and 229,040 x86 instructions.

Two sequential A/B/B/A blocks give baseline/candidate medians of 4.170/4.215
seconds user, 4.640/4.695 seconds wall, and 361,570/360,224 KiB peak RSS.
Median within-block candidate ratios are +1.20% user, +0.97% wall, and -0.42%
RSS, all inside the neutral gate. Every object is byte-identical across both
arms. The affected PA29/PA31/PA37/PA38 report passes 373/373 and the PA39 audit
has zero fatal findings. The full report passes 5,225/5,225.

The early call-result-carrier slice recognizes a full-width scalar call result
whose first use is a GPR call argument and whose later use still requires a
stable selected home. Existing function analysis marks only results with more
than one use, proves the first use remains in the defining block, and reuses
the RAX lower-bound query already made while computing the complete interval's
fixed-register clobbers. Lowering keeps one active `ValueId` and one bit only
for such a candidate. Until its first use, it checks the small MIR suffix
emitted for each intervening LowIR instruction with the existing register
definition mask and retires the carrier on an actual RAX definition. Extended
call setup also checks its already-emitted setup prefix. No unrelated function
or value scan, string identity, hash table, or allocation is added.

No checked fixture changes. The expanded
`cppgm.tests/course/pa29/structural/direct-call-result-consumers.t` witness returns a pointer in
RAX, passes it first to an extended reference call, and uses it again after
that call. Current MIR copies RAX once to the stable R12 home, passes the first
argument directly from RAX, and reads R12 for the later call. Both the current
and course-reference compilers execute the input successfully.  The updated
authoritative reference adopts the public MIR placement, so it is active
structural course coverage.

Against `c2dd56bd`, single-block preparation reaches zero operand rewrites;
its 23 dead definitions, three frame corrections, and 17,681 output
instructions are unchanged. The frozen object retains the same total size,
aggregate text, and instruction counts at 4,409,072 bytes, 923,572 bytes, and
229,040 instructions, respectively; its internal code layout changes because
the first call setup now reads RAX directly. Two sequential A/B/B/A blocks give
baseline/candidate medians of 4.260/4.245 seconds user, 4.750/4.690 seconds
wall, and 360,226/360,364 KiB peak RSS. Median within-block candidate ratios
improve user by 0.53% and wall by 1.42%, with RSS +0.20%. The affected report
passes 373/373, the full report passes 5,225/5,225, and the PA39 audit has zero
fatal findings.

The unread selected-parameter-home slice distinguishes a stable home that is
needed after a clobber from one whose selected consumers all read the intact
incoming ABI carrier. Each optional register transfer carries its compact
`ValueId` owner, and representation-preserving aliases copy that identity.
When selection resolves an operand through the stable home, it sets the
transfer's single `required` bit; when it resolves the intact incoming carrier,
the transfer remains unread and can be removed. Wide parameter intervals that
cross no clobber remain in their incoming registers before MIR is constructed,
so removing an optional transfer never leaves a stale physical operand. The
bounded final compaction visits only the setup moves. No MIR scan, string
identity, hash table, dependency graph, or value-sized demand vector is added.
A reservation generation count also prevents an omitted parameter home from
dropping a callee-save requirement when its register is later reused for
emitted code.

The strict `200-indirect-call-six-register-args` fixture and the raw/canonical
structural pair for `200-stack-arguments-beyond-six` now consume all six intact
incoming carriers directly and omit five callee saves plus six setup moves.
Their existing executable checks cover the behavior. The supplemental
`cppgm.tests/course/pa29/structural/incoming-parameter-emitted-clobbers.t` witness now likewise
serializes its final reference parameter use directly from RCX without an
unread `r9 <- rcx` transfer.  The updated authoritative reference adopts that
public placement, so the witness is active structural course coverage.

Against `c489ddcd`, frozen single-block preparation input falls from 17,704 to
17,594 instructions and its output is also 17,594: operand rewrites, dead
definitions, and frame corrections are all zero. Raw MIR falls by 725
instructions. The object falls from 4,409,072 to 4,406,784 bytes, aggregate
`.text*` from 923,572 to 921,144 bytes, and x86 instructions from 229,040 to
228,039. Two sequential A/B/B/A blocks give baseline/candidate medians of
4.185/4.225 seconds user, 4.665/4.700 seconds wall, and 360,570/360,336 KiB
peak RSS: +0.96%, +0.75%, and -0.06%, respectively, all inside the neutral
gate. Every object is deterministic within its arm. The affected report passes
373/373, the full report passes 5,225/5,225, and the PA39 audit has zero fatal
findings after frame planning was separated from the size-limited lowerer.

The compatibility-retirement slice encodes the typed MIR returned by baseline
lowering directly. It removes the single-block `MirFunction` copy, local alias
rewrite, dead-definition sweep, frame correction, preservation exceptions for
encoder folding patterns, and the associated preparation telemetry. PA38's
public O1/O2 optimizer remains unchanged and continues to operate on serialized
MIR. The scalar-float return normalization stays at that optimization boundary;
PA29 baseline return encoding already consumes its documented implicit ABI
fact. This deletes work from O0 rather than replacing it with another hidden
representation or scan.

No LowIR or MIR fixture changes. Against `16c8f774`, all four objects in each
arm of two sequential A/B/B/A blocks are byte-identical at 4,406,784 bytes with
SHA-256 `520af5ad2cc527d93df30616ee074ad5cfd906c301d9988b0ac0dc450b2f6af1`.
Baseline/candidate medians are 4.225/4.160 seconds user, 4.720/4.625 seconds
wall, and 360,370/360,302 KiB peak RSS, improving 1.54%, 2.01%, and 0.02%,
respectively. The affected report passes 373/373, the full report passes
5,225/5,225, and the PA39 audit has zero fatal findings.

The first clean self build after compatibility retirement exposed a PA29
wide-parameter placement dependency that the ordinary report corpus did not
previously cover. A mandatory stable home for the second SysV parameter uses
R9 and therefore clobbers the intact incoming carrier of the sixth parameter.
The bounded six-parameter planner now includes mandatory setup destinations in
its fixed-register clobber mask before retaining an incoming carrier. The
behavioral `wide-parameter-home-clobbers-incoming` reducer executes under both
the course reference and current compiler without imposing the reference's
different MIR layout. Parameter-home planning was kept in the dedicated
parameter-lowering component, leaving the PA39 audit with zero fatal findings.

The correction is byte-identical on frozen `semantic_overload.cpp`: both the
pre-fix and corrected objects are 4,406,784 bytes with SHA-256
`520af5ad2cc527d93df30616ee074ad5cfd906c301d9988b0ac0dc450b2f6af1`.
Sequential A/B/B/A medians for the pre-fix/corrected compilers are 4.215/4.185
seconds user, 4.720/4.650 seconds wall, and 362,262/362,434 KiB peak RSS. The
affected report passes 374/374 and the full report passes 5,226/5,226.

The clean self-host gate then exposed two further PA29 carrier failures. First,
the early RAX fact used only LowIR-level clobber information, but an intervening
address materialization selected RAX under register pressure. The
`rax-call-result-intervening-address` behavior reducer fails before the fix and
now forces the later call to reload the stable pointer home. The active-carrier
check described above uses definitions in the MIR actually emitted, so it also
covers future instruction-selection choices without pessimizing unrelated
values.

Second, a seven-argument EH wrapper selected R14 as the stable home of the
incoming R9 parameter, then retired the transfer even though its call setup had
already serialized a read from R14. The
`wide-forwarded-call-argument-home` behavior reducer supplies the private EH
runtime symbols needed by the older course reference and fails with a bad
pointer before the correction. Optional homes now have one unambiguous rule:
any selected read marks its transfer required. This removes the former soft
dependency state as well as the correctness hole.

Both corrections leave frozen `semantic_overload.cpp` byte-identical at
4,406,784 bytes with SHA-256
`520af5ad2cc527d93df30616ee074ad5cfd906c301d9988b0ac0dc450b2f6af1`.
Three sequential observations per arm give baseline/candidate medians of
4.280/4.250 seconds user, 4.750/4.710 seconds wall, and 360,672/360,364 KiB
peak RSS. The affected report passes 376/376, the full report passes
5,228/5,228, and the PA39 audit has zero fatal findings.

## Final completion gate

All VP0 through VP5 implementation phases are complete at `9f676b9c`. The
nonretained prototypes and the separate compact-identity migration remain
documented above, but neither is unfinished work in this plan.

Starting with no PA39 object tree, the final 32-way self build completed in
17.90 seconds wall, 402.89 seconds aggregate user time, and 40.94 seconds
system time, with 221,968 KiB peak RSS. It produced 180 objects and a
17,269,936-byte `cppgm++-self` binary with SHA-256
`8e8d7e8178b08607181f857635d7c0a08af822f3a1000e455f22cb9d3f370910`.

Each inception lane started with no inception object tree while retaining that
exact self compiler and its comparison objects. The clean 8-way comparison
completed in 4:01.85 wall, 1,817.94 seconds aggregate user time, and 42.56
seconds system time, with 227,212 KiB peak RSS. The separately cleaned 32-way
comparison completed in 1:53.04 wall, 2,918.99 seconds aggregate user time,
and 67.86 seconds system time, with 224,160 KiB peak RSS. Both lanes rebuilt
and matched all 180 objects, then reproduced the self binary byte for byte.

The final root report passes 5,228/5,228. The PA39 file audit has zero fatal
findings and 28 advisory warnings. At `-O0`, the development compiler and the
clean self compiler independently produce byte-identical 4,406,784-byte
objects for frozen `semantic_overload.cpp`, both with SHA-256
`520af5ad2cc527d93df30616ee074ad5cfd906c301d9988b0ac0dc450b2f6af1`.

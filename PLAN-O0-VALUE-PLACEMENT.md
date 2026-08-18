# Plan: O0 Native Value Placement and Address Selection

Status: in progress

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
go into the earliest owning course suite when the reference agrees.  A useful
shape witness unsupported by the older reference remains under
`proposed/pa29/` or `proposed/pa38/`.

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
| VP2 | 1 proposed LowIR witness | 5 existing PA29 MIR fixtures; indexed operand syntax added to the scaffold/canonicalizer | Frozen object -4,656 bytes and text -4,288 bytes; x86 instructions -1,741, including 1,848 fewer `lea`, 33 fewer `imul`, 7 fewer `add`, 215 fewer `push`, and 218 fewer `pop`; paired user +0.09%, wall +0.56%, RSS +0.24%; full report 5,189/5,189; audit zero fatal | landed in `4b36cd90` |
| VP3 | 3 proposed LowIR shape witnesses | 10 existing PA29 fixtures plus the PA38 call-address fixture at O1/O2; the call-result slice changes no existing fixture; fixed-home call forwarding changes 1 behavior-exact PA29 fixture; promoted-slot interval extension changes 2 strict and 1 behavior-exact PA29 fixtures; direct comparison returns change 8 exact and 17 structural report cases; direct unary returns change no existing fixture; direct integer-conversion returns change 4 exact and 1 structural report cases | Input-lifetime slice: frozen object -2,360 bytes, text -2,272 bytes, and 662 instructions. Placement slice: object -2,704 bytes, text -1,090 bytes, and 1,178 instructions. Direct call-result consumers: object -9,048 bytes, text -8,204 bytes, and 2,749 instructions; its paired medians improve user 0.71% and wall 0.85% with RSS +0.24%. Fixed-home forwarding: object -144 bytes, text -59 bytes, and 21 moves; paired user +0.27%, wall +0.73%, RSS +0.22%. Promoted-slot intervals: object -2,416 bytes, text -2,426 bytes, and 915 instructions; paired user +0.62%, wall -0.48%, RSS -0.09%. Dense slot analysis is object-identical and improves paired user 0.27%, wall 0.40%, and RSS 0.16%. Direct comparison returns: object -576 bytes, text -607 bytes, and 111 instructions/moves; paired user -1.32%, wall -1.04%, RSS tied. Direct unary returns: object/text -16 bytes and 4 instructions/moves; paired user -0.35%, wall -0.48%, RSS +0.63%. Direct integer-conversion returns: object -32 bytes, text -20 bytes, and 6 instructions/moves; paired user -1.05%, wall -0.40%, and RSS -0.21% | input lifetime, scalar address/return placement, safe scalar-copy sharing, immediate call-result argument/store placement, fixed-home call forwarding, promoted-slot interval extension, dense slot-analysis state, and integer comparison/unary/conversion return placement complete; broader producer placement pending |
| VP4 | 5 course LowIR correctness/shape reducers | Typed/copy slice: 12 strict, 9 structural, and 3 course-exact PA29 fixtures plus 4 PA38 O1/O2 fixtures; direct constraints: 3 strict and 1 structural PA29 fixtures; parameter retention: 3 strict, 12 structural, and 1 behavior-exact PA29 fixture, with overlap; wide spill reuse changes 2 strict and 1 structural PA29 cases (4 MIR/CMIR files); scalar spill reuse changes 1 behavior-exact PA29 fixture and adds PA29 O0 and PA38 O2 course behavior reducers | Caller-saved slice: frozen object -21,824 bytes, text -18,494 bytes, and 5,800 instructions. Typed-immediate/copy slice: object -24,688 bytes, text -23,682 bytes, MIR instructions -5,926, x86 instructions -4,533, moves -3,645, and spills 476 -> 318. Direct-constraint slice: object -160 bytes, text -155 bytes, 56 x86 instructions, and 55 moves. Intact-parameter slice: object -1,648 bytes, text -1,305 bytes, and 447 instructions. Its calm ABBA medians are user +0.09%, tied wall, and RSS +0.12%. Wide spill reuse is frozen-object-identical; paired user +0.18%, wall +0.08%, RSS -0.03%. Lifetime-keyed frame forwarding is also frozen-object-identical; paired user -0.70%, wall -0.24%, RSS -0.22%. Scalar reuse removes 80 object bytes and 74 text bytes with unchanged instruction counts; final paired user +0.45%, wall +0.53%, RSS +0.18% | caller-saved pool, clobber-safe reuse, typed-immediate rematerialization, safe copy sharing, direct ALU/shift constraints, intact ABI-parameter retention, all scalar spill-home reuse, and frame-forwarding lifetime identity complete; address rematerialization pending |
| VP5 | 0 | 3 strict PA29 MIR fixtures for bulk copy/zero placement; logical scalar returns change 18 strict, 19 structural, 3 behavior-exact, and 1 course-exact report cases (60 MIR/CMIR files) | Logical bulk operands reshape individual functions but leave aggregate `.text*` (943,292 bytes) and x86 instruction-family counts unchanged; total object -32 bytes. Two-block ABBA medians are user tied at 5.665 s, wall -0.28%, and RSS -0.04%. Logical scalar returns remove 355 hidden operand rewrites and 355 dead definitions; frozen object -32 bytes, text -24 bytes, and 6 moves/instructions; paired user -0.89%, wall -0.65%, RSS +0.19%. Explicit preparation telemetry now measures 959 remaining operand rewrites and 874 dead definitions in 2,684 single-block functions, costing about 5.6 ms; full report 5,204/5,204 and audit zero fatal | logical bulk and scalar-return operands complete; remaining scalar compatibility rewrites pending |

The VP1 proposed call-argument input predates the public MIR placement rule and
is now supplemental to the active `900-symbolic-global-call-argument` fixture.
The VP2 scaled-index witness remains proposed because the course reference
materializes `imul` plus `add`; both compilers execute it successfully, but
their MIR shapes intentionally disagree.

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
`proposed/pa29/direct-call-result-consumers.t` covers both consumers and runs
successfully under the current and course-reference compilers. The reference
forwards RAX to the consumer but retains dead intermediate result copies, so
the case remains proposed rather than imposing a new exact course oracle. The
student README and scaffold state only the required placement rule.

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

The comparison was added to `proposed/pa29/direct-return-placement.t`. Both
compilers execute the expanded witness successfully. The course reference
retains a temporary comparison register and final return copy, so the shape
witness remains proposed rather than becoming a conflicting active oracle.
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
`proposed/pa29/direct-return-placement.t`; both compilers execute it, while the
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
`proposed/pa29/direct-return-placement.t`; both compilers execute it, while
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

`proposed/pa29/index-address-placement.t` and
`proposed/pa29/direct-return-placement.t` execute successfully with both
compilers.  They remain proposed because the course reference introduces the
temporary copies or expanded multiply/add sequences that the public placement
rule excludes.

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

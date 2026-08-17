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
| VP3 | 3 proposed LowIR shape witnesses | 10 existing PA29 fixtures plus the PA38 call-address fixture at O1/O2; the call-result slice changes no existing fixture; fixed-home call forwarding changes 1 behavior-exact PA29 fixture; promoted-slot interval extension changes 2 strict and 1 behavior-exact PA29 fixtures | Input-lifetime slice: frozen object -2,360 bytes, text -2,272 bytes, and 662 instructions. Placement slice: object -2,704 bytes, text -1,090 bytes, and 1,178 instructions. Direct call-result consumers: object -9,048 bytes, text -8,204 bytes, and 2,749 instructions; its paired medians improve user 0.71% and wall 0.85% with RSS +0.24%. Fixed-home forwarding: object -144 bytes, text -59 bytes, and 21 moves; paired user +0.27%, wall +0.73%, RSS +0.22%. Promoted-slot intervals: object -2,416 bytes, text -2,426 bytes, and 915 instructions; paired user +0.62%, wall -0.48%, RSS -0.09%. Dense slot analysis is object-identical and improves paired user 0.27%, wall 0.40%, and RSS 0.16% | input lifetime, scalar address/return placement, safe scalar-copy sharing, immediate call-result argument/store placement, fixed-home call forwarding, promoted-slot interval extension, and dense slot-analysis state complete; broader load/store producer placement pending |
| VP4 | 3 course LowIR correctness/shape reducers | Typed/copy slice: 12 strict, 9 structural, and 3 course-exact PA29 fixtures plus 4 PA38 O1/O2 fixtures; direct constraints: 3 strict and 1 structural PA29 fixtures; parameter retention: 3 strict, 12 structural, and 1 behavior-exact PA29 fixture, with overlap | Caller-saved slice: frozen object -21,824 bytes, text -18,494 bytes, and 5,800 instructions. Typed-immediate/copy slice: object -24,688 bytes, text -23,682 bytes, MIR instructions -5,926, x86 instructions -4,533, moves -3,645, and spills 476 -> 318. Direct-constraint slice: object -160 bytes, text -155 bytes, 56 x86 instructions, and 55 moves. Intact-parameter slice: object -1,648 bytes, text -1,305 bytes, and 447 instructions. Its calm ABBA medians are user +0.09%, tied wall, and RSS +0.12%; full report 5,192/5,192; audit zero fatal | caller-saved pool, clobber-safe reuse, typed-immediate rematerialization, safe copy sharing, direct ALU/shift constraints, and intact ABI-parameter retention complete; address rematerialization and spill-slot reuse pending |
| VP5 | 0 | 3 strict PA29 MIR fixtures for bulk copy/zero placement | Logical bulk operands reshape individual functions but leave aggregate `.text*` (943,292 bytes) and x86 instruction-family counts unchanged; total object -32 bytes. Two-block ABBA medians are user tied at 5.665 s, wall -0.28%, and RSS -0.04%; affected report 357/357 | logical bulk operands complete; remaining scalar compatibility rewrites pending |

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
not be approximated by parallel ID and string models that can diverge.

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

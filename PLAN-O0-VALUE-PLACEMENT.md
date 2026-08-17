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
| VP1 | 0 | 5 existing MIR fixtures plus one new PA29 structural fixture | Frozen object/text -11,272 bytes; x86 instructions -1,962, including 1,895 moves; three-block ABBA medians tied at 6.295 s wall and 5.720 s user; full report 5,189/5,189; audit zero fatal | complete, pending commit |
| VP2 | pending | pending | pending | pending |
| VP3 | pending | pending | pending | pending |
| VP4 | pending | pending | pending | pending |
| VP5 | pending | pending | pending | pending |

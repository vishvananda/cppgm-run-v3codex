## CPPGM Programming Assignment 37 (`lowiropt`)

### Overview

PA37 adds the first explicit optimization stage to the compiler. The new tool,
`lowiropt`, reads PA13 LowIR text, applies a deterministic optimization
pipeline selected by `-O0`, `-O1`, or `-O2`, and writes LowIR text.

The same LowIR optimizer is also reached from `cppgm++` when source programs
are compiled with `--emit-lowir -O1`, `--emit-lowir -O2`, or through the
ordinary compile/link driver at an optimization level.

### Prerequisites

You should complete PA36 before starting this assignment.

You will reuse:

- the PA13 LowIR syntax and semantics
- the PA15 through PA36 source-to-LowIR lowering pipeline
- the PA29 native backend, PA30 driver, PA14 ABI naming, and PA31 host-runtime path
- the PA36 hosted compiler driver surface

### Starter Kit

The starter kit supplies:

- `pa37/Makefile`
- `pa37/lowiropt.cpp`, linked to the editable `dev/lowiropt.cpp`
- a `dev/lowiropt.cpp` scaffold based on `dev/lowiropt-scaffold.cpp`
- shared compiler support under `dev/src/`
- test directories under `pa37/tests/`
- harness scripts under `pa37/scripts/`
- checked-in `.ref` and `.ref.exit_status` files for the tests

The expected implementation work is in `dev/lowiropt.cpp` and shared optimizer
or driver support under `dev/src/`, especially the LowIR optimizer and
optimization-level plumbing. The supplied LowIR parser, dumper, driver helpers,
and test harness are support code; they do not implement the optimization
passes for you.

The harness uses checked-in references as the oracle. There is no
separate `lowiropt-ref` binary in the starter kit.

### Command Line

`lowiropt` accepts exactly one optimization level, one output path, and one or
more LowIR input files:

```sh
lowiropt -O0 -o <outfile> <lowirfile>...
lowiropt -O1 -o <outfile> <lowirfile>...
lowiropt -O2 -o <outfile> <lowirfile>...
```

`--help` and `-h` print usage information and exit successfully.

PA37 also requires the source driver to route these options through the same
optimizer:

```sh
cppgm++ --emit-lowir -g0 -O1 -o <outfile> <srcfile>...
cppgm++ --emit-lowir -g0 -O2 -o <outfile> <srcfile>...
cppgm++ --emit-lowir -gline-tables-only -O1 -o <outfile> <srcfile>...
cppgm++ --emit-lowir -gline-tables-only -O2 -o <outfile> <srcfile>...
```

The ordinary `cppgm++ -c` and link-driver paths must also accept `-O0`, `-O1`,
and `-O2` and use the same LowIR optimization level before object generation.
Compile mode must also accept serialized LowIR text as an input:

```sh
cppgm++ -c -O0 -o <objfile> <lowirfile>
cppgm++ -c -O1 -o <objfile> <lowirfile>
cppgm++ -c -O2 -o <objfile> <lowirfile>
```

This LowIR object input mode parses LowIR text, runs the same object-prep and
optimization path used by source object compilation, and writes the same
host-compatible relocatable object format.

### Output Format

`lowiropt` writes LowIR text to `<outfile>`. The output must remain valid LowIR
and must preserve the behavior of every defined input program.

The optimizer works on the same LowIR program representation that the object
path consumes. It may use typed internal data structures, but optimized output
must serialize back to valid LowIR, and object generation at a chosen
optimization level must not require extra semantic facts unavailable from that
optimized LowIR text.

This LowIR/object boundary is required in PA37. A correct compile path
may keep LowIR in memory for speed, but it must not pass private frontend or
semantic side data around the serialized LowIR representation. If object
emission needs a fact after optimization, that fact must either be represented
in LowIR or derived again by the object-lowering layer from LowIR. The direct
`cppgm++ -c` source object and the object produced by `--emit-lowir -O0`
followed by `cppgm++ -c -O<level>` on that LowIR file should therefore match
for the same source, flags, and optimization level.

LowIR top-level declaration/definition order is a presentation convention, not
a dependency order. Reference outputs and canonical dumps use the order defined
in `../pa13/lowir.md`: `declare global`, `declare function`, `global`, then
`function`, but the relaxed LowIR comparison canonicalizes top-level entries
before comparison. Your output must still be repeatable for the same
inputs; `../pa13/lowir.md` defines the canonical reference presentation and
notes where internal LowIR symbol names are only a presentation tie-breaker.
Your output must also preserve order-sensitive LowIR regions when they are present: instruction order inside
blocks, item order inside structured globals, vtable slot order, and action
order inside generated initialization, finalization, constructor, destructor,
and cleanup bodies.

For successful runs:

- `-O0` performs a deterministic parse/dump round trip.
- `-O1` applies local and control-flow-aware LowIR simplifications.
- `-O2` applies all `-O1` work and additional conservative slot-promotion
  optimizations.

The assignment grades the optimized LowIR shape as well as behavior
preservation. The goal is a deterministic optimization stage, not elapsed-time
benchmark wins.

### Error Handling

The tool must fail with a nonzero exit status when:

- no optimization level is provided
- `-o` is missing or has no following path
- there are no input files
- an input file cannot be read
- the input is not valid LowIR
- the output file cannot be written

For failure cases, diagnostics only need to be useful to a developer; exact
diagnostic text is not part of the grading contract. The contents of the
output file after a failed run are undefined.

### Optimization Levels

To complete PA37, implement these optimization levels:

`-O0` is the baseline canonicalization mode. It should parse the input program,
preserve all semantic content, and write the canonical LowIR dump without
running optimizing transforms.

`-O1` must include these semantic-preserving pass families where safe:

- constant folding for scalar `copy`, `unary`, `binary`, `cmp`, and `convert`
  instructions with known constant operands
- algebraic identities such as `x + 0`, `x - 0`, `x * 1`, `x & -1`, redundant
  `unary decay`, identity `convert`, and compares whose operands are known to
  be identical
- local and executable-edge-aware copy and constant propagation
- local and executable-edge-aware pure-expression reuse for eligible `addr`,
  `index`, `unary`, `binary`, `cmp`, and `convert` instructions
- safe normalization of commutative integer operations and reversible compare
  directions so equivalent expressions reuse the same producer
- boolean compare cleanup for `cmp eq` and `cmp ne` against `0` or `1` when the
  compared value is already known to be an `i64` boolean
- local reassociation of repeated integer `add`, `mul`, `and`, `or`, and `xor`
  chains with constants
- control-flow cleanup, including folding known `branch` and `switch`
  selectors, removing unreachable blocks, bypassing trivial jump-only blocks,
  merging safe straight-line block pairs, and collapsing empty branch diamonds
  when both arms resolve through non-EH jump-only blocks to the same
  continuation
- preservation of exceptional handler targets and exception-structure blocks
  while doing CFG cleanup
- conservative inlining of small direct calls, including `unwind=no` callees
  inside EH regions only when the caller EH shape can be preserved; a callee
  containing its own EH instructions must remain a call even at caller EH depth zero
- preservation of calls to functions marked `no_inline=yes`; source-level GNU
  `noinline` attributes must reach that LowIR metadata on the driver path
- bottom-up processing of the direct-call graph so an eligible callee is
  simplified before callers decide whether to inline it
- a deterministic 128-instruction whole-caller inlining budget, charged by
  the greater of a callee's original and simplified instruction counts, so
  repeated individually eligible calls cannot cause unbounded growth
- preservation of object-parameter copies and isolated return-merge slots and
  continuations when direct calls with object or nested multi-block callees are
  inlined
- removal of no-op EH markers in functions known not to unwind when the
  protected region contains no operation that can transfer to the handler
- discovery of natural loops from dominators and backedges, including loop
  headers, latches, exits, nesting, and existing canonical preheaders
- loop-invariant motion of nontrapping pure scalar and address calculations
  whose operands are defined outside the loop; explicitly located debug
  instructions, trapping division or remainder, EH regions, and operations
  with observable effects must stay in place
- creation of a loop preheader by splitting a single critical entry edge only
  when a small explicit block budget and sufficient invariant work justify it
- dead-code elimination for unused pure temp-producing instructions
- removal of unused calls only when the callee is explicitly `readnone`, cannot
  unwind, and is not `noreturn`
- removal of dead local-slot traffic for unused direct slot loads and for
  stores to direct local slots that have no remaining loads, escaping uses, or
  other non-store uses
- removal of slot declarations that become unused after simplification
- removal, after inlining, of unreachable weak or internal function
  definitions, while retaining externally visible strong definitions,
  `object_root=yes` definitions, lifecycle roots, and definitions referenced
  by calls, addresses, relocations, or structured object data

`-O2` must include all `-O1` work and then conservatively promote eligible
non-escaping scalar slots, including eligible `ptr` slots. Promotion must be
limited to slots accessed through direct `store` and `load` operations whose
current value is defined on every executable path. When predecessor paths
carry different values, promotion uses the PA13 `phi` instruction at an
ordinary join. A phi must not be introduced at an exceptional-handler target.
Beyond the direct slot cleanup already allowed at `-O1`, `-O2` also removes
dead stores to promoted slots when no observable load can see the stored
value. It must additionally support these conservative loop transforms:

- hoisting a direct global or nonescaping direct-slot load only when no store,
  call, atomic operation, or other memory effect in the loop can change the
  loaded object
- eliminating a redundant scalar load when a dominating load reads the same
  typed location and no store, call, atomic operation, or other memory effect
  can change that location on any intervening path; direct locations,
  nonescaping slot addresses, repeated pointer values, and proven disjoint
  constant projections must use the same conservative alias rules
- preserving load invalidation across control-flow joins, unknown pointers,
  ordinary read/write calls, atomic operations, and exception regions, while
  allowing explicitly `readnone` and `readonly` calls and immutable globals
  to retain the facts their metadata permits
- eliminating a fully redundant pure expression at an ordinary join by
  replacing the join computation with a `phi` of the available predecessor
  values
- eliminating a partially redundant expression only when the missing
  predecessor has a single successor, every operand is available there, and
  moving the operation cannot introduce a trap or other observable behavior;
  insertion is limited to 64 expressions and 64 phis per function
- recognizing simple `i64` induction variables with one latch and constant
  initial value, step, and bound
- canonicalizing an inverted counted-loop exit and replacing multiplication
  of the induction value by a positive power of two with a left shift
- removing a loop only when its body has no observable or trapping operation,
  its values are unused outside the loop, and its finite termination can be
  proved without integer overflow

Slot-value forwarding and promotion remain an `-O2` responsibility. At `-O1`,
a live load whose value is consumed along multiple successor paths must remain
unless an ordinary non-slot propagation rule independently proves each use.

### Validation Modes

LowIR output has presentation details, such as internal helper names and
metadata ordering, that are not semantic requirements. The PA37 harness checks
exit status, LowIR well-formedness, required IR facts, and behavior
preservation without requiring every non-semantic presentation choice to match
the course solution exactly. Exact textual LowIR matching is not a PA37 grading
requirement unless a test explicitly says so.

### Testing

Run the PA37 suite with:

```sh
make test
```

`make test` runs:

- `tests/o0`
- `tests/o1`
- `tests/o2`
- `tests/driver/o1`
- `tests/driver/o2`
- `tests/object-roundtrip`

These directories are organized by tool mode and validation mode, not by N3485
source-language clauses.

- `tests/o0` runs `lowiropt -O0` on handwritten LowIR.
- `tests/o1` runs `lowiropt -O1` on handwritten LowIR.
- `tests/o2` runs `lowiropt -O2` on handwritten LowIR.
- `tests/driver/o1` runs `cppgm++ --emit-lowir -g0 -O1` on source programs.
- `tests/driver/o2` runs `cppgm++ --emit-lowir -g0 -O2` on source programs.
- `tests/object-roundtrip` compares direct `cppgm++ -c` output against an
  object produced by `cppgm++ --emit-lowir -O0` followed by `cppgm++ -c` on
  the generated LowIR file. This checks that object emission can be
  reconstructed from serialized LowIR instead of from hidden frontend side
  data. These tests may be standalone `.cpp` files or symlinks to existing
  `.t` harness cases; a selected `.t` test expands to its numbered `.t.1`,
  `.t.2`, ... source files when those sidecars exist. The harness checks
  no-debug objects at `-O0`, `-O1`, and `-O2`.

Run the debug metadata preservation lanes with:

```sh
make test-debuginfo
```

This also runs `tests/object-roundtrip` in debuginfo mode, comparing direct
`cppgm++ -c` output against LowIR-input `cppgm++ -c` output with
`-gline-tables-only` at `-O0` and `-O1`.

`make test-debuginfo` runs:

- `tests/debuginfo/o1`
- `tests/debuginfo/o2`
- `tests/debuginfo/driver/o1`
- `tests/debuginfo/driver/o2`

The direct debug-info tests run `lowiropt -O*` over LowIR containing
`!dbg(...)` metadata. The driver debug-info tests run
`cppgm++ --emit-lowir -gline-tables-only -O*` and check that source locations
survive the source-to-LowIR optimizer path.

For each `.t` test, the harness records the tool exit status and compares the
generated output against the oracle for that test directory. Failed reference
cases are judged by exit status; successful reference cases are judged by the
directory's LowIR validation mode.

### Out Of Scope

PA37 does not require:

- input programs to arrive in SSA form
- PRE that requires critical-edge splitting, speculative trapping operations,
  or an unbounded insertion/fixed-point schedule
- alias-driven aggressive dead-store elimination
- loop unrolling, peeling, vectorization, or loop transformations beyond the
  conservative motion and counted-loop rules described above
- machine-IR scheduling or register-allocation optimization
- interprocedural optimization beyond direct-call graph ordering, the required
  small-function inlining, and post-inline reachability cleanup
- size-specific `-Os` or `-Oz` behavior

### Design Notes

A compact implementation can assign dense indices to function definitions and
store direct-call edges in adjacency arrays. Computing strongly connected
components once identifies recursive callees and provides a stable callee-first
order without repeated symbol-name lookup. Use the typed symbol operands and
metadata already present in LowIR when building the graph and its reachability
roots; string rendering is only needed when serializing the final program.

For slot promotion, collect definition blocks by typed slot identity and place
phis with an iterated dominance-frontier worklist. Dense block epochs let one
set of scratch vectors serve every slot. Keep the work proportional to direct
slot accesses, relevant control-flow edges, and inserted phis, and apply an
explicit growth bound before adding merge instructions.

For redundant-load elimination, derive compact location identities from the
typed `addr` and `index` operations. Sparse memory versions placed through the
same dominance frontiers can represent stores and conservative unknown-memory
barriers without copying a dense location table into every block. Hash keys
should contain typed IDs, offsets, types, and version numbers rather than
rendered LowIR text.

For expression PRE, index occurrences by the same typed expression key used
for ordinary value reuse. Dominator queries can find values available on each
incoming edge without copying expression maps into every block. Keep
availability probes and inserted instructions explicitly bounded, and skip a
candidate when a safe insertion would require splitting a critical edge.

CFG, dominator, and natural-loop facts can share one per-function analysis
owner with an explicit invalidation epoch. Dense block IDs, compact successor
lists, and epoch-marked membership vectors keep loop discovery proportional to
CFG edges and reported loop memberships. For invariant motion, build a packed
producer-to-user worklist and use typed value, slot, and global IDs for memory
checks; render names only while writing LowIR text.

### After PA37

Later optimization tests build on this assignment by optimizing after LowIR has
already been lowered to machine IR. PA37 focuses on the LowIR optimization
pipeline and the `lowiropt` structural oracle.

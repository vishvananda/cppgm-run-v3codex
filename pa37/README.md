## CPPGM Programming Assignment 37 (`lowiropt`)

### Overview

PA37 adds the first explicit optimization stage to the compiler. The new tool,
`lowiropt`, reads PA13 LowIR text, applies a deterministic optimization
pipeline selected by `-O0`, `-O1`, `-O2`, or `-O3`, and writes LowIR text.

The same LowIR optimizer is also reached from `cppgm++` when source programs
are compiled with `--emit-lowir -O1`, `--emit-lowir -O2`,
`--emit-lowir -O3`, or through the ordinary compile/link driver at an
optimization level.

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
lowiropt -O3 -o <outfile> <lowirfile>...
```

`--help` and `-h` print usage information and exit successfully.

PA37 also requires the source driver to route these options through the same
optimizer:

```sh
cppgm++ --emit-lowir -g0 -O1 -o <outfile> <srcfile>...
cppgm++ --emit-lowir -g0 -O2 -o <outfile> <srcfile>...
cppgm++ --emit-lowir -g0 -O3 -o <outfile> <srcfile>...
cppgm++ --emit-lowir -gline-tables-only -O1 -o <outfile> <srcfile>...
cppgm++ --emit-lowir -gline-tables-only -O2 -o <outfile> <srcfile>...
cppgm++ --emit-lowir -gline-tables-only -O3 -o <outfile> <srcfile>...
```

The ordinary `cppgm++ -c` and link-driver paths must also accept `-O0`, `-O1`,
`-O2`, and `-O3` and use the same LowIR optimization level before object
generation. With no `-O` option, the ordinary driver selects `-O3`.
Compile mode must also accept serialized LowIR text as an input:

```sh
cppgm++ -c -O0 -o <objfile> <lowirfile>
cppgm++ -c -O1 -o <objfile> <lowirfile>
cppgm++ -c -O2 -o <objfile> <lowirfile>
cppgm++ -c -O3 -o <objfile> <lowirfile>
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
- `-O1` applies local and control-flow-aware LowIR simplifications, then one
  bounded late inlining wave for small acyclic functions made compact by
  those transforms.
- `-O2` applies all `-O1` work and additional conservative slot-promotion
  optimizations, with its late inlining wave following the additional scalar
  and control-flow work.
- `-O3` applies all `-O2` work and bounded full unrolling of eligible small
  constant-trip loops before its bounded late inlining wave.

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
- removal of a conditional edge whose target begins with a call to the PA13
  `role=unreachable` operation, when the other edge remains a normal successor
- preservation of exceptional handler targets and exception-structure blocks
  while doing CFG cleanup
- conservative inlining of small direct calls, including `unwind=no` callees
  inside EH regions only when the caller EH shape can be preserved; serialized
  `object` identity does not make a callee ineligible when either its public
  boundary or typed body analysis proves that it cannot unwind; a callee
  containing its own EH instructions must remain a call even at caller EH
  depth zero
- preservation of calls to functions marked `no_inline=yes`; source-level GNU
  `noinline` attributes on named functions and lambda call operators must reach
  that LowIR metadata on the driver path
- independent treatment of object retention and inlining policy: a reachable
  constructor or destructor base entry may remain an `object_root=yes`
  definition while its eligible direct calls are inlined
- preservation of the source `inline` preference as `inline_hint=yes`, distinct
  from mandatory `force_inline=yes`, prohibitive `no_inline=yes`, linkage, and
  object-retention metadata
- bottom-up processing of the direct-call graph so an eligible callee is
  simplified before callers decide whether to inline it
- immediate local simplification, dead-code elimination, and control-flow
  cleanup after a late inlining batch, so the callee's compact shape is
  visible to every later caller in the same bottom-up traversal
- a deterministic 128-instruction whole-caller inlining budget, charged by
  the greater of a callee's original and simplified instruction counts, so
  repeated individually eligible calls cannot cause unbounded growth
- a separate definition-removing path for a weak or internal function that
  has exactly one direct call and no address, relocation, structured-data,
  alias, lifecycle, object-root, or other non-call use; this path may admit a
  body of at most 160 instructions, uses a 320-instruction budget per caller,
  and removes the transferred definition after substitution; its
  translation-unit budget is the greater of 10,240 and the original input
  instruction count
- one post-reachability graph rebuild followed by the definition-removing path
  alone, so removing dead callers may expose a new single-call definition;
  this wave uses the optimized body count with the same 160-instruction body,
  320-instruction caller, and proportional translation-unit limits, then
  prunes transferred definitions
- inlining a callee with no EH control instructions from an active caller EH
  region even when the callee may unwind; potentially throwing calls cloned
  from that callee remain inside the caller's active region and therefore use
  its landing destination
- inlining from an exceptional landing block only when the callee has no EH
  control instructions and its explicit or inferred boundary proves that it
  cannot unwind; potentially throwing, EH-bearing, indirect, and explicitly
  `no_inline` calls remain intact while an exception is already in flight
- treating `throw`, `exception`, `exception_selector`, and `resume` as
  EH-bearing instructions, just like the `eh_*` markers; a function containing
  any of them is not an ordinary inlining candidate
- preservation of the ordinary 128-instruction policy when a definition is
  in the single-call class but a separate single-call budget is exhausted;
  calls that meet the ordinary size and growth rules remain ordinary inline
  candidates
- preservation of object-parameter copies and isolated return-merge slots and
  continuations when direct calls with object or nested multi-block callees are
  inlined
- retargeting of successor `phi` inputs when inlining moves a caller block's
  terminating edge into a new continuation block
- removal of a matched protected region when its direct calls are already
  known not to unwind and it contains no indirect call, throw, resume, or
  other operation that can transfer to the handler; safe regions may be
  removed independently while unsafe sibling regions remain
- publication of an EH-free function as inferred nonthrowing after its last
  protected region is removed and every remaining direct call is nonthrowing,
  so later callers in the same callee-first traversal can use that fact
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
Each incoming value must have the phi result type. A type-changing `copy` that
feeds a promoted slot must therefore remain, and promotion must insert an
ordinary typed `copy` on a predecessor edge when another value needs the slot
type before it can become a phi input.
`-O2` must also scalar-replace a local `obj<1>`, `obj<2>`, `obj<4>`, or
`obj<8>` slot when its complete storage is consistently accessed as one
same-sized scalar value. Complete `copyobj` and `zeroinit` operations may then
be expressed as scalar loads, stores, and zero values before ordinary slot
promotion. The object must remain unchanged if its address escapes, an access
uses a nonzero or partial projection, its scalar access types disagree, or an
atomic or otherwise unmodelled operation can observe its storage.
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
  values; the phi and any inserted predecessor computation use the original
  expression's result type, including `ptr` for an `addr` result
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
- propagating an integer, floating, or global-address argument into an
  internal direct-call target when every remaining direct call supplies the
  same value and the target is not observable through an address, lifecycle
  role, object root, alias, or variadic signature
- removing an unused directly passed scalar parameter from such an internal
  target and all of its direct calls; computations that produced a removed
  argument remain subject to ordinary effect-aware dead-code elimination
- specializing a discardable weak target through one internal clone instead
  of changing the weak target's externally observable ABI; rooted,
  address-observable, recursive, `no_inline`, and mismatched-signature weak
  targets must remain unchanged

Interprocedural specialization is bounded to 256 clones and 8,192 cloned
LowIR instructions per translation unit. Budget exhaustion skips later
candidates in deterministic function order. `-O1` does not perform argument
specialization.

`-O3` must include all `-O2` work. It additionally fully unrolls a canonical
constant-trip loop when all of these conditions hold:

- the loop has one preheader, one latch, one exit, no exceptional region, and
  one straight-line body path back to its header
- header phis describe the loop-carried values, and an integral induction phi
  has a constant initial value, nonzero constant step, and constant signed or
  unsigned bound
- the exact trip count is at most four and can be proved without overflowing
  the induction value
- every loop value used after the exit has a value after the final iteration

The transform must preserve the order and count of calls, stores, atomics,
traps, and other observable body operations. It may unroll at most one loop
per function, clone at most 64 body instructions for that loop, and clone at
most 4,096 loop-body instructions per translation unit. If any proof or budget
is unavailable, the loop remains unchanged. `-O2` does not perform this full
unrolling.

After the level's scalar and control-flow transforms, every optimizing level
rebuilds the typed direct-call graph once and may inline an additional
nonrecursive function whose optimized body contains no exception-handling
instructions. A single-block, single-return body with no calls may have at
most 40 instructions. A body that contains a call or has multiple blocks may
have at most six instructions, or at most seven instructions when its
definition has `inline_hint=yes`. This late wave has a fresh 128-instruction
budget for each caller, charged by the optimized instruction count of every
inlined body. The hint does not override that budget or `no_inline=yes`. The
wave must continue to preserve variadic,
exception-region, unwind, and externally visible call-site restrictions from
ordinary inlining.

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
- `tests/o3`
- `tests/driver/o1`
- `tests/driver/o2`
- `tests/driver/o3`
- `tests/object-roundtrip`

These directories are organized by tool mode and validation mode, not by N3485
source-language clauses.

- `tests/o0` runs `lowiropt -O0` on handwritten LowIR.
- `tests/o1` runs `lowiropt -O1` on handwritten LowIR.
- `tests/o2` runs `lowiropt -O2` on handwritten LowIR.
- `tests/o3` runs `lowiropt -O3` on handwritten LowIR.
- `tests/driver/o1` runs `cppgm++ --emit-lowir -g0 -O1` on source programs.
- `tests/driver/o2` runs `cppgm++ --emit-lowir -g0 -O2` on source programs.
- `tests/driver/o3` runs `cppgm++ --emit-lowir -g0 -O3` on source programs.
- `tests/object-roundtrip` compares direct `cppgm++ -c` output against an
  object produced by `cppgm++ --emit-lowir -O0` followed by `cppgm++ -c` on
  the generated LowIR file. This checks that object emission can be
  reconstructed from serialized LowIR instead of from hidden frontend side
  data. These tests may be standalone `.cpp` files or symlinks to existing
  `.t` harness cases; a selected `.t` test expands to its numbered `.t.1`,
  `.t.2`, ... source files when those sidecars exist. The harness checks
  no-debug objects at `-O0`, `-O1`, `-O2`, and `-O3`. A
  `default-maximum-optimization` case also checks that omitting `-O` matches
  explicit `-O3`.

Run the debug metadata preservation lanes with:

```sh
make test-debuginfo
```

This also runs `tests/object-roundtrip` in debuginfo mode, comparing direct
`cppgm++ -c` output against LowIR-input `cppgm++ -c` output with
`-gline-tables-only` at `-O0`, `-O1`, `-O2`, and `-O3`.

`make test-debuginfo` runs:

- `tests/debuginfo/o1`
- `tests/debuginfo/o2`
- `tests/debuginfo/o3`
- `tests/debuginfo/driver/o1`
- `tests/debuginfo/driver/o2`
- `tests/debuginfo/driver/o3`

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
- partial unrolling, peeling, vectorization, or loop transformations beyond
  the conservative motion and full-unrolling rules described above
- machine-IR scheduling or register-allocation optimization
- unbounded interprocedural cloning, externally observable ABI changes,
  semantic body merging, or indirect-call specialization
- size-specific `-Os` or `-Oz` behavior

### Design Notes

A compact implementation can assign dense indices to function definitions and
store direct-call edges in adjacency arrays. Computing strongly connected
components once identifies recursive callees and provides a stable callee-first
order without repeated symbol-name lookup. Use the typed symbol operands and
metadata already present in LowIR when building the graph and its reachability
roots; string rendering is only needed when serializing the final program.
Represent an ordinary call to an internal function with its typed call edge,
not with a permanent object root.  Reserve object roots for independent
address, lifecycle, ABI, or explicit-publication requirements so a definition
can disappear when inlining removes its last call.

Publish a changed nonrecursive callee's cleaned instruction, block, call, and
EH shape before advancing through that stable order. Direct calls cloned from
the callee can only target descendants that have already been visited, so the
ordinary acyclic case converges in one traversal. This avoids whole-program
fixed-point retries; only a later policy that changes incoming-use eligibility
needs a deduplicated reverse-caller worklist.

The direct-call graph can record non-call observation in one byte per function
while it scans instruction operands and structured global data. Reverse-edge
counts then identify the single-direct-call case without another symbol lookup
or program pass. When that definition is discardable, move the instruction
payloads into their renamed caller form and release the old body immediately.
Keep the ordinary and definition-removing budgets independent so exhausting
the latter does not suppress an otherwise ordinary inline. Charging at most
one original translation unit of definition-removing work keeps the policy
linear while allowing a large input proportionally more useful transfers than
a small one. After pruning dead callers, rebuild the compact graph once and
run only this definition-removing policy in callee-first order. A transferred
callee can then become part of a later transferred caller in the same wave;
do not implement the cascade as repeated whole-program fixed-point scans.

Track the caller's active EH state as one dense fact per block. A callee that
contains no EH control instruction can inherit that state without rewriting
exception metadata: its cloned ordinary calls remain between the caller's
existing `eh_try`/`eh_cleanup` and `eh_end`. Keep landing blocks themselves and
callees with their own EH control out of the ordinary inlining path.

For dead protected-region removal, assign compact region IDs to `eh_try` and
`eh_cleanup` instructions and propagate the active region stack over ordinary
CFG edges. Mark a region unsafe from typed call/unwind facts, then propagate
that state to its parent once. Conflicting stacks at a merge reject the
function conservatively. This keeps the proof proportional to instructions,
CFG edges, and EH nesting rather than retrying the translation unit.

Interprocedural argument agreement can reuse that same direct-call graph and
its dense symbol-to-function table. Store parameter facts in one packed array
with per-function offsets, and scan typed call operands once; do not build a
second graph or use rendered symbol names as keys. A weak target keeps its
original body while direct calls are redirected to a bounded internal clone,
so pruning the now-undemanded weak body remains an ordinary reachability
decision. Run local simplification and effect-aware dead-code elimination only
for changed callees and callers.

For slot promotion, collect definition blocks by typed slot identity and place
phis with an iterated dominance-frontier worklist. Dense block epochs let one
set of scratch vectors serve every slot. Keep the work proportional to direct
slot accesses, relevant control-flow edges, and inserted phis, and apply an
explicit growth bound before adding merge instructions.

Small-object scalar replacement can reuse dense value-indexed address facts
and slot-indexed candidate facts. Trace only typed `addr`, pointer `copy`, and
`index` operations, reject a candidate as soon as an unsupported use is seen,
and hand the resulting scalar slot to the ordinary promotion pass. A
union/find over complete `copyobj` edges lets connected temporary objects share
one observed scalar type without string keys or repeated instruction scans.

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

Full unrolling can reuse those loop facts and a dense value-ID replacement
table. Plan the trip count and growth before cloning, use generated value IDs
for each iteration, and invalidate the shared CFG facts once after a retained
rewrite. A single function scan and a translation-unit instruction counter are
sufficient; no loop-level fixed point or rendered-name map is needed.

The late optimized inlining wave may rebuild the typed direct-call graph once
after the selected level's local transforms. Cache compact per-function
instruction counts and shape facts while building that graph. Admit only
nonrecursive bodies within the explicit size limits, charge each optimized
body against a fresh bounded caller budget, and revisit only callers that
changed. Multi-block substitution can reuse the ordinary typed block, value,
slot, return-merge, and phi-edge machinery. This permits scalar replacement
and CFG cleanup to expose small accessors and wrappers without an unbounded
optimizer fixed point or repeated body scans at call sites.

Carry the inline preference as one Boolean in typed symbol metadata and test it
beside the cached body-shape summary. It should not require source-name lookup,
rendered metadata parsing, a separate call graph, or an additional function
scan.

For unreachable-edge cleanup, build one dense bitmap indexed by `SymbolId` from
the program's typed role metadata, then mark target blocks by `BlockId` within
each function. This keeps the scan linear in symbols, blocks, and instructions
without symbol-name or metadata-text lookups.

### After PA37

Later optimization tests build on this assignment by optimizing after LowIR has
already been lowered to machine IR. PA37 focuses on the LowIR optimization
pipeline and the `lowiropt` structural oracle.

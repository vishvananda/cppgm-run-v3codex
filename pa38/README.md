## CPPGM Programming Assignment 38 (`lowir2native -O*`)

### Overview

PA38 adds machine-backend optimization levels to `lowir2native` and to the
shared native backend used by later `cppgm++` object and link-driver paths.

PA37 optimizes LowIR before backend lowering. PA38 starts after that boundary:
LowIR has already been translated into machine IR, and the backend must improve
the generated native path while preserving program behavior.

The questions for this assignment are:

- can `lowir2native -O1` perform local machine-IR cleanup?
- can `lowir2native -O2` perform whole-function machine-IR cleanup?
- can `lowir2native -O3` carry the maximum optimization level through the
  same optimized machine path?
- can all levels preserve debug metadata and generated program behavior?
- can the optimized machine-IR path stay reusable by `cppgm++` rather than
  becoming a standalone `lowir2native` shortcut?

### Prerequisites

You should complete:

- PA29 for the baseline `lowir2native` backend
- PA37 for the explicit LowIR optimization stage

You will reuse the PA13 LowIR input language. Handwritten PA38 tests should use
the maintained LowIR surface, including explicit role metadata such as
`[role=entry]` where required.

### Starter Kit

The starter kit supplies:

- `pa38/Makefile`
- `pa38/lowir2native.cpp`, linked to the editable `dev/lowir2native.cpp`
- a `dev/lowir2native.cpp` scaffold based on `dev/lowir2native-scaffold.cpp`
- shared machine-IR and native backend support under `dev/src/`
- optional typed machine-IR model scaffolding in `dev/src/mir_model.h`, with
  shared register support in `dev/src/x86_register_model.h`
- test directories under `pa38/tests/`
- harness scripts under `pa38/scripts/`
- checked-in structural machine-IR and generated-program oracle sidecars

The expected implementation work is in `dev/lowir2native.cpp` and the shared
machine-IR/native backend modules under `dev/src/`, especially machine-IR
optimization and object-generation plumbing. The supplied LowIR parser,
machine-IR data model, object writer, linker helpers, and harness scripts are
support code; they do not complete the optimization assignment for you.

The harness uses checked-in sidecars as the oracle. There is no separate
`lowir2native-ref` binary in the starter kit.

### Command Line

PA38 requires these invocations:

```sh
lowir2native -O1 -o <program> <lowirfile>...
lowir2native -O2 -o <program> <lowirfile>...
lowir2native -O3 -o <program> <lowirfile>...
lowir2native -O1 --dump-machine-ir <mirfile> <lowirfile>...
lowir2native -O2 --dump-machine-ir <mirfile> <lowirfile>...
lowir2native -O3 --dump-machine-ir <mirfile> <lowirfile>...
lowir2native -O1 --dump-machine-ir <mirfile> -o <program> <lowirfile>...
lowir2native -O2 --dump-machine-ir <mirfile> -o <program> <lowirfile>...
lowir2native -O3 --dump-machine-ir <mirfile> -o <program> <lowirfile>...
```

`--help` and `-h` print usage information and exit successfully.

The `--target <target>` option is inherited from the native backend. Tests may
set the target through the harness environment, but the optimization
contract is independent of host-specific elapsed time.

`-O0` remains the PA29 baseline. PA38 must preserve that earlier behavior while
adding the explicit `-O1`, `-O2`, and `-O3` backend optimization levels.

When `cppgm++` later emits native objects or executables at an optimization
level, it should use this same backend optimization pipeline after PA37 LowIR
optimization has produced the LowIR program to lower. Do not implement PA38 as
a display-only `lowir2native` transform that the compiler driver cannot reuse.

For backend investigation, the compile-only driver accepts
`--stats-functions` together with an optimization level.  It writes one
`function_census symbol=...` record per lowered function to standard error.
Each record includes MIR size, movement load/store/copy counts, planned
location grants/releases, planned grants blocked by live parameter or ordinary
value holders, the actual location class of planned definitions, spills, and
frame-home counts.  These contention fields let backend work distinguish a
value that was never planned from a plan that could not claim its selected
register.  The records are diagnostic data: tests validate their structure
and function coverage, not timings or exact counter values.

For every final-MIR natural loop, the same option also writes a
`loop_census symbol=...` record.  A natural loop is identified by a CFG
backedge whose header dominates its latch; arbitrary backward branches are
not loops.  The record identifies the stable header block and reports the
loop's member-block and MIR counts, calls, exception operations, nesting
depth, frame operands, function frame bindings, and callee-saved register
count.  A compiler-created block without a presentation label uses `block_`
followed by its numeric identity.  This is a structural view of loop pressure:
consumers may compare shapes or select a reducer, but must not depend on exact
counter values from an unrelated program.

### Output Format

With `--dump-machine-ir <mirfile>`, `lowir2native` writes the optimized machine
IR dump to `<mirfile>`.

With `-o <program>`, `lowir2native` writes a native executable to `<program>`.
When both options are present, both outputs must be produced from the same
optimized machine-IR program.

`--dump-machine-ir` is the serialized view of the machine-IR program that the
native backend consumes. The object/native path may keep the MIR in memory, but
it should not use a different hidden representation with extra backend facts
that the MIR dump cannot express.

The same rule applies when the machine backend is reached through `cppgm++`:
optimization, object writing, and executable writing should consume the same
machine-IR facts that `--dump-machine-ir` can serialize.

The raw `x.ref.mir` file is kept as the debugging-oriented dump produced by
`--dump-machine-ir`. The primary backend-shape oracle is the structural
machine-IR sidecar `x.ref.cmir`, compared against a canonicalized form of the
generated `x.my.mir` dump. The generated native program's exit status and
standard output are behavior-preservation oracles layered on top of that
structural check.

Call argument-use, stack-argument, variadic, unwind, and no-return annotations
described by PA29 remain part of optimized MIR.  A machine optimization may
remove argument setup only when the surviving call annotation no longer names
the removed register use.  It must preserve stack and exception-boundary facts.
Indexed memory operands likewise retain both register uses and their scale;
copy propagation must rewrite or invalidate the base and index independently.

### Error Handling

The tool must fail with a nonzero exit status when:

- neither `-o` nor `--dump-machine-ir` is provided
- an option requiring a path is missing that path
- there are no input files
- an input file cannot be read
- the input is not valid LowIR
- the target is unsupported
- a requested output file cannot be written
- native code generation or linking fails

For failure cases, exact diagnostic text is not part of the grading
contract. Output files after a failed run are undefined.

### Optimization Levels

To complete PA38, implement these backend optimization levels:

`-O1` is the local machine-improvement level. It must include these
semantic-preserving rewrites where safe:

- remove unconditional jumps to the immediately following block
- coalesce block-local integer and floating-point register copies
- remove redundant move chains and simple return shuffles
- remove integer and floating-point moves whose source and destination are the
  same physical register
- clean up call-result and call-argument copies
- retain register copies that are still required for ABI call-argument setup,
  distinct bulk-copy source/destination operands, or values live into successor
  blocks
- rematerialize cheap integer immediates into supported arithmetic,
  zero-compare, and call-argument instruction forms
- collapse conditional-branch plus unconditional-jump block tails when one
  target is the natural fallthrough block
- rewrite zero-comparison branches into direct `test reg, reg` machine IR when
  the backend supports that shape
- fold frame-address temporaries back into direct frame operands or direct
  `lea` call-argument setup where safe
- use one three-operand `lea` for an O1+ 64-bit integer or pointer addition
  when the result cannot overwrite its still-live left input and the right
  input is a register or signed 32-bit displacement
- keep safe frame, local-global, and constant-index storage addresses as
  rematerializable MIR operands across their complete use interval; observing
  the pointer value, clobbering a carrier, variable indexing, or arithmetic on
  the pointer requires materialization
- place an unplanned edge-live integer or pointer identity copy directly into
  its required frame home, and likewise allow an eligible scalar call result
  to be stored from its ABI return register; planned and exact-forward values
  retain their selected locations
- let a single-use scalar value from an acyclic merge `phi` share its frame
  home with a later acyclic merge `phi` that consumes it on one incoming edge.
  The shared home removes an identity edge transfer.  Inside a cyclic region,
  this is safe only when both non-loop-carried merges belong to the same cycle,
  so the source is refreshed before each dynamic use.  A loop-carried merge,
  a loop invariant feeding a repeated merge, a value with another use, or a
  representation change must retain an independent home unless a different
  register-resident implementation makes the transfer unnecessary
- keep a frequently reused, iteration-local scalar call result available to
  branch comparisons throughout one cyclic choice region.  The defining call
  must execute before every dynamic use, dominate every use, and have all of
  those uses in the same cyclic component; a loop invariant or a value used
  outside that component does not satisfy the proof.  When later comparisons
  cross another call, reserve enough call-preserved register capacity for the
  bounded region and keep overlapping cyclic allocations from consuming it.
  If the proof or capacity is unavailable, retain the ordinary frame-home
  path; no particular physical register is required
- permit loop-carried integer or pointer `phi` values in a guarded fast arm
  to reuse call-preserved register capacity when the fast arm cannot reach a
  call, the values die before the function's first call, and the sibling
  call-bearing arm already creates full preserved-register pressure.  This
  exception must not add save/restore capacity.  A loop whose header can
  reach a call, a function without the existing preserved pressure, or an
  otherwise unproved path relationship retains the ordinary frame homes; no
  particular physical registers are required
- permit a bypassable, call-free loop to begin loop-carried integer or pointer
  `phi` residency on its first incoming edge, after any call-bearing prefix,
  when the complete incoming-edge-through-backedge interval has no call or
  fixed-register clobber.  This local residency must use caller-saved capacity,
  must not add a function-wide save/restore, and must not overlap another
  planned owner of that capacity.  When an earlier call-bearing prefix already
  creates full call-preserved pressure, the planner should prefer otherwise
  free caller-saved argument capacity for the local interval so unrelated
  caller-saved temporaries do not block activation.  A call in the interval,
  unavailable capacity at the incoming edge, added preserved-register
  pressure, or an unproved span retains the ordinary frame-home transfers; no
  particular physical register is required
- forward a scalar compiler temporary from its defining register into an
  immediately following integer comparison when that comparison is the
  temporary's only use, both operations have the same machine type, and the
  other comparison operand is encodable with a register.  The final MIR drops
  the now-unneeded frame store and compares the register directly.  Volatile,
  debug-visible, multi-use, unannotated, non-adjacent, or otherwise unproved
  frame values retain their stable homes
- after complete MIR liveness is available, recolor a callee-saved physical
  register to an otherwise noninterfering caller-saved register when every
  occurrence is explicit, none of its live ranges crosses a clobber of the
  replacement, and debug or implicit-register state does not depend on the
  original register.  Call liveness should use the call's exact annotated
  argument set when present.  At O1 this is required for the profitable
  even-save call-function case, where removing one save also removes the
  SysV call-alignment padding adjustment; leaf functions, odd save counts,
  call-crossing ranges, and unproved interference retain their original
  placement.  No particular caller-saved register is required
- reuse a final-use pointer register as the destination of an eligible scalar
  load only when the effective address is consumed before the destination is
  overwritten, the pointer has no surviving aliases or edge use, and the load
  result needs the ordinary stable edge home
- omit the frame pointer only when the final MIR has no dynamic stack, host EH,
  scratch area, debug variable location, or frame operand; functions rejected
  by any of those guards keep it
- keep semantic returns distinct in MIR while using direct, unshared physical
  epilogues at O1+; O0 retains the shared-epilogue policy

`-O2` is the whole-function machine-improvement level. It must include all
`-O1` work and additionally:

- improve block layout by following unconditional jump traces so likely
  successors become natural fallthrough blocks
- retain a LowIR value in one physical register through joins or backedges
  when its complete interval conflicts with no fixed-register use; a value
  that crosses a call may remain in a callee-saved GPR, while an XMM value may
  remain register-resident only when it crosses no call
- consider single-use values as well as repeatedly used values for planned
  residency, and make the target ABI's otherwise available call-preserved GPR
  capacity eligible for bounded call-crossing intervals; no particular
  physical register is required
- plan call-free integer or pointer intervals in otherwise available
  caller-saved registers, including inside an exception-bearing function when
  the complete interval reaches no call; a call-reaching interval must instead
  be recomputed, stored, or placed in call-safe capacity
- use a frame home when exception flow or register pressure prevents a stable
  whole-function register placement; cyclic placement must leave register
  capacity for values first produced inside the cycle and must not evict a
  location that an already-emitted backedge will use on its next iteration
- allocate and initialize a planned edge value's fallback frame home only when
  an actual eviction or home-reading consumer requires it; a value that stays
  resident for its complete interval must not acquire an unused eager home
- remove callee-saved register preservation that is no longer needed after
  optimization
- recompute final stack reservation from the surviving frame state

The same interval machinery is available at every optimizing level for the
bounded O1 placement classes used by the checked fixtures. Loop-carried phis
may claim stable callee-saved homes for unavoidable loops and for the bounded
path-disjoint guarded-fast-arm case above; a cursor load may then take over
its dead phi-home address register. Loop invariants and
ordinary call-free values may remain in conflict-free registers, including in
an exception-bearing function when their interval crosses no call. Values
that require a frame fallback may acquire a second caller-saved register
segment only after their final call when one acyclic block dominates every
remaining use. Interval-end and final-use release must respect backedge and
backward-EH spans, cached address carriers, aliases, parameters, and fixed
register effects.

`-O3` must accept the LowIR produced by PA37's maximum optimization level and
apply all `-O2` machine improvements. PA38 does not require an additional
O3-only MIR rewrite; the distinct O3 transformation is the PA37 LowIR full
unroller, and its result must lower through the ordinary serialized MIR path.

All levels must preserve valid debug metadata. Optimizations may choose to be
more conservative when a rewrite would make source locations misleading.

### Validation Modes

Machine-IR dumps contain presentation details, such as scratch-register choices,
frame offsets, and host target spelling, that are not always semantic
requirements. The PA38 harness canonicalizes permitted non-semantic differences
while still checking the required backend facts and generated program behavior.
Exact textual machine-IR matching is not a PA38 grading requirement unless a
test explicitly makes that shape part of the oracle.

### Testing

Run the PA38 suite with:

```sh
make test
```

`make test` runs:

- `tests/o1`
- `tests/o2`
- `tests/o3`
- `tests/behavior/o1`
- `tests/behavior/o2`
- `tests/behavior/o3`
- `course/pa38/driver` for the `cppgm++ --stats-functions -c` diagnostic
- `course/pa38/controls` for focused structural and behavioral predicates

Run the debug metadata preservation lanes with:

```sh
make test-debuginfo
```

`make test-debuginfo` runs:

- `tests/debuginfo/o1`
- `tests/debuginfo/o2`
- `tests/debuginfo/o3`

These directories are organized by backend role and validation mode, not by
N3485 source-language clauses.

- `tests/o1` runs `lowir2native -O1` over LowIR inputs and checks local
  backend cleanup.
- `tests/o2` runs `lowir2native -O2` over LowIR inputs, repeats the `-O1`
  surface, and adds O2-only layout and frame cleanup cases.
- `tests/o3` runs `lowir2native -O3` over LowIR already optimized by PA37 and
  checks that the O2 machine path remains in use.
- `tests/behavior/o1`, `tests/behavior/o2`, and `tests/behavior/o3` check
  successful lowering and generated-program behavior where several valid
  machine-IR layouts are possible. These tests do not compare a machine-IR
  oracle.
- `course/pa38/controls` checks only documented focused relationships: selected
  frame homes and edge movement, residency of a repeatedly compared
  iteration-local call result across an intervening call, and guarded
  fast-loop `phi` residency without extra preserved-register capacity.  It
  then runs the generated program, permits equivalent physical-register
  choices, and does not compare a complete MIR dump or executable image.
- A survivor-property pass also reuses selected small `course/pa38/o1`
  reducers at `-O0` and `-O1`. It checks only local relationships such as a
  frame home disappearing while its guarded twin remains, a call result being
  stored directly to an existing home, or a constant address remaining a
  memory operand. The complete canonical MIR sidecars remain compatibility
  fixtures and are not the feature oracle for those checks.
- `tests/debuginfo/o1`, `tests/debuginfo/o2`, and `tests/debuginfo/o3` run
  equivalent machine-IR rewrite cases carrying `!dbg(...)` metadata.

For each `.t` test, the harness builds with `--dump-machine-ir` and `-o`,
records implementation exit status, runs the generated program when the build
succeeds, and compares:

- implementation exit status
- optimized raw machine-IR dump for structural tests, with the checked-in
  `x.ref.mir` retained for debugging and `x.ref.cmir` used as the structural
  machine-IR oracle
- generated-program exit status
- generated-program standard output, when relevant

Behavior tests omit `x.ref.mir` and `x.ref.cmir`; their checked-in compilation
status and generated-program results are the oracle.

Failed reference builds are judged by implementation exit status. Successful
reference builds are judged by structural machine-IR validation and
generated-program behavior.

### Out Of Scope

PA38 does not require:

- changing the LowIR optimizer from PA37
- redefining `lowir2native -O0`
- source-language semantic changes
- wall-clock performance grading
- unbounded graph-coloring register allocation or a private virtual-register
  IR that is absent from the machine-IR dump
- instruction scheduling, vectorization, or target-specific peephole work not
  covered by the tests
- interprocedural backend optimization

### Design Notes

Whole-function placement can reuse the dense LowIR definition, use, last-use,
call-clobber, and edge-liveness facts built for baseline lowering. A compact
interval table indexed by LowIR value ID and fixed-size tables indexed by
physical register keep allocation linear or near-linear without string keys.

Treat ABI argument and result registers, division and shift registers, calls,
and exception edges as explicit constraints. Prefer a callee-saved register
only when its save/restore cost is lower than the frame traffic it avoids.
When a proof is unavailable, retain the ordinary frame-home path. The
optimized machine-IR dump must contain the final physical registers, frame
bindings, edge copies, and callee-save list used by native encoding.

A definition-time frame copy can be reused if acyclic pressure later evicts a
retained register. Inside a cycle, earlier machine instructions execute again
after a backedge, so plan enough headroom up front instead of changing their
assumed location while lowering a later instruction.

### Handoff

PA39 uses the PA37 LowIR optimizer and the PA38 machine-backend optimizer as
part of the self-host ladder. By the end of PA38, optimized and unoptimized
native paths should remain deterministic enough for staged self-host builds and
test reruns.

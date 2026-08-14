# PA36 Implementation Plan

## Stage Design and Spec Alignment

PA36 extends the existing semantic graph -> typed LowIR -> per-function MIR ->
ELF path with demand-driven hosted header definitions and host ABI identities.
Relevant `spec.md` requirements are canonical declaration/fact identity (2),
separate template definition and emission demand (4), recorded-fact typed
lowering with one lowering per emission unit (6), linear per-function backend
work (7, 9), bounded phase ownership (8), and no external compiler or
library-name output shortcuts (10). Closed compiler-intrinsic dispatch precedes
the generic hosted alias fallback, preserving builtin identity through lowering.

## Current Failure Map

Current result: 42/79, up from 26/79. The complete remaining set is 25 link
failures from missing demanded hosted definitions or ABI object identities, 10
semantic/template compilation failures, and 2 runtime-lowering/lifetime
failures. No generated PA36 object retains an `alloca` relocation.

## Active Checkpoint

Next: repair the 25-case required-definition/ABI closure at the semantic graph
to emission boundary. Owner/data flow is demanded-use edges -> canonical
function identity -> one definition or external ABI symbol -> ELF relocation.
Expected work is O(demand edges + emitted bodies), without rendered-name scans
or whole-program retries. Validate representative stream, tree-node, and lambda
cases, then full PA36, through-PA35 regressions, and file audit.

## Performance Evidence

The 83,073-token hosted string probe compiled 231 functions and 6,132 LowIR
instructions in 0.85 s at 43,288 KiB RSS; the pressure probe took 0.01 s at
8,900 KiB. Both passed and the hosted object had zero `alloca` relocations.
The dispatch reorder is O(1) and adds no lookup, demand, or backend iteration.

## Completed Checkpoints

| Checkpoint | Result | Validation |
| --- | --- | --- |
| Closed builtin dispatch before hosted alias fallback | Eliminated all 36 observed `alloca` relocations; PA36 26 -> 42/79 | PA33/PA34/focused PA36 pass; through PA35 4907/4907; audit pass |

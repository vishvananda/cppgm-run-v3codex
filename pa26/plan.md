# PA26 Plan

## Stage Design and Spec Alignment

PA26 extends the PA25 typed semantic graph and direct typed-LowIR path at four
owners: capture/closure lowering, initializer-list and aggregate semantics,
RTTI demand/lowering, and EH-aware lifetime lowering. Relevant `spec.md`
requirements are canonical `TypeId`/`EntityId` identity (sections 2 and 4),
indexed lookup with selected facts retained for lowering (section 3), direct
typed lowering with stable emission identity (section 6), phase-local
ownership (section 8), and measured O(n) or O(n log n) work (section 9).

## Current Failure Map

Current result: **26/106**, up from the turn-start **16/106**. The remaining 80
failures group without overlap by owning behavior:

| Owner | Failing | Shared behavior |
| --- | ---: | --- |
| EH and lifetime lowering | 36 | handlers, throw conversion, unwind snapshots, branch/full-expression cleanup |
| initializer-list and aggregate semantics | 26 | overload phase, backing arrays, ranges, class-element construction |
| lambda capture semantics/lowering | 17 | value/default/nested/template capture; includes two lambda/RTTI fixtures and the timeout |
| object construction lowering | 1 | nontrivial copy construction in the cv/reference `typeid` fixture |
| RTTI semantics/lowering | 0 | all 14 independently reachable RTTI cases pass |

## Active Checkpoint

**Canonical RTTI demand and query lowering — complete.** Semantic analysis owns
qualified `std::type_info` lookup, static/dynamic query facts, comparison
validation, and PA26 single-inheritance cast legality. The typed graph carries
canonical source/target types and vtable demand into one linear global-demand
scan; lowering uses indexed RTTI symbols and emits ordinary typed LowIR plus
the ABI runtime calls. Expected and measured complexity is O(D + T + B), with
O(1) lowering lookup. Focused validation is 14/17; each residual fixture stops
in a different owner before or outside RTTI. Full-stage validation is 26/106,
through PA25 is 3607/3607, and the PA26 file audit passes.

## Performance Evidence

Release compiler, three runs per point, distinct class `typeid` queries:

| Demanded types | Median wall time | Peak RSS range | Emitted RTTI globals |
| ---: | ---: | ---: | ---: |
| 128 | 0.01 s | 7.1–7.2 MiB | 128 |
| 256 | 0.01 s | 7.9–8.0 MiB | 256 |
| 512 | 0.03 s | 9.5–9.7 MiB | 512 |
| 1024 | 0.06 s | 12.9–13.4 MiB | 1024 |

Doubling 512 to 1024 doubles time and output count; no superlinear growth was
observed. Demand is deduplicated by indexed `TypeId` and traversed once.

## Completed Checkpoints

| Checkpoint | Result | Validation |
| --- | --- | --- |
| Canonical RTTI demand and query lowering | +10 PA26 tests; static/dynamic `typeid`, pointer/class/fundamental RTTI, pointer/reference `dynamic_cast`, canonical ABI names | RTTI 14/17 (3 cross-owner); PA26 26/106; through PA25 3607/3607; audit pass |

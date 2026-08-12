# PA26 Plan

## Stage Design and Spec Alignment

PA26 is a monotonic extension of the PA25 semantic graph and typed LowIR path.
PA12 completes polymorphism before publishing canonical class-special-member,
construction/destruction, lifetime, and ordered unwind facts. PA18 owns demanded
ABI RTTI/runtime symbols; PA15 and its PA16/PA17/PA26 mixins consume those facts
per function, with projected references kept distinct from indirect call-result
boundaries. Builtin logical nodes publish compact control identity, and root
branch cleanup uses the complete `(owner, child)` key in per-function flat
storage; deeper dependence uses runtime lifetime slots. This follows `spec.md`
sections 2, 4-6, and 8-10: compact identity, monotonic fact ownership, direct
typed lowering, bounded phase-local state, observable linear work, and no
textual, lookup-recovery, or external fallback.

## Current Failure Map

Current result is **90/110**, preserving the audit turn-start baseline.

| Owner | Failing | Shared behavior |
|---|---:|---|
| PA12/PA17 lifetime and EH lowering | 13 | aggregate/call ownership, conditional initialization, static/default arguments, parameter teardown |
| lambda/template identity and RTTI presentation | 4 | stable closure specialization identity, fallback emission, ABI names |
| object construction/access | 1 | protected empty-base constructor scope |
| aggregate control flow | 1 | empty indirect result through `switch` |
| typeid object conversion | 1 | cv/reference stripping without a spurious copy |

## Active Checkpoint

Complete class-value parameter ownership across the call ABI boundary. The PA26
assignment boundary and `spec.md` sections 2, 6, 8, and 9 require PA12 to publish
the selected copy construction and transfer owner once; PA16 owns caller argument
staging and callee prologue materialization, while PA17 consumes the transferred
destruction and unwind facts. Work must be O(arguments + constructed subobjects +
cleanup actions), without rescanning functions or reconstructing types. Validate
class-value caller cleanup transfer, indirect-parameter prologue copy, and class
array constructor-failure cleanup; measure argument-count and array-bound scaling.

## Performance Evidence

Root-guard siblings use indexed branch cleanup; deeper guards retain runtime
lifetime state. Current-binary counters grow linearly through 128:

| Shape | N | Nodes / visits | Branch actions / slots | Dispatch entries | Blocks / instructions | Storage | Semantic / lowering |
|---|---:|---:|---:|---:|---:|---:|---:|
| root siblings | 8 | 146 / 85 | 8 / 0 | 9 | 73 / 240 | 52,207 B | 0.71 / 0.33 ms |
| root siblings | 32 | 482 / 301 | 32 / 0 | 33 | 265 / 888 | 179,927 B | 1.49 / 0.60 ms |
| root siblings | 128 | 1,826 / 1,165 | 128 / 0 | 129 | 1,033 / 3,480 | 690,847 B | 5.14 / 1.90 ms |
| nested fallback | 8 | 87 / 75 | 0 / 8 | 8 | 75 / 280 | 60,666 B | 0.47 / 0.35 ms |
| nested fallback | 32 | 255 / 243 | 0 / 32 | 32 | 267 / 1,024 | 215,146 B | 0.84 / 0.63 ms |
| nested fallback | 128 | 927 / 915 | 0 / 128 | 128 | 1,035 / 4,000 | 833,320 B | 2.43 / 1.78 ms |

A demanded two-specialization `Probe<N>` witness records 15 specialization
requests, 8 cache hits, 8 demand pushes/emissions, and 2 branch actions, then
executes successfully through LowIR, CY86, and Linux ELF.

## Completed Checkpoints

| Checkpoint | Result | Validation |
|---|---|---|
| Canonical RTTI demand and query lowering | Canonical query/cast identity and ABI RTTI | RTTI 14/17; PA26 30/110; through PA25 3,607/3,607; audit pass |
| Lambda capture ownership | Closure-owned explicit/default captures and projected access | +12; PA26 42/110; through PA25 3,607/3,607; audit pass |
| Scalar initializer-list interoperation | Canonical specialization, scalar backing, references, `auto`, range-for | +14; PA26 56/110; through PA25 3,607/3,607; linear to 512 |
| List overload and class-backing boundary | List ranks/deduction, selected source, typed class recipes | +7; PA26 63/110; focused 7/7; linear to 512 |
| Initializer-list and aggregate lifecycle | Static backing/finalization, local frontier, parameter teardown | +4; PA26 67/110; focused 4/4; linear to 1,024 |
| Scalar source-exception foundation | Typed throw/handler facts, scalar/ellipsis catches, rethrow, runtime roles | +8; PA26 75/110; focused 8/8; nested handlers linear to 127 |
| Lexical unwind snapshots and handler continuation | Live-action snapshots, segmented exits, dispatch interning | +5; PA26 80/110; focused 9/9; calls/handlers linear through 256/64 |
| Class exception objects and typed-handler routing | Canonical polymorphic special-member facts, selected direct construction/destructor transfer, temporary retirement, projection-safe call ABI | +6; PA26 86/110; focused 6/6 plus PA18 projection witness; through PA25 3,607/3,607; file/audit pass; throws linear to 128 |
| Guard-edge full-expression cleanup | Typed logical facts, complete root guard/child identity, branch-local destruction, retained nested values, runtime fallback | +4; PA26 90/110; focused 4/4 plus ELF/template witnesses; through PA25 3,607/3,607; file/audit pass; both paths linear to 128 |

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

Current result is **94/110**, up four from the 90/110 turn-start baseline.

| Owner | Failing | Shared behavior |
|---|---:|---|
| PA12/PA17 lifetime and EH lowering | 8 | static, conditional/reference, nested default-argument, and shared-dispatch temporary cleanup |
| lambda/template identity and RTTI presentation | 4 | stable closure specialization identity, EH fallback emission, ABI names |
| object construction/access and cleanup | 2 | protected empty-base scope and polymorphic array-reference cleanup |
| aggregate control flow | 1 | empty indirect result through `switch` |
| typeid object conversion | 1 | cv/reference stripping without a spurious copy |

## Active Checkpoint

Unify nested call/default-argument temporary cleanup and dispatch closure. PA26
goal 5 and `spec.md` sections 2, 6, 8, and 9 require PA12 to publish one ordered
typed lifetime graph and handler stops, PA16 to evaluate argument-owned work
before the outer call region, and PA17/PA26 to consume and close every source
handler exactly once. Work must be O(expression nodes + arguments + emitted
cleanup actions), using compact IDs without reconstruction or function rescans.
Validate the eight remaining static, conditional/reference, nested-default, and
shared-dispatch cleanup failures; measure nesting-depth and argument-count work.

## Performance Evidence

Construction ownership has proportional argument work and bound-independent
fixed-array LowIR:

| Shape | N | Nodes / dependency visits | Instructions | Typed storage | Semantic / lowering |
|---|---:|---:|---:|---:|---:|
| class-value arguments | 8 | 31 / 18 | 38 | 12,313 B | 0.34 / 0.23 ms |
| class-value arguments | 32 | 79 / 66 | 110 | 31,801 B | 0.47 / 0.35 ms |
| class-value arguments | 128 | 271 / 258 | 398 | 109,753 B | 1.10 / 0.87 ms |
| class-array bound | 8 / 128 / 2,048 | 14 / 0 each | 35 each | 10,881 B each | 0.19-0.22 / 0.16-0.17 ms |

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
| Construction and call-ABI ownership | Runtime/default initializer staging, member/array source handlers, transferred class parameters | +4; PA26 94/110; focused 5/5; through PA25 3,607/3,607; audit pass; arguments linear to 128, array IR fixed through 2,048 |

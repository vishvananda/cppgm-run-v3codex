# PA18 Full-Stage Plan

## Stage Design and Spec Alignment

PA18 extends the PA11/PA12 canonical semantic graph and PA15-PA17 typed LowIR
with single-inheritance polymorphism. Each completed class owns an inherited,
source-ordered virtual-slot vector. A dense table keyed by canonical function
name/signature selects overrides, and a binding-indexed table maps selected
functions to physical vtable slots. The same retained facts drive vptr-at-zero
layout, constructor/destructor vptr actions, dynamic calls, vtable/RTTI globals,
and complete/base/deleting destructor entries.

```text
source -> PA10 SyntaxArena
       -> PA11 Program IDs + PA12 typed actions/polymorphism facts
       -> borrowed SemanticGraphView -> PA15-PA18 TypedProgram
       -> terminal LowIR text
```

The retained PA10 syntax graph and textual LowIR endpoint are assignment
boundaries. Within that staged surface, identity is canonical, lowering is
direct and typed, and no semantic or LowIR text is parsed back. Machine code
and ELF are later assignments and are not claimed for PA18.

## Performance Evidence

| Probe | Sizes | Final result |
|---|---:|---|
| One class with many virtual functions | 4,000/8,000/16,000 | Virtual signature lookups and vtable slots are exactly N; semantic time is 41.9/85.6/190.6 ms, wall time 0.13/0.23/0.49 s, and RSS 19,888/34,520/63,300 KiB. At N=16,000 semantic time fell from 776.1 ms to 190.6 ms with byte-identical LowIR. |
| Single-inheritance override chain | 800/1,600/3,200 | Classes, slots, signature lookups, vtable demands, and vtable slots are exactly N; overrides are N-1. Wall time is 0.09/0.17/0.45 s while output grows 0.78/1.57/3.16 MB. |
| Reused demanded function template | two `sink<int>` calls | Four request paths, three specialization-cache hits, one demand push, and one demanded declaration emission. |

Callgrind on the 800-class chain records 304.3 million instructions. String
interning is the largest self-cost at 11.96%; virtual-slot scanning is no longer
a dominant path. The measured PA18 work counters grow with declarations,
classes, and emitted slots.

## Architecture Review

| `spec.md` audit area | PA18 result |
|---|---|
| Representation and ownership | Pass for the staged contract. Source/syntax, one canonical semantic graph, and the typed program have bounded owners. The graph view is borrowed only during synchronous lowering; no text round trip exists. |
| Identity and lookup | Pass. Hot names, types, scopes, entities, functions, layouts, virtual roots/finals, and call targets are compact IDs. Dense signature and binding indexes replace slot rescans. |
| Templates and repeated work | Pass for the inherited declaration-only template surface. Specializations are keyed by canonical pattern/argument identities, demand is monotonic, and repeated requests reuse one declaration. Template-aware virtual classes are outside PA18. |
| Lowering and backend | Pass through typed LowIR. Dynamic calls consume retained slot numbers; vptr actions consume class IDs; vtables and destructor entries consume selected binding/symbol IDs. Each current-TU function list is merged once for ABI ordering. |
| Allocation and scaling | Pass. Phase-local vectors grow geometrically, virtual completion uses one dense table per class, call lowering uses O(1) slot lookup, and deleting-destructor discovery/merge are linear scans owned by the translation unit. |
| Self-containment | Pass. Production sources contain no host/reference compiler invocation, cached-output path, source/test-name branch, or required subprocess. |

## Final Architecture Review

The final audit closed illegal virtual-specifier acceptance, incomplete
covariant-return validation, loss of complete-destructor work during virtual
delete, quadratic wide-vtable completion, linear per-call slot lookup, missing
PA18 telemetry, and source-ownership/file-size blockers. Seven audit regression
tests cover these closures. The final file audit and full through-stage report
are the release gates; no correctness, architecture, performance,
self-containment, or file-audit blocker remains.

## Checkpoint Ledger

| Checkpoint | Final result | Principal evidence |
|---|---|---|
| PA17 handoff | Pass | PA1-PA17 retained behavior through the required full report |
| Virtual declarator legality | Pass after audit fix | Nonmember virtual, virtual constructor, and static override rejection |
| Canonical virtual identity | Pass after audit fix | Dense signature index, exact/covariant result checks, final/override/pure controls |
| Polymorphic layout/base projection | Pass | Offset-zero vptr and supported nonzero direct-base projection tests |
| Dynamic and qualified calls | Pass after audit cleanup | Binding-indexed slot and structured qualification suppression |
| Vtable/RTTI emission | Pass | Demand-owned globals, address points, pure slots, and physical destructor width |
| Constructor/destructor vptr actions | Pass | Typed class/action identities and complete/base entry controls |
| Virtual delete and class deallocation | Pass after audit fix | D0-to-D1 ownership, exceptional deallocation, ABI function order |
| Function-local polymorphic identity | Pass | Canonical local symbols without filename/source matching |
| Full-stage architecture | Pass after final audit fixes | Linear work counters, profiler review, file audit, and full validation |

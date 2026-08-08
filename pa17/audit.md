# PA17 Full-Stage Audit

## Findings

The independent audit started from a clean 241/241 PA17 and 1,677/1,677
through-stage baseline. Review of `spec.md`, the PA17 contract, all 33 PA17
stage commits, the complete stage delta, current records, and representative
data paths found no functional regression, test-specific branch, text
round-trip, lowering lookup, external compiler/reference dependency, or
cross-phase ownership leak.

It did find two performance architecture defects not exposed by the checked-in
tests:

1. A fixed non-trivial class array had O(bound) PA12 destructor nodes and
   O(bound) constructor/destructor LowIR expansion. A 16/64/256-element member
   array produced 69/117/309 semantic nodes, 16/64/256 destructor actions,
   41/137/521 blocks, 520/2,008/7,960 instructions, and
   96,392/355,496/1,392,228 typed bytes from essentially constant-size source.
2. Empty destructor-chain classification recursively revisited the same
   canonical base/member chain for each temporary. The complete-chain probe
   showed superlinear semantic time, and the unresolved-base probe made the
   repeated work explicit at 1,056/4,160/16,512 visits for N=32/64/128.

Both findings are closed. The 11 file-audit header-division warnings are
pre-existing advisory findings, not blockers; no warning was added.

## Changes

- PA12 now represents a large fixed member-array destructor obligation with one
  typed array action rather than one node per outer element. Small arrays up to
  eight retain the previous deterministic inline representation.
- PA16 constructor-array lowering emits a forward index loop above the inline
  limit. A throwing element constructor enters one cleanup loop that destroys
  exactly the successfully constructed prefix in reverse order before resume.
- PA16 destructor lowering flattens bounded multidimensional arrays and emits a
  reverse index loop above the same limit. Local, temporary, member, normal,
  and unwind destructor actions all use that owner.
- PA12 now memoizes the automatic-destructor lowering decision by canonical
  `BindingId`. State is monotonic: a destructor is either proven empty or is
  conservatively retained. A later empty out-of-line definition may miss an
  optional elision, but retained destruction remains correct and no stale
  positive fact is possible.
- Release telemetry now publishes empty-destructor-chain visits and cache hits,
  aggregates them across translation units, and includes cache storage in the
  semantic storage count.

## Architecture Review

### Representative declaration trace

For `Holder { Elem values[N]; }` with non-trivial `Elem` construction and
destruction, preprocessing retains source-offset tokens and PA10 parses the
declarations once into its assignment-mandated `SyntaxArena`. PA11 interns the
class names and types, assigns `EntityId`/`BindingId` identities to the classes,
member, constructors, and destructors, and stores the array as a canonical
`TypeId`. PA12 class completion selects the canonical `Elem` special members
and records constructor-array and destructor-array actions against the member
binding. No name or signature is recovered later.

`ConsumeSemanticTranslationUnit` exposes that graph as a synchronous borrowed
`SemanticGraphView`. PA15-PA17 creates function-owned typed slots, operands,
blocks, calls, EH edges, and forward/reverse array loops. The semantic and
syntax owners die after the callback; the typed program remains only until the
required multi-source LowIR view is rendered once. The rendered LowIR is never
parsed back by the production PA17 path.

At the boundaries, immutable source plus PA10 tokens/syntax are live while the
single PA11/PA12 graph is built; during synchronous lowering that graph and the
growing typed program overlap. The first overlap is required by the staged
PA10 contract and is the documented adaptation from `spec.md`'s future
integrated parser. No semantic-text copy or duplicate semantic graph exists.

### Demanded template trace

The supported inherited probe
`template<class T> void sink(T); sink<int>(1); sink<int>(2);` parses one pattern
and retains its syntax identity. The specialization table keys the canonical
pattern and canonical `int` `TypeId`; four request paths produce three cache
hits, one demand push, and one demanded declaration emission. Both calls retain
the same selected `BindingId`, and lowering emits one LowIR declaration and two
typed calls. There is no token replay or grammar reparse. General template body
instantiation and template-aware PA17 class-value semantics are outside the
assignment contract, so this audit does not claim their later-stage behavior.

### Adapted `spec.md` checklist

- Representation/ownership: each source, syntax, semantic, and typed owner has
  a bounded destruction phase; the graph view cannot outlive semantic analysis.
  No text is an in-process transport.
- Identity/lookup: `NameId`, `TypeId`, `ScopeId`, `EntityId`, and `BindingId`
  are hot keys. Scope/name/kind, function-signature, using-edge, hidden-friend,
  specialization, lifetime, and binding indexes constrain work to relevant
  facts. Deterministic text is a final view.
- Templates/repeated work: the available specialization signature is keyed by
  canonical identity and demand is monotonic/deduplicated. Lookup cache reverse
  dependencies invalidate only affected entries; there is no retry-all loop.
- Lowering/backend: typed lowering consumes selected bindings, conversions,
  layouts, ABI entries, and lifetime actions directly. Every function/ABI entry
  lowers once. Lifecycle-role coalescing is the only final function-wide scan
  and owns genuinely cross-translation-unit presentation. Machine IR and ELF
  are not PA17 surfaces.
- Allocation/scaling: semantic storage is translation-unit-owned, common small
  sequences stay inline, large vectors grow geometrically, and lowering state
  is function-local. The two accidental repeated-work paths found by telemetry
  are now bounded by semantic input and emitted IR.
- Self-containment: production implementation scans show no host/reference
  invocation, filename/source/test recognition, cached output, or required
  subprocess. The fork/exec code found in `test_runner.cpp` belongs solely to
  the test harness.

## Performance Evidence

### Fixed array lifetime work

| Bound | Before nodes/actions/blocks/instructions | Final nodes/actions/blocks/instructions | Before -> final typed bytes | Before -> final LowIR bytes |
|---:|---:|---:|---:|---:|
| 16 | 69 / 16 / 41 / 520 | 54 / 1 / 21 / 90 | 96,392 -> 23,230 | 21,657 -> 4,639 |
| 64 | 117 / 64 / 137 / 2,008 | 54 / 1 / 21 / 90 | 355,496 -> 23,230 | 83,626 -> 4,640 |
| 256 | 309 / 256 / 521 / 7,960 | 54 / 1 / 21 / 90 | 1,392,228 -> 23,230 | 339,616 -> 4,644 |

Final nine-run median semantic/lowering/render times in milliseconds are
0.251/0.204/0.077, 0.224/0.176/0.069, and 0.223/0.171/0.067. A 3x4
multidimensional probe records one destructor action and loop count 12, and a
nonthrowing-constructor probe omits EH cleanup while retaining the same bounded
loop shape.

### Destructor decision work

For complete empty chains at N=32/64/128/256, final visits are
64/128/256/512 with 31/63/127/255 cache hits. Semantic nodes are
106/202/394/778 and instructions are 67/131/259/515. Nine-run median semantic
times are 1.003/1.819/3.496/7.056 ms; lowering and render remain proportional.

For an unresolved out-of-line base destructor, visits fell from
1,056/4,160/16,512 to 64/128/256 at N=32/64/128. The N=128 semantic time fell
from 7.81 to 4.06 ms, and byte-for-byte LowIR comparison is unchanged.

### Class-value and lookup work

At 32/64/128 functional class-value member-call sites, overload candidates are
320/640/1,280, conversion checks 553/1,097/2,185, temporary visits
277/533/1,045, semantic nodes 426/778/1,482, instructions 254/446/830, and
typed bytes 60,691/99,187/176,179. Nine-run median
semantic/lowering/render times are 0.777/0.334/0.133,
1.255/0.436/0.212, and 2.254/0.660/0.361 ms. No unexplained slow path remains;
phase time follows the measured candidates, graph, and output.

## Validation

- Focused array lifetime controls: 3/3 pass, including synthesized member
  arrays, multidimensional lifetime, and non-trivial array new/delete.
- `make test-pa17`: 241/241 pass (228 handout plus 13 course audit tests).
- `perl scripts/cppgm_file_audit.pl --stage pa17 --paths dev/src`: pass with
  11 non-fatal established header-division advisories.
- `make test-report-through-pa17`: 1,677/1,677 tests and 17/17 stages pass.
- Final commit is cohesive and `git status --short` is empty.

## Checkpoint Audit Ledger

| Checkpoint | Audit disposition | Final evidence |
|---|---|---|
| Ref-qualified member boundary | Pass after checkpoint audit | Mixed cv/ref/static identity and ranking controls |
| Trivial class-value transfer | Pass | Direct ABI transfer controls |
| Synthesized assignments | Pass | Implicit/defaulted/deleted controls |
| Synthesized constructors | Pass | Call/return/materialization controls |
| Conversion functions | Pass | Indexed candidate and selected-conversion controls |
| Built-in converted operators | Pass | Operator ranking and scalar conversion controls |
| Scalar new/delete | Pass | Allocation, initialization, and deallocation controls |
| Dynamic array new/delete | Pass | Count, cookie, cleanup, and delete[] controls |
| Union semantics | Pass | Declaration, initialization, and lifetime controls |
| Temporary identity | Pass | Full-expression ordering and lifetime controls |
| Condition declarations | Pass | Scope and branch cleanup controls |
| Branch-local cleanup | Pass after checkpoint audit | Normal/unwind and conditional-state controls |
| Loop full expressions | Pass after checkpoint audit | Iteration cleanup and discarded-value controls |
| Class direct initialization | Pass after checkpoint audit | Direct/list/candidate controls |
| Constructor delegation/defaults | Pass after checkpoint audit | Delegation, complete/base entry, rejection controls |
| Composite subobject transfer | Pass after checkpoint audit | Base/member/array copy-move controls |
| Value category/reference binding | Pass after checkpoint audit | Binding, ancestry, and category controls |
| Canonical lookup/candidates | Pass after checkpoint audit | Using/hidden-friend/overload merge controls |
| Prvalue destination propagation | Pass after checkpoint audit | Direct/indirect result and temporary controls |
| Synthesized construction classification | Pass | Move/copy classification controls |
| Aggregate appertainment | Pass | Clause targeting and namespace copy controls |
| Function-exit ownership | Pass | Binding/CFG cleanup controls |
| Scalar/bit-field normalization | Pass | Width/storage owner controls |
| Functional class casts | Pass | Functional construction and member-object controls |
| Full-stage architecture | Pass after final audit fixes | Fixed-array loop scaling, destructor decision cache, full gates |

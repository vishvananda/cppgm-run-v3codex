# PA17 Full-Stage Plan

## Stage Design and Spec Alignment

PA17 is complete at 241/241 stage tests. It extends the PA11/PA12 canonical
semantic model and PA15/PA16 typed LowIR with non-polymorphic class value
semantics: special-member selection and synthesis, direct and indirect class
results, temporary identity and cleanup, constructor delegation, conversion
functions, scalar and array new/delete, unions, bit-fields, and aggregate
initialization.

The stage data flow is:

```text
immutable source buffer
  -> PA10 retained tokens and SyntaxArena
  -> PA11 Program (NameId/TypeId/ScopeId/EntityId/BindingId)
     plus PA12 DumpArena typed semantic actions
  -> borrowed SemanticGraphView
  -> PA15-PA17 TypedProgram
  -> one terminal textual LowIR view
```

The PA10 syntax boundary and textual LowIR endpoint are assignment contracts,
so `spec.md`'s integrated parser and source-to-ELF requirements are adapted at
those two boundaries. Within the available PA17 surface, semantic selection is
recorded once and lowering is direct and typed. There is no semantic-text or
LowIR-text transport, lowering lookup, host compiler, reference binary, or
name-based recovery path. The syntax and semantic graph are translation-unit
owned and released after synchronous lowering; the typed program remains for
the required final multi-source presentation.

## Performance Evidence

Final release telemetry covers source size, graph size, lookup and conversion
work, demand, lifetime work, typed storage, emitted blocks/instructions, output
bytes, and phase time.

| Probe | Sizes | Scaling result |
|---|---:|---|
| Fixed non-trivial member array | 16/64/256 elements | 54 semantic nodes, 1 destructor action, 21 blocks, 90 instructions, and 23,230 typed bytes at every bound; LowIR is 4,639/4,640/4,644 bytes. Nine-run median semantic/lowering/render time is 0.251/0.204/0.077, 0.224/0.176/0.069, and 0.223/0.171/0.067 ms. |
| Repeated class-value member calls | 32/64/128 sites | Candidates 320/640/1,280; conversions 553/1,097/2,185; temporary visits 277/533/1,045; instructions 254/446/830; typed bytes 60,691/99,187/176,179. Nine-run median semantic/lowering/render time is 0.777/0.334/0.133, 1.255/0.436/0.212, and 2.254/0.660/0.361 ms. |
| Empty destructor chain | 32/64/128/256 depth and uses | Visits are 64/128/256/512 with 31/63/127/255 cache hits; retained graph, instructions, storage, and median phase time are proportional. |
| Unresolved out-of-line base destructor | 32/64/128 depth and uses | Audit baseline visits 1,056/4,160/16,512; final visits 64/128/256 with identical LowIR. |
| Reused function-template declaration | two `sink<int>` calls | 4 specialization requests, 3 cache hits, 1 demand push, and 1 demanded declaration emission. |

The checkpoint probes also cover destination forwarding, synthesized
subobjects, aggregate appertainment, function-exit cleanup, bit-field emission,
and lookup/candidate identity. Their counters and phase times grow with the
represented semantic input or emitted IR.

## Architecture Review

| `spec.md` audit area | PA17 result |
|---|---|
| Representation and ownership | Pass for the staged contract. Source/PA10 syntax, one PA11/PA12 semantic graph, and typed LowIR have explicit owners. `SemanticGraphView` is borrowed synchronously; no text is reparsed. The assignment-mandated full PA10 arena overlap is recorded as a staged boundary, not hidden. |
| Identity and lookup | Pass. Names, types, scopes, entities, bindings, layouts, selected functions, conversions, temporary objects, and ABI entries use compact canonical IDs. Scope/name/kind and function-signature indexes avoid unrelated declaration scans; lowering consumes retained identities. |
| Templates and repeated work | Pass for the inherited declaration-only template surface available through PA17. Canonical template identity plus canonical arguments key specialization reuse, and monotonic demand emits once. Template-aware class-value semantics and general body instantiation remain explicitly outside PA17. |
| Lowering and backend | Pass through typed LowIR. Functions and ABI entries lower once from semantic facts. The only whole-program pass coalesces cross-translation-unit lifecycle roles. Machine IR, native code, and ELF are later assignments and are not claimed here. |
| Allocation and scaling | Pass. Translation-unit semantic arenas, compact IDs/indexes, inline-small sequences, geometric vectors, and function-local lowering state own hot data. Large fixed class arrays now lower to loops, and destructor-chain decisions are memoized. |
| Self-containment | Pass. Production sources contain no reference/host compiler invocation, cached answers, test/source-name branches, or subprocess output path. `dev/src/support/testing/test_runner.cpp` is test harness infrastructure and is not used to produce compiler output. |

The file audit's 11 header-division advisories are unchanged and non-fatal.
They identify established CRTP/model headers; this audit added no new source
owner and no new warning.

## Final Architecture Review

The final PA-wide review closed two blockers found despite a clean functional
baseline:

1. Fixed non-trivial arrays were expanded once per element in PA12 destructor
   actions and again in constructor/destructor lowering. Bounds above eight now
   retain one typed action and emit constant-size forward/reverse loops,
   including reverse cleanup of only successfully constructed elements.
2. Empty destructor-chain elision recursively recomputed the same base/member
   fact for every temporary. The semantic owner now records one monotonic
   decision per destructor binding: proven empty, or conservative retain. New
   counters expose visits and cache hits.

Small arrays retain their exact checked-in form. Focused array tests, all PA17
tests, the file audit, and the required through-stage report pass. No remaining
correctness, architecture, scaling, self-containment, or file-audit blocker was
found.

## Checkpoint Ledger

| Checkpoint | Final result | Principal closure |
|---|---|---|
| Ref-qualified member identity and selection | Pass | Canonical mixed-set identity, object ranking, and ABI path |
| Direct trivial class-value transfer and ABI | Pass | Exact direct transfer and retained boundary facts |
| Synthesized copy/move assignment | Pass | Implicit/defaulted/deleted classification and typed recipes |
| Synthesized copy/move construction | Pass | Materialization, value call, and return ownership |
| Conversion functions | Pass | Indexed candidates and retained selected conversion |
| Built-in operators after class conversion | Pass | Conversion reuse without lowering lookup |
| Scalar allocation/deallocation | Pass | Typed allocation, initialization, and delete actions |
| Dynamic array allocation/deallocation | Pass | Count/cookie/lifetime ownership and loop lowering |
| Union declaration and object actions | Pass | Active storage rules and non-polymorphic union semantics |
| Typed temporary identity | Pass | Stable temporary IDs and linear lifetime regions |
| Condition-declaration lifetimes | Pass | Scope-owned condition cleanup |
| Branch-local class cleanup | Pass | Normal/unwind state and shared cleanup suffixes |
| Loop full-expression regions | Pass | Per-iteration materialization and bounded cleanup |
| Class direct initialization | Pass | Selected constructor/list conversions retained once |
| Constructor delegation/default completion | Pass | Canonical complete/base entries and cycle/rejection rules |
| Composite subobject transfer | Pass | Member/base recipes and bounded array loops |
| Value-category/reference binding | Pass | Canonical category, conversion, and indexed ancestry facts |
| Canonical lookup/candidate identity | Pass | Indexed using relations and compact overload merging |
| Class-prvalue destination propagation | Pass | Direct/indirect result split and selected constructor invariant |
| Synthesized construction classification | Pass | Move/copy and class completion closure |
| Aggregate appertainment/namespace copies | Pass | Cached target facts and source-owned initialization |
| Function-exit identity/reachability | Pass | Binding-indexed cleanup and CFG ownership |
| Typed scalar/bit-field normalization | Pass | Width-correct values and storage-transfer owner indexing |
| Functional class-prvalue calls | Pass | Functional construction through member-object conversion |
| Full-stage architecture closure | Pass | Constant-size fixed-array lifetime IR and memoized destructor-chain decisions |

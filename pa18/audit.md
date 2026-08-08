# PA18 Full-Stage Audit

## Findings

The audit began from clean commit `a320c7ca`, a reported 29/29 PA18 and
1,706/1,706 through-stage baseline, and a passing file audit. The complete
`spec.md`, PA18 contract, single PA18 implementation commit, stage delta,
current plan, checked tests, and prior full-run log were reviewed independently.
The primary log does confirm 18/18 stages and 1,706/1,706 tests, but the clean
functional baseline concealed four architecture/correctness groups:

1. Virtual-specifier ownership was incomplete. Namespace-scope `virtual`, a
   virtual constructor, and `static ... override` were accepted.
2. Override identity ignored invalid return differences after matching the
   parameter signature. Unrelated pointer results, cv-worsening results, and an
   inaccessible covariance base were accepted.
3. A generated deleting destructor replayed only the vptr/base suffix. It could
   omit the complete destructor's user body and non-trivial member destruction.
4. Class completion compared every member with every existing virtual slot,
   making a wide class O(V^2), and each dynamic call rescanned the class slots.
   PA18-specific work counters were absent, so this repeated work was not
   directly observable.

All findings are closed. The audit found no production text reparse,
whole-program semantic retry, host/reference invocation, cached answer,
test-name branch, or cross-phase pointer escape. The 11 file-audit warnings are
unchanged advisory header-division findings and are not blockers.

## Changes

- Virtual/pure/override/final specifiers on static members are rejected in the
  virtual declaration owner. Namespace/out-of-class virtual specifiers and
  virtual constructors are rejected at their declaration boundaries.
- Override lookup now uses a canonical `(name, function signature)` key, with a
  destructor key for the shared destructor slot. A matched override separately
  validates exact or covariant return type, including pointer/reference kind,
  class ancestry, cv monotonicity, and base accessibility.
- Per-class completion constructs one dense signature table from inherited
  slots and updates it for source-ordered new slots. A binding-indexed physical
  slot table makes dynamic call lookup O(1); destructor slots retain width two.
- Deleting-destructor preparation scans typed function bodies once to determine
  whether the canonical complete destructor owns work not represented by the
  old inline vptr/base suffix. Such D0 entries call D1 under an EH cleanup and
  deallocate on both normal and exceptional exits.
- Generated deleting functions are merged once into the current translation
  unit immediately before their complete entries, preserving the required
  base/deleting/complete ABI order without repeated vector insertion.
- Qualified-call suppression now reads structured `NamePath` identity, and the
  implicit-object non-null rule compares the exact `this` `BindingId` rather
  than the rendered name.
- Release telemetry now reports polymorphic classes, virtual slots, signature
  lookups, overrides, slot lookups, vtable demands, dynamic calls, vptr stores,
  vtable entries, and deleting destructors. Their storage is included in the
  semantic storage count.
- Storage accounting moved to `pa12_semantic_storage.cpp`, and out-of-class
  special-member analysis moved to its semantic owner. Both prior oversized
  source files are below the audit limit, and the new source is registered in
  the `cppgm++` source set.
- Seven PA18 course regressions cover all rejected forms and the non-trivial
  virtual deleting-destructor path.

## Architecture Review

### Representative polymorphic trace

For a base with virtual `clone` and a virtual destructor, and a derived class
that overrides `clone`, owns a non-trivial member, and is deleted through the
base, preprocessing preserves source-offset tokens and PA10 parses once into
the assignment-mandated `SyntaxArena`. PA11 interns names/types and assigns
canonical `ScopeId`, `EntityId`, and `BindingId` values. PA12 class completion
uses the function signature table to replace the inherited slot, records the
root/final bindings and physical slot, gives the polymorphic layout its vptr at
offset zero, and records typed virtual-call, vptr, member-destruction, and
delete actions.

`ConsumeSemanticTranslationUnit` exposes the graph as a synchronous borrowed
`SemanticGraphView`. PA18 preparation maps class and function IDs to typed
symbols and emits demand-owned RTTI/vtable globals. Dynamic call lowering loads
the vptr and retained slot directly. Constructor/destructor actions store the
class vtable address point. The deleting destructor calls the canonical
complete entry when it owns user/member work, then invokes the retained class
or global deallocator; its cleanup does the same deallocation if D1 throws.
The resulting `TypedProgram` is rendered once as terminal LowIR and is never
parsed back by the production path.

At the parser boundary, immutable source plus retained PA10 tokens/syntax are
live. During synchronous lowering, that one PA11/PA12 graph overlaps the
growing typed program. The graph and its syntax owner are released after the
callback; the typed program remains only for the required multi-source output.
This overlap and the LowIR endpoint are staged assignment adaptations to the
future integrated source-to-ELF architecture in `spec.md`.

### Demanded template trace

The inherited supported probe
`template<class T> void sink(T); sink<int>(1); sink<int>(2);` parses one pattern
and keys its specialization by canonical pattern and `int` `TypeId`. Four
request paths produce three cache hits, one demand push, one demanded
declaration emission, and two binding-indexed call probes. Lowering emits one
declaration identity and two calls without token replay or grammar reparse.
General body instantiation and template-aware polymorphism are outside PA18,
so this audit does not claim later-stage behavior.

### Adapted `spec.md` checklist

- Representation/ownership: source, syntax, semantic graph, and typed program
  each have a bounded owner/destruction phase. The borrowed graph cannot escape
  semantic analysis, and no rendered text is an in-process transport.
- Identity/lookup: canonical IDs are hot keys. Scope/name/kind, function
  signature, specialization, binding-slot, layout, and symbol indexes constrain
  work to relevant facts; deterministic ordering is retained in vectors and
  applied only where the ABI/output requires it.
- Templates/repeated work: the available specialization key is canonical,
  demand is monotonic, and duplicate requests reuse one fact. Virtual
  completion and deleting-destructor emission use bounded indexed/linear work,
  with no retry-all loop or exception-driven expected failure.
- Lowering/backend: lowering consumes selected bindings, slot numbers, class
  layouts, action kinds, and ABI symbols directly. It performs no name lookup,
  source parse, or function-wide repeated global scan. Machine IR and ELF are
  not PA18 surfaces.
- Allocation/scaling: semantic and lowering vectors are phase-owned and grow
  geometrically. Dense tables use compact keys, the binding-slot map is flat,
  and the only new whole-TU operations are one graph scan and one function
  merge. Counters expose every PA18-sensitive work unit.
- Self-containment: a production-source scan found no `system`, `popen`,
  `fork`/`exec`, reference-binary name, test/ref path, or source-filename output
  branch. Test-runner process control is harness-only.

## Performance Evidence

### Wide virtual class

| Virtuals | Baseline semantic / wall | Final semantic / wall | Final RSS | Final signature lookups / slots | LowIR bytes |
|---:|---:|---:|---:|---:|---:|
| 4,000 | 70.4 ms / 0.14 s | 41.9 ms / 0.13 s | 19,888 KiB | 4,000 / 4,000 | 744,506 |
| 8,000 | 204.6 ms / 0.36 s | 85.6 ms / 0.23 s | 34,520 KiB | 8,000 / 8,000 | 1,492,506 |
| 16,000 | 776.1 ms / 1.09 s | 190.6 ms / 0.49 s | 63,300 KiB | 16,000 / 16,000 | 3,012,506 |

The final N=16,000 semantic phase is 4.1x faster. Output is byte-for-byte the
same, while declarations, signature probes, emitted slots, graph size, storage,
and output bytes all scale linearly. One dynamic call performs one slot lookup
at every size.

### Override chain and profile

| Classes | Overrides | Semantic / lowering | Wall / RSS | Output bytes |
|---:|---:|---:|---:|---:|
| 800 | 799 | 43.3 / 20.7 ms | 0.09 s / 18,036 KiB | 775,367 |
| 1,600 | 1,599 | 89.7 / 42.4 ms | 0.17 s / 30,460 KiB | 1,567,366 |
| 3,200 | 3,199 | 224.0 / 139.0 ms | 0.45 s / 55,792 KiB | 3,160,966 |

At each size, polymorphic classes, logical slots, signature lookups, vtable
demands, and emitted vtable slots equal N; slot lookup remains one. The larger
wall-time step follows the 3.16 MB typed output and memory/string work, not a
superlinear PA18 visit counter. Callgrind on N=800 records 304,299,447
instructions; `InternedStringTable::InternRange` is the largest self-cost at
11.96%, while no virtual-slot scan is a leading path.

## Validation

- Focused semantic/LowIR probes reject all six illegal declarations/returns
  and preserve complete destructor work under virtual delete.
- `make test-pa18`: 29/29 handout plus 7/7 course audit tests pass.
- `perl scripts/cppgm_file_audit.pl --stage pa18 --paths dev/src`: pass with 11
  unchanged non-fatal header-division advisories.
- `make test-report-through-pa18`: 1,713/1,713 tests and 18/18 stages pass.
- The cohesive audit commit leaves `git status --short` empty.

## Checkpoint Audit Ledger

The PA18 implementation arrived as one production checkpoint
(`a320c7ca`), so the ledger reconstructs its semantic ownership boundaries
rather than assuming the commit-level conclusion was complete.

| Checkpoint | Audit disposition | Final evidence |
|---|---|---|
| PA17 handoff | Pass | Full PA1-PA17 report retained |
| Virtual declaration parsing/legality | Pass after audit fix | Namespace, constructor, static, pure, override, and final controls |
| Override and covariance identity | Pass after audit fix | Canonical signature index plus kind, ancestry, cv, and accessibility controls |
| Class slot/layout completion | Pass after audit refactor | Source order, inherited roots, final functions, dtor width, vptr offset/base projection |
| Dynamic call selection | Pass after audit refactor | One binding-indexed slot probe and structured qualified-call suppression |
| Vtable/RTTI demand and globals | Pass | Retained class/function symbols, pure entries, RTTI/address points |
| Constructor/destructor vptr actions | Pass | Typed class/action ownership and complete/base phase tests |
| Virtual delete/deallocation | Pass after audit fix | Complete body/member work, normal/EH deallocation, D2/D0/D1 ordering |
| Function-local class identity | Pass | Canonical source ordinal/scope identities without filename recovery |
| Performance architecture | Pass after audit refactor | Linear slot counters, 4.1x wide-class closure, profile review |
| Representation/self-containment | Pass | Bounded graph view, no text round trip or external producer |
| File ownership and release gates | Pass after audit cleanup | Source split, source-set registration, file audit, focused/full reports, clean tree |

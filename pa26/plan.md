# PA26 Plan

## Stage Design and Spec Alignment

PA26 is complete at **110/110 local/course tests**, with all **3,607** earlier
tests retained. It extends the existing staged compiler rather than introducing
a parallel frontend or an alternate lowering route:

`source -> preprocessing/tokens -> SyntaxArena -> Program + DumpArena ->`
`SemanticGraphView -> typed LowIR -> LowIR renderer`

The syntax/parser/analyzer scratch dies before the graph consumer runs. PA26
facts cross that boundary as compact node, `TypeId`, `BindingId`, `EntityId`,
`ScopeId`, branch-owner/child, slot, and symbol identities. The renderer only
serializes the completed `TypedProgram`; lowering never reparses rendered
LowIR. The stage ends at typed LowIR, so `spec.md` object/ELF requirements are
owned by later stages and are not duplicated here.

Representative ownership traces are:

- A capturing template lambda is indexed by source syntax plus canonical
  enclosing/specialization context, publishes capture bindings and closure
  fields, and lowers retained calls, closure construction, RTTI, and exception
  actions from those identities.
- A class-element braced list marks the canonical `std::initializer_list`
  template specialization, selects list conversions, creates one backing-array
  temporary, attaches element construction/destruction and unwind actions, and
  lowers them through typed slots and cleanup blocks.
- `typeid`/pointer `dynamic_cast` publish canonical queried types and vtable
  demand. One graph scan closes type and base dependencies before ABI RTTI
  globals and runtime declarations are emitted.
- A conditional temporary records its full-expression owner and branch child.
  PA17 retires path-local cleanup before merge, while PA26 exception lowering
  maps handler nodes to function-local selectors.

This matches `spec.md` sections 2, 4-6, and 8-10: stable identity, monotonic
on-demand work, explicit phase boundaries, typed lowering, bounded scratch,
and observable scaling counters.

## Performance Evidence

Five-run medians compare the untouched stage tip (`91ce2ec6`) with the audited
binary on identical generated inputs. Times are internal phase telemetry;
selected largest LowIR outputs are byte-identical.

| Workload | Size | Baseline | Audited | Change | Structural evidence |
|---|---:|---:|---:|---:|---|
| Independent conditional functions | 4,096 | 91.659 ms lower | 62.262 ms lower | -32.1% | 4,099 resets; selector table grows once to 65,556 nodes |
| Single-inheritance RTTI chain | 2,048 | 132.781 ms lower | 92.887 ms lower | -30.0% | 4,095 base-dependency visits (`2N-1`) |
| Nested negative list-lifetime queries | 4,096 | 54.786 ms semantic | 32.095 ms semantic | -41.4% | 4,096 indexed queries for 4,096 conditions |

The repaired paths remain linear at intermediate sizes: branch lowering is
1.937/15.020/62.262 ms for 128/1,024/4,096 functions; RTTI lowering is
5.035/20.391/92.887 ms for 128/512/2,048 classes; nested-scope semantic time is
4.371/16.090/32.095 ms for 512/2,048/4,096 scopes.

Independent PA26-surface witnesses also scale with represented work:

- Default lambda capture at 32/128/512 names performs 65/257/1,025 syntax
  visits and records 32/128/512 name uses; semantic time is
  1.030/3.233/12.469 ms.
- Class initializer lists at 32/128/512 elements produce 95/287/1,055
  temporary-dependency visits and 238/718/2,638 instructions; semantic time is
  0.825/1.388/3.774 ms.
- 512 independent handlers produce 513 function resets, exactly 512 selector
  assignments, and one selector-table growth to the 8,198-node graph.

## Architecture Review

The PA-wide `spec.md` checklist was applied to the surfaces present in PA26.

- **Owning representations:** PA26 adds facts to the canonical semantic graph
  and typed LowIR only. There is no PA26 token/AST/IR duplicate and no textual
  round trip.
- **Identity and lookup:** semantic decisions use interned/canonical IDs.
  Rendered names and ABI mangles are presentation or object-format data, not
  semantic lookup keys. The recognized `std::initializer_list` name is the
  required language hook; later checks use its canonical template marker.
- **Demand and templates:** RTTI, vtables, constructors, destructors, retained
  lambda bodies, and exception helpers use deduplicated flags, caches, and
  worklists. Unevaluated `typeid` operands do not publish runtime-call demand.
- **Lifetimes and EH:** normal, branch, constructor-failure, call-argument,
  catch-miss, rethrow, and partial-array cleanup share ordered semantic actions
  and typed dispatch blocks. Function-local lowering state is reset without a
  whole-graph clear.
- **Scaling:** graph-wide discovery is one pass; per-function structures touch
  current entries; scope lifetime predicates are indexed; RTTI ancestry stops
  at a closed dependency. Counters cover each repaired work dimension.
- **Self-containment:** implementation does not invoke refs, previous
  solutions, host compilers, or external processes, and contains no test,
  filename, fixture, or source-text dispatch.
- **Scope boundary:** pointer `dynamic_cast`, capturing lambdas without init
  captures, initializer lists, `typeid`, and PA26 EH/lifetime behavior are
  covered. Multiple/virtual inheritance casts, `dynamic_cast<void*>`, and init
  captures remain outside the assignment contract.

Audit fixes removed four ownership-path hazards: TU-sized selector clearing per
function, retained-capacity cleanup-cache clearing per function, repeated RTTI
base-chain walks, and lexical-parent scans for initializer-list lifetime
queries.

## Final Architecture Review

**Pass.** The implementation is monotonic, typed, self-contained, and
phase-bounded. Every PA26 requirement reaches typed LowIR through canonical
facts; there is no lookup recovery or text-based fallback in lowering. The
largest generated witnesses preserve byte-identical LowIR after the audit
refactors, and no residual superlinear or unexplained hot path appears in the
instrumented dimensions. No correctness, architecture, performance,
self-containment, file-audit, or validation blocker remains.

## Checkpoint Ledger

| Checkpoint | Commits | Final audit result |
|---|---|---|
| Canonical RTTI demand and query lowering | `9eb277da`, `ffa3ae54` | Pass: canonical categories, evaluated demand, cast legality, typed ABI globals |
| Lambda capture ownership | `d5267826` | Pass: explicit/default captures, closure fields, projected access |
| Scalar initializer-list interoperation | `64e76f40` | Pass: scalar backing, references, `auto`, and range-for |
| List overload and class-backing boundary | `128ba385` | Pass: list ranking/deduction and typed class recipes |
| Initializer-list and aggregate lifecycle | `43c64bea` | Pass: static/local backing and parameter teardown |
| Scalar source-exception foundation | `b4f7f936` | Pass: throw/catch/rethrow facts and runtime roles |
| Lexical unwind snapshots and continuation | `e05062b1`, `8fd4193d` | Pass: ordered snapshots and interned dispatch suffixes |
| Class exception objects and routing | `336f0c80`, `a5e57e4a` | Pass: direct construction, destructor transfer, typed routing |
| Guard-edge full-expression cleanup | `e4d47678`, `fc7aead1` | Pass: owner/child identity and path-local retirement |
| Construction and call-ABI ownership | `17f052a8`, `1faf7b09` | Pass: staged lifetime start, transferred parameters, partial arrays |
| Nested call and lifetime frontier | `0c9fb54b`, `41b004f2` | Pass: default subtrees, guarded statics, indexed lifetime ownership |
| Canonical lambda specialization identity | `b6ad3640` | Pass: source/context identity and on-demand ABI/RTTI/EH |
| Projected lifetime and template boundaries | `b6ad3640` | Pass: projected destruction and polymorphic cleanup |
| Conditional initializer lifetime ownership | `91ce2ec6` | Pass: reachable-arm cleanup and final demand publication |
| Full-stage architecture audit | current audit | Pass: four scaling fixes, complete ledger, required gates clean |

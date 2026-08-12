# PA26 Final Audit

## Findings

The independent review reconstructed PA26 from the assignment contract,
`spec.md`, all stage commits since PA25, the complete source delta, and the
current test/ref inventory. The semantic graph to typed-LowIR architecture is
sound, but four final-audit performance/ownership defects were present:

1. Exception selector storage was assigned to the complete semantic-node count
   for every source or synthetic function. A translation unit with many small
   functions therefore performed O(functions x graph nodes) zeroing.
2. `CleanupDispatchCache::Clear` scanned its retained hash-table capacity for
   every function, even when the current function used few or no entries.
3. Every demanded vtable walked its complete direct-base chain even when an
   earlier vtable or RTTI demand had already closed the same ancestors. A deep
   single-inheritance family therefore performed quadratic dependency visits.
4. Every condition under an initializer-list backing lifetime walked lexical
   parents and each scope's lifetime obligations. Deep negative queries were
   quadratic in scope depth.

No output correctness defect accompanied these paths, but each violated the
bounded/local-work requirements in `spec.md`. No additional blocker was found:
PA26 facts use canonical IDs, demand is monotonic and deduplicated, typed
lowering does not recover semantic lookup, renderer text is not reparsed, and
the compiler contains no ref/host-compiler shellout or test/source dispatch.

## Changes

- Exception handler selectors now retain node-indexed value and epoch tables.
  Reset is O(1), table growth is TU-bounded, and only used handler nodes receive
  the current function epoch. The wrap path preserves correctness without
  imposing normal full-table clears.
- Cleanup dispatch now records occupied hash slots. Function reset clears only
  those entries; rehash rebuilds the occupied-slot index.
- RTTI vtable propagation stops at an already-demanded ancestor, whose base
  closure is already complete through `DemandRtti` or an earlier vtable root.
- Semantic scope state now carries the nearest active initializer-list backing
  lifetime. New scopes inherit one compact `ScopeId`; adding the backing
  obligation marks its owner; the condition query is O(1).
- Frontend telemetry now reports initializer-list lifetime queries, RTTI base
  dependency visits, exception selector resets, table growth, and assignments.
  Their storage is included in semantic side-storage accounting.
- Scope-index hooks and lifetime-query aggregation were centralized in their
  owning modules after the fresh file audit exposed two 3,000-line crossings
  and one oversized stats-driver function. All three fatal findings are gone.

The fixes span their actual owners in PA12 semantic scope/lifetime state,
PA17 temporary cleanup, PA18 global RTTI preparation, PA26 exception lowering,
and the shared PA15 telemetry handoff. They do not change the stage contract or
introduce PA26-specific files outside `dev/` implementation ownership.

## Performance Evidence

Measurements use five-run medians from the untouched pre-audit commit
`91ce2ec6` and the audited binary. Both compilers received the same generated
source. Current and baseline LowIR are byte-identical for the largest case in
each repaired family.

| Family | Scale | Baseline | Audited | Improvement | Audited work counter |
|---|---:|---:|---:|---:|---|
| Conditional temporary in independent functions | 4,096 | 91.659 ms lowering | 62.262 ms | 32.1% | 4,099 O(1) resets; 65,556 one-time table growth |
| Deep polymorphic class chain | 2,048 | 132.781 ms lowering | 92.887 ms | 30.0% | 4,095 RTTI base visits |
| Nested negative initializer-list lifetime query | 4,096 | 54.786 ms semantic | 32.095 ms | 41.4% | 4,096 indexed queries |

The counter slopes explain the remaining time: branch graph nodes grow
2,068/16,404/65,556 for 128/1,024/4,096 functions; RTTI ancestry is exactly
255/1,023/4,095 visits for 128/512/2,048 classes; list-lifetime queries are
512/2,048/4,096 for the same number of conditions. A 512-handler witness also
records 513 resets, 512 selector assignments, and one table growth to its 8,198
semantic nodes.

Other PA26 surfaces remain proportional to represented work. Default-capture
lambda families at 32/128/512 names visit 65/257/1,025 syntax nodes and retain
32/128/512 capture uses. Class initializer-list families at 32/128/512 elements
perform 95/287/1,055 temporary-dependency visits and emit
238/718/2,638 instructions. No unexplained residual hot path remained after
these counters and matched-size timings were examined.

## Validation

| Gate | Result |
|---|---|
| PA26 focused report | 110/110 passing |
| Earlier stages | 3,607/3,607 passing |
| Required through report | 3,717/3,717 tests; 26/26 stages passing |
| `perl scripts/cppgm_file_audit.pl --stage pa26 --paths dev/src` | Pass |
| Largest generated output comparisons | Byte-identical LowIR for all three repaired families |
| Source audit | No external compiler/ref invocation, fixture access, filename dispatch, or LowIR text round trip |
| Worktree handoff | Final audit commit; clean `git status --short` |

## Checkpoint Audit Ledger

| Checkpoint | Result |
|---|---|
| Canonical RTTI demand and query lowering (`9eb277da`, `ffa3ae54`) | Pass: canonical type demand, evaluated operand routing, pointer-cast legality, ABI RTTI globals |
| Lambda capture ownership (`d5267826`) | Pass: explicit/default capture identity, closure fields, copy/reference projection |
| Scalar initializer-list interoperation (`64e76f40`) | Pass: canonical scalar backing, `auto`, reference binding, range-for |
| List overload and class-backing boundary (`128ba385`) | Pass: two-phase list selection, conversion ranking, typed class construction |
| Initializer-list and aggregate lifecycle (`43c64bea`) | Pass: local/static backing lifetime, parameter boundary, ordered destruction |
| Scalar source-exception foundation (`b4f7f936`) | Pass: exception facts, scalar objects, catch routing, rethrow/runtime roles |
| Lexical unwind snapshots (`e05062b1`, `8fd4193d`) | Pass: bounded ordered snapshots, handler continuation, shared typed suffixes |
| Class exception objects (`336f0c80`, `a5e57e4a`) | Pass: canonical special members, direct object construction, destructor transfer |
| Guard-edge cleanup (`e4d47678`, `fc7aead1`) | Pass: compact branch identity and path-local full-expression retirement |
| Construction/call ABI (`17f052a8`, `1faf7b09`) | Pass: pre-lifetime unwind, parameter ownership transfer, partial arrays |
| Nested call/lifetime frontier (`0c9fb54b`, `41b004f2`) | Pass: default-subtree ownership, guarded statics, indexed enclosing lifetime |
| Lambda specialization and projected lifetimes (`b6ad3640`) | Pass: canonical source/context keys, deferred demand, projected destruction |
| Conditional initializer cleanup (`91ce2ec6`) | Pass: reachable-arm cleanup, retained-body demand, merge retirement |
| Full-stage architecture audit (current) | Pass: per-owner scaling fixes, independent performance evidence, complete gates |

Final result: PA26 is correct for its stated surface, architecturally aligned
with the applicable `spec.md` requirements, performance-bounded in the audited
dimensions, self-contained, and ready for PA27.

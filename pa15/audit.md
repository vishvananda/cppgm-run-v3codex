# PA15 Final Audit

## Findings

All findings are closed:

1. Hot instructions embedded presentation strings and four vector owners, inflating
   every scalar instruction to 448 bytes.
2. Every function zero-filled three whole-translation-unit ID maps, producing a
   quadratic many-function curve.
3. Recursive PA15 top-level, statement/control, and switch walks overflowed on valid
   deep inputs.
4. PA15 retained `Symbol&` across nested lowering that could append string-literal
   symbols; ASan proved a heap use-after-free.
5. PA12 retained type-table references and parameter spans across recursive type
   interning; ASan proved two independent heap use-after-frees.
6. Repeated inherited name lookup across nested control scopes was quadratic; `perf`
   attributed 97.5% of samples to `LookupName`/`FindEntry`.
7. PA12 eagerly interned a full qualified prefix for every nested namespace, retaining
   quadratic presentation data.
8. Lowering, model implementation, and text rendering exceeded their appropriate file
   ownership boundaries.

## Changes

- Compact LowIR records now store typed POD fields; floating/null literals are
  interned, while call arguments and switch cases live in flat program-owned tables.
- Function-wide ID maps are graph-owned and initialized once. Top-level emission,
  control lowering, statement sequencing, and switch discovery use explicit worklists.
- Symbol mutation is performed by stable ID after nested lowering. PA12 snapshots
  compact type records and copies function parameter IDs before recursive analysis.
- PA11 owns a revisioned derived lookup cache invalidated by bindings, namespaces,
  aliases, using edges, and type-name mutations.
- Namespace prefixes retain structural parent/segment facts and materialize a full
  presentation spelling only when an emitted name requests it.
- PA15 is divided into typed records, model/index implementation, graph lowering, and
  text rendering, with each new source listed in `frontend_source_sets.mk`.

## Performance Evidence

- Assignments, 5k -> 10k: instructions 15,003 -> 30,003; lowering 7.31 ->
  15.58 ms; typed storage 1.97 -> 3.93 MB. Pre-audit storage was 7.79 -> 15.58 MB.
- Functions, 4k -> 8k -> 16k: lowering 12.27 -> 24.84 -> 50.48 ms. The former
  2k/4k/8k curve was approximately 11/34/103 ms and profiled in repeated zero-fill.
- Nested `if`, 8k -> 16k: 0.07 -> 0.16 s total. Before lookup caching, the 16k
  semantic phase alone took 6.27 s.
- Nested namespaces, 4k/8k/12k: 0.02/0.05/0.07 s total after lazy prefixes. The
  former semantic-only runs took 0.29/1.12/2.64 s.
- 32k compound nesting and 32k pointer modifiers complete in 0.09 and 0.05 s.

## Validation

- `perl scripts/cppgm_file_audit.pl --stage pa15 --paths dev/src`:
  pass, 66 files, zero findings.
- Combined ASan+UBSan fixture runs: PA11 68+2, PA12 166+8, PA15 108; all pass.
- Process-only `strace`: initial compiler `execve`, `exit_group(0)`, no child process.
- `make test-report-through-pa15`: pass, 1,145/1,145 tests and 15/15 stages.

## Checkpoint Audit Ledger

| Checkpoint | Audit result | Closure evidence |
|---|---|---|
| Scalar semantic handoff | Pass after audit fixes | Typed identity, correct linkage/truth handling, no text identity, linear scalar/declarator probes |
| Procedural lowering | Pass after audit fixes | Complete PA15 contract, explicit CFG worklist, flat side tables, no recursive PA15 depth failure |
| Full-stage final audit | Pass | UAFs removed across PA12/PA15 ownership boundaries; semantic quadratics removed; file audit, sanitizers, self-containment trace, and 1,145-test report clean |

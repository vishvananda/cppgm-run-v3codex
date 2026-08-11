# PA25 Plan

## Stage Design and Spec Alignment

PA25 remains an extension of the shared pipeline: PA10 parses source once into
compact syntax plus interned semantic payloads; PA12 publishes canonical
declarations, `TypeId`s, selected conversions, lifetimes, layouts, template
specializations, and closure facts; PA15 consumes that borrowed graph into
typed LowIR. Text syntax, semantic dumps, and LowIR are output views, not
in-process transport. PA25 stops at LowIR, so `spec.md` machine-IR and ELF
requirements are checked through the later backend adapters available in the
tree.

The PA25 surfaces preserve that division:

- Placeholder and retained-template result deduction use canonical four-state
  body facts, cache-before-analysis, and completed function-type publication.
- Range-for syntax uses bounded lookahead; semantics owns one range
  materialization, selected member/ADL `begin` and `end`, iterator operations,
  hidden locals, and complete-statement cleanup.
- Aggregate and class conversion paths retain member actions, selected
  constructors/conversion functions, and class-value ABI facts for direct
  typed lowering.
- Lambda introducers now publish semantic-only interned capture facts during
  the only parse. A syntax-owned, four-state capture-use summary is keyed by
  `NodeId`; canonical closures remain keyed by enclosing binding and syntax.
  Capture fields, aliases, call operators, results, ABI context, and ordinals
  remain semantic facts consumed directly by lowering.

This satisfies the applicable `spec.md` requirements for one parse, compact
canonical identity, indexed lookup, retained dependent bodies, monotonic
demand states, direct typed lowering, explicit phase ownership, and measured
work. Production frontend sources contain no reference-compiler, host-compiler,
or subprocess fallback.

## Performance Evidence

The audit exposed repeated full-body scans for nested `[&]` lambdas. At depth
64 the old path performed 183,232 lexical scope visits and spent 48.46 ms in
semantics; depth 128 reached 1,431,424 visits, 307.55 ms, and 55.4 MB of peak
semantic storage. Capture syntax is now summarized once and nested summaries
are reused.

| Workload | N | Capture syntax visits / uses | Summary requests / hits | Lookup scope visits | Median semantic time | Typed storage | LowIR bytes |
|---|---:|---:|---:|---:|---:|---:|---:|
| Empty nested `[&]` | 16 | 153 / 0 | 31 / 15 | 352 | 1.378 ms | 43,263 | 5,592 |
| Empty nested `[&]` | 64 | 633 / 0 | 127 / 63 | 4,480 | 5.881 ms | 173,086 | 22,379 |
| Empty nested `[&]` | 256 | 2,553 / 0 | 511 / 255 | 67,072 | 47.598 ms | 692,669 | 90,214 |
| Nested outer-variable use | 16 | 153 / 16 | 31 / 15 | 418 | 1.394 ms | 58,933 | 7,532 |
| Nested outer-variable use | 64 | 633 / 64 | 127 / 63 | 4,738 | 6.010 ms | 234,853 | 29,900 |
| Nested outer-variable use | 256 | 2,553 / 256 | 511 / 255 | 68,098 | 47.678 ms | 938,984 | 120,178 |
| Wide explicit capture | 16 | 0 / 16 | 1 / 0 | 103 | 0.594 ms | 35,193 | 4,988 |
| Wide explicit capture | 64 | 0 / 64 | 1 / 0 | 391 | 1.531 ms | 132,681 | 19,746 |
| Wide explicit capture | 256 | 0 / 256 | 1 / 0 | 1,543 | 5.805 ms | 522,633 | 80,905 |

Capture visits, uses, storage, and output scale with syntax, required capture
edges, and emitted IR. Maximum LowIR line length stayed at 108-144 bytes. The
remaining superlinear count under pathological lexical nesting is the explicit
sum of distinct parent-scope edges visited by ordinary unqualified lookup; a
dependency-cache experiment did not reduce those visits and was discarded.

## Architecture Review

- Parsing: no `PayloadSource` reconstruction or capture grammar replay remains.
  Capture names enter semantics as interned IDs; the public PA10 syntax view is
  unchanged because the new facts are semantic-only.
- Identity: types, declarations, specializations, capture names, and closure
  keys are compact identities. Internal closure presentation names no longer
  recursively embed enclosing names; ABI identity still uses canonical local
  context plus lambda ordinal.
- Lookup and conversions: member/ADL range operations and class conversions
  are selected once. Lambda capture discovery produces names only; ordinary
  semantic lookup resolves each required name once per closure.
- Templates and demand: retained bodies and deduced results use explicit
  not-started/in-progress/succeeded/failed states and demand worklists. No
  whole-program retry or fixed-point rescan was found.
- Lowering and lifetime: PA15 reads selected bindings, typed actions, layout,
  cleanup, and ABI facts. No lowering-time overload search or textual LowIR
  round-trip was found.
- Ownership and self-containment: analyzer/parser scratch dies before borrowed
  graph lowering, output is rendered once, and production frontend code does
  not invoke host tools or reference binaries.

## Final Architecture Review

The range lifetime trace is source -> one `range-for-statement` -> hidden
`__range`/`__begin`/`__end` bindings -> selected `Range::begin` and
`Range::end` -> typed loop CFG -> destructor action at complete-statement exit
-> LowIR. The generated LowIR passed PA13 LowIR-to-CY86 lowering, PA9 Linux ELF
emission, and execution with status 0.

The demanded-template trace is one retained template body -> canonical
specialization -> capture-use summary -> closure/capture fields -> retained
call operator and pack substitutions -> demand worklist -> typed call/body
LowIR. It also passed the PA13/PA9 Linux ELF route with status 0. PA29 in this
checkout returns its explicit not-implemented status 86 before MIR emission,
so it is a downstream-stage limitation rather than a PA25 transport failure.

All final-audit findings are closed: the capture text reparse is gone, nested
subtrees are summarized once, capture-list classification is indexed,
duplicates are rejected, and closure presentation identity has bounded growth.
No correctness, architecture, performance, self-containment, or file-audit
blocker remains.

## Checkpoint Ledger

| Checkpoint | Commits | Final disposition |
|---|---|---|
| Ordinary placeholders | `583b174a`, `7737d2a5` | Canonical variable/function results and retained-body ownership pass. |
| Range-for | `b985f854`, `db9bf14a` | One materialization, selected operations, hidden locals, and cleanup pass. |
| Aggregate initialization | `ece08579` | Direct/nested aggregate plans and typed helper boundaries pass. |
| Class conversions | `cec97359`, `c3651ce0` | Selected conversion functions and direct class-value boundaries pass. |
| Template placeholder results | `2e7bf454`, `1d508e97` | Four-state result demand and canonical ABI publication pass. |
| Captureless call operators | `60cd11b4`, `ade1022b` | Canonical closure/body, pack identity, and lexical access facts pass. |
| Captureless pointer conversion | `440c7070` | Static invoker and retained pointer-conversion facts pass. |
| By-reference/`this` captures | `7c963c77` | Capture layout, nested/pack propagation, transfer, and ABI pass. |
| Final PA-wide audit | this audit | One-parse capture facts, cached free-use summaries, bounded presentation identity, duplicate diagnosis, performance counters, and final validation pass. |

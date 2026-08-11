# PA26 Plan

## Stage Design and Spec Alignment

PA26 remains a monotonic extension of the shared PA25 pipeline. PA10 parses
once; PA12 publishes canonical declarations, types/entities, selected
operations, layouts, lifetimes, template/closure facts, and demand; PA15
borrows that graph to construct typed LowIR directly. Syntax, semantic dumps,
ABI names, and LowIR remain output views rather than semantic identity or
in-process transport.

RTTI queries, lambda captures, and scalar initializer lists now have explicit
semantic owners. The canonical `std::initializer_list<T>` specialization owns
its element/layout fact; each braced conversion owns its selected overload and
element conversions; an ordinary array-temporary recipe owns backing storage
and lifetime. Calls, `auto`, range-for, slot planning, and lowering consume
those retained facts without lookup or syntax recovery. This follows
`spec.md` sections 2-6 and 8-10: canonical identity, direct typed lowering,
precise demand, explicit ownership, and observable bounded work.

## Current Failure Map

Current result is **56/110**, up from **42/110** at this checkpoint's start.
The complete remaining set is grouped by first owning boundary:

| Owner | Failing | Shared behavior |
|---|---:|---|
| EH and lifetime lowering | 39 | handlers/throws, unwind snapshots, branch/full-expression cleanup, construction failure |
| initializer-list and aggregate semantics | 11 | assignment/member/template ranking, nested lists, class-element EH, backing duration |
| lambda/RTTI presentation integration | 2 | closure ABI spelling in RTTI name objects |
| template emission ordering | 1 | stable anonymous ordering across member-template specializations |
| object construction lowering | 1 | nontrivial copy in the cv/reference `typeid` fixture |

## Active Checkpoint

Complete the remaining initializer-list/aggregate boundary. PA12 owns the
assignment/member overload phase and braced template deduction, recursively
retains nested list conversions, and exposes per-element construction and
cleanup recipes. Storage-duration analysis promotes namespace backing arrays;
PA15 consumes only those facts and emits per-element EH transitions. Data flows
from canonical specialization element facts through selected callable and
array recipes to lifetime/slot planning and lowering. Expected work is
O(candidates + converted elements + constructed elements), with indexed
specialization and braced-fact reuse. Validate the 10 remaining list cases and
the adjacent nontrivial aggregate case, measure class-element scaling, then run
the PA26 report, through-PA25 report, and file audit.

## Performance Evidence

Release compiler, three runs per point; timings/RSS are medians. A generated
`initializer_list<int>` call exercises the production semantic and LowIR path:

| Elements | Conversions | Semantic nodes | Instructions | Semantic | Lowering | Typed storage | RSS |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 64 | 130 | 76 | 135 | 0.507 ms | 0.181 ms | 33,435 B | 6,536 KiB |
| 128 | 258 | 140 | 263 | 0.621 ms | 0.260 ms | 64,155 B | 6,620 KiB |
| 256 | 514 | 268 | 519 | 0.880 ms | 0.331 ms | 125,595 B | 6,872 KiB |
| 512 | 1,026 | 524 | 1,031 | 1.436 ms | 0.537 ms | 248,475 B | 6,972 KiB |

From 256 to 512, conversion/node/instruction/storage counts double and
semantic/lowering time grows 1.63/1.62x. Prior capture scaling was also linear
through 512 captures, and the former nested-return timeout remains eliminated.

## Completed Checkpoints

| Checkpoint | Result | Validation |
|---|---|---|
| Canonical RTTI demand and query lowering | Canonical query/cast identity and ABI RTTI | RTTI 14/17; audit 4/4; PA26 30/110; through PA25 3,607/3,607; audit pass |
| Lambda capture ownership | Explicit/default value/reference members, class copy/lifetime facts, projected access, cycle-safe identity | 12 new passes; PA26 42/110; timeout removed; through PA25 3,607/3,607; audit pass |
| Scalar initializer-list interoperation | Canonical specialization/element fact, two-phase construction, backing arrays, references, `auto`, range-for, functional/return materialization | 14 new passes; PA26 56/110; focused checks pass; through PA25 3,607/3,607; audit pass; linear through 512 elements |

# PA26 Plan

## Stage Design and Spec Alignment

PA26 remains a monotonic extension of the shared PA25 pipeline. PA10 owns the
single parse, PA12 publishes canonical declarations, `TypeId`s, `EntityId`s,
selected operations, layouts, lifetimes, template/closure facts, and demand,
and PA15 borrows that graph to construct typed LowIR directly. Textual syntax,
semantic dumps, ABI names, and LowIR are output views rather than in-process
transport or semantic identity.

RTTI demand and lambda capture ownership now publish indexed semantic facts
once: canonical queried types, closure entities, per-name capture mode/source,
closure members, selected copy construction, and lifetime facts. PA15 consumes
those identities directly; nested local-type identity depends on the enclosing
function signature rather than its return type, so closure-return cycles remain
bounded. This aligns with `spec.md` sections 2-6 and 8-10: canonical identity,
precise demand, direct typed lowering, explicit ownership, observable bounded
work, and self-contained implementation.

## Current Failure Map

Current result is **42/110**, up from **30/110**. The remaining 68 failures are
partitioned by their first owning behavior:

| Owner | Failing | Shared behavior |
|---|---:|---|
| EH and lifetime lowering | 38 | handlers, throw conversion, unwind snapshots, branch/full-expression cleanup; includes two lambda-body cases |
| initializer-list and aggregate semantics | 26 | overload phase, backing arrays, ranges, class-element construction |
| lambda/RTTI presentation integration | 2 | closure ABI type spelling in RTTI name objects |
| template emission ordering | 1 | stable anonymous function ordering across two member-template specializations |
| object construction lowering | 1 | nontrivial copy construction in the cv/reference `typeid` fixture |

## Performance Evidence

Release compiler, three runs per point. `CPPGM_FRONTEND_STATS` counters and
timings are production-path observations. For `[=]` bodies using every local:

| Captures | Syntax visits | Layout members | Semantic | Lowering | Typed storage | LowIR output | RSS |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 64 | 129 | 64 | 4.434 ms | 0.927 ms | 132,681 B | 18,248 B | 7,104 KiB |
| 128 | 257 | 128 | 10.074 ms | 2.395 ms | 262,665 B | 36,792 B | 7,652 KiB |
| 256 | 513 | 256 | 19.085 ms | 3.927 ms | 522,633 B | 74,229 B | 8,708 KiB |
| 512 | 1,025 | 512 | 37.307 ms | 7.812 ms | 1,042,569 B | 151,595 B | 10,748 KiB |

From 256 to 512, capture/layout work and storage double, semantic/lowering time
grows 1.95/1.99x, and output grows 2.04x. The former nested-return timeout now
finishes with two closure requests, two capture facts, 0.347 ms semantic and
0.201 ms lowering time, 8,408 B typed storage, and 6,692 KiB RSS. The prior RTTI
series also doubled its demand/lookups exactly through 1,024 queried classes.

## Active Checkpoint

Implement initializer-list interoperation as one overload-to-lifetime
increment. PA12 owns the selected initializer-list constructor/conversion,
canonical element type, conversion facts, backing-array storage duration, and
destruction actions; range-for borrows its `__begin`/`__size` facts; PA15 emits
the recorded backing storage and element construction directly.

This applies `spec.md` sections 2-4, 6, 8, and 9: selected overload/conversions
are retained by canonical identity, demand stays at the specialization/storage
owner, and lowering performs no textual or lookup recovery. Expected work is
O(required candidates and element conversion checks + materialized elements),
with one linear backing-storage/lifetime plan. Validate all 26 mapped cases,
measure scalar and class-element list scaling, then run the full PA26 report,
through-PA25 report, and file audit.

## Completed Checkpoints

| Checkpoint | Result | Validation |
|---|---|---|
| Canonical RTTI demand and query lowering | Canonical queries/casts and ABI RTTI; audit closed conditional demand, unreachable scans, missing type categories, and cast legality | RTTI 14/17 (3 cross-owner), audit 4/4, PA26 30/110 augmented (original 26/106), through PA25 3,607/3,607, file audit pass |
| Lambda capture ownership | Explicit/default copy/reference members, const/mutable aliases, class copy/lifetime facts, captured-object projection, and cycle-safe nested closure identity | 12 new passes; capture boundary 18/23; PA26 42/110; former timeout passes; through PA25 3,607/3,607; file audit pass |

# PA30 Final Audit Plan

## Stage Design and Spec Alignment

PA30 is the full source/object/link driver over the shared front end and PA29
native backend. Its production flow is:

```text
source/options -> canonical semantic graph -> typed PA15 LowIR
               -> direct PA30 adapter -> versioned compiler object
compiler objects + ELF64 helper objects -> indexed canonical link
               -> per-function LowIR analysis/MIR/encoding -> fixups -> ELF
```

The semantic graph dies inside `BuildTypedLowIRProgram`; the returned typed
LowIR is adapted in memory without a textual view. Object I/O streams the typed
backend model. The linker indexes ABI symbol identities, renames internal
identities once per TU, coalesces weak ODR definitions, and constructs ordered
init/fini aggregators. The compiler driver then retains only program-wide
link/global/fixup facts while a `ProgramLoweringSession` lowers, encodes, and
reclaims one MIR function at a time. The standalone PA29 tool deliberately
retains MIR for its requested dump and accepts textual LowIR through the
explicit parser adapter.

This satisfies `spec.md` sections 1-10: canonical semantic identities and
demand facts, direct typed lowering, role-based runtime facts, average-O(1)
link indexes, bounded per-function native state, direct ELF emission,
low-overhead release counters/timers, and no host/reference/compiler process in
the required path. Foreign ELF input is the assignment-authorized helper
boundary and is parsed, relocated, and linked in-process.

PA30 object mode explicitly enables complete typed constructor-unwind demand.
The staged `--emit-lowir` tool retains PA15's checked historical
explicit-throw view so through-stage reference contracts remain stable; this
is a driver-selected assignment surface, not a production recovery fallback.

## Performance Evidence

Release measurements link one compiler object containing 5,001/10,001/20,001
functions/symbols. Times are wall seconds and memory is peak RSS KiB.

| Functions | Pre-audit time / RSS | Final time / RSS | Final work counters |
| ---: | ---: | ---: | --- |
| 5,001 | 0.08 / 26,324 | 0.05 / 15,144 | 10,002 symbol, 5,002 rename probes |
| 10,001 | 0.16 / 47,880 | 0.11 / 26,212 | 20,002 symbol, 10,002 rename probes |
| 20,001 | 0.34 / 90,972 | 0.23 / 48,296 | 40,002 symbol, 20,002 rename probes |

At 20,001 functions, measured phases are 109.47 ms input, 43.13 ms link,
40.01 ms lowering, and 18.80 ms encoding. LowIR/MIR counts are 20,009/40,019;
all counters scale linearly. Peak RSS falls 46.9% from the original path.
Compiler-object bytes and final executables are byte-identical at all three
sizes. A truncated 12.7 MiB-advertising object fails in 0.00 s at 4,376 KiB
instead of allocating from untrusted counts.

The demanded-template trace performs one request and one demand push in each
TU, then links five symbols with nine symbol probes and one weak coalescence.
The cross-TU data-relocation trace links four symbols, three definitions, four
LowIR instructions, and ten MIR instructions. Both execute with their expected
results.

## Architecture Review

- Representation and ownership: source, semantic, typed LowIR, serialized
  object, linked LowIR, function-local MIR, and ELF buffers have explicit
  owners. No text is rendered and reparsed in production. Object serialization
  and final ELF output avoid duplicate whole-file buffers.
- Identity and lookup: semantic type/declaration/template identity remains
  compact IDs. Linkage uses indexed ABI strings only at the object boundary;
  each rename and definition lookup is one hash probe. Runtime, lifecycle,
  RTTI, allocator, and startup behavior is carried by typed roles.
- Templates and demand: the representative out-of-class template body is
  retained once per TU, demanded once for its complete specialization, emitted
  weakly, and coalesced once. No retry-all loop or lowering-time lookup exists.
- Lowering and backend: selected declarations, conversions, layouts, lifetime
  actions, member-pointer slots, integer values, symbol roles, and ABI names
  cross typed boundaries. Every function is lowered and encoded once; MIR is
  reclaimed before the next function. Final labels/fixups are program-wide as
  required for relocation layout.
- Allocation and scaling: streamed object I/O, moved linked definitions,
  bounded object counts, geometric containers, one function-local allocator,
  and linear probe/timing slopes replace duplicate object/MIR ownership. No
  global mutable cache or complete-program fixed-point retry is present.
- Self-containment: searches and traces find no process launch, assembly,
  prior-stage/reference invocation, fixture dispatch, filename/source
  recognition, or cached answer. External helper compilation exists only in
  the test harness; PA30 consumes its ELF relocatable input itself.

## Final Architecture Review

All audit findings are closed at their owning boundaries. PA30 constructor
unwind demand consumes typed nonthrowing facts rather than source syntax. Legacy
LowIR spellings are normalized once by the parser, while the backend requires
roles. ABI object symbols and aliases survive through MIR to final labels, and
supported Clang/GCC helper relocations are resolved in-process. Typed scalar
values no longer undergo production text parsing. The object reader enforces
its remaining-byte budget before allocation, and the driver exposes enough
phase and work evidence to distinguish required work from repetition.

The exact PA30 file audit passes with the same 21 inherited header-division
warnings recorded at PA29. No correctness, architecture, performance,
self-containment, file-placement, or file-audit blocker remains.

## Checkpoint Ledger

| Checkpoint | Final status | Evidence |
| --- | --- | --- |
| Typed object/driver/link boundary | Complete | Direct/source/object/mixed links, options, lifecycle, duplicate/unresolved handling, helper import |
| Native exception regions and constructor unwind | Complete after audit | Typed call nonthrowing facts drive prefix cleanup; indirect-throw regression passes |
| Polymorphic support and link ownership | Complete after audit | Typed RTTI roles, vtables/VTT, casts, ABI object labels, aliases, and weak coalescing |
| Numeric values, calls, and native allocation | Complete after audit | i128/x87/call pressure retained; integer and atomic facts consumed as typed values |
| Scoped semantic regions and ordered EH | Complete | Typed body roles, ordered handlers, bounded statement scheduler, prior regressions |
| Aggregate value initialization and member-pointer ABI | Complete after audit | Canonical virtual slot/adjustment path; lowering ownership split removes PA30 warning |
| Object/link/native ownership audit | Complete | Streamed bounded object I/O, moved definitions, single-probe renames, incremental MIR/ELF path |
| Foreign helper/ABI audit | Complete | C/C++ object symbols, aliases, REL32/PLT32/ABS64/GOTPCRELX support; end-to-end regression |
| Final full-stage audit | Complete | Representative traces, malformed-input probe, three-size scaling, exact required gates |

## Validation

PA30 local tests pass 92/92 (88 assignment plus four course regressions). The
required file audit passes with 21 inherited warnings. The final through-stage
gate, `make test-report-through-pa30`, passes 4,132/4,132 tests and 30/30
stages.

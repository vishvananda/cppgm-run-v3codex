# PA34 Final Plan

## Stage Design and Spec Alignment

PA34 extends the existing compiler rather than adding a hosted-only path. The
production flow is:

```text
immutable source buffers
  -> streaming hosted preprocessor/post-token sink
  -> interned compact syntax tokens and syntax arena
  -> canonical typed semantic graph and demand worklists
  -> direct typed LowIR and typed-ID local transforms
  -> native adapter and bounded per-function MIR
  -> direct ELF64 relocatable writer
```

The parser/semantic boundary is the existing PA syntax-arena surface: semantics
borrows that arena without rendering or reparsing text, and parser,
substitution, lookup, and demand scratch are destroyed before typed lowering
borrows the semantic graph. Identifier, type, scope, entity, binding,
specialization, symbol, temporary, slot, and block identities are compact IDs.
The hosted probe configuration is generated at build time; the running compiler
does not invoke a host compiler, assembler, reference tool, or prior stage.

Hosted parser concessions and compiler builtins publish typed facts at their
owning syntax/semantic boundary. PA34's bounded hosted primary shims attach once
to canonical class-template declarations; subsequent specialization, lookup,
lowering, and ABI work uses canonical IDs. Object compilation and
`--emit-lowir` share `BuildTypedLowIRProgram`, including always-inline CFG
expansion, so the rendered LowIR remains a view of what object emission uses.

## Performance Evidence

Final-audit measurements use release binaries and 1/8/64 generated workloads on
the same machine.

| Surface | 1 / 8 / 64 evidence |
| --- | --- |
| Hosted preprocessing | 82/208/1,216 post-tokens and 22/148/1,156 expansions; peak rescan 18/42/46 tokens; 2.325/2.189/2.640 ms; 6,796/7,064/6,964 KiB RSS |
| Demanded templates | 10/80/640 specialization requests, 8/64/512 cache hits, and exactly 1/8/64 demand pushes/emissions; 44/233/1,745 semantic nodes; 0.683/1.745/11.670 ms semantic time; 72,700/314,790/2,400,006 peak bytes |
| Typed always-inline transform | 1/8/64 candidates and calls, 36/246/1,926 probes, 3/24/192 added blocks, and 20/139/1,091 final instructions; lowering plus adaptation 0.250/0.609/4.554 ms; 8,772/8,924/13,332 KiB RSS |
| No-inline control at 64 | Zero transform probes/copies, 515 instructions, 1.749 ms lowering plus adaptation, and 10,144 KiB RSS |
| Force-inline before/after at 64 | The former post-adapter whole-program rewrite used 10.789 ms lowering plus adaptation and 18,020 KiB RSS. Typed-ID rewriting uses 4.554 ms and 13,332 KiB: 57.8% less phase time and 26.0% less peak RSS while producing the same 1,091 instructions. |

The template workload emitted 1/8/64 functions once, and the 8-case ELF trace
contains eight canonical weak `read_box<N>` symbols and two relocations to each
called specialization. The nontrivial CFG trace emitted a valid x86-64 ELF
relocatable, linked and ran successfully, expanded ordinary and nested
always-inline calls, and retained only the recursive call.

## Architecture Review

| Checklist area | Review result |
| --- | --- |
| Representation and ownership | Source and intern tables have translation-unit ownership; syntax is borrowed by semantics; semantic scratch dies before graph consumption; typed LowIR is constructed directly; no text is rendered and parsed back. |
| Identity and lookup | Canonical `NameId`, `TypeId`, `ScopeId`, `EntityId`, `BindingId`, and `SymbolId` keys drive semantic and lowering work. Type interning and scope lookup use flat hash slots with dependency-indexed invalidation. Strings are limited to source/presentation, ABI output, and explicit textual-LowIR adapters. |
| Templates and repeated work | Specializations use canonical pattern/argument/partition keys. Request tables distinguish not-started, in-progress, succeeded, and failed states. Deferred member/default/function queues advance with monotonic cursors; no whole-program retry loop was found. |
| Lowering and backend | The semantic graph lowers directly to typed LowIR. Always-inline rewriting now runs there by dense symbol ID and Tarjan SCC state. The native adapter runs once, MIR is lowered and encoded one function at a time, and ELF sections/relocations are written directly. |
| Allocation and scaling | Front-end graphs use contiguous vectors/arenas and compact slices. Hot indexes are flat/dense. The typed inliner scans input/output a bounded number of times and allocates only output-proportional CFG, slot, and operand storage. Counters show proportional preprocessing, specialization, demand, and IR growth. |
| Self-containment | Runtime source compilation contains no shell-out or test/source-path/reference lookup. The legacy string-keyed force-inline pass remains only as an adapter for explicit textual LowIR and now avoids any speculative copy when no forced definition exists. |

## Final Architecture Review

The audit found and closed one cross-layer blocker. Source compilation formerly
adapted canonical IDs to string-keyed LowIR, copied the complete program to
expand `always_inline`, and then entered a native session that attempted the
same rewrite again. Expansion now occurs once in typed LowIR using dense IDs,
explicit SCC handling, and output-work counters; source compilation clears the
consumed attribute before adaptation. Ordinary no-inline compilation performs
zero inliner probes and no program copy.

Representative declaration and demanded-template traces, static source/file
checks, focused compile/link/run probes, scaling telemetry, and the cumulative
test report found no remaining PA34 correctness, architecture, performance,
self-containment, or file-audit blocker. The file audit's 22 header-division
warnings are inherited advisory findings and do not involve the new audit
module.

## Checkpoint Ledger

| Commits | Owned stage surface | Final audit result |
| --- | --- | --- |
| `17cba749`, `822c5e06`, `12f77862` | Hosted preprocessing, type queries, extension annotations | Pass: streaming/configuration and canonical syntax/semantic facts |
| `256ec8ed`, `e376846b`, `8e13c7c1`, `6e49ff4c` | Integer, memory, atomic, floating, and abort builtins | Pass: bounded registries, typed effects/values, direct lowering |
| `1534ea89`, `ea8089a5`, `20e91bac`, `71c6a55f` | Layout attributes, GNU asm, vectors, block pointers | Pass: canonical layout/type identities and structured lowering |
| `db73c970`, `e2240028`, `91a7f96f` | Generic lambdas, numeric scalars, GNU complex | Pass: retained demand, canonical scalar/pair types, ABI facts |
| `9ac4482f`, `e8c174e9`, `b336b679`, `dc9a6846` | Construction/assignment/retained traits and ownership audit | Pass: shared overload/special-member facts and cached replay |
| `e3a3bb7c`, `d08e1253`, `9b251086` | Fold/pack replay, aggregates, compiler function builtins | Pass: bounded replay, binding-owned projections, typed calls/constants |
| `92d88ba6`, `e733c57c` | Hosted runtime/configuration and declaration compatibility | Pass: declaration-backed aliases and canonical overload/declaration state |
| `c6e310d3`, `fc68d159`, `2dd03fc5` | Selection replay, hosted type formation, dependent callables | Pass: selected-branch demand and canonical specialization/type replay |
| `91d55cb8`, `2e32615f`, `c73800f6` | Declaration types, wide constants, runtime object actions | Pass after final typed-ID force-inline ownership refactor |
| `c6db9b6d` | ABI and source identity | Pass: canonical owner/substitution rendering and object names |

Final ledger: PA34 369/369; through PA34 4,756/4,756; all 34 tracked
stages pass; file audit passes.

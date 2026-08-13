# PA32 Implementation Plan

## Stage Design and Spec Alignment

PA32 keeps the production path `semantic bindings/types/templates -> canonical ABI facts -> typed LowIR symbols/aliases -> MIR/fixups -> direct ELF`. Canonical callable identity and typed lifecycle roles now survive through host member-pointer pairs and ELF lifecycle sections. This follows `spec.md` §§2, 4, 6–9: canonical identity before mangling, demand separate from ownership, direct typed lowering, and indexed work proportional to declarations, demanded facts, cleanup edges, and emitted symbols.

## Current Failure Map

Current result: **136/140**. PA1–PA31 pass **4150/4150** and file audit passes.

- EH/control-flow cleanup (4): goto out of try; call-argument temporary cleanup; member-constructor unwind; delegating-constructor unwind.

## Active Checkpoint

**Explicit EH cleanup graph and complete protected-region coverage.** The remaining four failures converge on lifetime actions leaving normal control flow: goto exits reject instead of consuming scope cleanups; call-argument temporaries lack exceptional cleanup; constructor/delegating-constructor unwind paths do not consistently destroy completed subobjects; and MIR/LSDA coverage can omit a resume continuation before the first throwing call-site interval.

- Spec alignment: §§2 and 6 require lifetime actions and selected destructors to be recorded once and consumed by typed lowering; §§5 and 7 require explicit dependency/control-flow edges and bounded per-function MIR work; §§8–9 require phase-local cleanup state and O(nodes + cleanup edges + call sites) behavior.
- Owner/data flow: semantic scope/temporary/constructor lifetime facts -> typed normal and exceptional cleanup edges -> per-function MIR protected-region states and landing pads -> complete ordered LSDA call-site intervals -> host unwinder.
- Complexity: O(statements + lifetime actions + control-flow edges + EH regions + call sites), with each cleanup edge and interval emitted once and no translation-unit retry or repeated full-function rescan.
- Validation: all four remaining fixtures; adjacent goto, temporary, constructor, delegating-constructor, resume, catch, and LSDA inspection tests; generated nested-cleanup scaling; full PA32, PA1–PA31, and file audit.

## Performance Evidence

A generated translation unit contains 16/64/256 distinct member-function-pointer globals, demanded inline member bodies, indirect calls, and external constructor actions. All host-object compilations succeed with `CPPGM_DRIVER_STATS=1`.

| Callable owners | Semantic nodes | Demand pushes | LowIR instructions | ELF relocations | Semantic + lowering | Object bytes |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 16 | 722 | 48 | 397 | 120 | 5.03 ms | 214,720 |
| 64 | 2,834 | 192 | 1,549 | 456 | 19.28 ms | 835,952 |
| 256 | 11,282 | 768 | 6,157 | 1,800 | 74.55 ms | 3,328,976 |

Across 16x more owners, semantic nodes, demand pushes, instructions, relocations, object bytes, and combined semantic/lowering time grow 15.6x, 16.0x, 15.5x, 15.0x, 15.5x, and 14.8x. Inspection shows one direct 8-byte `INIT_ARRAY` entry per translation unit and a function relocation plus adjustment word for each member-pointer global. Stats-on/off 64-owner objects are byte-identical (`sha256 d8f8f18b56647d96bbdf677521180edf8410efb54d06f0aeb75e28fd593dbb23`).

## Completed Checkpoints

| Checkpoint | Result |
| --- | --- |
| Canonical host ABI symbols and ODR roots | Dependent substitutions, array bounds, placement overloads, runtime names, and weak-root COMDAT; PA32 59→75. |
| Demand-owned host lifecycle emission | Host demand/pruning and lifecycle aliases; PA32 75→78. |
| Parameter-rooted dependent-result recipes | Immutable ordinal/member/modifier result nodes; PA32 78→79. |
| Canonical standard-template substitutions | Generic/exact std substitutions for types and owners; PA32 79→82. |
| Typed dependent NTTP defaults | Source literal type/value retained beside target shape; PA32 82→83. |
| Structured dependent result/expression recipes | Framed alias arguments and typed trailing-`decltype` recipes; PA32 83→84. |
| Canonical callable and member-entity ABI facts | Typed function/member NTTP terminals and callable shapes; PA32 85→87 plus four audit fixtures. |
| Canonical external object-data identity and addressing | Typed linkage, weak/section, and relocatable-address facts; PA32 91→96. |
| Typed ELF sections and host TLS ownership | ELF sections, TLS symbols/wrappers, and TPOFF32 relocations; PA32 96→99. |
| Host virtual-inheritance ABI publication | Primary slots, complete-object calls, local support identities, COMDAT ownership; PA32 99→110. |
| Canonical lifecycle entries and template preemption | Typed C1/C2 and D1/D2 peers plus canonical class-member suppression; PA32 110→119. |
| Canonical dependent ABI owner and substitution spelling | Empty-pack owners, qualified enum types, internal-argument linkage, dependent-default substitution order, declaration-less static calls, and template-template pack result/constructor expansion; PA32 119→126, prior 4150/4150, audit pass, linear 16→256 evidence. |
| Canonical linkage and anonymous-entity ownership | C linkage/visibility separation, redeclaration validation, typedef linkage names, local ordinals through typed emission/ABI identity, and canonical projected-storage initialization including union/default owners; original PA32 126→131 plus two audit regressions, prior 4150/4150, file audit pass, linear 16→256 evidence. |
| Canonical host callable construction and invocation | Distinct system include paths, demand-owned member-pointer pairs/calls, and direct ELF init/fini arrays; PA32 133→136, prior 4150/4150, audit pass, linear 16→256 evidence. |

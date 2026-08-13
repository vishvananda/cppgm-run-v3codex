# PA32 Implementation Plan

## Stage Design and Spec Alignment

PA32 completes the production path `semantic bindings/types/templates/lifetimes -> canonical ABI and cleanup facts -> typed LowIR -> MIR/LSDA/fixups -> direct ELF`. Callable identity, object ownership, lifecycle roles, protected-context exits, and selected destructors survive as typed facts. This follows `spec.md` §§2, 4–9: canonical identity before mangling, demand separate from ownership, explicit control/lifetime edges, direct typed lowering, and indexed work proportional to declarations, demanded facts, cleanup edges, and emitted symbols.

## Current Failure Map

Current result: **140/140**. PA1–PA31 pass **4150/4150** and file audit passes.

- No current failures. The final EH group (goto exit, call-argument temporary, member-constructor unwind, and delegating-constructor unwind) is closed.

## Active Checkpoint

**Completed — explicit EH cleanup graph and complete protected-region coverage.** Semantic analysis records point-in-time label/goto lifetime snapshots, protected-context exit counts, managed throwing-call temporaries, throwing constructor bodies, and delegation's selected whole-object destructor. Typed lowering emits the corresponding cleanup edges; standalone and host unwinding route cleanup landing pads before catches/resume.

- Spec alignment: §§2 and 6 require lifetime actions and selected destructors to be recorded once and consumed by typed lowering; §§5 and 7 require explicit dependency/control-flow edges and bounded per-function MIR work; §§8–9 require phase-local cleanup state and O(nodes + cleanup edges + call sites) behavior.
- Owner/data flow: semantic label scope and nested EH context -> goto destructor edges plus exit count; temporary construction state -> managed full-expression dispatch; constructor initialization/delegation action -> selected subobject/whole-object destructor -> typed cleanup blocks -> MIR landing pads and ordered LSDA intervals -> host unwinder.
- Complexity: O(statements + captured active-lifetime facts + emitted cleanup actions + exited EH regions + CFG edges + call sites). Labels and pending gotos are function-local indexed facts; each cleanup edge, region close, landing pad, and interval is emitted once, without translation-unit retry or repeated full-function rescan.
- Validation: all four former failures plus same-region/cross-sibling/outward goto, temporary-call, member/delegating constructor, resume, catch, and LSDA adjacency tests; generated nested-cleanup scaling; full PA32, PA1–PA31, and file audit.

## Performance Evidence

A recursive generated program instantiates 16/64/256 throwing call frames, each owning one automatic guard and exceptional cleanup. All objects compile and all linked programs unwind to `live == 0` with `CPPGM_DRIVER_STATS=1`.

| Cleanup depth | Semantic nodes | Demand pushes | LowIR instructions | EH states / edges / call sites | Object bytes |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 16 | 346 | 20 | 227 | 35 / 38 / 17 | 124,992 |
| 64 | 1,258 | 68 | 803 | 131 / 134 / 65 | 421,200 |
| 256 | 4,906 | 260 | 3,107 | 515 / 518 / 257 | 1,607,928 |

From depth 64 to 256 (4x), semantic nodes, LowIR, EH states, edges, call sites, and object bytes grow 3.90x, 3.87x, 3.93x, 3.87x, 3.95x, and 3.82x. This is consistent with O(instantiations + lifetime actions + protected edges + call sites), with fixed runtime/translation-unit overhead.

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
| Explicit EH cleanup graph and protected-region coverage | Goto lifetime/context exits, managed argument temporaries, member/delegating constructor cleanup, standalone cleanup-first dispatch, and host resume coverage; PA32 136→140, prior 4150/4150, audit pass, near-linear depth 16→256 evidence. |

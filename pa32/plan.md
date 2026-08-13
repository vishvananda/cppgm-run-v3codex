# PA32 Implementation Plan

## Stage Design and Spec Alignment

PA32 keeps the production path `semantic bindings/types/templates -> canonical ABI facts -> typed LowIR symbols/aliases -> MIR/fixups -> direct ELF`. Language linkage remains independent of visibility; local occurrence ordinals participate in semantic, typed-emission, and ABI identity; and projected anonymous members retain one direct storage owner with compact reverse edges. This follows `spec.md` §§2, 4, 6–9: canonical identity before mangling, demand separate from ownership, direct typed lowering, and indexed work proportional to declarations, projections, and emitted symbols.

## Current Failure Map

Current result: the original suite remains **131/138** with its exact seven failures; two audit regressions pass, for **133/140** overall. PA1–PA31 pass **4150/4150**.

- EH/control-flow cleanup (4): goto out of try; call-argument temporary cleanup; member-constructor unwind; delegating-constructor unwind.
- Host callable/lifecycle ABI (3): external default-constructor ownership, member-function-pointer representation/call, and system-header move-constructor body demand.

## Active Checkpoint

**Canonical host callable construction and invocation.** The next three failures converge where declared lifecycle ownership and callable representations cross from semantics into host LowIR.

- Spec alignment: §§2 and 6 require one canonical declaration/special-member owner; §4 requires body and external-reference demand to remain distinct; §§7–9 require typed member-pointer lowering, ABI-sized storage, direct fixups, and linear per-action work.
- Owner/data flow: parsed declaration/include provenance -> canonical lifecycle or member-pointer fact -> demand/reference decision -> typed LowIR construction/call -> MIR pair registers or external relocation -> ELF.
- Complexity: O(callables + construction actions + references), using binding/member indexes and one pass over each demanded body or member-pointer pair.
- Validation: the three grouped fixtures, adjacent external lifecycle/member-pointer/include tests, generated 16/64/256 callable-demand scaling, PA32, PA1–PA31, and file audit.

## Performance Evidence

A generated function contains 16/64/256 disjoint same-named local classes. Each class has two projected anonymous-struct members, explicit construction, a member call, and a demanded `width<Local>` specialization. All object compilations succeed with `CPPGM_DRIVER_STATS=1`.

| Ownership blocks | Semantic nodes | Lookup scope visits | Template requests | LowIR instructions | Semantic + lowering | Object bytes |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 16 | 754 | 1,037 | 48 | 572 | 5.76 ms | 280,208 |
| 64 | 2,962 | 4,109 | 192 | 2,252 | 21.57 ms | 1,102,160 |
| 256 | 11,794 | 16,397 | 768 | 8,972 | 83.73 ms | 4,397,840 |

Across 16x more blocks, semantic nodes, lookup visits, template requests, instructions, and object bytes grow 15.6x, 15.8x, 16.0x, 15.7x, and 15.7x; combined semantic/lowering time grows 14.5x. The 256-block object links and runs, and stats-on/off 64-block objects are byte-identical (`sha256 c603aa374c78095d0831a3eccd1e0f83cb44dd70d09c3825d80a6643ddd8ccc1`).

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

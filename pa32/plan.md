# PA32 Implementation Plan

## Stage Design and Spec Alignment

PA32 keeps the production path `semantic bindings/types/templates -> canonical ABI facts -> typed LowIR symbols/aliases -> MIR/fixups -> direct ELF`. Language linkage is independent of symbol visibility, local and typedef-named anonymous types now own stable ABI identity, and anonymous storage members retain canonical projection paths. This follows `spec.md` §§2, 4, 6–9: typed identity before mangling, demand separate from ownership, one lowering per unit, and indexed work proportional to declarations and emitted symbols.

## Current Failure Map

Current result: **131/138**, up from the **126/138** checkpoint baseline and **99/138** turn baseline. **7** tests remain; PA1–PA31 pass **4150/4150**.

- EH/control-flow cleanup (4): goto out of try; call-argument temporary cleanup; member-constructor unwind; delegating-constructor unwind.
- Host callable/lifecycle ABI (3): external default-constructor ownership, member-function-pointer representation/call, and system-header move-constructor body demand.

## Active Checkpoint

**Canonical host callable construction and invocation.** The next three failures converge where declared lifecycle ownership and callable representations cross from semantics into host LowIR.

- Spec alignment: §§2 and 6 require one canonical declaration/special-member owner; §4 requires body and external-reference demand to remain distinct; §§7–9 require typed member-pointer lowering, ABI-sized storage, direct fixups, and linear per-action work.
- Owner/data flow: parsed declaration/include provenance -> canonical lifecycle or member-pointer fact -> demand/reference decision -> typed LowIR construction/call -> MIR pair registers or external relocation -> ELF.
- Complexity: O(callables + construction actions + references), using binding/member indexes and one pass over each demanded body or member-pointer pair.
- Validation: the three grouped fixtures, adjacent external lifecycle/member-pointer/include tests, generated 16/64/256 callable-demand scaling, PA32, PA1–PA31, and file audit.

## Performance Evidence

A generated function contains 16/64/256 disjoint declarations of the same named local class, each with demanded constructor and member-call bodies. All object compilations succeed with `CPPGM_DRIVER_STATS=1`.

| Local declarations | Semantic nodes | Lookup scope visits | LowIR instructions | Semantic + lowering | Object bytes |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 16 | 392 | 450 | 291 | 2.93 ms | 154,408 |
| 64 | 1,544 | 1,794 | 1,155 | 9.94 ms | 611,096 |
| 256 | 6,152 | 7,170 | 4,611 | 40.70 ms | 2,442,280 |

Across 16x more declarations, semantic nodes, lookup visits, instructions, and object bytes grow 15.7x, 15.9x, 15.8x, and 15.8x; combined semantic/lowering time grows 13.9x. Indexed local-name occurrence and injected-member ownership remain linear.

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
| Canonical linkage and anonymous-entity ownership | C linkage/visibility separation, redeclaration validation, typedef linkage names, exact local discriminators, and projected anonymous-storage initialization; PA32 126→131, prior 4150/4150, audit pass, linear 16→256 evidence. |

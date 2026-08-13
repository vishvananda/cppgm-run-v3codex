# PA32 Implementation Plan

## Stage Design and Spec Alignment

PA32 keeps the production path `semantic bindings/types/templates -> canonical ABI facts -> typed LowIR symbols/aliases -> MIR/fixups -> direct ELF`. Template specialization identity is now independent of argument count, named types retain their namespace path, template-template pack results retain `Dp`, and internal-linkage arguments flow to function/local-static publication. This follows `spec.md` §§2, 4, 6–9: typed canonical identity before mangling, demand separate from symbol ownership, one lowering per unit, and indexed work proportional to arguments and emitted symbols.

## Current Failure Map

Current result: **126/138**, up from the **119/138** checkpoint baseline and **99/138** turn baseline. **12** tests remain; PA1–PA31 pass **4150/4150**.

- EH/control-flow cleanup (4): goto out of try; call-argument temporary cleanup; member-constructor unwind; delegating-constructor unwind.
- Host call/runtime ABI (3): external default constructor, member-function pointer runtime, and system-header move/reset.
- Linkage and anonymous entities (5): anonymous storage initialization, anonymous-namespace `extern "C"` call, invalid C/static redeclaration, same-named local classes, and typedef-linked anonymous types.

## Active Checkpoint

**Canonical linkage and anonymous-entity ownership.** The next five failures converge where declaration linkage and anonymous/local type ownership become canonical binding identity.

- Spec alignment: §§2 and 6 require declaration merging and anonymous identity to be typed before lookup/emission; §4 requires demand to consume that identity without changing linkage; §§7 and 9 require one canonical merge key and linear indexed scans.
- Owner/data flow: declaration parsing -> canonical binding/type owner -> redeclaration/linkage validation -> LowIR identity/internal owner -> ELF visibility and relocation.
- Complexity: O(declarations + referenced anonymous entities), with direct binding/scope indexes and no rendered-name or pairwise scans.
- Validation: the five grouped fixtures, adjacent C-linkage/local-type tests, generated 16/64/256 declaration-owner scaling, PA32, PA1–PA31, and file audit.

## Performance Evidence

A generated template-template function returns `Tuple<Ts...>` and constructs it from 16/64/256 expanded arguments. All object compilations succeed with `CPPGM_DRIVER_STATS=1`.

| Pack args | Semantic nodes | LowIR instructions | Semantic + lowering | Object bytes | Peak RSS |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 16 | 85 | 90 | 1.93 ms | 49,616 | 8,016 KiB |
| 64 | 277 | 330 | 5.06 ms | 176,168 | 9,272 KiB |
| 256 | 1,045 | 1,290 | 15.67 ms | 684,800 | 13,740 KiB |

Across 16x more pack elements, semantic nodes, instructions, and object bytes grow 12.3x, 14.3x, and 13.8x; combined semantic/lowering time grows 8.1x. Retained result publication and pack expansion remain linear in arguments plus emitted instructions.

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

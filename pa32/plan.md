# PA32 Implementation Plan

## Stage Design and Spec Alignment

PA32 keeps the production path `semantic BindingRecord/TypeRecord/FunctionTemplateAbiRecipe -> canonical ABI graph -> typed LowIR Symbol/AddressBinding -> MIR/fixup metadata -> direct ELF`. Semantic/lowering records own entity terminals, callable qualifiers, source-template arguments/results, definition/preemption state, names, and emission demand; the encoded ELF owner holds one label-to-section/offset index shared by alias, relocation, and symbol publication, while final stable ordering is separate. ABI publication remains typed and transactional, section buffers move across the writer boundary, and telemetry observes the path without changing serialized object metadata. This applies `spec.md` §§2 and 6 (stable typed identity and no name reconstruction), §4 (separate specialization/emission facts), §7 (direct ELF), §8 (explicit ownership), and §9 (linear work with behavior-neutral counters).

## Current Failure Map

Current result: **99/138**, preserving the **99/138** audit-entry baseline and up from the **96/138** checkpoint implementation baseline and **91/138** stage baseline. **39** tests remain failing; PA1–PA31 pass **4150/4150**.

- ABI/template identity, demand, and coalescing (13): OOC constructor templates; empty owner pack; extern-template constructor/member/static-data; enum and variadic-template-template names; internal-template local static; ODR default, static-self, and synthetic template-argument substitutions.
- Host call ABI and EH (6): goto-out-of-try; three cleanup/unwind cases; member-function-pointer runtime; system-include move/reset.
- Semantic/linkage remainder (9): anonymous-namespace implicit/explicit special members, storage and call; invalid C/static redeclaration; explicit-specialization data; external default constructor; same-named local classes; typedef-linkage anonymous types.
- Virtual inheritance/lifecycle (11): result vbase access; external construction thunk; host vbase call; heap dispatch; local/multilevel objects; primary/secondary polymorphic layout; construction vtable; two virtual-diamond cases.

## Active Checkpoint

**Canonical virtual-base layout and access.** Unify the remaining virtual-inheritance failures at the semantic-layout/lowering boundary: complete-object and base-entry construction, vbase offset lookup, construction-vtable selection, and direct/virtual dispatch must consume one canonical layout contract.

- Spec alignment: `spec.md` §§2 and 6 require stable layout/ABI facts consumed by identity, §4 separates class layout/vtable/emission demand, and §9 requires work proportional to demanded layouts and inheritance edges.
- Owner/data flow: semantic `EntityRecord`/direct-base/virtual-base layout facts -> typed base-path and ABI-entry records -> constructor/member/vcall lowering -> LowIR address projections and demanded thunks/vtables. Lowering must not rediscover offsets from names or rescan unrelated classes.
- Complexity: class layout and virtual views should visit each relevant base edge/slot once per canonical class fact; each lowered access or call performs O(1) cached layout lookup plus path-length work, with no translation-unit retry loop.
- Validation: group all 11 virtual-inheritance failures by construction, access, and dispatch; use focused runtime/link/object checks, PA28/PA32 regressions, full prior/current/audit gates, and generated widening/deepening inheritance counters.

## Performance Evidence

Generated sources placed one ordinary global in a distinct custom section and one TLS global plus accessor per case. Representative counters scale with produced semantics and output:

| Cases | Tokens | Semantic nodes | Functions | Globals | Fixups | Custom sections / TPOFF32 | Object bytes |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 32 | 1,130 | 421 | 33 | 64 | 193 | 32 / 64 | 139,544 |
| 128 | 4,490 | 1,669 | 129 | 256 | 769 | 128 / 256 | 553,608 |
| 512 | 17,930 | 6,661 | 513 | 1,024 | 3,073 | 512 / 1,024 | 2,220,352 |

Across the 16x range, semantic time was 1.87–27.87 ms, typed lowering 0.63–7.30 ms, native encoding 1.27–20.28 ms, and semantic peak storage 479,280–7,574,988 bytes. Structural counters, sections, relocations, output size, and measured phase work remain linear. Section names and encoded labels are indexed once; alias, relocation, and export consumers perform average-O(1) lookup, empty relocation sections are omitted, and stable symbol ordering is the sole O(n log n) step. The 512-case object links and runs; stats-on/off objects are byte-identical (`sha256 d370ba1a87c07d3856076f39cfd6953b9abd0c4d65e9c9c708eeb7a81dae0fa5`).

## Completed Checkpoints

| Checkpoint | Result |
| --- | --- |
| Canonical host ABI symbols and ODR roots | Correct dependent substitutions/array-bound expressions, placement overload fallback, host new/delete names, and linear weak-root COMDAT records; pa32 59→75, prior 4150/4150, audit pass. |
| Demand-owned host lifecycle emission | Host-only demand separates body validation from emission, emits used empty destructors, prunes unused inline/trivial lifecycle roots and calls, and exports lifecycle aliases with target linkage; pa32 75→78, prior 4150/4150, audit pass. |
| Parameter-rooted dependent-result recipes | Semantic registration publishes immutable ordinal/member/modifier nodes and ABI lowering consumes them for global/local names; array-result spelling passes, pa32 78→79, prior 4150/4150, audit pass. |
| Canonical standard-template substitutions | Semantic std template identities select generic `Sa`/`Sb` or exact `Ss`/`Si`/`So`/`Sd` ABI facts for types and owners; allocator, operator, and ostream fixtures pass, pa32 79→82, prior 4150/4150, audit pass. |
| Typed dependent NTTP defaults | Canonical arguments retain source literal type/value beside non-deduced target shape, so source `Li0E` and concrete `Lm0E` remain distinct; pa32 82→83, prior 4150/4150, audit pass. |
| Structured dependent result/expression recipes | Alias expansion publishes framed class-template arguments, canonical source `TypeId`s, and typed trailing-`decltype` nodes; incomplete recipe reads roll back atomically. The landed suite moves pa32 83→84 and the audit regression passes for 85/134 total, with the original 49 failures unchanged; prior 4150/4150 and file audit pass. |
| Canonical callable and member-entity ABI facts | Function types retain cv/ref qualifiers; non-static member NTTPs retain typed owner, terminal, qualifier, parameter, and source-template facts in the enclosing substitution sequence. Audit regressions cover operator, conversion, member-template, and pack-template terminals; telemetry no longer mutates object metadata. PA32 85→87 plus four passing audit fixtures (91/138), prior 4150/4150, file audit pass, and linear 16→128 evidence. |
| Canonical external object-data identity and addressing | Direct linkage declarations, inherited C linkage, GNU weak/section facts, and relocatable-only symbol-address intent flow through typed owners; the audit replaced ELF name/label reconstruction with a retained local/preemptible address fact. PA32 91→96 (four selected fixtures plus adjacent `f64` shuffle), prior 4150/4150, file audit pass, and linear 32→128 evidence. |
| Typed ELF sections and host TLS ownership | Canonical section/TLS facts select real ELF sections, STT_TLS symbols, weak ABI wrappers, and TPOFF32 relocations; the audit centralizes encoded label ownership and removes section-product lookup and empty relocation records. PA32 96→99, prior 4150/4150, file audit pass, and linear 32→512 evidence. |

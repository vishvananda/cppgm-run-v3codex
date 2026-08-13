# PA32 Implementation Plan

## Stage Design and Spec Alignment

PA32 keeps the production path `semantic BindingRecord/TypeRecord/FunctionTemplateAbiRecipe/ClassPolymorphismFacts -> canonical ABI graph -> typed LowIR Symbol/AddressBinding -> MIR/fixup metadata -> direct ELF`. The host projection adds ABI-required complete-object primary slots without changing PA28's staged semantic graph; internal vtable/VTT support objects retain local identity, while genuine weak host definitions receive exact symbol-table-owned COMDAT signatures. The encoded ELF owner holds one label-to-section/offset index shared by alias, relocation, and publication. This applies `spec.md` §§2 and 6 (stable typed identity and one emission identity), §4 (separate semantic and emission demand), §7 (direct ELF/linkage), §8 (explicit ownership), and §9 (linear indexed work and behavior-neutral telemetry).

## Current Failure Map

Current result: **110/138**, up from the turn-start **99/138** baseline. **28** tests remain failing; PA1–PA31 pass **4150/4150**.

- ABI/template identity, demand, and coalescing (13): OOC constructor templates; empty owner pack; extern-template constructor/member/static-data; enum and variadic-template-template names; internal-template local static; ODR default, static-self, and synthetic template-argument substitutions.
- Host call ABI and EH (6): goto-out-of-try; three cleanup/unwind cases; member-function-pointer runtime; system-include move/reset.
- Semantic/linkage remainder (9): anonymous-namespace implicit/explicit special members, storage and call; invalid C/static redeclaration; explicit-specialization data; external default constructor; same-named local classes; typedef-linkage anonymous types.

## Active Checkpoint

**Host virtual-inheritance ABI artifacts and primary slots — completed.** All 11 virtual-inheritance failures shared the semantic-to-host ABI and ELF-publication boundary. Host lowering now derives complete-object primary entries for overrides represented only in secondary/virtual views, resolves complete-object virtual calls through that host slot map, keeps internal construction/view objects local, and signs genuine weak COMDAT groups by exact global symbol ordinal.

- Spec alignment: `spec.md` §2 requires stable ABI-entity identity instead of rendered-name equality, §6 requires one stable emission identity per support object/thunk/entry, §7 owns COMDAT/linkage and direct ELF serialization, and §9 permits the existing final stable ordering while requiring linear publication work.
- Owner/data flow: canonical `ClassPolymorphismFacts` -> one host primary-slot projection and binding-to-slot index -> typed vtable globals/call loads -> local/global `HostSymbol` partitions -> exact global ordinal in `SHT_GROUP.sh_info`. PA28 remains the owner of staged slot facts; host lowering owns only ABI projection, and ELF serialization never reconstructs identity from a colliding name.
- Complexity: primary/view collection and call lookup are O(bindings + demanded slots/views); symbol and relocation publication is O(symbols + relocations + weak definitions), with only final stable ELF ordering O(symbols log symbols).
- Validation: focused construction/access/dispatch and PA28 boundary checks pass **16/16**; PA32 is **110/138**, PA1–PA31 **4150/4150**, file audit passes, generated width scaling is linear, and the largest witness links/runs.

## Performance Evidence

Generated sources use a virtual diamond plus 8/32/128 secondary polymorphic bases and direct overrides. Representative counters scale with produced semantics and output:

| Overrides | Tokens | Semantic nodes | Signature lookups / overrides | Functions / globals | Native fixups | Object bytes |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 8 | 385 | 242 | 17 / 8 | 49 / 55 | 223 | 238,848 |
| 32 | 1,273 | 770 | 65 / 32 | 169 / 151 | 679 | 711,688 |
| 128 | 4,825 | 2,882 | 257 / 128 | 649 / 535 | 2,503 | 2,611,712 |

Across the 16x range, semantic time was 1.15–10.33 ms, typed lowering 0.83–6.96 ms, native lowering 1.13–9.42 ms, and encoding 1.77–19.21 ms. The host slot projection visits each canonical primary/view slot once and uses a dense binding index; no hierarchy retry or name scan was added. The 128-wide object links and returns 0, its 1,047 COMDAT groups contain no internal leading-`@` signature, and telemetry-on/off objects are byte-identical (`sha256 ee4c73f1e1c73be79ede11df1deeee15e48ab7915a2ff207c98d38f8259cbde8`).

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
| Host virtual-inheritance ABI publication | Host-only primary slots, complete-object call lookup, local support identities, and ordinal-owned COMDAT signatures pass all 11 grouped fixtures; PA32 99→110, prior 4150/4150, audit pass, and linear 8→128 evidence. |

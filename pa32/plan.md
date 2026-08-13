# PA32 Implementation Plan

## Stage Design and Spec Alignment

PA32 keeps the production path `semantic binding/type/template facts -> canonical ABI graph -> typed LowIR symbols/aliases -> MIR/fixups -> direct ELF`. Lifecycle bindings now own a typed complete/base-entry relation; class-template declarations publish preemption on canonical functions and a dense static-member index before demand is lowered. Mangling consumes typed terminal/template facts, and ELF publication alone chooses local, weak, defined, or imported storage. This follows `spec.md` §§2, 4, 6–9: stable identities, monotonic demand/preemption, one lowering of each unit, explicit ownership, and indexed linear work.

## Current Failure Map

Current result: **119/138**, up from the **110/138** checkpoint baseline. **19** tests remain failing; PA1–PA31 last passed **4150/4150**.

- Dependent ABI identity/coalescing (7): empty owner packs; namespaced/direct enum and variadic-template-template spelling; internal-template local static separation; ODR default-parameter and static-self identities.
- Host call ABI and EH (7): goto-out-of-try; three cleanup/unwind cases; external default constructor; member-function-pointer runtime; system-header move/reset.
- Semantic/linkage remainder (5): anonymous storage initialization; anonymous `extern "C"` call; invalid C/static redeclaration; same-named local classes; typedef-linked anonymous types.

## Active Checkpoint

**Canonical dependent ABI owner and substitution spelling.** The next seven failures converge where canonical template arguments and declaration owners become ABI fact paths: empty packs, enum arguments, template-template packs, and self/default parameter substitutions lose source distinctions or choose the wrong reusable prefix; local-static coalescing then inherits the bad identity.

- Spec alignment: §§2 and 6 require canonical typed owner/argument identities before mangling; §4 keeps identity publication separate from emission demand; §§7 and 9 require one coalescing key per semantic entity and indexed work proportional to arguments/emissions.
- Owner/data flow: template parsing/substitution -> canonical `TemplateArgumentList` and function ABI recipe -> ABI owner/path encoder -> typed symbol/coalescing identity -> ELF symbol and COMDAT publication.
- Complexity: extend immutable argument/owner recipes and consume them in one pass, O(template arguments + emitted symbols), without rendered-name scans or candidate cross-products.
- Validation: the seven grouped failures plus adjacent pack/enum/function-local-static spelling fixtures, generated 16/64/256 specialization scaling, PA32, PA1–PA31, and file audit.

## Performance Evidence

Generated sources instantiate 16/64/256 distinct class-template owners, each with in-class and out-of-class functions, constructor/destructor, static data, use sites, and an `extern template` declaration.

| Owners | Source bytes | LowIR time / bytes | Object time / bytes | Peak RSS (LowIR / object KiB) |
| ---: | ---: | ---: | ---: | ---: |
| 16 | 2,347 | 0.01 s / 14,009 | 0.01 s / 126,632 | 7,816 / 8,728 |
| 64 | 8,683 | 0.02 s / 56,489 | 0.03 s / 504,496 | 9,492 / 11,720 |
| 256 | 34,807 | 0.10 s / 228,749 | 0.15 s / 2,022,088 | 18,180 / 24,916 |

All six compilations succeed. Across 16x more owners, LowIR/object bytes grow 16.3x/16.0x and elapsed time 10x/15x at this timer resolution; dense member indexes and one preemption walk per explicit class declaration show no superlinear growth.

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
| Canonical lifecycle entries and template preemption | Typed C1/C2 and D1/D2 peers, constructor result-free recipes, local lifecycle aliases, canonical function/static-data suppression, and declaration-only storage pass all nine grouped fixtures; PA32 110→119 and audit pass, with linear 16→256 evidence. |

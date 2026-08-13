# PA32 Implementation Plan

## Stage Design and Spec Alignment

PA32 keeps the production path `semantic BindingRecord/FunctionTemplateAbiRecipe -> typed LowIR Symbol -> LowIR/MIR symbol metadata -> direct ELF`. The semantic/lowering layer owns canonical Itanium names and emission demand; the ELF writer owns only binding, coalescing, sections, and relocations. Result-identity producers frame every template argument and store nondependent source types as canonical `TypeId`s; ABI recipe publication is transactional, so incomplete reads cannot leave reachable or orphaned type/expression nodes. This applies `spec.md` §§2 and 6 (stable typed ABI identity and no name reconstruction), §4 (separate definition/emission demand), §7 (direct ELF and linkage/COMDAT as the permitted whole-program decision), §8 (explicit ownership), and §9 (linear or `n log n` lowering/object work).

## Current Failure Map

Current result: **87/134** (turn-start **85/134**, pre-audit **84/133**, implementation start **59/133**). The callable/member-entity checkpoint resolves two ABI failures without introducing a new failure family; **47** tests remain.

- ABI/template identity, demand, and coalescing (13): OOC constructor templates; empty owner pack; extern-template constructor/member/static-data; enum and variadic-template-template names; internal-template local static; ODR default, static-self, and synthetic template-argument substitutions.
- ELF data, TLS, and sections (7): C variable import and inherited definition; defined/imported global relocation class; GNU section; TLS import/export.
- Host call ABI and EH (7): goto-out-of-try; three cleanup/unwind cases; `f64` shuffle; member-function-pointer runtime; system-include move/reset.
- Semantic/linkage remainder (9): anonymous-namespace implicit/explicit special members, storage and call; invalid C/static redeclaration; explicit-specialization data; external default constructor; same-named local classes; typedef-linkage anonymous types.
- Virtual inheritance/lifecycle (11): result vbase access; external construction thunk; host vbase call; heap dispatch; local/multilevel objects; primary/secondary polymorphic layout; construction vtable; two virtual-diamond cases.

## Active Checkpoint

**Completed: canonical callable and member-entity ABI facts.** Preserve function-type cv/ref qualifiers in the canonical ABI type graph, and carry non-static member NTTPs as structured owner/member facts so their external-name encoding participates in the enclosing substitution sequence. This applies `spec.md` §§2 and 6: canonical `TypeRecord`/`BindingRecord` identities, rather than rendered names, own the facts consumed by mangling and object lowering.

- Owner/data flow: semantic `TypeRecord` function qualifiers and member `BindingRecord` ownership flow through `AbiFactBuilder` into interned ABI type/argument nodes; the encoder emits those nodes using its existing substitution table. No lowering-time lookup or qualified-name reconstruction is added.
- Complexity: one visit per callable/member type component, plus average O(1) canonicalization/substitution probes; no overload, specialization, or translation-unit scan.
- Validation: both targeted spelling/runtime fixtures and focused PA14 mangler fixtures pass; a qualified member-function NTTP probe exactly matches the host `g++` raw symbol. Pa32 is 87/134, prior-through-pa31 is 4150/4150, and file audit passes. The scaling sample below covers both qualified callable and structured member-entity paths.

## Performance Evidence

Generated stress sources instantiated independent cv-overloaded function templates and member-pointer NTTPs at sizes 16, 32, 64, and 128. Representative counters scale with produced semantics and output:

| Cases | Tokens | Semantic nodes | Overload candidates | Template requests / hits | Functions | LowIR instructions | Object bytes |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 16 | 2,661 | 989 | 64 | 112 / 64 | 97 | 666 | 353,248 |
| 32 | 5,301 | 1,965 | 128 | 224 / 128 | 193 | 1,322 | 701,112 |
| 64 | 10,581 | 3,917 | 256 | 448 / 256 | 385 | 2,634 | 1,397,048 |
| 128 | 21,141 | 7,821 | 512 | 896 / 512 | 769 | 5,258 | 2,791,056 |

Across the same 8x range, semantic time was 10.36–73.92 ms, typed lowering 1.98–14.92 ms, and semantic peak storage 1,800,730–14,291,382 bytes. Structural work, storage, and output remain linear in case count; qualifier and member-entity encoding add constant work per visited type component and no specialization or whole-program scan.

## Completed Checkpoints

| Checkpoint | Result |
| --- | --- |
| Canonical host ABI symbols and ODR roots | Correct dependent substitutions/array-bound expressions, placement overload fallback, host new/delete names, and linear weak-root COMDAT records; pa32 59→75, prior 4150/4150, audit pass. |
| Demand-owned host lifecycle emission | Host-only demand separates body validation from emission, emits used empty destructors, prunes unused inline/trivial lifecycle roots and calls, and exports lifecycle aliases with target linkage; pa32 75→78, prior 4150/4150, audit pass. |
| Parameter-rooted dependent-result recipes | Semantic registration publishes immutable ordinal/member/modifier nodes and ABI lowering consumes them for global/local names; array-result spelling passes, pa32 78→79, prior 4150/4150, audit pass. |
| Canonical standard-template substitutions | Semantic std template identities select generic `Sa`/`Sb` or exact `Ss`/`Si`/`So`/`Sd` ABI facts for types and owners; allocator, operator, and ostream fixtures pass, pa32 79→82, prior 4150/4150, audit pass. |
| Typed dependent NTTP defaults | Canonical arguments retain source literal type/value beside non-deduced target shape, so source `Li0E` and concrete `Lm0E` remain distinct; pa32 82→83, prior 4150/4150, audit pass. |
| Structured dependent result/expression recipes | Alias expansion publishes framed class-template arguments, canonical source `TypeId`s, and typed trailing-`decltype` nodes; incomplete recipe reads roll back atomically. The landed suite moves pa32 83→84 and the audit regression passes for 85/134 total, with the original 49 failures unchanged; prior 4150/4150 and file audit pass. |
| Canonical callable and member-entity ABI facts | Function types retain cv/ref qualifiers and non-static member NTTPs retain structured owners through canonical ABI lowering, sharing the active substitution table; pa32 85→87, prior 4150/4150, focused PA14 and file audit pass, with linear 16→128 stress evidence. |

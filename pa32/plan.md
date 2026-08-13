# PA32 Implementation Plan

## Stage Design and Spec Alignment

PA32 keeps the production path `semantic BindingRecord/TypeRecord/FunctionTemplateAbiRecipe -> canonical ABI graph -> typed LowIR Symbol -> LowIR/MIR symbol metadata -> direct ELF`. Semantic/lowering records own entity terminals, callable qualifiers, source-template arguments/results, names, and emission demand; the ELF writer owns only binding, coalescing, sections, and relocations. ABI publication remains typed and transactional, and telemetry observes the path without changing serialized object metadata. This applies `spec.md` §§2 and 6 (stable typed identity and no name reconstruction), §4 (separate specialization/emission facts), §7 (direct ELF), §8 (explicit ownership), and §9 (linear work with behavior-neutral counters).

## Current Failure Map

Current result: **91/138** (audit turn-start **87/134**, checkpoint start **85/134**, implementation start **59/133**). The audit adds four passing host-symbol regressions; the same **47** existing tests remain failing and no failure family regressed.

- ABI/template identity, demand, and coalescing (13): OOC constructor templates; empty owner pack; extern-template constructor/member/static-data; enum and variadic-template-template names; internal-template local static; ODR default, static-self, and synthetic template-argument substitutions.
- ELF data, TLS, and sections (7): C variable import and inherited definition; defined/imported global relocation class; GNU section; TLS import/export.
- Host call ABI and EH (7): goto-out-of-try; three cleanup/unwind cases; `f64` shuffle; member-function-pointer runtime; system-include move/reset.
- Semantic/linkage remainder (9): anonymous-namespace implicit/explicit special members, storage and call; invalid C/static redeclaration; explicit-specialization data; external default constructor; same-named local classes; typedef-linkage anonymous types.
- Virtual inheritance/lifecycle (11): result vbase access; external construction thunk; host vbase call; heap dispatch; local/multilevel objects; primary/secondary polymorphic layout; construction vtable; two virtual-diamond cases.

## Next Substantial Checkpoint

**Canonical object-data linkage and relocation classes.** Carry language linkage, definition/import state, storage duration, TLS model, section selection, and address-use kind from semantic `BindingRecord`s into typed object symbols and relocations. This targets C variable import/inherited definitions, defined PC-relative versus imported GOT references, GNU section placement, and TLS import/export as one ownership path.

- Owner/data flow: semantic declaration/linkage facts -> typed LowIR global/symbol/address-use records -> MIR relocation intent -> direct ELF symbol, section, and relocation records; no symbol-spelling classification in the backend.
- Complexity: O(1) average symbol lookup and one pass over demanded globals/relocations, with final section layout O(n) or O(n log n); no per-reference scan of all globals.
- Validation: focused C/C++ import/export, PIE/GOT/PC-relative, custom-section, and TLS fixtures; inspect exact ELF symbols/relocations before full gates.

## Performance Evidence

Generated stress sources instantiated independent cv/ref-qualified operator, conversion-function, and member-function-template NTTPs at sizes 16, 32, 64, and 128. Representative counters scale with produced semantics and output:

| Cases | Tokens | Semantic nodes | Overload candidates | Template requests / hits | Functions | LowIR instructions | Object bytes |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 16 | 3,672 | 1,395 | 64 | 240 / 176 | 113 | 752 | 415,744 |
| 32 | 7,336 | 2,787 | 128 | 480 / 352 | 225 | 1,504 | 830,816 |
| 64 | 14,664 | 5,571 | 256 | 960 / 704 | 449 | 3,008 | 1,661,016 |
| 128 | 29,320 | 11,139 | 512 | 1,920 / 1,408 | 897 | 6,016 | 3,324,072 |

Across the same 8x range, semantic time was 13.87–111.04 ms, typed lowering 2.37–18.54 ms, and semantic peak storage 2,781,433–22,184,213 bytes. Structural work, storage, and output remain linear in case count; terminal selection is constant per entity and template argument/type visits are proportional to the retained recipe. Stats-on and stats-off object builds are byte-identical after the audit fix (`sha256 e05d38e27c8940716e3a8eea60ab2bde2b69f39aa4771c284118c298eb01a70b`).

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

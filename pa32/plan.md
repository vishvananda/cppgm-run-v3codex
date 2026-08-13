# PA32 Implementation Plan

## Stage Design and Spec Alignment

PA32 keeps the production path `semantic BindingRecord/FunctionTemplateAbiRecipe -> typed LowIR Symbol -> LowIR/MIR symbol metadata -> direct ELF`. The semantic/lowering layer owns canonical Itanium names and emission demand; the ELF writer owns only binding, coalescing, sections, and relocations. Result-identity producers frame every template argument and store nondependent source types as canonical `TypeId`s; ABI recipe publication is transactional, so incomplete reads cannot leave reachable or orphaned type/expression nodes. This applies `spec.md` §§2 and 6 (stable typed ABI identity and no name reconstruction), §4 (separate definition/emission demand), §7 (direct ELF and linkage/COMDAT as the permitted whole-program decision), §8 (explicit ownership), and §9 (linear or `n log n` lowering/object work).

## Current Failure Map

Current result: **85/134** including the audit regression (turn-start checkpoint **84/133**, pre-checkpoint **83/133**, implementation start **59/133**). The same **49** original tests remain failing; the audit introduced no new failure family.

- ABI/template identity, demand, and coalescing (15): OOC constructor templates; member-function cv; empty owner pack; data-member-pointer owner; extern-template constructor/member/static-data; enum and variadic-template-template names; internal-template local static; ODR default, static-self, and synthetic template-argument substitutions.
- ELF data, TLS, and sections (7): C variable import and inherited definition; defined/imported global relocation class; GNU section; TLS import/export.
- Host call ABI and EH (7): goto-out-of-try; three cleanup/unwind cases; `f64` shuffle; member-function-pointer runtime; system-include move/reset.
- Semantic/linkage remainder (9): anonymous-namespace implicit/explicit special members, storage and call; invalid C/static redeclaration; explicit-specialization data; external default constructor; same-named local classes; typedef-linkage anonymous types.
- Virtual inheritance/lifecycle (11): result vbase access; external construction thunk; host vbase call; heap dispatch; local/multilevel objects; primary/secondary polymorphic layout; construction vtable; two virtual-diamond cases.

## Next Substantial Checkpoint

**Source function-type ABI recipes.** Retain dependent member-pointer owners and function cv/ref qualifiers in function-template parameter recipes so overloads remain distinct and source types survive specialization.

- Owner/data flow: declarator type formation publishes immutable owner/function nodes beside existing parameter shapes; recipes reference ordinal roots; lowering walks those nodes into ABI facts without matching concrete specialization types.
- Complexity: linear in parameter type nodes and qualifiers, with no overload-set or specialization scan.
- Validation: member-function cv overload spelling, dependent data-member-pointer owner spelling, static self-parameter spelling, focused substitution checks, then full gates.

## Performance Evidence

An audit stress source instantiated independent alias/result recipes at sizes 16, 32, 64, and 128. Representative counters scale with the produced source and output:

| Templates | Tokens | Semantic nodes | Identity requests / hits | Atom visits | Syntax visits | Environment probes | LowIR instructions | Object bytes |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 16 | 921 | 331 | 16 / 15 | 682 | 272 | 128 | 163 | 87,104 |
| 32 | 1,753 | 651 | 32 / 31 | 1,386 | 544 | 256 | 323 | 171,344 |
| 64 | 3,417 | 1,291 | 64 / 63 | 2,794 | 1,088 | 512 | 643 | 340,016 |
| 128 | 6,745 | 2,571 | 128 / 127 | 5,610 | 2,176 | 1,024 | 1,283 | 677,648 |

Across the same 8x range, semantic time was 3.38–23.48 ms, lowering 0.97–5.66 ms, and semantic peak storage 737,449–5,327,783 bytes. Structural work, storage, and output remain linear in template count; argument framing and rollback add constant work per published node and no specialization or whole-program scan.

## Completed Checkpoints

| Checkpoint | Result |
| --- | --- |
| Canonical host ABI symbols and ODR roots | Correct dependent substitutions/array-bound expressions, placement overload fallback, host new/delete names, and linear weak-root COMDAT records; pa32 59→75, prior 4150/4150, audit pass. |
| Demand-owned host lifecycle emission | Host-only demand separates body validation from emission, emits used empty destructors, prunes unused inline/trivial lifecycle roots and calls, and exports lifecycle aliases with target linkage; pa32 75→78, prior 4150/4150, audit pass. |
| Parameter-rooted dependent-result recipes | Semantic registration publishes immutable ordinal/member/modifier nodes and ABI lowering consumes them for global/local names; array-result spelling passes, pa32 78→79, prior 4150/4150, audit pass. |
| Canonical standard-template substitutions | Semantic std template identities select generic `Sa`/`Sb` or exact `Ss`/`Si`/`So`/`Sd` ABI facts for types and owners; allocator, operator, and ostream fixtures pass, pa32 79→82, prior 4150/4150, audit pass. |
| Typed dependent NTTP defaults | Canonical arguments retain source literal type/value beside non-deduced target shape, so source `Li0E` and concrete `Lm0E` remain distinct; pa32 82→83, prior 4150/4150, audit pass. |
| Structured dependent result/expression recipes | Alias expansion publishes framed class-template arguments, canonical source `TypeId`s, and typed trailing-`decltype` nodes; incomplete recipe reads roll back atomically. The landed suite moves pa32 83→84 and the audit regression passes for 85/134 total, with the original 49 failures unchanged; prior 4150/4150 and file audit pass. |

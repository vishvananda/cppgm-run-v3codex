# PA32 Implementation Plan

## Stage Design and Spec Alignment

PA32 keeps the production path `semantic BindingRecord/FunctionTemplateAbiRecipe -> typed LowIR Symbol -> LowIR/MIR symbol metadata -> direct ELF`. The semantic/lowering layer owns canonical Itanium names and emission demand; the ELF writer owns only binding, coalescing, sections, and relocations. This applies `spec.md` §§2 and 6 (stable ABI identity and recorded ABI facts, no backend name reconstruction), §4 (separate definition/emission demand), §7 (direct ELF and linkage/COMDAT as the permitted whole-program decision), and §9 (linear or `n log n` lowering/object work).

## Current Failure Map

Current result: **79/133** (checkpoint baseline **78/133**, implementation start **59/133**); all **54** remaining failures are grouped below.

- ABI/template identity, demand, and coalescing (20): OOC constructor templates; dependent alias/NTTP/member-cv names; empty owner pack; data-member-pointer owner; extern-template constructor/member/static-data; enum and variadic-template-template names; internal-template local static; namespace operator, ODR default, static-self, ostream, and synthetic std/template substitutions.
- ELF data, TLS, and sections (7): C variable import and inherited definition; defined/imported global relocation class; GNU section; TLS import/export.
- Host call ABI and EH (7): goto-out-of-try; three cleanup/unwind cases; `f64` shuffle; member-function-pointer runtime; system-include move/reset.
- Semantic/linkage remainder (9): anonymous-namespace implicit/explicit special members, storage and call; invalid C/static redeclaration; explicit-specialization data; external default constructor; same-named local classes; typedef-linkage anonymous types.
- Virtual inheritance/lifecycle (11): result vbase access; external construction thunk; host vbase call; heap dispatch; local/multilevel objects; primary/secondary polymorphic layout; construction vtable; two virtual-diamond cases.

## Active Checkpoint

**Template-rooted ABI recipes.** Extend the typed source-identity graph from parameter-rooted members to class-template roots, type/value template arguments and defaults, then use the same nodes for dependent function parameters. This should cover alias-expanded iterator results, dependent NTTP defaults, and standard-library member result substitutions without syntax replay in lowering.

- Owner/data flow: semantic template registration resolves template entities/defaults and publishes immutable type/argument/expression nodes in `Program`; function ABI recipes reference roots for result and parameter occurrences; lowering walks only those nodes into ABI facts.
- Complexity: linear in expanded components/arguments with canonical argument-list reuse; no specialization-wide or global scan.
- Validation: dependent alias result, NTTP-default result, ostream member result, synthetic substitution fixtures, then full gates and representative identity/argument scaling statistics.

## Performance Evidence

The parameter-rooted result fixture measured 170 tokens, 125 semantic nodes, two result-identity requests/one cache hit, 12 identity-atom visits, two syntax visits, three emitted functions, and 267 LowIR instructions (0.952 ms semantic, 0.374 ms lowering, 0.478 ms object encoding). Its two overload patterns each publish a two-node parameter/member recipe; publication and encoding are linear in recipe length. Earlier lifecycle evidence: 114 tokens/29 nodes/3 demanded functions for the complex owner and 75 tokens/29 nodes/2 demanded functions for the trivial owner.

## Completed Checkpoints

| Checkpoint | Result |
| --- | --- |
| Canonical host ABI symbols and ODR roots | Correct dependent substitutions/array-bound expressions, placement overload fallback, host new/delete names, and linear weak-root COMDAT records; pa32 59→75, prior 4150/4150, audit pass. |
| Demand-owned host lifecycle emission | Host-only demand separates body validation from emission, emits used empty destructors, prunes unused inline/trivial lifecycle roots and calls, and exports lifecycle aliases with target linkage; pa32 75→78, prior 4150/4150, audit pass. |
| Parameter-rooted dependent-result recipes | Semantic registration publishes immutable ordinal/member/modifier nodes and ABI lowering consumes them for global/local names; array-result spelling passes, pa32 78→79, prior 4150/4150, audit pass. |

# PA32 Implementation Plan

## Stage Design and Spec Alignment

PA32 keeps the production path `semantic BindingRecord/FunctionTemplateAbiRecipe -> typed LowIR Symbol -> LowIR/MIR symbol metadata -> direct ELF`. The semantic/lowering layer owns canonical Itanium names and emission demand; the ELF writer owns only binding, coalescing, sections, and relocations. This applies `spec.md` §§2 and 6 (stable ABI identity and recorded ABI facts, no backend name reconstruction), §4 (separate definition/emission demand), §7 (direct ELF and linkage/COMDAT as the permitted whole-program decision), and §9 (linear or `n log n` lowering/object work).

## Current Failure Map

Current result: **82/133** (checkpoint baseline **79/133**, implementation start **59/133**); all **51** remaining failures are grouped below.

- ABI/template identity, demand, and coalescing (17): OOC constructor templates; dependent alias/NTTP/member-cv names; empty owner pack; data-member-pointer owner; extern-template constructor/member/static-data; enum and variadic-template-template names; internal-template local static; ODR default, static-self, and synthetic template-argument substitutions.
- ELF data, TLS, and sections (7): C variable import and inherited definition; defined/imported global relocation class; GNU section; TLS import/export.
- Host call ABI and EH (7): goto-out-of-try; three cleanup/unwind cases; `f64` shuffle; member-function-pointer runtime; system-include move/reset.
- Semantic/linkage remainder (9): anonymous-namespace implicit/explicit special members, storage and call; invalid C/static redeclaration; explicit-specialization data; external default constructor; same-named local classes; typedef-linkage anonymous types.
- Virtual inheritance/lifecycle (11): result vbase access; external construction thunk; host vbase call; heap dispatch; local/multilevel objects; primary/secondary polymorphic layout; construction vtable; two virtual-diamond cases.

## Active Checkpoint

**Canonical template-argument source recipes.** Extend the retained ABI type graph with class-template roots and typed type/value arguments, including semantic expansion of source defaults. Reuse those nodes for dependent results and function parameters so alias-expanded iterator and dependent NTTP-default spellings no longer collapse to concrete types or sentinels.

- Owner/data flow: semantic result/default formation resolves template entities and publishes immutable argument/type nodes in `Program`; function ABI recipes reference result and parameter roots; lowering performs one typed walk into ABI facts with no syntax access.
- Complexity: linear in expanded components/arguments, with canonical argument-list reuse and no specialization-set/global scans.
- Validation: dependent alias result, dependent NTTP default result/parameter, synthetic template-argument substitution, focused default-expansion scaling evidence, then full gates.

## Performance Evidence

The mixed std/non-std substitution fixture measured 327 tokens, 83 semantic nodes, 30 template requests/14 cache hits, four emitted functions, and 26 LowIR instructions (0.502 ms lowering, 0.082 ms object encoding). Classification performs one path walk per specialization plus bounded 2–3 argument checks. The parameter-rooted result fixture measured 170 tokens, 125 nodes, two identity requests/one hit, 12 atom visits, and 0.374 ms lowering; lifecycle probes remain 114/75 tokens with 3/2 demanded functions.

## Completed Checkpoints

| Checkpoint | Result |
| --- | --- |
| Canonical host ABI symbols and ODR roots | Correct dependent substitutions/array-bound expressions, placement overload fallback, host new/delete names, and linear weak-root COMDAT records; pa32 59→75, prior 4150/4150, audit pass. |
| Demand-owned host lifecycle emission | Host-only demand separates body validation from emission, emits used empty destructors, prunes unused inline/trivial lifecycle roots and calls, and exports lifecycle aliases with target linkage; pa32 75→78, prior 4150/4150, audit pass. |
| Parameter-rooted dependent-result recipes | Semantic registration publishes immutable ordinal/member/modifier nodes and ABI lowering consumes them for global/local names; array-result spelling passes, pa32 78→79, prior 4150/4150, audit pass. |
| Canonical standard-template substitutions | Semantic std template identities select generic `Sa`/`Sb` or exact `Ss`/`Si`/`So`/`Sd` ABI facts for types and owners; allocator, operator, and ostream fixtures pass, pa32 79→82, prior 4150/4150, audit pass. |

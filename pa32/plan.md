# PA32 Implementation Plan

## Stage Design and Spec Alignment

PA32 keeps the production path `semantic BindingRecord/FunctionTemplateAbiRecipe -> typed LowIR Symbol -> LowIR/MIR symbol metadata -> direct ELF`. The semantic/lowering layer owns canonical Itanium names and emission demand; the ELF writer owns only binding, coalescing, sections, and relocations. This applies `spec.md` §§2 and 6 (stable ABI identity and recorded ABI facts, no backend name reconstruction), §4 (separate definition/emission demand), §7 (direct ELF and linkage/COMDAT as the permitted whole-program decision), and §9 (linear or `n log n` lowering/object work).

## Current Failure Map

Current result: **78/133** (checkpoint baseline **75/133**, implementation start **59/133**); all **55** remaining failures are grouped below.

- ABI/template identity, demand, and coalescing (21): OOC constructor templates; dependent alias/NTTP/array/member-cv names; empty owner pack; data-member-pointer owner; extern-template constructor/member/static-data; enum and variadic-template-template names; internal-template local static; namespace operator, ODR default, static-self, ostream, and synthetic std/template substitutions.
- ELF data, TLS, and sections (7): C variable import and inherited definition; defined/imported global relocation class; GNU section; TLS import/export.
- Host call ABI and EH (7): goto-out-of-try; three cleanup/unwind cases; `f64` shuffle; member-function-pointer runtime; system-include move/reset.
- Semantic/linkage remainder (9): anonymous-namespace implicit/explicit special members, storage and call; invalid C/static redeclaration; explicit-specialization data; external default constructor; same-named local classes; typedef-linkage anonymous types.
- Virtual inheritance/lifecycle (11): result vbase access; external construction thunk; host vbase call; heap dispatch; local/multilevel objects; primary/secondary polymorphic layout; construction vtable; two virtual-diamond cases.

## Active Checkpoint

**Typed dependent-result recipes.** Publish canonical dependent qualified-result structure at function-template registration, including alias expansion, template-parameter ordinals, member components, template arguments, and `decltype` expressions. ABI lowering consumes the typed recipe directly instead of inferring dependence from the concrete specialization result.

- Owner/data flow: template registration owns immutable result-recipe nodes in `Program`; retained result-identity/lookup facts provide canonical entities and parameter ordinals; `pa15_lowering_abi.cpp` performs one typed recipe walk into ABI facts. Syntax remains semantic-phase-only.
- Complexity: publication and encoding are linear in expanded result components/arguments, with canonical interning for repeated results; no specialization-wide or global scan.
- Validation: array-reference qualified result, alias-expanded iterator result, dependent NTTP default result, ostream member result, then neighboring synthetic substitution fixtures and full gates.

## Performance Evidence

The lifecycle checkpoint's complex owner measured 114 tokens, 29 semantic nodes, 3 demand pushes/3 demanded functions, 4 emitted functions, and 14 LowIR instructions (0.555 ms semantic, 0.415 ms lowering, 0.081 ms object encoding). The trivial-owner case measured 75 tokens, 29 nodes, 2 pushes/2 demanded functions, 3 emitted functions, and 17 instructions, with zero `Box<int>` constructor symbols. Classification is O(1) per action/root and alias linkage propagation is linear in produced aliases.

## Completed Checkpoints

| Checkpoint | Result |
| --- | --- |
| Canonical host ABI symbols and ODR roots | Correct dependent substitutions/array-bound expressions, placement overload fallback, host new/delete names, and linear weak-root COMDAT records; pa32 59→75, prior 4150/4150, audit pass. |
| Demand-owned host lifecycle emission | Host-only demand separates body validation from emission, emits used empty destructors, prunes unused inline/trivial lifecycle roots and calls, and exports lifecycle aliases with target linkage; pa32 75→78, prior 4150/4150, audit pass. |

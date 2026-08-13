# PA32 Implementation Plan

## Stage Design and Spec Alignment

PA32 keeps the production path `semantic BindingRecord/FunctionTemplateAbiRecipe -> typed LowIR Symbol -> LowIR/MIR symbol metadata -> direct ELF`. The semantic/lowering layer owns canonical Itanium names and emission demand; the ELF writer owns only binding, coalescing, sections, and relocations. This applies `spec.md` §§2 and 6 (stable ABI identity and recorded ABI facts, no backend name reconstruction), §4 (separate definition/emission demand), §7 (direct ELF and linkage/COMDAT as the permitted whole-program decision), and §9 (linear or `n log n` lowering/object work).

## Current Failure Map

Current result: **75/133** (turn start **59/133**); all **58** remaining failures are grouped below.

- ABI/template identity, demand, and coalescing (24): complex-owner duplicate; OOC constructor templates; dependent alias/NTTP/array/member-cv names; empty owner pack; data-member-pointer owner; extern-template constructor/member/static-data; enum and variadic-template-template names; internal-template local static; namespace operator, ODR default, static-self, ostream, synthetic std/template substitutions; trivial-ctor pruning; qualified-inline linkage.
- ELF data, TLS, and sections (7): C variable import and inherited definition; defined/imported global relocation class; GNU section; TLS import/export.
- Host call ABI and EH (7): goto-out-of-try; three cleanup/unwind cases; `f64` shuffle; member-function-pointer runtime; system-include move/reset.
- Semantic/linkage remainder (9): anonymous-namespace implicit/explicit special members, storage and call; invalid C/static redeclaration; explicit-specialization data; external default constructor; same-named local classes; typedef-linkage anonymous types.
- Virtual inheritance/lifecycle (11): result vbase access; external construction thunk; host vbase call; heap dispatch; local/multilevel objects; primary/secondary polymorphic layout; construction vtable; two virtual-diamond cases.

## Active Checkpoint

**Typed dependent-result and lifecycle emission identity.** Publish dependent qualified-result structure and lifecycle demand as stable semantic recipe facts, then have ABI lowering consume those facts without syntax replay. This targets qualified template results, OOC/extern-template constructor and static-data references, empty user destructors, and qualified-inline pruning.

- Owner/data flow: function-template registration owns canonical result recipes and demand edges in `Program`; `pa15_lowering_abi.cpp` consumes them once; typed LowIR records only concrete emission identities.
- Complexity: recipe publication and encoding must be linear in result-type/name components; lifecycle demand uses the existing deduplicated worklist and must not scan all templates/functions.
- Validation: array/dependent-alias/NTTP/ostream result spellings, complex-owner and OOC constructor-template duplicates, extern-template references, trivial lifecycle pruning, then full pa32/prior/audit gates.

## Performance Evidence

`CPPGM_DRIVER_STATS=1` on the one-specialization duplicate measured 40 tokens, 15 semantic nodes, 6 LowIR instructions, one weak group, 0.332 ms semantic, 0.198 ms lowering, and 0.048 ms object encoding. The complex owner measured 114 tokens, 25 nodes, 12 instructions, two demanded weak groups, 0.503 ms semantic, 0.260 ms lowering, and 0.056 ms encoding. Work counters and groups rose with produced declarations/functions; no repeated global scan appeared.

## Completed Checkpoints

| Checkpoint | Result |
| --- | --- |
| Canonical host ABI symbols and ODR roots | Correct dependent substitutions/array-bound expressions, placement overload fallback, host new/delete names, and linear weak-root COMDAT records; pa32 59→75, prior 4150/4150, audit pass. |

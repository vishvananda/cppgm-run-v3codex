# PA32 Implementation Plan

## Stage Design and Spec Alignment

PA32 keeps the production path `semantic BindingRecord/FunctionTemplateAbiRecipe -> typed LowIR Symbol -> LowIR/MIR symbol metadata -> direct ELF`. The semantic/lowering layer owns canonical Itanium names and emission demand; the ELF writer owns only binding, coalescing, sections, and relocations. This applies `spec.md` §§2 and 6 (stable ABI identity and recorded ABI facts, no backend name reconstruction), §4 (separate definition/emission demand), §7 (direct ELF and linkage/COMDAT as the permitted whole-program decision), and §9 (linear or `n log n` lowering/object work).

## Current Failure Map

Current result: **84/133** (checkpoint baseline **83/133**, implementation start **59/133**); all **49** remaining failures are grouped below.

- ABI/template identity, demand, and coalescing (15): OOC constructor templates; member-function cv; empty owner pack; data-member-pointer owner; extern-template constructor/member/static-data; enum and variadic-template-template names; internal-template local static; ODR default, static-self, and synthetic template-argument substitutions.
- ELF data, TLS, and sections (7): C variable import and inherited definition; defined/imported global relocation class; GNU section; TLS import/export.
- Host call ABI and EH (7): goto-out-of-try; three cleanup/unwind cases; `f64` shuffle; member-function-pointer runtime; system-include move/reset.
- Semantic/linkage remainder (9): anonymous-namespace implicit/explicit special members, storage and call; invalid C/static redeclaration; explicit-specialization data; external default constructor; same-named local classes; typedef-linkage anonymous types.
- Virtual inheritance/lifecycle (11): result vbase access; external construction thunk; host vbase call; heap dispatch; local/multilevel objects; primary/secondary polymorphic layout; construction vtable; two virtual-diamond cases.

## Active Checkpoint

**Source function-type ABI recipes.** Retain dependent member-pointer owners and function cv/ref qualifiers in function-template parameter recipes so overloads remain distinct and source types survive specialization.

- Owner/data flow: declarator type formation publishes immutable owner/function nodes beside existing parameter shapes; recipes reference ordinal roots; lowering walks those nodes into ABI facts without matching concrete specialization types.
- Complexity: linear in parameter type nodes and qualifiers, with no overload-set or specialization scan.
- Validation: member-function cv overload spelling, dependent data-member-pointer owner spelling, static self-parameter spelling, focused substitution checks, then full gates.

## Performance Evidence

The dependent-default fixture measured 170 tokens, 56 semantic nodes, 20 canonical argument-list requests/16 cache hits/20 probes, three emitted functions, and 25 LowIR instructions (0.380 ms lowering, 0.071 ms object encoding). Literal capture is O(1); interning remains linear in list length. The mixed std fixture measured 327 tokens/83 nodes/26 instructions; the parameter-rooted result fixture measured 170 tokens/125 nodes/12 identity-atom visits. The alias/`decltype` fixture measured 280 tokens, 70 semantic nodes, 42 LowIR instructions, and five functions (0.473 ms lowering, 0.106 ms object encoding); publication and lowering each walk the type/expression graph once.

## Completed Checkpoints

| Checkpoint | Result |
| --- | --- |
| Canonical host ABI symbols and ODR roots | Correct dependent substitutions/array-bound expressions, placement overload fallback, host new/delete names, and linear weak-root COMDAT records; pa32 59→75, prior 4150/4150, audit pass. |
| Demand-owned host lifecycle emission | Host-only demand separates body validation from emission, emits used empty destructors, prunes unused inline/trivial lifecycle roots and calls, and exports lifecycle aliases with target linkage; pa32 75→78, prior 4150/4150, audit pass. |
| Parameter-rooted dependent-result recipes | Semantic registration publishes immutable ordinal/member/modifier nodes and ABI lowering consumes them for global/local names; array-result spelling passes, pa32 78→79, prior 4150/4150, audit pass. |
| Canonical standard-template substitutions | Semantic std template identities select generic `Sa`/`Sb` or exact `Ss`/`Si`/`So`/`Sd` ABI facts for types and owners; allocator, operator, and ostream fixtures pass, pa32 79→82, prior 4150/4150, audit pass. |
| Typed dependent NTTP defaults | Canonical arguments retain source literal type/value beside non-deduced target shape, so source `Li0E` and concrete `Lm0E` remain distinct; pa32 82→83, prior 4150/4150, audit pass. |
| Structured dependent result/expression recipes | Alias expansion publishes class-template arguments and source NTTP types; trailing `decltype` publishes typed parameter/member/call/binary nodes, with expression-local substitution order retained; distance/operator spelling passes, direct-result sharing is preserved, pa32 83→84, prior 4150/4150, audit pass. |

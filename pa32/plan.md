# PA32 Implementation Plan

## Stage Design and Spec Alignment

PA32 keeps the production path `semantic BindingRecord/TypeRecord/FunctionTemplateAbiRecipe -> canonical ABI graph -> typed LowIR Symbol/AddressBinding -> MIR/fixup metadata -> direct ELF`. Semantic/lowering records own entity terminals, callable qualifiers, source-template arguments/results, definition/preemption state, names, and emission demand; the ELF writer owns only coalescing, sections, and encoding already-selected relocation intent. ABI publication remains typed and transactional, and telemetry observes the path without changing serialized object metadata. This applies `spec.md` §§2 and 6 (stable typed identity and no name reconstruction), §4 (separate specialization/emission facts), §7 (direct ELF), §8 (explicit ownership), and §9 (linear work with behavior-neutral counters).

## Current Failure Map

Current result: **96/138**, up from the **91/138** checkpoint baseline. **42** tests remain failing; PA1–PA31 pass **4150/4150**.

- ABI/template identity, demand, and coalescing (13): OOC constructor templates; empty owner pack; extern-template constructor/member/static-data; enum and variadic-template-template names; internal-template local static; ODR default, static-self, and synthetic template-argument substitutions.
- ELF sections and TLS (3): GNU section placement and TLS import/export surfaces.
- Host call ABI and EH (6): goto-out-of-try; three cleanup/unwind cases; member-function-pointer runtime; system-include move/reset.
- Semantic/linkage remainder (9): anonymous-namespace implicit/explicit special members, storage and call; invalid C/static redeclaration; explicit-specialization data; external default constructor; same-named local classes; typedef-linkage anonymous types.
- Virtual inheritance/lifecycle (11): result vbase access; external construction thunk; host vbase call; heap dispatch; local/multilevel objects; primary/secondary polymorphic layout; construction vtable; two virtual-diamond cases.

## Next Substantial Checkpoint

**ELF section and TLS object ownership.** Consume the now-canonical section and thread-storage facts by placing each demanded object in its selected ELF section, publishing TLS symbol/type/wrapper facts, and selecting the supported host TLS relocation model. This targets GNU section placement and TLS import/export as one object-layout boundary.

- Spec alignment: `spec.md` §§6 and 8 require minimal typed lowering facts and explicit ownership; §7 assigns final sections, relocations, and cross-function linkage to the direct ELF writer; §9 requires O(n) or O(n log n) object writing.
- Owner/data flow: canonical semantic storage/attribute facts -> typed LowIR global storage and section metadata -> MIR global placement/TLS intent -> direct ELF section, STT_TLS symbol, and relocation records. The backend consumes facts and must not classify source names.
- Complexity: group globals by interned section identity in O(g) average, emit each global and relocation once, and perform only the existing stable O(s log s) symbol ordering; no per-global scan of all sections or symbols.
- Validation: focused custom-section and TLS import/export fixtures with `readelf` symbol/section/relocation checks, linked execution, the full PA32/prior/audit gates, and generated multi-section/TLS scaling evidence.

## Performance Evidence

Generated sources paired one defined and one imported object address per case at sizes 32, 64, and 128. Representative counters scale with produced semantics and output:

| Cases | Tokens | Semantic nodes | Functions | LowIR insns | Fixups | PC32 / GOT | Object bytes |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 32 | 993 | 417 | 64 | 128 | 128 | 96 / 32 | 108,424 |
| 64 | 1,985 | 833 | 128 | 256 | 256 | 192 / 64 | 215,880 |
| 128 | 3,969 | 1,665 | 256 | 512 | 512 | 384 / 128 | 431,664 |

Across the 4x range, semantic time was 1.69–6.59 ms, typed lowering 0.64–2.15 ms, native encoding 1.09–4.32 ms, and semantic peak storage 470,270–1,860,434 bytes. Every structural counter and output size remains linear. The audit now derives local/preemptible address binding once by typed `SymbolId`; each fixup selects its relocation with one enum branch and no name-keyed definition/weakness probe. Stats-on/off objects are byte-identical (`sha256 6ca023bff96c62d6677f8cd9b3fb407d2681d4ba7ee8c0a9945a7b04cefb72ff`).

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

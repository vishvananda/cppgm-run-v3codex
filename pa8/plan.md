# PA8 Implementation Plan

## Stage Design and Spec Alignment

PA8 extends the PA7 front end as
`immutable source buffers -> shared PA1-PA5 preprocessing/post-token callbacks ->
compact PA8 token facts -> integrated declaration/expression semantics -> one
canonical program graph -> linked emission identities -> direct relocated mock
image`. `dev/src/pa8_semantic.*` owns the program graph and `dev/nsinit.cpp` is
only the file/CLI adapter. Tokens retain decoded literal bytes, interned names,
and token order; declarations retain canonical `TypeId`, `EntityId`, linkage,
definition, initializer, and value-category/constant facts. No syntax tree or
textual semantic transport is introduced.

Relevant `spec.md` requirements are sections 1-3 (single forward parse/semantic
construction, canonical identity, direct scope/name indexes), 5 (typed facts
and precise work ownership), 6 (direct consumption of selected declarations
and conversions), 8 (central geometric storage and phase-local tokens), 9
(linear parsing/layout and average-O(1) identity lookup), and 10
(self-contained output). Templates, machine IR, allocation, and ELF behavior
remain later-stage concerns and are not synthesized here.

## Current Failure Map

Turn-start baseline was **0/60 PA8 tests** because every case reached the
`nsinit` stub. CP1 closes all groups and the current result is **60/60**:

- parsing/declaration ownership: function definitions, specifier validation,
  typedef declarators, qualified declarations, and namespace conflicts;
- typed initialization: literal/id expressions, value categories, standard
  conversions, arrays/string literals, references, `constexpr`, and
  `static_assert`;
- program ownership: storage/linkage, ODR/redeclarations across translation
  units, stable first-declaration order, image layout, relocation, and writing;
- scale ownership: deep namespace parsing/lookup and linear entity/image walks.

## Active Checkpoint

**CP1 — canonical PA8 initialization/link/image pipeline (complete).** The full
current grammar and diagnostic-required semantic surface is implemented at the
program-graph boundary. Names and types are interned once per invocation; each translation
unit has an isolated global scope; declarations flow to canonical entities,
typed initializer facts, an external-linkage index, and then stable emission
records. Layout assigns offsets in the three required blocks, after which one
relocation/write pass consumes recorded symbolic values.

Expected complexity: preprocessing and parsing O(tokens); direct lookup and
link insertion O(1) average per queried name/entity; namespace qualification
O(depth); type interning O(type arity) only when creating a canonical type;
link validation O(declarations plus required same-name candidates);
layout/relocation/write O(emitted bytes plus entities, temporaries, and string
literals). Validation is 60/60 PA8, 332/332 prior-through-PA7, 392/392
through-PA8, 60/60 under ASan+UBSan, clean `-Wall -Wextra -Werror`, and a
passing file audit. Its one body-weight warning is on the declaration-only
internal interface; implementations remain in responsibility-owned `.cpp`
files.

## Performance Evidence

Release `dev/nsinit`, generated nested namespace depth N with N initialized
variables, `CPPGM_FRONTEND_STATS=1`:

| N | tokens / scopes / declarations / image bytes | elapsed / RSS |
| ---: | --- | --- |
| 300 | 2,701 / 301 / 300 / 1,204 | 2.38 ms / 4,400 KiB |
| 600 | 5,401 / 601 / 600 / 2,404 | 5.05 ms / 4,568 KiB |
| 1,200 | 10,801 / 1,201 / 1,200 / 4,804 | 10.02 ms / 4,900 KiB |
| 2,400 | 21,601 / 2,401 / 2,400 / 9,604 | 21.22 ms / 5,940 KiB |

Every run recorded zero lookup visits, using-edge visits, and linkage
candidates because the workload requires none. Counts and time provide linear
evidence for the deep-namespace/parser/layout risk represented by the checked-in
600-level case; no retry or unrelated semantic scan appears.

## Completed Checkpoints

| Checkpoint | Result |
| --- | --- |
| CP1: canonical initialization, cross-TU linkage, relocation, and image emission | complete; PA8 60/60 and through-PA8 392/392 |

# PA10 Implementation Plan

## Stage Design and Spec Alignment

PA10 owns `immutable source buffer -> shared PA5 preprocessing callbacks ->
compact phase-7 token buffer -> integrated syntax parser -> arena-owned syntax
graph -> deterministic AST view`. The production parser preserves identifiers,
literals, unresolved qualified/template names, declarators, declarations,
statements, and expressions as typed syntax nodes; it does not perform PA11+
lookup or type checking. This applies `spec.md` §§1, 8, 9, and 10: one retained
token representation, interned identifier identity, bounded checkpoints with no
retained abandoned trees, geometric flat storage, O(source bytes + produced
tokens) preprocessing, O(tokens) expected parsing/rendering, and no external
compiler/reference path. Canonical semantics, demand, lowering, allocation,
and backend requirements in §§2–7 remain deferred.

## Current Failure Map

- **Closed — driver/ownership:** `cppgm++ --emit-ast` preprocesses each source
  in process and gives the PA10 parser sole ownership of tokens and syntax.
- **Closed — declarations/declarators:** qualified and template names, classes,
  members, functions, pointer forms, initializers, and ambiguity cases pass.
- **Closed — statements/expressions:** block disambiguation, control flow,
  precedence, casts, lambdas, calls, and template/expression `<` cases pass.
- **Closed — rejection/rendering:** malformed input exits unsuccessfully and all
  successful trees match the checked-in wrapper, spelling, and child order.

## Active Checkpoint

None. CP1 is closed: the driver owns sources/output, the token buffer owns
compact phase-7 facts, and the parser owns flat nodes and child IDs. Validation
confirmed the expected O(source bytes + tokens + syntax nodes) time and O(n)
storage without introducing PA11 lookup or type checking.

## Performance Evidence

Release build, AST output discarded, one warmed run per point. Counts and peak
bytes scale linearly; elapsed time remains approximately 2x per input doubling.

| Workload | Items | Tokens | Nodes | Peak bytes | Time (ms) |
|---|---:|---:|---:|---:|---:|
| declarations | 5k / 10k / 20k | 15,001 / 30,001 / 60,001 | 35,001 / 70,001 / 140,001 | 2,210,678 / 4,422,294 / 8,855,526 | 22.0 / 45.4 / 92.4 |
| expressions | 5k / 10k / 20k | 30,015 / 60,015 / 120,015 | 30,021 / 60,021 / 120,021 | 920,381 / 1,837,885 / 3,672,893 | 26.4 / 52.7 / 106.9 |

## Completed Checkpoints

| Checkpoint | Result |
|---|---|
| CP1 — source-to-syntax boundary | Complete: PA10 157/157 (also ASan/UBSan), through PA10 576/576, audit pass, linear 1x/2x/4x scaling. |

# PA23 Checkpoint Audit

## Current Checkpoint Review

The landed `84a3f7c5` increment moved the report from 308/404 to 315/404 by
making abstract construction/allocation, narrowing, virtual-base downcasts,
compound assignment, pseudo-destruction, and `void()` expression validity
candidate-local. It records virtual-base syntax on canonical base edges,
retains the reverse pointer-add fact for lowering, and exposes virtual-path
visits. The audit found one defect in that increment: its new `bool += pointer`
path treated every pointer as arithmetic-capable, so `void*`, function
pointers, pointers to incomplete classes, and pointers to unknown-bound arrays
incorrectly remained viable. The lowering-special-case flag was therefore
being published without the complete-object precondition required by the
language, contrary to `spec.md` sections 2, 3, 6, and 10.

Pointer arithmetic now has one typed semantic owner that applies array decay,
walks canonical cv/array structure, demands a named definition only where
completeness is semantically required, and accepts only a pointer to a complete
object. Binary addition/subtraction, increment, subscript, and both compound
directions use that owner; invalid candidate expressions record compact failure
without publishing the lowering flag. The same file audit exposed an oversized
binary-expression owner, so comparison and arithmetic validation were split
into typed helpers and their expected failures now use the same no-throw
candidate result. The selected valid assignment alone carries the reverse-add
fact into LowIR lowering; there is no string dispatch, lookup replay, or test
recognition on this path.

For 8/16/32/64 independent complete/incomplete-pointer probe groups, overload
candidates are 256/512/1,024/2,048, specialization requests
832/1,664/3,328/6,656, deduction visits 96/192/384/768, and lookups
1,837/3,653/7,285/14,549. Peak semantic bytes are
1,341,484/2,668,404/5,142,620/10,271,148 and three-run semantic medians are
7.87/15.91/30.42/61.87 ms. Virtual chains of depth 16/32/64/128 take
34/66/130/258 path visits and 0.88/1.26/2.10/3.88 ms. Candidate work, graph
work, storage, and time are linear in the participating candidates and edges;
the complete-object failure guard also finishes under `gdb catch throw`
without a C++ throw.

The original checkpoint remains 315/404 and the audit guard makes the combined
report 316/405, with all 89 prior failures unchanged and no timeout. PA1--PA22
pass 2,639/2,639, the file audit passes with the same 13 inherited
header-division advisories, and `git diff --check` passes.

## Checkpoint Audit Ledger

| Checkpoint | Evidence and disposition |
| --- | --- |
| Direct array-extent NTTP deduction (`b6d38290`, this audit) | Canonical bound deduction and ordinary typed lowering pass; candidate ownership was repaired at the source index and scales linearly; baseline preserved. |
| Defaulted function-template substitution (`ef0fa8c5`, this audit) | Declaration-owned default contexts, normalized dependent-result identity, and complete request states repair redeclaration correctness and repeated failed work; PA23 203 -> 210 with no regressions and linear success/failure scaling. |
| Immediate expression substitution and variadic class calls (`63596fc7`, this audit) | Single-branch parsing, compact explicit/deduced candidate failure, and a semantic variadic-class fact replace reparsing, exception control flow, and lowering reconstruction; PA23 stays 223/401 with linear scaling. |
| Declaration-time result lookup (`bf4b7a83`, this audit) | Pattern-owned canonical type/call facts preserve first-declaration lookup through redeclaration and substitution; original PA23 292 -> 296 with no regressions and linear candidate/depth evidence. |
| Candidate-local alias and detector replay (`94a33b59`, this audit) | Typed no-throw formation and monotonic alias failure states replace broad exception handling; retained base syntax replaces spelling dispatch; 307/403 is preserved and failure scaling is linear. |
| Candidate-local expression validity (`84a3f7c5`, this audit) | Shared complete-object pointer arithmetic repairs the reverse compound path; typed comparison/arithmetic failure preserves candidate ownership; original 315/404 is intact, the audit guard passes, and candidate/path scaling is linear. |

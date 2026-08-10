# PA23 Checkpoint Audit

## Current Checkpoint Review

The landed `94a33b59` increment moved the full report from 298 to 307 by
replaying dependent alias results, retained zero-argument call surrogates, and
`decltype` bases inside explicit candidate frames. The audit found two defects
in that increment. `FunctionCandidates` caught every `runtime_error` from
explicit argument and specialization formation as SFINAE; the invalid-alias
probe demonstrably unwound from `TypeTable::Pointer` through alias lookup and
candidate construction. `ResolveClassDirectBase` also selected `decltype`
replay by comparing the first eight payload characters, causing an ordinary
class named `decltype_base` to disappear. These violated `spec.md` sections
2--4 and the representation, candidate-failure, and repeated-work checklist.

Canonical type ownership now exposes no-throw `Try*` formation for qualifiers,
pointers, references, arrays, member pointers, and functions. Declarator
analysis converts the `kNoType` result to its candidate frame at the exact
invalid construct; array-bound formation has a separate bounded owner.
Alias-specialization requests distinguish in-progress, success, expected
failure, and hard failure, and cache hits replay expected failure without
throwing. The explicit-template caller therefore has no broad catch, while
hard errors still propagate. This traces one typed path from retained argument
syntax through canonical type and alias formation to overload candidate
discard.

The parser's retained `decltype` base expression is now the discriminator for
base replay. Structured names use their structured node, retained expressions
use their semantic child, and ordinary bases use indexed name lookup; payload
text is presentation only. The new guard covers the spelling-prefix collision,
and the landed `decltype`-base detector cases remain on the same parsed syntax.

For 16/32/64/128 independent invalid-alias candidates, overload visits are
16/32/64/128, specialization requests are 176/352/704/1,408, cache hits are
96/192/384/768, failed-default cache hits are 16/32/64/128, deduction visits
are 32/64/128/256, and lookup queries are 844/1,660/3,292/6,556. Peak semantic
bytes are 727,475/1,446,883/2,887,131/5,766,323 and three-run semantic medians
are 4.08/7.89/15.35/30.90 ms. Work, storage, and time track participating
candidates linearly; representative invalid-alias and nested-alias programs
also complete under `gdb catch throw` with no C++ throw.

The landed 307/403 checkpoint baseline is intact; the new guard makes the full
report 308/404, while the original suite remains 305/401 and all 96 prior
failures remain confined to the existing map with no timeout. PA1--PA22 are
2,639/2,639, the file audit passes with the same 13 inherited advisories, and
`git diff --check` passes.

## Checkpoint Audit Ledger

| Checkpoint | Evidence and disposition |
| --- | --- |
| Direct array-extent NTTP deduction (`b6d38290`, this audit) | Canonical bound deduction and ordinary typed lowering pass; candidate ownership was repaired at the source index and scales linearly; baseline preserved. |
| Defaulted function-template substitution (`ef0fa8c5`, this audit) | Declaration-owned default contexts, normalized dependent-result identity, and complete request states repair redeclaration correctness and repeated failed work; PA23 203 -> 210 with no regressions and linear success/failure scaling. |
| Immediate expression substitution and variadic class calls (`63596fc7`, this audit) | Single-branch parsing, compact explicit/deduced candidate failure, and a semantic variadic-class fact replace reparsing, exception control flow, and lowering reconstruction; PA23 stays 223/401 with linear scaling. |
| Declaration-time result lookup (`bf4b7a83`, this audit) | Pattern-owned canonical type/call facts preserve first-declaration lookup through redeclaration and substitution; original PA23 292 -> 296 with no regressions and linear candidate/depth evidence. |
| Candidate-local alias and detector replay (`94a33b59`, this audit) | Typed no-throw formation and monotonic alias failure states replace broad exception handling; retained base syntax replaces spelling dispatch; 307/403 is preserved and failure scaling is linear. |

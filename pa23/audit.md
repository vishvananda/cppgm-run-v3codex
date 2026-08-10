# PA23 Checkpoint Audit

## Current Checkpoint Review

The landed `bf4b7a83` increment correctly rejected three unknown nondependent
result names and moved PA23 from 292 to 295, but its lookup ownership was not
safe. Redeclaration comparison looked up an earlier result again in the current
scope, declaration validation retained no type or call identity, and a fixed
qualified call with dependent template arguments escaped validation. A later
declaration could therefore change the first declaration's meaning, contrary
to N3485 14.5.6.1 and 14.6 and `spec.md` sections 2--4.

`FunctionTemplatePattern` now owns a compact sorted set of declaration-time
result facts: syntax identity, canonical type declaration or namespace,
access context, and the existing retained call candidate sequence plus ADL
eligibility. Redeclaration equivalence compares the recorded canonical root;
when a definition replaces declaration syntax, one bounded syntax walk remaps
the first declaration's facts. Substitution activates that pattern-owned view,
so structured type formation and call analysis consume retained identities
without a new lookup or a global syntax cache. Fixed nondependent qualifiers
are validated immediately, while genuinely dependent qualifiers remain
deferred.

The complete path is now retained syntax and trailing-return scope, then
declaration-owned lookup facts, canonical redeclaration merge, candidate-local
substitution, and ordinary typed lowering. The existing recursive dependent
call probe now passes because its first candidate set is stable. Added audit
guards cover later type shadowing, namespace/global qualification, later
overloads, and an unknown fixed-qualified dependent call.

For 32/64/128/256 paired first-lookup call and type declarations, lookup queries
are 954/1,882/3,738/7,450, deduction visits are 128/256/512/1,024, peak semantic
bytes are 1,586,723/3,158,951/6,038,783/12,061,031, and three-run semantic
medians are 6.87/13.12/25.84/51.59 ms. Nested retained-result depths
16/32/64/128 use 76/124/220/412 lookups and 90,279/133,607/308,466/694,579
peak bytes, with medians 0.79/1.20/2.12/4.78 ms. The counters and memory track
participating declarations and syntax depth without unrelated lookup growth.

The original PA23 suite is 296/401; with two audit guards the report is
298/403, above the 295/401 turn-start baseline with no regression. PA1--PA22
are 2,639/2,639, the file audit passes with the same 13 inherited advisories,
and `git diff --check` passes.

## Checkpoint Audit Ledger

| Checkpoint | Evidence and disposition |
| --- | --- |
| Direct array-extent NTTP deduction (`b6d38290`, this audit) | Canonical bound deduction and ordinary typed lowering pass; candidate ownership was repaired at the source index and scales linearly; baseline preserved. |
| Defaulted function-template substitution (`ef0fa8c5`, this audit) | Declaration-owned default contexts, normalized dependent-result identity, and complete request states repair redeclaration correctness and repeated failed work; PA23 203 -> 210 with no regressions and linear success/failure scaling. |
| Immediate expression substitution and variadic class calls (`63596fc7`, this audit) | Single-branch parsing, compact explicit/deduced candidate failure, and a semantic variadic-class fact replace reparsing, exception control flow, and lowering reconstruction; PA23 stays 223/401 with linear scaling. |
| Declaration-time result lookup (`bf4b7a83`, this audit) | Pattern-owned canonical type/call facts preserve first-declaration lookup through redeclaration and substitution; original PA23 292 -> 296 with no regressions and linear candidate/depth evidence. |

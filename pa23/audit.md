# PA23 Checkpoint Audit

## Current Checkpoint Review

The landed `63596fc7` increment produced the intended 12 gains in immediate
expression substitution and variadic class calls, but three ownership defects
crossed the checkpoint boundary. `sizeof(f<T>())` first parsed its operand as a
type-id, rolled back, and parsed it again as an expression; ordinary SFINAE
rejections unwound `std::runtime_error` through explicit and deduced candidate
paths; and lowering rediscovered an ellipsis class argument from function arity
and value type. These violated `spec.md` sections 1, 3, and 6 even though the
223/401 checkpoint baseline passed.

Trait disambiguation now uses the parser's bounded, cached token-only template
angle scan and selects exactly one grammar branch. No abandoned type-id tree or
second expression parse is created. Semantic candidate frames now own compact
failure state across explicit argument binding, deduction, default
materialization, declarator/type formation, member and overload lookup,
construction, `decltype`, `noexcept`, and invalid operator checks. Invalid
types stop at each typed boundary before reaching canonical type construction
or declaration publication; hard errors outside candidate substitution retain
their diagnostic behavior.

The selected call node now records `variadic_class_argument` alongside its
materialization fact. PA15 lowering consumes that bit directly when choosing
the class-object storage path and no longer reconstructs template or ABI
semantics from arity and type. The candidate-frame capacity is included in
semantic storage telemetry.

At 1,024/2,048/4,096 repeated valid and failed probes, deduction visits are
2,048/4,096/8,192 and overload-candidate visits are respectively
5,123/10,243/20,483 and 4,099/8,195/16,387. Each disposition materializes its
default once; failed-request cache hits are 2,047/4,095/8,191. Peak semantic
bytes are 7,762,775/15,509,719/31,003,607 for valid probes and
7,764,295/15,513,031/31,010,503 for failed probes. Three-run semantic medians
are 38.8/79.0/159.1 ms and 38.4/78.6/155.2 ms. The failed medians were
59.4/117.3/238.1 ms before the audit, so removing exception unwinding materially
reduced rejection cost while preserving linear work and memory.

The required PA23 report remains 223/401, PA1--PA22 remain 2,639/2,639, and the
file audit passes with the same 13 inherited header-division advisories. All 12
landed gains plus two baseline-sensitive substitution cases complete under
`gdb catch throw` with no C++ throw, and `git diff --check` passes.

## Checkpoint Audit Ledger

| Checkpoint | Evidence and disposition |
| --- | --- |
| Direct array-extent NTTP deduction (`b6d38290`, this audit) | Canonical bound deduction and ordinary typed lowering pass; candidate ownership was repaired at the source index and scales linearly; baseline preserved. |
| Defaulted function-template substitution (`ef0fa8c5`, this audit) | Declaration-owned default contexts, normalized dependent-result identity, and complete request states repair redeclaration correctness and repeated failed work; PA23 203 -> 210 with no regressions and linear success/failure scaling. |
| Immediate expression substitution and variadic class calls (`63596fc7`, this audit) | Single-branch parsing, compact explicit/deduced candidate failure, and a semantic variadic-class fact replace reparsing, exception control flow, and lowering reconstruction; PA23 stays 223/401 with linear scaling. |

# PA25 Plan

## Stage Design and Spec Alignment

PA25 extends the shared PA10 syntax, PA12 semantic graph, and PA15 typed
lowering path without adding a transport representation. Range delimiter
lookahead is bounded and allocation-free; selected syntax regions are parsed
once. Placeholder/range initializers and conversion targets are analyzed once;
canonical `TypeId`, selected-operation, conversion, and class-boundary ABI facts
remain at their semantic owner, and PA15 consumes those identities directly.
Demanded placeholder-template bodies use a canonical monotonic state, are
analyzed only after specialization cache publication, and republish their
completed function type/ABI before lowering demand. Class completion publishes
generic result and parameter ABI facts; template patterns retain dependent
result identity, and canonical function bindings retain only callable-specific
overrides. Each captureless closure has one canonical syntax/enclosing-function
key and owns its call parameters, declaration-level pack identity, result/body
state, and lexical parent-function access edge. Access follows only indexed
lexical/member/friend facts and does not import an uncaptured implicit object.
Return-slot planning and PA15 consume those facts directly.
Full-expression and scope lifetime actions use the shared typed cleanup path;
LowIR text remains only the requested output view.

This aligns with `spec.md` sections 1 (one parse and bounded checkpoints), 2
(canonical type/declaration identity), 3 (indexed member/ADL lookup with chosen
conversions retained), 4 (dependent retained bodies instantiated on demand), 5
(monotonic specialization and body facts), 6 (direct typed lowering without
lookup replay), 8 (explicit phase-local ownership), and 9 (work proportional
to syntax, actual candidates, and emitted IR).

## Current Failure Map

The original PA25 set remains 102/132, up from the pre-checkpoint 90/132; three
passing audit regressions make the current report 105/135. The remaining 30
failures group by first owner: captureless invocation-function/pointer
conversion and namespace identity; capture field/layout and nested capture
lookup; closure transfer/special-member viability through templates; broader
template-pack substitution; and one independent retained local-declaration /
nested class-template parse ambiguity. Lexical private access now reaches the
deferred invocation-function output path rather than failing semantic analysis.

## Active Checkpoint

The next substantial checkpoint is captureless invocation-function and
pointer-conversion ownership. PA12 will attach one canonical static invocation
binding and one implicit conversion fact to each eligible closure, including
namespace-scope identity. Overload resolution will consume that conversion
through the indexed class candidate set, and PA15 will lower the selected
invocation binding directly. This applies `spec.md` sections 2-5 (canonical identity, indexed
selection, complete cache keys, and monotonic demand), 6 (selected typed facts
crossing into lowering), 8 (closure-owned facts), and 9. Expected complexity is
O(parameters + selected candidates + demanded body + emitted IR) once per
closure, with O(1)-average binding/fact lookup. Validate direct prvalue calls,
function-pointer initialization/unary `+`/comparison, wrapper conversion,
global lambdas, lexical-access interaction, and template-specialized closures
before the complete PA25, PA1-24, scaling, and audit gates.

## Performance Evidence

For 16/64/256 sibling captureless closures performing private-member access,
tokens were 471/1,719/6,711, semantic nodes 348/1,308/5,148, closure requests
16/64/256, access path visits 32/128/512, and access-grant probes
64/256/1,024. Demand pushes/emissions were 18/66/258, functions 19/67/259,
instructions 223/847/3,343, and typed storage
66,195/256,323/1,017,459 bytes. Five-run median semantic time was
1.495/4.799/18.736 ms and lowering time 0.459/1.222/4.426 ms; work, storage,
and time track closures and emitted IR.

## Completed Checkpoints

| Checkpoint | Result | Validation |
|---|---|---|
| Ordinary placeholder deduction and checkpoint audit | Canonical variable/function results; condition/static-member auto; one retained member body; cv-reference correctness; direct initializer ownership; no placeholder-path deep copies | Shipped PA25 46/121 preserved; audit regressions 4/4; local PA25 50/125; PA1-24 3,471/3,471; file audit pass |
| Range-for statement closure and audit | Single-parse dispatch; category-correct one-time range materialization; counted array/braced paths; selected member/ADL and iterator facts; retained templates; complete condition/iteration lifetimes; direct typed CFG lowering | Range-owned 13/13 plus audit 3/3; PA25 67/128 (+17 from pre-range); PA1-24 3,471/3,471; reducer executables, file audit, and diff checks pass |
| Target-directed list, aggregate, and array initialization | Fundamental `T{...}`; adjacent strings; braced char arrays and reference viability; direct/omitted aggregate plans; canonical helper-prefix reuse; nested array members; class boundary copies; array temporary identity | Checkpoint 12/12; PA25 79/128 (+12); PA1-24 3,471/3,471; file audit and diff checks pass; 16/64/256 scaling is linear in elements and emitted actions |
| Selected class conversions and typed value boundaries plus checkpoint audit | One-class conditional construction; single-parse canonical conversion targets; modifiable lvalue-reference increment; semantic-owned direct derived parameter ABI | Checkpoint and neighbors 10/10; PA25 baseline 83/128 preserved plus audit 2/2 (85/130); PA1-24 3,471/3,471; file audit passes; 16/64/256 candidates scale linearly |
| Function-template placeholder results and deduced class-value locals plus checkpoint audit | Canonical four-state retained-body demand; cache-before-analysis; completed canonical result publication; semantic-owned generic class-result ABI and dependent-pattern/function override facts; selected same-type class transfer | Shipped PA25 88/130 (+1 audit repair), audit regressions 2/2 for 90/132; PA1-24 3,471/3,471; file audit and diff checks pass; 16/64/256 specializations scale linearly |
| Ordinary captureless call-operator formation plus checkpoint audit | Canonical closure key; parameter/default and zero-pack facts; scoped explicit and four-state implicit results; cache-before-body analysis; lexical access edge without implicit-object capture; direct typed call/body lowering; empty closure storage identity | Original PA25 102/132 (+12) preserved plus audit 3/3 for 105/135; focused 8/8; PA1-24 3,471/3,471; runtime and file audit pass; 16/64/256 closure/access scaling is linear |

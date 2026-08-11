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
overrides. Each closure has one canonical syntax/context key and owns its call
parameters, result/body state, lexical access edge, and contiguous capture
member range. Call scopes borrow indexed aliases, so nested and pack capture
chains remain explicit; only empty closures own the static invocation and
pointer-conversion facts. Return-slot planning and PA15 consume those facts
directly.
Full-expression and scope lifetime actions use the shared typed cleanup path;
LowIR text remains only the requested output view.

This aligns with `spec.md` sections 1 (one parse and bounded checkpoints), 2
(canonical type/declaration identity), 3 (indexed member/ADL lookup with chosen
conversions retained), 4 (dependent retained bodies instantiated on demand), 5
(monotonic specialization and body facts), 6 (direct typed lowering without
lookup replay), 8 (explicit phase-local ownership), and 9 (work proportional
to syntax, actual candidates, and emitted IR).

## Current Failure Map

PA25 is 135/135, up from the turn-start 105/135 and checkpoint baseline
115/135. The capture/layout, closure transfer, retained pack, dependent lookup,
and declaration/expression ambiguity groups are closed. PA1-24 remains
3,471/3,471 and the stage file audit passes.

## Active Checkpoint

The completed checkpoint implements the PA25 by-reference-local and `this`
closure object model with closure transfer. PA12 owns parsed/discovered capture
facts, canonical reference/pointer fields, layout, call-scope aliases, and the
copy-constructor fact. Nested closures and packs consume explicit alias chains;
capturing closures gain neither assignment nor captureless pointer conversion.
PA15 lowers the existing aggregate/member facts directly. Reentrant
constructor/ABI analysis, retained dependency emission, and the remaining
declaration ambiguity now preserve canonical owners across lazy publication.
This applies `spec.md` sections 1-6, 8, and 9. Work is O(captures + referenced
names + viable candidates + body + IR), with O(1)-average indexed lookup.

## Performance Evidence

For 16/64/256 sibling captureless closures converted to function pointers,
tokens were 461/1,805/7,181, semantic nodes 359/1,415/5,639, closure requests
and overload candidates 16/64/256, conversion checks 258/1,026/4,098, demand
pushes/emissions 16/64/256, instructions 131/515/2,051, and typed storage
42,500/167,156/666,404 bytes. Five-run median semantic time was
1.515/5.141/20.101 ms and lowering time 0.296/0.866/3.096 ms; all counted work,
storage, and time track closures and emitted IR.

For one default-reference closure capturing 16/64/256 locals, tokens were
138/474/1,818, semantic nodes 142/526/2,062, layout visits 16/64/256, lookup
queries 113/401/1,553, conversion checks 81/321/1,281, instructions
148/580/2,308, and typed storage 35,199/132,687/522,639 bytes. Five-run median
semantic time was 0.629/1.590/6.108 ms and lowering time
0.199/0.438/1.543 ms, consistent with linear capture and emitted-IR growth.

## Completed Checkpoints

| Checkpoint | Result | Validation |
|---|---|---|
| Ordinary placeholder deduction and checkpoint audit | Canonical variable/function results; condition/static-member auto; one retained member body; cv-reference correctness; direct initializer ownership; no placeholder-path deep copies | Shipped PA25 46/121 preserved; audit regressions 4/4; local PA25 50/125; PA1-24 3,471/3,471; file audit pass |
| Range-for statement closure and audit | Single-parse dispatch; category-correct one-time range materialization; counted array/braced paths; selected member/ADL and iterator facts; retained templates; complete condition/iteration lifetimes; direct typed CFG lowering | Range-owned 13/13 plus audit 3/3; PA25 67/128 (+17 from pre-range); PA1-24 3,471/3,471; reducer executables, file audit, and diff checks pass |
| Target-directed list, aggregate, and array initialization | Fundamental `T{...}`; adjacent strings; braced char arrays and reference viability; direct/omitted aggregate plans; canonical helper-prefix reuse; nested array members; class boundary copies; array temporary identity | Checkpoint 12/12; PA25 79/128 (+12); PA1-24 3,471/3,471; file audit and diff checks pass; 16/64/256 scaling is linear in elements and emitted actions |
| Selected class conversions and typed value boundaries plus checkpoint audit | One-class conditional construction; single-parse canonical conversion targets; modifiable lvalue-reference increment; semantic-owned direct derived parameter ABI | Checkpoint and neighbors 10/10; PA25 baseline 83/128 preserved plus audit 2/2 (85/130); PA1-24 3,471/3,471; file audit passes; 16/64/256 candidates scale linearly |
| Function-template placeholder results and deduced class-value locals plus checkpoint audit | Canonical four-state retained-body demand; cache-before-analysis; completed canonical result publication; semantic-owned generic class-result ABI and dependent-pattern/function override facts; selected same-type class transfer | Shipped PA25 88/130 (+1 audit repair), audit regressions 2/2 for 90/132; PA1-24 3,471/3,471; file audit and diff checks pass; 16/64/256 specializations scale linearly |
| Ordinary captureless call-operator formation plus checkpoint audit | Canonical closure key; parameter/default and zero-pack facts; scoped explicit and four-state implicit results; cache-before-body analysis; lexical access edge without implicit-object capture; direct typed call/body lowering; empty closure storage identity | Original PA25 102/132 (+12) preserved plus audit 3/3 for 105/135; focused 8/8; PA1-24 3,471/3,471; runtime and file audit pass; 16/64/256 closure/access scaling is linear |
| Captureless invocation and pointer-conversion ownership | Canonical static invoker and conversion binding; retained constructor-argument conversion; direct temporary surrogate calls; stored call operators; namespace lambda ABI/init identity | PA25 115/135 (+10); focused 11/11; PA1-24 3,471/3,471; file audit pass; 16/64/256 conversion scaling is linear |
| By-reference/`this` captures and PA25 closure | Canonical capture ranges, fields, aliases, and nested/pack chains; copy-only transfer; typed aggregate lowering; stable reentrant ABI/constructor analysis; retained dependency and ambiguity closure | PA25 135/135 (+20 checkpoint, +30 turn); PA1-24 3,471/3,471; audit/diff pass; 16/64/256 capture scaling is linear |

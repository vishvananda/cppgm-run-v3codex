# PA25 Plan

## Stage Design and Spec Alignment

PA25 extends the shared PA10 syntax, PA12 semantic graph, and PA15 typed
lowering path without adding a transport representation. Range delimiter
lookahead is bounded and allocation-free; selected syntax regions are parsed
once. Placeholder/range initializers and conversion targets are analyzed once;
canonical `TypeId`, selected-operation, conversion, and class-boundary ABI facts
remain at their semantic owner, and PA15 consumes those identities directly.
Full-expression and scope lifetime actions use the shared typed cleanup path;
LowIR text remains only the requested output view.

This aligns with `spec.md` sections 1 (one parse and bounded checkpoints), 2
(canonical type/declaration identity), 3 (indexed member/ADL lookup with chosen
conversions retained), 4 (dependent retained bodies instantiated on demand), 6
(direct typed lowering without lookup replay), 8 (explicit phase-local
ownership), and 9 (work proportional to syntax, actual candidates, and emitted
IR).

## Current Failure Map

The landed checkpoint's 83/128 baseline remains intact; two audit regressions
now produce 85/130. The same 45 failures remain and group by first owner:
deferred function-template result deduction (1), lambda closure synthesis/calls
(41), and retained-template recovery (3). The lambda/range case remains charged
to lambda because closure synthesis fails before its range body is analyzed.

## Active Checkpoint

The next substantial checkpoint closes the four function-template result and
retained-recovery failures at the on-demand specialization boundary. PA19/PA23
own canonical function result identity, specialization lookup, retained-body
ownership, and dependency demand; PA12 publishes the completed typed graph and
PA15 lowers it without reparsing or replaying deduction. Relevant `spec.md`
requirements are sections 2 (canonical declaration/type identity), 4 (retained
syntax instantiated on demand), 6 (direct typed lowering), 8 (explicit lifetime
and cache ownership), and 9 (work proportional to retained syntax, requested
specializations, dependency edges, and emitted IR). Expected complexity is
O(retained syntax + requested specializations + dependency edges + emitted
actions), with indexed identity lookups and no translation-unit scan. Validate
all four failures, adjacent passing result-deduction/lifecycle cases, PA1-24,
PA25 progress, and file audit.

## Performance Evidence

For 16/64/256 conversion-function candidates resolved by one retained
canonical-target using-declaration, tokens were 211/787/3,091, declarations
69/213/789, lookup queries 73/217/793, signature probes 82/226/802, and access
checks 25/73/265. Five-run median semantic time was 0.437/1.118/3.783 ms; peak
semantic storage was 102,152/333,672/1,319,100 bytes. Lowering stayed at
0.033/0.034/0.052 ms with no emitted functions. Candidate work and storage track
the declaration set without a whole-program or quadratic lookup trend.

## Completed Checkpoints

| Checkpoint | Result | Validation |
|---|---|---|
| Ordinary placeholder deduction and checkpoint audit | Canonical variable/function results; condition/static-member auto; one retained member body; cv-reference correctness; direct initializer ownership; no placeholder-path deep copies | Shipped PA25 46/121 preserved; audit regressions 4/4; local PA25 50/125; PA1-24 3,471/3,471; file audit pass |
| Range-for statement closure and audit | Single-parse dispatch; category-correct one-time range materialization; counted array/braced paths; selected member/ADL and iterator facts; retained templates; complete condition/iteration lifetimes; direct typed CFG lowering | Range-owned 13/13 plus audit 3/3; PA25 67/128 (+17 from pre-range); PA1-24 3,471/3,471; reducer executables, file audit, and diff checks pass |
| Target-directed list, aggregate, and array initialization | Fundamental `T{...}`; adjacent strings; braced char arrays and reference viability; direct/omitted aggregate plans; canonical helper-prefix reuse; nested array members; class boundary copies; array temporary identity | Checkpoint 12/12; PA25 79/128 (+12); PA1-24 3,471/3,471; file audit and diff checks pass; 16/64/256 scaling is linear in elements and emitted actions |
| Selected class conversions and typed value boundaries plus checkpoint audit | One-class conditional construction; single-parse canonical conversion targets; modifiable lvalue-reference increment; semantic-owned direct derived parameter ABI | Checkpoint and neighbors 10/10; PA25 baseline 83/128 preserved plus audit 2/2 (85/130); PA1-24 3,471/3,471; file audit passes; 16/64/256 candidates scale linearly |

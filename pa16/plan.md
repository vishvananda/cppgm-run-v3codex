# PA16 Plan

## Stage Design and Spec Alignment

PA16 extends the shared PA11/PA12 semantic graph and PA15 typed LowIR path in
place: syntax publishes canonical entities and bindings, semantic analysis
resolves lookup, access, conversions, initialization, and lifetime, and lowering
consumes those IDs and actions without repeating lookup or transporting text.
Class entities own completion/layout/special-member facts; bindings own names,
signatures, access, storage, and declaration identity; resolved expression nodes
retain the selected binding, value category, conversion, and subobject path.

The operator spine follows `spec.md` sections 2-6, 8, and 9. Friend declarations
retain lexical and granting-class identity, associated classes/scopes are
canonical generation-marked IDs, and ordinary/member/ADL candidates are deduped
before one non-throwing viability/ranking pass. The selected call uses the
existing typed call ABI path; built-in analysis is fallback, not a competing
textual rewrite. Work is O(S + C*A) for S associated scopes, C candidates, and A
operands, with scratch owned by semantic analysis and reused across expressions.

The next increments preserve that boundary: lookup/access adds canonical base
paths and using/friend grants to semantic facts; initialization, layout, and
metadata then consume those facts. Demand remains monotonic, whole-program state
is restricted to emission/lifecycle ordering, and lowering stays lookup-free.

## Current Failure Map

Current state is **185/269 PA16 tests**, up from 154/269: 31 baseline failures
are fixed, no baseline pass is lost, and PA1-PA15 remain 1,145/1,145. The complete
84-failure remainder, assigned once by primary owner, is: 20 lookup/access/
inheritance; 19 layout/bit-field/inheriting-constructor; 15 procedural cast and
metadata; 14 initialization/temporary-lifetime; 12 call/declarator metadata; and
4 residual operator/ADL/callable cases whose primary blockers are temporary
materialization, user-defined conversion, or literal-operator publication.

## Active Checkpoint

**Next: indexed access/friend/using closure.** Class and declaration analysis
will own direct-base access, friend grants, using-declaration source/target, and
out-of-class member ownership as canonical IDs. Lookup will return a selected
binding plus naming class and bounded base path; access checking will consume
that path and the current member/friend context exactly once. Data flow is
`declaration -> canonical class/base/grant edges -> indexed lookup candidate ->
access/path decision -> resolved expression or structured rejection -> typed
lowering`. Expected work is O(B + C) over visited base edges and direct
candidates, with generation marks preventing repeated traversal. Validation
covers the 20-case lookup/access group, hidden-friend definition ownership,
using-declaration hiding/re-exposure, nested/enclosing access, exact 1x/2x edge
counters, through-PA15, the PA16 report, and file audit.

## Performance Evidence

| Boundary | Representative 1x / 2x evidence |
|---|---|
| Field/layout and use lookup | 5k/10k fields: 5,000/10,000 visits; 5k/10k uses: 5,003/10,003 probes; semantic and lowering times scale proportionally |
| Member/constructor overloads | 1,001/2,001 candidates: 2,006/4,006 conversion checks for members and 1,004/2,004 for constructors; emitted instructions remain constant |
| Aggregate/constructor actions | 1k/2k members: 1,000/2,000 owned actions and proportional semantic/lowered nodes; bounded nested retention removes the former depth-by-leaf product |
| Base construction/lookup | 250/500 edges: 250/500 base actions, 500/1,000 lookup-edge visits, 2.11/4.58 ms semantic and 1.71/3.27 ms lowering medians |
| Cleanup/destruction | 100/200 elements/members: proportional action visits and shared suffixes; 1,505/3,005 array and 1,015/2,015 destructor instructions |
| Namespace lifetime | 1k/2k objects: 1,000/2,000 actions, 4,016/8,016 instructions, 2,003/4,003 probes; one ordered cross-TU init/fini pair |
| Operator/ADL candidates | 128/256 associated classes: 128/256 scope visits, 258/514 candidates, 836/1,668 conversion checks, 0.544/1.000 ms semantic and 0.441/0.749 ms lowering five-run medians |

Exact counters establish proportional work at each scaling-sensitive owner; the
operator probe also emitted 220/412 instructions and retained 47,373/86,541
typed bytes.

## Completed Checkpoints

| Checkpoint | State | Closure evidence |
|---|---|---|
| Direct-member object spine | Complete | Canonical layout/member facts and typed field projection; 42/247; linear field/use curves; gates clean |
| Resolved member-call spine | Complete | Object-aware ranking, access/cv facts, hidden `this`, stable ABI IDs, typed calls; 51/247; linear candidate curve |
| Local aggregate-action spine | Complete | C++11 aggregate/union rules, borrowed brace cursor, member-ID actions, bounded projections; 61/248; gates clean |
| Special-member initialization spine | Complete | Init-mode selection, canonical ordered actions, exception facts, typed references/subobjects; 91/255; linear curves |
| Single-base construction spine | Complete | Canonical base/access edges, naming-class lookup, selected actions, recorded projections; 120/259; linear edge curves |
| Destruction and lexical-cleanup spine | Complete | Demand/access/deletion facts, reverse lifetime, lexical exits, shared EH suffixes; 132/265; linear cleanup curves |
| Namespace/static lifetime spine | Complete | TLS/linkage facts, static serialization, sparse identity, one ordered program lifecycle pair; 154/269; linear curves |
| Operator/ADL callable spine | Complete | Canonical associated scopes and hidden-friend edges, mixed member/free ranking, built-in fallback, callable/out-of-class operators, ABI terminals; 185/269 (+31, no losses); 1,145/1,145 through PA15; exact 128/256 scaling; audit pass |

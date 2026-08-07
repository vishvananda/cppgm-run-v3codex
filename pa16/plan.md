# PA16 Plan

## Stage Design and Spec Alignment

PA16 extends the shared PA11/PA12 semantic graph and PA15 typed LowIR path in
place: syntax publishes canonical entities and bindings, semantic analysis
resolves lookup, access, conversions, initialization, and lifetime, and lowering
consumes those IDs and actions without repeating lookup or transporting text.
Class entities own completion/layout/special-member facts; bindings own names,
signatures, access, storage, and declaration identity; resolved expression nodes
retain the selected binding, value category, conversion, and subobject path.

The audited operator spine follows `spec.md` sections 2-6, 8, and 9. Canonical
bindings retain typed operator kind and literal suffix; ordinary functions and
hidden friends have separate `(scope, name)` and `(granting class, name)` indexes;
associated classes/scopes are generation-marked IDs. Candidate union, conversion,
and ranking happen once before the selected call enters the existing typed ABI
path. Built-in analysis remains fallback. `nullptr_t`, generated UDL arguments,
and ABI terminals cross lowering as typed facts, never recovered from spelling.
Work is O(S + H + C*A) for associated scopes S, eligible hidden-friend edges H,
candidates C, and operands A; unrelated same-named hidden friends are not visited.

The access closure now adds canonical enclosing/base paths and using/friend
grants to those facts. The next layout increment consumes the same identities;
demand remains monotonic, whole-program state stays restricted to emission and
lifecycle ordering, and lowering remains lookup-free.

## Current Failure Map

Current state is **195/269 PA16 tests**, up from the 185/269 audit baseline and
154/269 before the operator checkpoint; no baseline pass is lost, and PA1-PA15
remain 1,145/1,145. The complete 74-failure remainder, assigned once by primary
owner, is: 19 layout/bit-field/inheriting-constructor; 14 procedural cast and
metadata; 14 initialization/temporary-lifetime; 12 call/declarator metadata; 12
remaining lookup/access/inheritance forms; and 3 residual operator-shaped cases
whose primary blockers are temporary materialization, user-defined conversion,
or procedural conversion presentation.

## Active Checkpoint

**Next: unified layout, alignment, and bit-field closure.** This checkpoint
applies `spec.md` sections 2, 5, 6, 8, and 9 plus PA16's complete-layout,
`alignas`/`alignof`, anonymous-member, and signed/zero-width bit-field rules. The
class entity will own one monotonic layout fact containing natural/requested
alignment, direct-base/member offsets, and bit-field storage-unit descriptors;
resolved member actions will retain field IDs and bit slices for lookup-free
typed lowering. Data flow is `class members/direct base/alignment requests ->
canonical layout work item -> offsets and bit slices -> resolved member/init
actions -> typed LowIR`. Expected work is O(M + U) for members M and produced
storage units U, with one completion transition per class. Validation covers the
19-case layout/bit-field/inheriting-constructor group, weak-alignment rejection,
1x/2x member/unit counters, PA16, through-PA15, and file audit.

## Performance Evidence

| Boundary | Representative 1x / 2x evidence |
|---|---|
| Field/layout and use lookup | 5k/10k fields: 5,000/10,000 visits; 5k/10k uses: 5,003/10,003 probes; semantic and lowering times scale proportionally |
| Member/constructor overloads | 1,001/2,001 candidates: 2,006/4,006 conversion checks for members and 1,004/2,004 for constructors; emitted instructions remain constant |
| Aggregate/constructor actions | 1k/2k members: 1,000/2,000 owned actions and proportional semantic/lowered nodes; bounded nested retention removes the former depth-by-leaf product |
| Base construction/lookup | 250/500 edges: 250/500 base actions, 500/1,000 lookup-edge visits, 2.11/4.58 ms semantic and 1.71/3.27 ms lowering medians |
| Cleanup/destruction | 100/200 elements/members: proportional action visits and shared suffixes; 1,505/3,005 array and 1,015/2,015 destructor instructions |
| Namespace lifetime | 1k/2k objects: 1,000/2,000 actions, 4,016/8,016 instructions, 2,003/4,003 probes; one ordered cross-TU init/fini pair |
| Operator/ADL candidates | Dense 128/256 associated classes: 128/256 scope visits and 258/514 candidates; sparse 512/1,024 unrelated hidden friends: 1/1 scope and declaration visits, 2/2 candidates, 1,028/2,052 instructions, 7.348/14.762 ms semantic and 4.436/8.734 ms lowering medians |
| Friend/access grants | 50/100 friend classes and demanded accesses: 51/101 candidates, 153/303 conversions, 510/1,010 instructions, 121,759/240,799 typed bytes; 1.204/2.320 ms semantic and 0.551/0.961 ms lowering medians |

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
| Operator/ADL callable spine | Pass after audit fixes | Typed operator/null/UDL facts, direct ordinary/hidden-friend indexes, mixed ranking and built-in fallback; 186/269 (+32 from pre-checkpoint, no losses); 1,145/1,145 through PA15; dense-linear and sparse-constant candidate evidence; gates clean |
| Access/base-path closure | Complete | Canonical enclosing/grant/access-owner facts, qualified-friend ownership, nested classes/aliases, and using re-exposure with recorded projections; 195/269 (+9, no losses); 1,145/1,145 through PA15; linear 1x/2x grant probe; gates clean |

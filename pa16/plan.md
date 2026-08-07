# PA16 Plan

## Stage Design and Spec Alignment

PA16 extends the shared compiler in place. PA10 retains scoped syntax/name
classifications; PA11 owns canonical entities, bindings, types, storage, and ABI
identity; PA12 owns lookup, access, conversions, initialization, lifetime, and
demand; PA15/PA16 lowering consumes typed facts and plans storage without
repeating semantic lookup. This follows `spec.md` sections 2, 3, 5, 6, 8, and
9 and the PA16 object-model contract.

Qualified `decltype`, special-member definitions, mutable/static-member facts,
user-defined conversions, destructor variants, unevaluated demand, and
declaration-only TLS storage cross boundaries as compact canonical records.
Lookup and demand remain indexed; constructor/destructor work follows the
single-base/member graph; slot allocation is a bounded preorder; lowering is
O(nodes + emitted instructions), without whole-program retry or textual type
reconstruction.

## Current Failure Map

No current failures. PA16 is **291/291**, PA1-PA15 are **1,145/1,145**, and the
PA16 file audit passes. The turn-start groups are closed at their owning
boundaries: constructor access/demand (1), member/name selection (2),
declarator/qualified type use (4), cv/static storage/lifetime (6), and
conversion constraints (2).

## Active Checkpoint

**Complete: PA16 full-stage closure.** Validation is the required PA16 report,
the through-PA15 report, and the PA16 `dev/src` audit. The completed flow is
`PA10 expression/declaration shape -> PA12 canonical member, conversion,
storage, demand, and lifetime facts -> PA15/PA16 typed call/address/lifetime
lowering`. No later-assignment behavior was introduced.

## Performance Evidence

| Boundary | Representative 1x / 2x evidence |
|---|---|
| Friend/ADL converting calls | 32/64 overloads: 64/128 candidates, 162/322 conversions, 31/63 cache hits, 32/64 misses; 1.017/1.980 ms semantic medians |
| Physical single-base projection | 64/128 edges: 67/131 layouts, 394/778 path visits, one projection at both sizes; 0.701/1.317 ms semantic medians |
| Scoped declarator/type use | 1x/2x classes: 49/88 syntax nodes, 6/12 access checks, 4/8 instructions; 0.031/0.048 ms parse and 0.090/0.143 ms semantic medians |
| Cv/member/lifetime closure | 64/128 mutable-member updates plus fixed TLS/explicit-dtor demand: 551/1,063 semantic nodes, 71/135 conversions, 269/525 access checks, 385/769 path visits, 470/918 instructions, 69,123/130,563 typed bytes; 0.509/0.842 ms semantic and 0.311/0.439 ms lowering medians |

The final probe keeps fixed TLS/destructor demand constant while doubled member
work scales 1.90-2.00x in deterministic counters; median semantic and lowering
times remain sublinear relative to the doubled variable work.

## Completed Checkpoints

| Checkpoint | Closure evidence |
|---|---|
| Direct-member object spine | Canonical layout/projection; 42/247; linear field/use curves |
| Resolved member-call spine | Object-aware ranking, hidden `this`, stable ABI IDs; 51/247 |
| Local aggregate-action spine | C++11 aggregate/union rules and bounded projections; 61/248 |
| Special-member initialization spine | Ordered typed init actions and exception/reference facts; 91/255 |
| Single-base construction spine | Canonical base/access edges and retained projections; 120/259 |
| Destruction and lexical cleanup | Reverse lifetime, lexical exits, shared cleanup suffixes; 132/265 |
| Namespace/static lifetime | TLS/linkage facts and one ordered lifecycle pair; 154/269 |
| Operator/ADL callable spine | Indexed ordinary/hidden-friend union and typed ranking; 186/269 |
| Access/base-path closure | Indexed grants/signatures and object-correct protected access; 202/275 |
| Layout/bit-field/inherited-ctor closure | Focused 29/29; 231/283; prior and audit pass |
| Typed declarator/call/boundary closure | Focused 15/15; 249/286; prior and audit pass |
| Aggregate/value-init/materialization closure | Landed gains plus audit regressions; 262/288; proportional probes |
| Friend/ADL call-boundary closure | Focused 11/11; 273/290; complete-key cache and proportional probe |
| Physical single-base layout/projection closure | Focused 7/7; 276/291; proportional probes |
| Scoped declarator/type-use closure | Focused 7/7; 280/291 from 276/291 without regressions; proportional probe |
| Cv/member/lifetime and full-stage closure | Mutable cv, indexed-reference lowering, on-demand TLS, explicit/pseudo destruction, parser selection, converting construction, narrowing, and unevaluated demand; **291/291** from **280/291**; prior **1,145/1,145**; audit and proportional probe pass |

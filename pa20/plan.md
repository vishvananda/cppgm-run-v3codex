# PA20 Full-Stage Plan

## Stage Design and Spec Alignment

PA20 extends the retained PA19 template graph at semantic analysis; PA10 parses
syntax once and successful programs still lower typed PA12 facts directly to
LowIR.  `spec.md` requires canonical constants and specialization arguments,
O(1)-average cache lookup, immutable parent-linked template environments,
separate demand states, and no lowering-time reconstruction from source text.
The stage therefore shares one typed constant path across assertions and value
arguments, uses tagged parameter/argument records, and selects canonical
specializations before demand-driven replay.  PA21 constexpr-function
evaluation and PA22/PA23 SFINAE remain out of scope.

## Current Failure Map

The stage is 125/164, up from the checkpoint baseline of 120/164.  Canonical
specialization presentation, selective static-member definition demand,
reference-bound nested storage, and integral/enum non-type vtable identity now
pass.  The 39 remaining failures group into literal/operator decoding and
casts; variable-template, qualified-name, and declaration ambiguity; one
invalid pack acceptance and one extra array decay; explicit specialization
(`300-*`); and stale-primary refresh (`400-*`).

## Active Checkpoint

Build a typed literal boundary covering ordinary multi-character/wide literals
and cooked user-defined integer literal dispatch.  `spec.md` requires source
bytes to become canonical typed constants once, overload results to retain the
selected declaration/conversions, and lowering to consume those facts without
re-decoding token text.

PA10 owns retained literal-token syntax, PA12 owns bounded decoding and typed
constant facts, PA20 owns literal-operator lookup/template selection, and
lowering consumes the selected call or canonical immediate.  Data flows from
one token slice through one decoder into indexed overload resolution.  Expected
work is O(token bytes plus viable literal-operator candidates), with each token
decoded once and O(1)-average name/cache lookup.  Validate ordinary
multi-character and wide literals, cooked non-template/overload/template
operators, unchanged built-in integer suffixes, and rejection of malformed
literals.

## Performance Evidence

Seven-run release medians for 64/128/256 macro-expanded assertions: tokens
1,098/2,186/4,362; semantic nodes 453/901/1,797; edges 388/772/1,540;
conversion checks 769/1,537/3,073; peak semantic-stage bytes
147,772/279,396/542,605; semantic time 0.704/1.303/2.526 ms.  Lowered nodes stay
at three and typed LowIR storage at 1,735 bytes.  Work and storage track the
assertion expression count without retry or output growth.

Five-run release medians for 32/64/128 distinct integral function
specializations, each requested twice: semantic time 2.558/4.770/9.408 ms;
semantic nodes 452/900/1,796; peak semantic-stage bytes
317,536/627,968/1,248,888; typed LowIR bytes 52,562/104,402/208,082.
Requests were 128/256/512 with 96/192/384 cache hits, while emitted functions
were 33/65/129.  Time, storage, and output scale linearly; repeated keys hit the
cache and do not duplicate emitted specializations.

Seven-run release medians for 32/64/128-element type/function packs relayed
through `sink(values...)`: semantic nodes 148/276/532; conversion checks
71/135/263; instructions 110/206/398; peak semantic-stage bytes
90,225/157,649/285,842; typed LowIR bytes 39,232/73,856/143,104; semantic time
0.444/0.580/1.050 ms.  Two canonical specializations and two emissions remain
constant while work, storage, and output grow linearly with pack length.

Seven-run release medians for nested lockstep
`sink(identity<T>(values)...)` at 16/32/64 elements: semantic nodes
121/217/409; conversion checks 56/104/200; instructions 81/145/273; peak
semantic-stage bytes 125,698/212,732/403,500; typed LowIR bytes
24,038/42,406/79,142; semantic time 0.673/0.957/1.538 ms.  Doubling the pack
approximately doubles semantic work and output, with no Cartesian expansion.

Seven-run release medians for two independently deduced packs expanded in
lockstep at 16/32/64 elements: semantic nodes 107/187/347; conversion checks
38/70/134; specialization requests 22/38/70 with 18/34/66 cache hits;
instructions 61/109/205; peak semantic-stage bytes
156,781/259,181/499,309; typed LowIR bytes 21,733/36,181/65,077; semantic time
0.854/1.191/1.938 ms.  Argument partitions, cache work, storage, and emitted
output grow linearly with produced elements.

Eleven-run release medians for 16/32/64 nonempty value-pack bases with
lockstep initializers: semantic nodes 324/628/1,236; layout visits and base
actions 16/32/64; specialization requests 65/129/257 with 32/64/128 hits;
instructions 176/336/656; peak semantic bytes
586,517/1,163,966/2,298,040; typed LowIR bytes
92,174/181,567/360,635; semantic time 5.885/11.381/21.673 ms.  Direct lexical
identity overlays reduced inherited-edge scans from 512/2,048/8,192 to zero;
work, storage, demand, and output now scale linearly with produced base edges.

Seven-run release medians for 16/32/64 distinct dependent-parameter/default and
`alignas` specializations: tokens 202/346/634; semantic nodes 213/421/837;
lookups 471/935/1,863; member visits 16/32/64; specialization requests
32/64/128; peak semantic bytes 356,362/707,930/1,411,066; semantic time
1.821/5.384/6.423 ms.  Deterministic work and storage scale linearly; timings at
this sub-7-ms size were noisy but showed no corresponding retry/work growth.

Seven-run release medians for 16/32/64 nested dependent bool helper packs:
tokens 194/226/290; semantic nodes 53/85/149; lookups 273/433/753;
specialization requests 23/39/71 with 17/33/65 hits; peak semantic bytes
183,740/267,509/477,781; semantic time 1.111/1.535/2.283 ms.  Incremental work,
storage, and time scale linearly with pack length; nested discovery does not
form a Cartesian expansion.

Seven-run release medians for 16/32/64 object-demanded static definitions plus
the same number of alias-only specializations: tokens 332/604/1,148; semantic
nodes 247/487/967; lookups 716/1,420/2,828; specialization requests
48/96/192; demand pushes 32/64/128; emitted globals 32/64/128; peak semantic
bytes 546,536/1,091,104/2,176,418; semantic time 2.354/4.614/8.990 ms.  Each
demanded object produces its object and static-member globals; alias-only
specializations produce none.  Work, storage, queue traffic, and output scale
linearly.

## Completed Checkpoints

| Checkpoint | Result |
|---|---|
| Typed integral constants and `static_assert` | Width/signedness-aware casts, folds, literals, declaration/class/block validation, and `wchar_t`/`char16_t`/`char32_t` lowering; PA20 15 -> 39, PA1-PA19 2,013/2,013, audit pass |
| Canonical fixed-arity integral template arguments | Tagged type/value slots and keys, normalized defaults/expressions/enums, constant substitution, retained member replay, canonical LowIR identity and ABI value mangling; PA20 39 -> 83, focused 14/14, scaling linear |
| Canonical trailing type/function packs | Ordered scope-indexed pack ranges, flattened canonical keys with pattern-owned boundaries, variable-width replay offsets, empty/fixed-prefix deduction, named forwarding, `sizeof...` and simple `sizeof(Ts)...` expansion; PA20 83 -> 96, PA1-PA19 2,013/2,013, audit pass, scaling linear |
| Lockstep expression and initializer expansion | Recursive pack discovery, equal-length element scopes, nested call/functional/braced/member expansion, empty named function packs, and out-of-class owner-pack replay; PA20 96 -> 103, PA1-PA19 2,013/2,013, audit pass, nested scaling linear |
| Canonical multiple type/value pack ranges | Per-parameter offsets in specialization keys/replay, symbolic integral shape arguments, nested class-template pack deduction, partial explicit value arguments, and ADL over variable-width class arguments; PA20 103 -> 106, PA1-PA19 2,013/2,013, audit pass, two-pack scaling linear |
| Ordered base-pack edges and initializers | Compact ordered base identities/offsets, variable-width lookup/layout, type/value base expansion, lexical element overlays, lockstep initializer actions, and offset-driven lowering; PA20 106 -> 110, PA1-PA19 2,013/2,013, audit pass, base-pack scaling linear |
| Typed dependent declaration/value boundaries | Retained `decltype` carrier/member syntax, semantic `alignas` disambiguation, dependent `typename` non-type parameter declarations/defaults, and bounded type/expression template-argument ambiguity; PA20 110 -> 116, PA1-PA19 2,013/2,013, audit pass, dependent-specialization work/storage linear |
| Canonical dependent helper values and nested packs | Restricted structural constexpr conversion, structured `alignas` lookup, recursive lockstep type-pack scopes, and non-empty value-initialized argument staging; PA20 116 -> 120, PA1-PA19 2,013/2,013, audit pass, helper-pack scaling linear |
| Canonical specialized owners and static-definition demand | Type-ID-independent argument presentation, separate retained static definitions, deduplicated object/function/vtable and reference/address demand, and canonical lowering identity; PA20 120 -> 125, PA1-PA19 2,013/2,013, audit pass, demand/output scaling linear |

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

The stage is 96/164, up from the 83/164 turn baseline.  Remaining `200-*`
failures divide into lockstep expression/member/base/initializer expansion,
value packs, out-of-class owner packs, dependent `decltype` defaults, and
array/explicit-function-id parsing; the unrelated user-defined-literal cases
remain with literal lookup.  The `100-*` group is dependent qualified lookup,
delayed member validation/demand, variable templates, declaration ambiguity,
and literal/cast forms.  Explicit specialization owns `300-*`; stale-primary
refresh owns `400-*`.  LowIR-only mismatches in those groups are replay/global,
vtable, or specialization-selection ownership rather than pack collection.

## Active Checkpoint

Extend the typed pack environment from single-pack call sites to lockstep
expression, member/base, and initializer expansion, then add value-pack
elements.  Multiple packs at one site must have equal length rather than form a
Cartesian product; empty expansions remain valid.

Owner/data flow remains PA10 ellipsis syntax -> PA12 typed expansion recipe ->
PA19 canonical pack ranges/substitution scope -> class/function replay -> typed
lowering.  Each site is O(syntax + packs + expanded elements), specialization
lookup remains O(1)-average, and replay occurs once per canonical demand.
Validate same-pack and independent lockstep calls, base/member initialization,
empty packs, value/type mixtures and malformed unequal packs before full gates.

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

## Completed Checkpoints

| Checkpoint | Result |
|---|---|
| Typed integral constants and `static_assert` | Width/signedness-aware casts, folds, literals, declaration/class/block validation, and `wchar_t`/`char16_t`/`char32_t` lowering; PA20 15 -> 39, PA1-PA19 2,013/2,013, audit pass |
| Canonical fixed-arity integral template arguments | Tagged type/value slots and keys, normalized defaults/expressions/enums, constant substitution, retained member replay, canonical LowIR identity and ABI value mangling; PA20 39 -> 83, focused 14/14, scaling linear |
| Canonical trailing type/function packs | Ordered scope-indexed pack ranges, flattened canonical keys with pattern-owned boundaries, variable-width replay offsets, empty/fixed-prefix deduction, named forwarding, `sizeof...` and simple `sizeof(Ts)...` expansion; PA20 83 -> 96, PA1-PA19 2,013/2,013, audit pass, scaling linear |

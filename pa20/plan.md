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

The stage is 83/164, up from the 15/164 turn baseline and 39/164 prior
checkpoint.  The largest remaining owner is parameter-pack collection and
expansion (`200-*`): parser-retained expansion sites need one pack environment
and lockstep replay.  Explicit specialization and stale-primary refresh own the
`300-*`/`400-*` group.  The remaining fixed-arity failures cluster around
dependent qualified lookup, delayed member validation/demand, variable
templates, declaration ambiguity, and a few literal/cast forms.  Several of
these compile but differ in replayed globals, vtables, or specialization choice.

## Active Checkpoint

Add typed type/value parameter-pack collection, `sizeof...`, and supported
pack-expansion replay.  The canonical argument environment must represent a
pack as an ordered range without flattening ownership; each expansion consumes
that range in source order, and multiple packs at one site must have equal
length rather than forming a Cartesian product.  Empty packs remain valid.

Owner/data flow: PA10 pack/ellipsis syntax -> PA12 typed parameter and expansion
facts -> PA19 canonical pack bindings/substitution scope -> existing class or
function replay -> typed lowering.  Collection and replay must be O(parameters
+ expanded arguments), with O(1)-average specialization lookup and one demand
per canonical specialization.  Validate `sizeof...`, empty and fixed-prefix
packs, explicit type/value packs, lockstep expansions, malformed mixed packs,
then the full PA20 report, PA1-PA19 gate, audit, and 1x/2x/4x expansion probes.

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

## Completed Checkpoints

| Checkpoint | Result |
|---|---|
| Typed integral constants and `static_assert` | Width/signedness-aware casts, folds, literals, declaration/class/block validation, and `wchar_t`/`char16_t`/`char32_t` lowering; PA20 15 -> 39, PA1-PA19 2,013/2,013, audit pass |
| Canonical fixed-arity integral template arguments | Tagged type/value slots and keys, normalized defaults/expressions/enums, constant substitution, retained member replay, canonical LowIR identity and ABI value mangling; PA20 39 -> 83, focused 14/14, scaling linear |

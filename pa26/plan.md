# PA26 Plan

## Stage Design and Spec Alignment

PA26 remains a monotonic extension of the shared PA25 pipeline. PA12 owns
canonical types, declarations, overload decisions, template deductions,
lifetimes, and demand; PA15 consumes that typed graph directly. Initializer-list
conversion facts now separate list-category preference from element conversion
rank, retain the selected whole-list source, and publish ordinary class-value or
aggregate backing recipes. This preserves the `spec.md` sections 2-6 and 8-10
boundaries: canonical identity, explicit ownership, precise demand, direct typed
lowering, and bounded observable work.

## Current Failure Map

Current result is **63/110**, up from **56/110** at this checkpoint's start.

| Owner | Failing | Shared behavior |
|---|---:|---|
| EH and lifetime lowering | 39 | throws/handlers, unwind snapshots, branch/full-expression cleanup, construction failure |
| initializer-list and aggregate lifetime | 4 | namespace backing duration, element frontier, temporary ownership, direct aggregate destination |
| lambda/RTTI presentation integration | 2 | closure ABI spelling in RTTI name objects |
| template emission ordering | 1 | stable anonymous ordering across member-template specializations |
| object construction lowering | 1 | nontrivial copy in the cv/reference `typeid` fixture |

## Active Checkpoint

Complete initializer-list backing ownership and adjacent aggregate lifecycle.
PA12 storage/lifetime analysis owns namespace promotion, the constructed-element
frontier, and the backing array's destruction obligation; PA15 must consume
those facts without recovering syntax, initialize directly into the final
destination, and emit reverse cleanup for exactly the constructed prefix. Data
flows from the selected list conversion to the temporary/namespace array recipe,
then to slot/global planning and EH lowering. Expected work is O(elements), with
one frontier transition per nontrivial element. Validate the four remaining
list/aggregate cases, measure nontrivial-element scaling, then run the PA26
report, through-PA25 report, and file audit.

## Performance Evidence

Release compiler, three runs per point; timings/RSS are medians. A generated
`initializer_list<item>` call with nontrivial class elements exercises the
completed conversion, demand, backing-array, and lowering path:

| Elements | Conversions | Semantic nodes | Instructions | Semantic | Lowering | Typed storage | RSS |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 64 | 643 | 275 | 141 | 1.096 ms | 0.243 ms | 44,192 B | 6,660 KiB |
| 128 | 1,283 | 531 | 269 | 1.807 ms | 0.293 ms | 83,360 B | 6,936 KiB |
| 256 | 2,563 | 1,043 | 525 | 3.039 ms | 0.401 ms | 161,696 B | 7,172 KiB |
| 512 | 5,123 | 2,067 | 1,037 | 5.603 ms | 0.581 ms | 318,368 B | 7,732 KiB |

From 256 to 512, conversion work doubles, node/instruction/storage counts grow
1.98/1.98/1.97x, and semantic/lowering time grows 1.84/1.45x. Earlier scalar
list and lambda-capture measurements were likewise linear through 512 items.

## Completed Checkpoints

| Checkpoint | Result | Validation |
|---|---|---|
| Canonical RTTI demand and query lowering | Canonical query/cast identity and ABI RTTI | RTTI 14/17; PA26 30/110; through PA25 3,607/3,607; audit pass |
| Lambda capture ownership | Explicit/default captures, closure members, projected access, cycle-safe identity | 12 new passes; PA26 42/110; timeout removed; through PA25 3,607/3,607; audit pass |
| Scalar initializer-list interoperation | Canonical specialization, two-phase construction, scalar backing, references, `auto`, range-for | 14 new passes; PA26 56/110; through PA25 3,607/3,607; audit pass; linear to 512 |
| List overload and class-backing boundary | Separate list/element ranks, whole-list template deduction, selected source, typed class recipes and compact backing addresses | 7 new passes; PA26 63/110; focused 7/7; through PA25 3,607/3,607; audit pass; class scaling linear to 512 |

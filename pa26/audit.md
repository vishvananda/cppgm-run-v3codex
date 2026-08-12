# PA26 Audit

## Current Checkpoint Review

The construction and call-ABI checkpoint (`17f052a8`) passes its bounded
review. Its landed scope is the five construction witnesses: class-array
constructor failure, placement-new constructor unwind, aggregate class-member
construction, class-value parameter transfer, and conditional initialization
that throws before the destination exists. All five match their checked-in
LowIR oracles; aggregate member construction also executes successfully through
the LowIR-to-CY86 Linux ELF path. The remaining 16 PA26 failures belong to the
next mapped ownership paths.

The durable ownership path is source initializer/call/new expression -> PA12
canonical type, binding, selected constructor, and ordered lifetime actions ->
PA16/PA17 per-function cleanup state -> direct typed LowIR. PA12 publishes the
actual runtime initializer root and stages its exceptional suffix before the
destination lifetime is registered, so an unconstructed conditional
destination cannot enter its own unwind snapshot. Class arguments are fully
materialized before a potentially throwing outer constructor/call region is
opened; ownership then crosses the typed ABI boundary and the callee's existing
parameter obligation performs destruction. Aggregate leaves invoke their
selected constructor in the projected destination, while array-new cleanup uses
the constructed-count slot to destroy the prefix in reverse, deallocate once,
and continue through the active source try without over-closing its region.

All keys and cross-phase facts on this path are compact node, type, binding,
scope, block, or slot identities. Traversals are bounded by initializer edges,
arguments, scopes, and emitted cleanup actions; lowering state is reset at the
function boundary. The audit found no rendered-name recovery, source/test
dispatch, whole-program retry, external compiler/reference call, or broadened
cache owner in the increment.

Five-run current-binary measurements use a declared destructible class copied
into 8/32/128 value parameters and class-array `new` bounds 8/128/2,048. The
argument cases record 25/73/265 semantic nodes, 18/66/258 temporary-dependency
visits, 27/99/387 instructions, and 9,955/29,443/107,395 typed bytes; median
semantic/lowering times are 0.317/0.211, 0.497/0.328, and 1.091/0.865 ms. Every
array bound records 14 nodes, 5 visits, 2 nonthrowing-action visits, 35
instructions, and 10,881 typed bytes, with medians within 0.195-0.202 ms
semantic and 0.160-0.173 ms lowering. This corrects the prior unlabeled zero in
the array evidence while preserving the supported conclusion: argument work is
linear and array-bound construction is represented by one fixed-size runtime
loop.

Final validation preserves both baselines: the focused checkpoint is 5/5,
PA1-PA25 pass 3,607/3,607, and PA26 remains 94/110 with the same 16 failures and
no timeout. The required PA26 report therefore matches the 94/110 audit-turn
baseline and stays above the 90/110 implementation-checkpoint baseline. The
file audit passes with the same 19 inherited division warnings. No relevant
spec, correctness, performance, shortcut, timeout, ownership, or file-audit
issue remains in this landed increment.

## Checkpoint Audit Ledger

| Checkpoint | Audit result |
|---|---|
| Canonical RTTI demand and query lowering (`9eb277da`, audit follow-up) | Pass: evaluated demand, reachable collection, complete canonical RTTI categories, cast legality, linear counters, baseline and earlier stages preserved. |
| Lexical unwind snapshots and handler continuation (`e05062b1`, audit follow-up) | Pass: complete bounded owners, ordered handler exit, non-duplicated typed suffixes, linear counters, and both baselines preserved. |
| Class exception objects and typed-handler routing (`336f0c80`, audit follow-up) | Pass: canonical special-member facts, direct typed construction/destructor transfer, projection-safe call ABI, linear evidence, and both baselines preserved. |
| Guard-edge full-expression cleanup (`e4d47678`, audit follow-up) | Pass: typed logical identity, complete owner/child indexing, path-local retirement with bounded runtime fallback, linear evidence, and both baselines preserved. |
| Construction and call-ABI ownership (`17f052a8`, audit follow-up) | Pass: typed runtime roots, pre-lifetime unwind staging, transferred parameter ownership, partial-array routing, corrected scaling evidence, and both baselines preserved. |

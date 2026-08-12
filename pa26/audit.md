# PA26 Audit

## Current Checkpoint Review

The nested call and full-expression lifetime checkpoint (`0c9fb54b`) passes its
bounded review after one audit fix. Its landed scope is the seven recovered
guarded-static, condition-call, const-reference, nested/default-argument,
reference-prvalue, and shared-dispatch witnesses. All seven match their
checked-in LowIR oracles. Five self-contained cases also return zero through the
LowIR-to-CY86 Linux ELF path. The remaining nine PA26 failures are unchanged and
belong to the next mapped ownership paths.

The durable ownership path is source initializer and nested call -> PA12
canonical selected-call, default-subtree, object-lifetime, and ordered cleanup
facts -> PA16 retained destinations and temporary slots -> PA17 per-function
cleanup segments/dispatch -> PA21 guarded-static edge -> direct typed LowIR.
Nested default arguments retain subtree identity, potentially throwing regions
become active only after their owned temporary exists, and normal cleanup
closes only a segment that was opened. Local-static initializer cleanup remains
on the initialization edge, while selected constructors, destructors, and
special-member helpers cross phases by binding and node identity.

The audit found that the new `enclosing_lifetime_cleanup` fact was originally
computed by walking lexical parents for every eligible initializer. A family
with one outer destructible object and 32/128/512 nested scalar initializers
would therefore imply 562/8,386/131,842 parent probes. The fix stores a
cumulative nontrivial-object count on each scope and answers the query, including
a `try` stop, with one or two indexed prefix reads. Current counters report
33/129/513 queries, 140/524/2,060 semantic nodes, 65/257/1,025 temporary visits,
37/133/517 instructions, and 14,112/47,808/182,592 typed bytes. Five-run median
semantic/lowering times are 0.562/0.185, 1.526/0.297, and 5.754/0.915 ms.

All identities on the path are compact node, type, binding, scope, block, or
slot values; the lifetime prefix is phase-local with one compact entry per scope.
Lowering state is reclaimed at each function. The audit found no rendered-name
semantic key, lookup recovery, source/test dispatch, whole-program retry,
external compiler/reference call, or broadened cache owner.

Final validation preserves both baselines: the focused checkpoint passes 7/7,
PA1-PA25 pass 3,607/3,607, and PA26 remains 101/110 with the identical nine
failures and no timeout. The required file audit passes with the same 19
inherited division warnings. No relevant spec, correctness, performance,
shortcut, timeout, ownership, or file-audit issue remains in this landed
increment.

## Checkpoint Audit Ledger

| Checkpoint | Audit result |
|---|---|
| Canonical RTTI demand and query lowering (`9eb277da`, audit follow-up) | Pass: evaluated demand, reachable collection, complete canonical RTTI categories, cast legality, linear counters, baseline and earlier stages preserved. |
| Lexical unwind snapshots and handler continuation (`e05062b1`, audit follow-up) | Pass: complete bounded owners, ordered handler exit, non-duplicated typed suffixes, linear counters, and both baselines preserved. |
| Class exception objects and typed-handler routing (`336f0c80`, audit follow-up) | Pass: canonical special-member facts, direct typed construction/destructor transfer, projection-safe call ABI, linear evidence, and both baselines preserved. |
| Guard-edge full-expression cleanup (`e4d47678`, audit follow-up) | Pass: typed logical identity, complete owner/child indexing, path-local retirement with bounded runtime fallback, linear evidence, and both baselines preserved. |
| Construction and call-ABI ownership (`17f052a8`, audit follow-up) | Pass: typed runtime roots, pre-lifetime unwind staging, transferred parameter ownership, partial-array routing, corrected scaling evidence, and both baselines preserved. |
| Nested call and full-expression lifetime frontier (`0c9fb54b`, audit follow-up) | Pass: typed default/lifetime ownership, guarded-edge cleanup, O(1) scope-prefix queries, representative scaling, and both baselines preserved. |

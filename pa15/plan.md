# PA15 Implementation Plan

## Stage Design and Spec Alignment

PA15 reuses PA10 parsing and PA12 canonical names, types, bindings, overload choices,
conversions, and value categories. A synchronous semantic-graph view crosses into a
PA15-owned typed LowIR program; only the `--emit-lowir` tool serializes that model.
This aligns with `spec.md` §§2, 3, and 6 (stable semantic identity, retained overload
results, typed lowering without lookup or text round trips), §8 (explicit phase-local
ownership), and §9 (linear lowering with counters). PA14's typed ABI encoder owns
external object spellings; source-to-LowIR does not use the fact-text adapter.

## Current Failure Map

The required report is 27/108. Of the 81 remaining failures, 19 success-oracle
inputs fail while PA12 constructs the graph (default arguments, scoped/unscoped enum
rules, reference casts, array conditionals, labels, and void-return semantics). The
other 62 reach PA15 and are rejected at explicit typed-lowering boundaries shared by
structured/short-circuit control flow; addressable references, pointers, arrays, and
aggregate globals; or indirect/reference/variadic calls. Both compile-fail oracles pass.

## Active Checkpoint

**Addressable storage and lvalue boundary.** Extend the PA15 expression owner from
binding-indexed scalar slots to typed addresses for references, pointers, arrays,
subscripts, decay, and address-valued global constants. Preserve PA12's selected
bindings/value categories and emit `addr`/`load`/`store`/`index` without lookup or
reevaluation. This applies `spec.md` §§2 and 6; per-binding/per-expression address
facts remain average `O(1)`, and array initialization is `O(elements)`. Validate the
reference-alias, array-init/subscript, pointer arithmetic, global-address, and
evaluate-LHS-once clusters plus all checkpoint exit gates.

## Performance Evidence

A generated scalar function with 5,000 versus 10,000 assignments produced exactly
15,003 versus 30,003 instructions, 25,006 versus 50,006 lowered-node visits, and
10,002 versus 20,002 binding-index probes. Typed storage was 6,160,688 versus
12,321,072 bytes. Three-run elapsed ranges were 0.06s versus 0.13-0.14s; median
lowering time was 19.85ms versus 42.45ms. Work counts are linear; no repeated lookup
or whole-graph retry appeared.

## Completed Checkpoints

| Checkpoint | Result | Validation |
|---|---|---|
| Semantic handoff and scalar procedural spine | 0 -> 27/108 PA15 | focused scalar/ABI/global/call/control cases; through-PA14 1037/1037; linear 1x/2x counters |

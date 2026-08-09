# PA21 Final Audit Plan

## Stage Design and Spec Alignment

PA21 keeps one typed, ID-based path from source to LowIR:

`preprocessor -> compact syntax tokens/SyntaxArena -> Program + DumpArena -> typed constexpr and storage facts -> SemanticGraphView -> GraphLowerer -> LowIR`

Parser rollback uses an exact edge-mutation journal whose storage is released at the syntax boundary. Semantic identity is canonical `BindingId`/`TypeId`; scalar, object, address, call, static-initializer, and local-static facts remain owned by semantic analysis and are consumed synchronously by lowering. Objects and addresses are structurally interned, call facts have explicit in-progress/success/failure states, and constant evaluation uses a discardable scratch arena. Evaluation demand is separate from runtime emission demand.

Function-local statics are keyed by canonical function identity plus declaration ordinal. Their typed action carries object/type/initializer/destructor and constant-initialization state into global storage, optional guard CFG, and finalization lowering. No path scans source text, reparses rendered types, invokes another compiler, or reads tests/reference fixtures.

This aligns the available PA21 surfaces with `spec.md` §§1–6 and 8–10: compact canonical identity, indexed lookup, phase-owned facts, explicit demand/cycle state, typed lowering, bounded temporary ownership, deterministic ordinals, and observable work counters. `spec.md` §7 machine-code concerns remain outside PA21's LowIR endpoint. PA22 template extensions and richer thread-safe guard/destruction policy remain outside the assignment contract.

## Performance Evidence

- An ordinary runtime call `spin(1000000)` previously triggered 1,000,000 constexpr steps and 2,189,468,949 ns of semantic work. It now records zero constexpr requests/steps, 218,483 ns semantic time, 0.00 s elapsed, and 7,120 KiB peak RSS.
- Unique constexpr locals at 8,192/16,384/32,768 declarations take 26.865/59.090/114.561 ms semantic time, with exactly `N+1` name-index probes and `N+2` evaluator steps. The old scan path took 55.863/183.218/1,519.634 ms and became sharply superlinear.
- Local type aliases at 8,192/16,384/32,768 take 10.212/20.219/41.669 ms, with exactly `N` alias-index probes and `N+2` evaluator steps.
- Functions containing 1,024/2,048/4,096 local statics produce exactly N globals; semantic time is 7.562/13.497/28.899 ms and lowering time is 1.585/3.208/5.673 ms.

The counters identify no unexplained replay: local and alias probes, evaluator steps, globals, and lowering work scale with owned input/output structure.

## Architecture Review

- Representation and ownership: syntax, semantic facts, and LowIR each have one owning representation; later phases retain compact IDs or synchronous views, not source buffers or serialized IR.
- Graphs and lookup: dense scope-restored name/pack/alias heads replace hot vector scans; structural hash indexes verify full equality; recursive calls observe in-progress cache state.
- Phase boundaries: constant-required contexts request evaluation; ordinary runtime calls only create emission demand. Namespace and block-static initialization explicitly probe constant initialization.
- Typed lowering: resolved object/function addresses take precedence over scalar placeholder facts, and function-address dependencies demand the referenced specialization before emission.
- Determinism and self-containment: source ordinals and ABI identities drive local-static symbols; no filesystem/test/ref/compiler oracle is consulted by production code.
- Allocation and limits: parser rollback and constexpr scratch storage are released in bulk; iterative walks and explicit depth/step limits bound recursive language behavior.
- File division: conversion and simple-function declaration handling now have dedicated translation units and are listed in `frontend_source_sets.mk`.

## Final Architecture Review

No PA21 correctness, performance, self-containment, or file-audit blocker remains. The required file audit passes; its 13 warning-only header-division advisories are inherited template/CRTP organization notes and do not cross a fatal threshold. The supplied primary log records the original 2,322-test baseline; after adding seven audit regressions, the through-PA21 report passes 2,329/2,329 tests across 21/21 stages. The final audit is committed with a clean-worktree handoff.

## Checkpoint Ledger

| Stage commit(s) | Owned stage | Final audit disposition |
| --- | --- | --- |
| `dd3dd301`, `3f92499b` | Integral scalar invocation and evaluator ownership | Pass; demand separation and dense local indexes remove replay/scans |
| `267d7437` | Floating constants | Pass; typed scalar facts and call keys remain canonical |
| `22051550` | Aggregate/array objects | Pass; structural interning verifies full equality |
| `0e7c1ea7` | Constructors and member calls | Pass; literal-owner validation and C++11 implicit-const typing repaired |
| `f76ac972` | Constant addresses | Pass; kind/identity/offset/bounds facts remain typed and interned |
| `d4d44664`, `263efed0` | Class-valued calls | Pass; complete-object identity and escape checks preserved |
| `44134d03`, `5fa7f407` | Base subobjects | Pass; all direct bases participate in literal/object validation |
| `149f92db`, `f49edb9b` | Callable/contextual conversion | Pass; conversion declarations share constexpr suitability/inline rules |
| `49e62fbb`, `cc85a99d` | `noexcept` | Pass; constant-required evaluation and lifetime facts remain separated from emission |
| `9db9e273`, `23502678` | Qualified static constants | Pass; canonical recipes/dependencies and namespace constant initialization are owned |
| `290fab26` | Full PA21 stage | Pass after local-static identity, initialization, finalization, declaration, scaling, and file-division repairs |

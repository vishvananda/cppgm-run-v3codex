# PA21 Final Audit

## Findings

All blocking findings are resolved.

1. Ordinary runtime calls to `constexpr` functions were eagerly interpreted, so runtime work could become compile-time work. Evaluation is now gated by a constant-required or active constexpr context at scalar-call, constructor, list-initialization, conversion, and address boundaries.
2. Constexpr local declarations, lookups, packs, and type aliases used frame/block vector scans. Dense `NameId` heads with scope-restored predecessor links now make declaration and lookup proportional to actual facts visited.
3. Local-static identity depended on rescanning raw source text and could alias same-named declarations in one template function. Canonical function identity plus declaration ordinal now owns deterministic, distinct storage without source retention.
4. Constant-initialized class local statics were forced through zero/dynamic guard lowering, and recorded local destructors were dead. Typed constant-initialization facts now enable static class data; dynamic finalizers check guards, while statically initialized objects receive unconditional finalization in the supported PA21 model.
5. C++11 constexpr declaration validation was fragmented. Shared validation now covers literal variables, callable result/parameters, all direct bases and members, volatile subobjects, constructors, conversions, friends, template specializations, implicit inline status, virtual rejection, and the implicit `const` type of non-static constexpr members.
6. Ordinary namespace objects did not probe constexpr-call initializers for constant initialization. Static-storage definitions now enter the same constant-aware path without reintroducing evaluation for automatic runtime calls.
7. Static-initializer lowering could consume a scalar zero placeholder before a resolved typed address, and function-address dependencies did not always demand the referenced specialization. Address facts now take precedence and their dependency walk reaches the owning function binding.
8. The final file audit exposed an oversized declaration routine and two source files at/over fatal limits. Conversion and simple-function declarations now have focused translation units; all new files are registered in the compiler source set.

No production path shells out, consumes reference/test data, reconstructs types from text, or hardcodes a fixture. Checked-in PA21 LowIR fixtures were migrated only where canonical local-static identity and correct class constant initialization intentionally change the stage contract.

## Changes

- Added typed dense indexes and work counters for constexpr locals, packs, and type aliases, with balanced scope/frame release.
- Removed semantic ownership of source path/text and source-scanning local-static metadata.
- Added canonical per-function local-static ordinals, constant-initialization state, guarded dynamic finalization, and class static-data lowering.
- Centralized constexpr literal/callable/member validation and propagated constexpr/inline facts through ordinary, class, constructor, conversion, friend, function-template, specialization, and variable-template paths.
- Added constant-context gates so evaluation and emission demand remain independent, including namespace static initialization.
- Preserved typed object/function addresses through static-initializer lowering and made constant function-address dependencies demand their specialization.
- Split conversion and simple-function declaration implementations into dedicated source files to satisfy file-audit ownership limits.
- Added seven focused PA21 regressions for declaration identity, guarded destruction, namespace constant initialization, C++11 implicit-const members, nonliteral member owners, nonliteral secondary bases, and nonliteral constexpr variables.

## Performance Evidence

| Workload | Evidence |
| --- | --- |
| Runtime `spin(1000000)` call | Before: 1 request, 1,000,000 steps, 2,189.469 ms semantic. After: 0 requests, 0 steps, 0.218 ms semantic, 0.00 s elapsed, 7,120 KiB RSS. |
| Unique constexpr locals, N=8,192/16,384/32,768 | 26.865/59.090/114.561 ms; probes `N+1`; steps `N+2`; RSS 12,784/20,176/33,868 KiB. Previous 32,768 case was 1,519.634 ms. |
| Local aliases, N=8,192/16,384/32,768 | 10.212/20.219/41.669 ms; probes exactly N; steps `N+2`; RSS 8,860/12,940/21,728 KiB. |
| Local statics, N=1,024/2,048/4,096 | N globals; semantic 7.562/13.497/28.899 ms; lowering 1.585/3.208/5.673 ms; elapsed 0.02/0.04/0.07 s. |

The measured doubling behavior and exact counters confirm linear owned work. No residual hot path required profiler sampling after the counter-attributed scans and eager evaluator were removed.

## Validation

- `perl scripts/cppgm_file_audit.pl --stage pa21 --paths dev/src`: pass, with 13 warning-only header-division advisories.
- `make test-pa21`: pass, 129/129 handout tests and 15/15 course tests.
- `make test-report-through-pa21`: pass, 2,329/2,329 tests and 21/21 stages.
- `git diff --check`: pass.
- Final audit changes committed with a clean `git status --short` handoff.

## Checkpoint Ledger

| Checkpoint | Audit result | Final evidence |
| --- | --- | --- |
| `dd3dd301` / `3f92499b` scalar evaluator | Pass after repair | Runtime demand separated; local/alias indexes and explicit counters are linear |
| `267d7437` floating evaluator | Pass | Typed floating facts remain in scalar/call/storage paths |
| `22051550` object evaluator | Pass | Immutable structural objects, full collision equality, bounded projection walks |
| `0e7c1ea7` constructors/member calls | Pass after repair | Complete callable validation, literal owners, C++11 implicit const, static class initialization |
| `f76ac972` address evaluator | Pass | Interned canonical address kind/identity/offset/bounds; local escapes rejected |
| `d4d44664` / `263efed0` class-valued calls | Pass | Receiver/complete object/address included in invocation identity and result facts |
| `44134d03` / `5fa7f407` base completion | Pass after repair | Ordered base facts preserved; every direct base checked for literal ownership |
| `149f92db` / `f49edb9b` callables | Pass | Exact parser rollback, typed conversion functions, no semantic text roundtrip |
| `49e62fbb` / `cc85a99d` `noexcept` | Pass | Constant-required context and temporary-lifetime facts remain bounded and typed |
| `9db9e273` / `23502678` static constants | Pass after repair | Canonical recipes/dependencies plus namespace constant-initialization probing |
| `290fab26` full stage | Pass after repair | Local-static ownership/finalization, declaration validation, linear scaling, fatal-free file audit |

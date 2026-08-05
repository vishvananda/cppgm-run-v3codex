# PA4 Implementation Plan

## Stage Design and Spec Alignment

PA4 owns the `source bytes -> macro-expanded preprocessing-token events ->
post-token events` boundary.  One immutable source buffer flows through the
shared PA1 tokenizer into a logical-line directive adapter.  Non-directive
tokens enter one rescan deque; `#define`/`#undef` lines update a translation-unit
macro registry indexed by interned identifier ID.  Final expanded events feed
the reusable PA2 post-tokenization session directly, so textual preprocessing
output is never a transport format.

This applies `spec.md` sections 1, 2, 4, 8, 9, and 10: streaming phase
boundaries; interned identifier and macro identity; demand-only, per-invocation
argument prescan with cached results; compact token/paint IDs and explicit
translation-unit versus expansion ownership; average-O(1) macro lookup and
work proportional to source plus produced expansion; and no host/reference
fallback.  Scope lookup, declarations, templates, lowering, backend, and ELF
remain outside PA4 rather than being simulated early.

Ownership and data flow are: immutable bytes (entry point) -> borrowed PA1
events (PA1) -> one compact logical line (directive adapter) -> interned macro
definitions and a bounded pending rescan deque (PA4) -> borrowed expanded
events (PA2 session) -> textual tool view (entry point).  Macro definitions own
only normalized compact replacement tokens. Tokens carry an interned spelling
ID plus compact persistent temporary/permanent paint-trie roots. Argument
expansion facts live only for their invocation, and explicit expansion frames
avoid host-stack ownership at deep nesting.

## Current Failure Map

Turn-start baseline was 0/72 PA4 tests, with every case returning
`EXIT_NOT_IMPLEMENTED`. All groups are now closed at 72/72; PA1-PA3 remain
99/99 and the PA4 file audit passes.

| Owner | Complete failing group | Count |
| --- | --- | ---: |
| Stream/directive boundary | course `100`, `200-*`, `400`; local `100-*`, `150-max`, `200-*` | 10 |
| Directive grammar and registry | course invalid `250/300` definitions, redefinition, and unterminated forms; local `250-badvargs*`, `300-*`, `700-redef2/redeferr*` | 27 |
| Substitution operators | course `150`, valid `250`, paste/stringize `300/410/500`; local `250-join`, `500`, `700-*-a/q`, `800`, `850` | 23 |
| Rescan and token paint | course directive-from-expansion plus `600-*`; local `600`, `650`, `900`, `910`, `920` | 12 |

## Active Checkpoint

**CP1: complete PA4 macro-processing boundary — completed.** Directive parsing,
definition equivalence, object/function/variadic invocation, balanced raw
arguments, lazy cached argument prescan, stringizing, placemarkers, token-paste
retokenization, rescan, and course-defined unavailable paint share one registry
and rescan owner; PA2 owns only final token recognition.

Expected complexity is average `O(source tokens + produced expansion tokens +
pasted spelling bytes)`, apart from repeated output required by macro
expansion.  Macro lookup is average O(1), queue operations are amortized O(1),
balanced source groups carry precomputed close/comma facts, each demanded
argument is prescanned once and reused, and paint add/membership has a bounded
32-bit persistent-trie path. Explicit frames make nesting depth a heap-backed
worklist rather than C++ recursion. Validation covers the PA4 report,
through-PA3 report, file audit, sanitizer suite, generated scaling probes, and
the final repository audit.

## Performance Evidence

Release measurements after the frame/trie refactors:

| Workload | Scale | Wall time | Work/storage evidence |
| --- | ---: | ---: | --- |
| Mixed function, variadic, helper, and paste calls | 25k / 100k / 400k lines | 0.46 / 1.83 / 7.35 s | invocations 0.1 / 0.4 / 1.6M; prescans scale exactly 4x; peak rescan 16 tokens and paint trie 162 nodes |
| Nested ordinary argument demand | depth 1k / 4k / 16k / 64k | 0.01 / 0.03 / 0.13 / 0.51 s | invocations/prescans 1k / 4k / 16k / 64k; no host-stack recursion; RSS 7 / 16 / 52 / 198 MiB |
| Indirect paint cycle | 2.5k / 10k / 40k macros | 0.01 / 0.05 / 0.24 s | invocations 2.5k / 10k / 40k; trie nodes 160k / 640k / 2.56M; RSS 7 / 16 / 53 MiB |

The initial nested-argument path took 4.08 s and about 2.3 GiB at depth 4k
and crashed by depth 8k; range moves, precomputed grouping, and explicit frames
removed the quadratic copies and recursion. The initial full-vector paint sets
were also replaced before acceptance; current counters and time remain
proportional to source plus produced expansion work.

## Completed Checkpoints

| Checkpoint | Result | Evidence |
| --- | --- | --- |
| CP1: complete PA4 boundary | closed, 0/72 -> 72/72 | Interned streaming registry/rescan pipeline; directive, substitution, paste, variadic, paint, and deferred-helper groups pass; PA1-PA3 99/99; sanitizer and linear-scaling probes pass. |

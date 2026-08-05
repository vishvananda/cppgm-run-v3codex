# PA6 Recognition Stage

## Stage Design and Spec Alignment

PA6 adds one shared recognition boundary after PA5. Each immutable source buffer
flows through the existing preprocessor directly into a compact phase-7 token
cursor; identifiers are interned once and tokens retain only kind, identifier
ID, and the literal predicates required by PA6. A fixed grammar table drives a
memoized maximal recursive-descent recognizer. Mock name lookup is owned by the
name nonterminals, while one delimiter-depth pass assigns template opening and
closing roles before parsing. `recog` owns per-source preprocessing, token,
memo, and telemetry state and releases all of it after writing that source's
status.

This is the PA6-relevant implementation of `spec.md` §§1, 8, 9, and 10: there
is no textual token round trip or duplicate syntax/semantic tree; identifier
identity is compact; parser checkpoints are memoized by `(nonterminal,
position)` without cloning token storage; ownership ends at the source; and
required output remains in-process and self-contained. Semantic graph,
templates-as-semantics, lowering, backend, and ELF requirements are later-stage
surfaces and are not pulled into recognition.

## Current Failure Map

The turn-start failure set was one missing stage: all 47 tests exited
`EXIT_NOT_IMPLEMENTED`. The driver/token-boundary, core grammar, categorized
name, angle commitment, BAD-input, and ambiguity groups are now closed by CP1;
the current PA6 report is 47/47 with no open behavior group.

## Active Checkpoint

**CP1 — complete typed grammar recognition (closed).** Owner/data flow:
`PreprocessFile -> RecognitionTokenSink -> angle-role classifier -> Parser ->
OK/BAD`. Implement all PA6 productions through fixed read-only grammar
metadata, attach the handout's mock lookup and special-token predicates at the
matcher boundary, preserve per-file error isolation, and expose token, memo,
rule, and angle counters. With grammar size fixed, tokenization,
classification, and deterministic recognition are `O(source bytes + tokens)`;
token and compact memo ownership is `O(tokens)` with a fixed nonterminal
factor. Validation covers focused positive/BAD/angle cases, all PA6 tests,
through-PA5, file audit, sanitizers, and generated scaling workloads.

## Performance Evidence

Release `dev/recog`, `/dev/null` output, and `CPPGM_FRONTEND_STATS=1` produced:

| Workload | Final evidence |
|---|---|
| 1k/2k/4k/8k independent declarations | 101,047/202,047/404,047/808,047 rule evaluations; 11.2/20.6/44.4/88.3 ms; 6,704/8,976/13,864/23,528 KiB RSS |
| 1k/2k/4k/8k additive operands | 37,323/74,323/148,323/296,323 rule evaluations; 5.8/11.1/23.1/49.6 ms |
| valid template depth 64/128/256/512 | 6,676/13,204/26,260/52,372 rule evaluations; 1.7/2.2/4.1/7.2 ms |
| malformed template depth 64/128/256/512 | 6,633/13,161/26,217/52,329 rule evaluations; 1.6/2.5/4.8/8.4 ms; all rejected |
| 8k-declaration memo representation, before -> after compaction | 42,032 -> 23,604 KiB RSS and 103.1 -> 83.6 ms, with identical 4,752,396 memo keys and parser work |

Counts double with input size, including failed deep-template work; no retry or
Cartesian growth appears. Three 8k-declaration runs with telemetry off/on were
both 0.08 s wall and 23.5-23.6 MiB RSS; counters are optional and do not select
alternate recognition behavior.

## Completed Checkpoints

| Checkpoint | Result |
|---|---|
| CP1: complete typed grammar recognition | closed, 0/47 -> 47/47; prior 241/241, file audit 21 files, ASan/UBSan PA6 pass, linear scaling evidenced above |

# PA1 Implementation Plan

## Stage Design and Spec Alignment

PA1 owns the stable `immutable source bytes -> preprocessing-token event stream`
boundary.  `dev/src/pp_tokenizer.{h,cpp}` will own a pull-based UTF-8/phase-1/2
cursor and a maximal-munch phase-3 lexer; `dev/pptoken.cpp` will remain only the
stdin/debug-rendering adapter.  Data flows forward without a translated-source
or token vector: bounded cursor lookahead feeds one reusable token spelling to
`IPPTokenStream`.

This implements the PA1 phases 1-3 contract and the current-stage parts of
`spec.md` sections 1, 8, 9, and 10: immutable source ownership, streaming token
production, phase-local storage, self-contained behavior, and O(source bytes +
emitted token bytes) work.  Fixed Unicode tables and raw delimiters (at most 16
code points) bound lookups; buffers grow geometrically.  The optional stats
record source bytes, decoded code points, and emitted tokens for scaling audits.

## Current Failure Map

| Shared behavior and owner | Baseline failing | Current failing |
|---|---:|---:|
| UTF-8/BOM, trigraphs, UCNs, splicing — translation cursor | 11 | 0 |
| Whitespace/comments and termination — phase-3 lexer | 5 | 0 |
| Identifiers and pp-number maximal munch — classifiers | 8 | 0 |
| Quoted/raw/user-defined literals and errors — literal scanners | 19 | 0 |
| Operators, digraphs, and `<::` — punctuator scanner | 3 | 0 |
| Directive-position header names — lexer context | 4 | 0 |
| Empty/basic/real-world forward-flow integration — adapter + lexer | 3 | 0 |

Turn-start evidence is `0/53` from
`make test-report ACTIVE_TEST_REPORT_PAS='pa1'`; every case currently exits
`EXIT_NOT_IMPLEMENTED`.

## Active Checkpoint

**CP1 — complete source-to-preprocessing-token boundary (completed).** Bundle all phase
transformations and token families because raw literals, comments, directive
context, and maximal munch share cursor state and cannot form honest isolated
pipelines.  Validation: focused PA1 runs during implementation, then
`make test-pa1`, `make test-report-through-pa1`, the required active report,
and the PA1 file audit.  There is no residual PA1 checkpoint.

## Performance Evidence

The repeated 30-byte mixed-token line `alpha 123.4e+5 /* c */ R"(x)"` measured
0.31 s/7,192 KiB at 983,040 bytes, 1.22 s/7,384 KiB at 3,932,160 bytes, and
4.92 s/19,056 KiB at 15,728,640 bytes.  Each 4x input increase took 3.94x and
4.03x elapsed time; retained memory tracks the one immutable source buffer.
Translation lookahead, Unicode classification, punctuator matching, and raw
terminator search are bounded by fixed tables, four characters, or an
18-code-point pattern.  `PPTokenizationStats` exposes bytes, decoded code
points, and emitted tokens without changing tokenization behavior.

## Completed Checkpoints

| Checkpoint | Result | Validation |
|---|---|---|
| CP1: phases 1-3 streaming token boundary | 0/53 -> 53/53 | PA1 local, through-PA1, active report, audit, and 4x scaling series pass |

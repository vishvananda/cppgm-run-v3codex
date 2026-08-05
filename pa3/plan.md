# PA3 Implementation Plan

## Stage Design and Spec Alignment

PA3 owns the `preprocessing-token events -> controlling-expression result`
boundary.  The immutable source buffer flows once through the PA1 tokenizer,
then through a reusable PA2 post-token session.  A line adapter flushes only at
logical-line boundaries, and the PA3 owner retains compact typed tokens and
arena-indexed expression nodes only for the current line.  The parser records
operator, type, value, and child identities once; evaluation consumes those
facts with short-circuit demand and renders only the final result.

This applies `spec.md` sections 1, 2, 4, 8, 9, and 10 at the current stage:
streamed forward phase boundaries, one parse per source region, stable compact
node identity, explicit branch demand, geometric line-local storage, linear
token/node work, optional work/storage/time counters, and no external compiler
or textual round trip.  Scope lookup, templates, lowering, machine IR, and ELF
have no PA3 owner and remain outside this stage.

## Current Failure Map

The turn-start 0/20 failure set is now resolved at 20/20 with no open PA3
failure.  The complete set grouped by shared behavior and owner as follows:

| Group | Tests | Owning behavior | Status |
| --- | ---: | --- | --- |
| Typed primaries and `defined` | 5 | PA2 event reuse, integral promotion, identifier/keyword handling, mock definition query | closed |
| Grammar and operators | 9 | PA3 precedence parser, associativity, signed/unsigned conversions, typed node construction | closed |
| Demand and diagnostics | 6 | Short-circuit/conditional demand, static conditional type, checked arithmetic, line isolation and full consumption | closed |

## Active Checkpoint

CP1 and its final gate audit are complete; no PA3 implementation checkpoint
remains active.  The implemented owner exposes PA2 as a reusable streaming
session, collects compact current-line facts, builds 32-bit arena-indexed typed
nodes with a precedence parser, and evaluates demanded nodes iteratively.
Ownership remains source in `ctrlexpr`, transient spelling in PA1, one pending
literal sequence in PA2, and reused current-line vectors in PA3.  Observed work
matches the expected O(source bytes + tokens + nodes) time and O(max logical-
line tokens/nodes) storage bounds.

## Performance Evidence

A mixed precedence/conditional line repeated 25,000/100,000/400,000 times
(1.1/4.4/17.6 MB) produced exactly 0.5/2.0/8.0 million post-tokens,
0.3/1.2/4.8 million nodes, and 0.225/0.9/3.6 million demanded visits.  Measured
pipeline time was 0.192/0.764/3.048 seconds: successive 4x inputs took 3.97x
and 3.99x time, while peak PA3 line storage stayed at 1,360 bytes.

A single left-associative chain with 50,001/200,001/800,001 tokens took
0.018/0.074/0.296 seconds.  Token, node, visit, and storage counters grew
exactly with input; successive 4x time ratios were 4.11x and 4.01x.  Packing hot
records and using 32-bit node IDs reduced the 800,001-token peak from 95.13 MB
to 54.96 MB and measured time from 0.371 to 0.296 seconds.

## Completed Checkpoints

| Checkpoint | Result | Evidence |
| --- | --- | --- |
| CP1: complete controlling-expression boundary | closed, 0/20 -> 20/20 | PA3 20/20, prior 79/79, file audit 16/16, ASan/UBSan 20/20, mixed and long-line linear scaling |

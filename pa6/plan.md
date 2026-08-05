# PA6 Final Recognition Audit

## Stage Design and Spec Alignment

The PA6 data path is `immutable source string -> PreprocessFile -> streamed
IPostTokenStream callbacks -> RecognitionTokenSink -> angle-role classifier ->
memoized grammar recognizer -> per-file OK/BAD`. The sink owns one 8-byte token
array and one phase-local identifier table. It splits `>>`, records only the
literal predicates PA6 needs, and appends EOF while preprocessing is still
streaming. Stable non-angle delimiter-region IDs then assign template roles;
that scratch is released before the parser allocates its dense compact memo.
All translation-unit state dies after that source's status is written.

This is the PA6 surface of `spec.md` §§1, 8, 9, and 10. There is no token-text
round trip, owning post-token vector, AST-to-semantic copy, abandoned tree, or
external implementation. Tokens use compact kinds and identifier IDs; parser
results are memoized by `(nonterminal, token position)` with an explicit
in-progress state; the embedded grammar is bounded read-only compiler metadata.
Canonical semantics, real lookup/templates, lowering, backend, and ELF are not
available in PA6 and §§2–7 are therefore deferred, not approximated here.

Representative trace: in `TC<(T < 1) + (2 > 1)> value;`, PA5 emits compact
phase-7 facts, `TC` is interned and categorized class/template, the outer `< >`
share one region, both parenthesized comparisons occupy distinct child regions,
and only the outer pair receives angle roles. Grammar terminals consume those
facts directly; no source spelling is rendered or reparsed.

## Findings

- **F1, correctness/architecture — closed.** Numeric delimiter depths let an
  angle opened in one child delimiter consume `>` from a sibling, and globally
  committed `T <` inside an outer template argument. Valid nested relational
  arguments such as `TC<(T < 2)>` were rejected.
- **F2, observability — closed.** Token/rule/memo work was visible, but PA6-owned
  token, identifier, classifier-scratch, memo, and peak storage were not.
- No remaining correctness, ownership, retry, self-containment, timeout,
  file-audit, or scaling finding was found.

## Changes

- Replaced numeric delimiter depths with stable region identities and one
  reverse `has_later_close` summary. A nested template candidate is committed
  only when its exact child region owns a close; classification remains O(n)
  even for relational-heavy malformed inputs.
- Added a reference-generated regression covering parenthesized relational
  arguments, sibling delimiter regions, a lambda body, and a true nested
  template in the same outer argument.
- Added capacity-accounted stage storage telemetry and reset reusable stats at
  each recognition call. Telemetry still observes the same semantic path.

## Performance Evidence

Release `dev/recog`, `/dev/null`, `CPPGM_FRONTEND_STATS=1`:

| Workload | Rule evaluations | Time / memory evidence |
|---|---:|---|
| 1k/2k/4k/8k unique declarations | 101,047 / 202,047 / 404,047 / 808,047 | 12.2 / 21.4 / 40.3 / 81.8 ms; 7,520 / 8,996 / 13,912 / 23,332 KiB RSS; peak stage storage 2.47 / 4.95 / 9.89 / 19.78 MB |
| 1k/2k/4k/8k additive operands | 37,203 / 74,203 / 148,203 / 296,203 | 5.7 / 11.5 / 21.4 / 44.4 ms |
| valid template depth 128/256/512/1024 | 13,169 / 26,225 / 52,337 / 104,561 | 2.2 / 4.2 / 7.7 / 13.5 ms; all accepted |
| malformed depth 128/256/512/1024 | 13,092 / 26,148 / 52,260 / 104,484 | 2.2 / 3.7 / 6.9 / 14.5 ms; all rejected |
| 500/1k/2k/4k nested `T < 1` terms | 44,378 / 88,378 / 176,378 / 352,378 | 6.1 / 13.0 / 24.0 / 50.0 ms; classifier scratch 10,083 / 20,083 / 40,083 / 80,083 bytes; exactly one angle pair |

All work and owned storage double with semantic input. On three 8k-declaration
runs telemetry was 0.08–0.09 s and 23.36–23.56 MiB off versus 0.09–0.10 s and
23.56–23.60 MiB on. A callgrind profile of 1k declarations attributed 45.05%
of 53.9M instruction references to `Parser::ParseNode`; no textual fallback,
global retry, or unrelated subsystem appeared. A 32k declaration system run
used 367 ms task-clock; its 20,428 page faults are explained by the linear dense
memo allocation.

## Architecture Review

- **Representation/ownership:** one source buffer and one retained PA6 token
  vector; preprocessor line/rescan storage is transient, angle scratch dies
  before parsing, and token/identifier/memo ownership ends per source. No later
  pointer escapes and no text is reparsed.
- **Identity/lookup:** tokens carry 32-bit identifier IDs; spelling equality is
  isolated to interning and the two context-sensitive words. Mock name lookup
  is a packed flag test. Runtime string maps exist only while constructing the
  bounded process-global grammar table.
- **Templates/repeated work:** PA6 recognizes syntax only. Every reached
  `(rule, position)` is evaluated once, expected failure is a compact sentinel,
  and recursion observes an in-progress memo state. No semantic specialization
  or environment exists yet.
- **Allocation/scaling:** token and scratch vectors grow geometrically; parser
  nodes are fixed metadata and allocate nothing per visit; the dense 32-bit
  memo has a fixed 198-rule factor and exact byte telemetry. Work counters and
  valid/malformed scaling are linear.
- **Self-containment:** the implementation calls only the shared PA1–PA5
  in-process path and its own grammar recognizer. No host compiler, reference
  tool, subprocess, filename/text fixture branch, or cached answer is present.

## Final Architecture Review

**PASS.** The full PA6 ownership path now matches every applicable normative
requirement. The linear region classifier fixes the only discovered
whole-stage correctness issue without introducing suffix scans; the parser is
bounded, deterministic, observable, self-contained, and linear. No PA6 object
retains an earlier phase, no production representation is duplicated, and no
later-stage semantic/backend mechanism has been pulled forward.

## Validation

- Reference-generated focused regression: PASS.
- PA6 release suite: 48/48 PASS.
- PA6 ASan+UBSan suite: 48/48 PASS with leak detection.
- Multi-file `OK / missing / #error / OK` isolation: PASS, process exit 0.
- `perl scripts/cppgm_file_audit.pl --stage pa6 --paths dev/src`: PASS, 21
  files checked.
- `make test-report-through-pa6`: PASS, 289/289 tests and 6/6 stages.

## Checkpoint Ledger

| Checkpoint | Result |
|---|---|
| CP1: initial complete grammar recognizer | closed at 47/47 PA6 and 288/288 through PA6 |
| CP2: independent spec/architecture audit | closed; F1 region-context defect fixed across classifier and matcher input, F2 telemetry gap closed |
| CP3: final performance and validation | closed; linear valid/malformed evidence, callgrind profile, sanitizer pass, file audit pass, 48/48 PA6 and 289/289 through PA6 |

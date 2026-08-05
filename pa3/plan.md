# PA3 Final Audit Plan

## Stage Design and Spec Alignment

PA3 owns the `preprocessing-token events -> controlling-expression result`
boundary.  `ctrlexpr` retains one immutable source buffer and runs it once
through the shared PA1 tokenizer.  Borrowed token spellings flow directly into
a reusable PA2 post-tokenization session; the logical-line adapter flushes its
single pending string-literal sequence at each line boundary.  PA3 retains only
compact current-line value/operator/definition facts, builds typed nodes in a
vector arena with 32-bit IDs, and evaluates the resulting root with explicit
demand and reusable iterative frames.  Only the final value is rendered.

A representative `(1 + 2 * 3 < 8 && 4) ? (9u << 2) : (5 / 0)` therefore
flows as source bytes -> PA1 events -> 23 typed PA3 tokens -> 16 typed nodes ->
13 demanded node visits -> `36u`; the invalid false branch is represented for
static type computation but not evaluated.  No text is rendered and reparsed.

This is the PA3 application of `spec.md` sections 1, 2, 4, 8, 9, and 10:
forward streaming phase boundaries, one parse per logical line, compact stable
node identity, explicit branch demand, geometric line-local storage, linear
work with release telemetry, and self-contained output.  Identifiers are
borrowed only long enough to compute the PA3 definition fact; no identifier
spelling survives the callback.  Scope lookup, declarations, templates,
lowering, machine IR, and ELF have no owner in PA3, so checklist questions for
those later surfaces are not applicable rather than deferred inside this tool.

## Findings

| ID | Severity | Finding | Resolution |
| --- | --- | --- | --- |
| PA3-A1 | blocker | The evaluator was iterative, but nested parentheses and right-associated conditionals still recursed in the parser.  Valid inputs at depth 100,000 terminated with SIGSEGV. | Replaced recursive parsing with an explicit precedence/operator stack and operand stack. |
| PA3-A2 | architecture | Parser stack work and retained capacity were absent from observability and the reported PA3 storage peak. | Added parser operator/operand high-water counters and included both capacities in line-storage telemetry. |

No correctness, ownership, self-containment, timeout, file-audit, or remaining
performance finding is open.  A stricter host-C++ signed-left-shift hypothesis
was explicitly rejected during audit because the tracked `300-triple` oracle
requires PA3's two's-complement bit-pattern behavior; the implementation
performs that shift on `uint64_t`, so it does not invoke host signed-shift UB.

## Changes

- Replaced recursive primary/conditional/precedence descent with one iterative
  shunting-yard-style parser.  Unary and binary precedence, left associativity,
  right-associated `?:`, `defined` operands, and malformed-delimiter rejection
  now share one bounded-state owner.
- Reused geometric operator and operand vectors across logical lines and kept
  node construction arena-indexed; parser rejection remains a compact result,
  not exception control flow.
- Extended `CPPGM_FRONTEND_STATS` with `peak_parser_operators` and
  `peak_parser_operands`, and made `peak_line_storage_bytes` account for those
  vectors.

## Performance Evidence

Release telemetry was measured after the final parser refactor:

| Workload | Scale | Front-end time | Work counters | Peak PA3 storage |
| --- | ---: | ---: | --- | ---: |
| Mixed demanded conditional lines | 25k / 100k / 400k lines (1.075 / 4.3 / 17.2 MB) | 0.208 / 0.830 / 3.331 s | 0.575 / 2.3 / 9.2M tokens; 0.4 / 1.6 / 6.4M nodes; 0.325 / 1.3 / 5.2M visits | constant 1,280 B |
| Left-associative addition | 50,001 / 200,001 / 800,001 tokens | 0.020 / 0.080 / 0.338 s | tokens, nodes, and visits each grow exactly 4x | 3.93 / 15.73 / 62.91 MB |
| Nested parentheses | depth 25k / 100k / 400k | 0.020 / 0.082 / 0.326 s | one node/visit; operator high-water equals depth | 1.31 / 5.24 / 20.97 MB |
| Right-associated conditionals | depth 25k / 100k / 400k | 0.037 / 0.149 / 0.609 s | 75,001 / 300,001 / 1,200,001 nodes; only 3 demanded visits | 6.82 / 27.26 / 109.05 MB |

Successive 4x inputs took 3.98-4.21x time.  Work and retained storage counters
explain the scaling, and no retry, reparsing, or superlinear path remains.  The
two depth-100,000 inputs that previously crashed now complete in 0.08 and 0.14
seconds; ASan/UBSan also completed both forms at depth 200,000.

## Architecture Review

- **Representation and ownership:** one source owner, one reusable PA1 spelling
  buffer, at most one PA2 pending literal sequence, and current-line PA3 token,
  node, parse-stack, and evaluation-stack vectors.  Callbacks borrow spellings
  and literal bytes; no later phase retains them.  All line-local vectors are
  cleared together and their capacity is reused.  There is no textual transport.
- **Identity and lookup:** operators/types are enums, values are 64-bit facts,
  and child links are 32-bit node IDs.  Strings are neither keys nor semantic
  identities.  PA3 has no scope or candidate lookup and no ordered hot map.
- **Demand and repeated work:** every line is tokenized, converted, and parsed
  once.  Conditional and logical nodes record static result type once; the
  iterative evaluator follows only the selected branch/operand and counts
  skipped subexpressions.  Parse failures return `kNoNode`; expected failures
  do not throw.
- **Allocation and scaling:** hot tokens, nodes, parser state, and evaluation
  state use geometrically growing contiguous vectors.  There is no per-node
  allocation or recursive destruction.  Optional counters observe the same
  path and are null-checked out of normal execution.
- **Self-containment:** the source set is exactly PA1 + PA2 + PA3; it contains no
  compiler/reference invocation, subprocess path, filename/source fixture
  recognition, cached answer, or external semantic fallback.

## Final Architecture Review

The final ownership path is forward-only and bounded by the immutable source
plus maximum logical-line state.  Deep syntax is represented explicitly rather
than on the host stack, all semantic evaluation is demand-driven, and measured
work is linear in source/tokens/nodes.  PA3 remains encapsulated from the later
C++ expression parser and introduces no premature declaration, template,
lowering, backend, or object-writer architecture.  The adapted `spec.md`
checklist has no unexplained round trip, global retry/invalidation, fallback
lookup, duplicate parse, per-node allocation, or open architecture defect.

## Validation

- 20,000 generated valid signed expressions: exact output parity with the
  reference implementation over precedence, associativity, parentheses, and
  conditional structure.
- 100,000 generated malformed/mixed lines: one isolated result per line, final
  `eof`, no crash; telemetry reported 1,087 demanded visits and 1,312 B peak
  line storage.
- ASan/UBSan: PA3 20/20 plus depth-200,000 parenthesis and conditional probes,
  all clean (the instrumented large fixture required an extended test timeout).
- `perl scripts/cppgm_file_audit.pl --stage pa3 --paths dev/src`: pass, 16
  files checked.
- `make test-report-through-pa3`: pass, 99/99 tests and 3/3 stages.
- Repository: cohesive final-audit commit; tracked and untracked status clean.

## Checkpoint Ledger

| Checkpoint | Result | Evidence |
| --- | --- | --- |
| CP1: complete controlling-expression boundary | closed | PA3 0/20 -> 20/20; prior stages 79/79; streamed PA1/PA2 reuse and typed PA3 nodes |
| CP2: independent spec/ownership audit | closed | representative source-to-result trace; all applicable architecture-checklist owners reviewed |
| CP3: deep-syntax and scaling audit | closed | reproduced two SIGSEGVs, removed recursive parser ownership, linear 4x scaling, ASan/UBSan clean |
| CP4: final exit gates and repository state | closed | file audit 16/16 and through-PA3 99/99 pass; cohesive audit committed with clean status |

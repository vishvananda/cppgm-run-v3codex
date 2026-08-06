# PA10 Final Audit

## Stage Design and Spec Alignment

PA10 owns `immutable source buffer -> shared PA5 preprocessing callbacks ->
one compact phase-7 token buffer -> syntax parser -> arena-owned tag/payload
graph -> deterministic AST view`. The token buffer is the only retained token
representation and is required for bounded C++ syntax lookahead. Identifiers
and payloads use compact interned IDs; nodes and child links use geometric flat
arrays; parser classification uses a dense byte vector keyed by identifier ID.

This is the PA10 application of `spec.md` §§1, 8, 9, and 10. Parsing retains
source-faithful unresolved declarations, declarators, statements, expressions,
and template syntax without PA11+ lookup or type checking. Syntax text is a
rendered view, not phase transport. Expected work is O(source bytes + produced
tokens + syntax nodes + rendered bytes); the indentation contract can make the
rendered view larger than the token stream. Canonical semantics, demand,
lowering, machine IR, and ELF requirements in §§2–7 are outside this syntax
stage and remain deferred.

## Findings

- **F1 — fixed, performance:** an unclassified `identifier <` scanned to TU
  end when no `>` preceded the next statement. Repeated relational statements
  therefore took quadratic time (0.77/2.94/11.76 s at 5k/10k/20k items).
- **F2 — fixed, architecture:** `SyntaxNode` fused kind and payload into one
  presentation string, and function-style-call classification parsed an
  already-rendered `id-expression ...` label.
- **F3 — fixed, architecture/ownership:** parser classification used three
  node-allocated string hash maps; speculative fact changes were outside the
  token/node checkpoint ownership path.
- **F4 — fixed, observability:** telemetry did not count template lookahead or
  parser state and could not separate linear parsing from output-size-driven
  rendering.
- **No remaining blocker found:** successful fixture output is unchanged;
  malformed syntax still fails; no external compiler/reference invocation,
  filename/source/test special case, text transport, process-global mutable
  cache, per-node allocation, or retained abandoned syntax graph is present.

## Changes

- Stop failed top-level angle lookahead at statement/body boundaries, cap
  semantically unresolved probes at 256 tokens, and memoize structural results
  by token position plus parser-fact revision. Established/mock template names
  retain unbounded grammar-required argument lists without repeated failed work.
- Record probe, scan, scanned-token, maximum-lookahead, and failed-scan counts.
- Store syntax tags and payloads as separate interned IDs and render them only
  in the AST view; child-kind decisions now inspect tag identity directly.
- Replace string-key maps with dense identifier-ID fact bytes and a compact
  undo journal included in every parser checkpoint.
- Add parser/render timers, rendered byte/depth counters, parser/render storage
  accounting, and source bytes to the stage peak-live estimate.
- Split compact token/interner/arena ownership into
  `pa10_syntax_model.{h,cpp}`, register the new implementation source, and
  isolate template-parameter forms in focused parser helpers.

## Performance Evidence

Release `-O3`, warmed binary, AST output sent to `/dev/null`, one measured run
per point. Times are stage telemetry; storage is the retained source, tokens,
strings, graph, parser state, and renderer stack.

| Workload | Items | Tokens | Nodes | Peak MB | Parse ms | Total ms |
|---|---:|---:|---:|---:|---:|---:|
| declarations | 5k / 10k / 20k | 15,001 / 30,001 / 60,001 | 35,001 / 70,001 / 140,001 | 2.32 / 4.64 / 9.29 | 5.7 / 13.1 / 30.4 | 21.6 / 44.7 / 95.3 |
| expressions | 5k / 10k / 20k | 30,015 / 60,015 / 120,015 | 30,021 / 60,021 / 120,021 | 1.33 / 2.65 / 5.30 | 4.4 / 8.9 / 20.0 | 27.0 / 54.0 / 116.4 |
| template uses | 5k / 10k / 20k | 30,011 / 60,011 / 120,011 | 35,009 / 70,009 / 140,009 | 2.59 / 5.18 / 10.36 | 7.3 / 13.1 / 28.8 | 35.8 / 64.4 / 132.9 |
| failed relational probes | 5k / 10k / 20k | 20,007 / 40,007 / 80,007 | 20,010 / 40,010 / 80,010 | 2.02 / 4.05 / 8.11 | 3.7 / 8.4 / 18.5 | 25.0 / 51.9 / 108.9 |

The F1 workload improved from 767/2,935/11,763 ms to 25/52/109 ms. Its
lookahead counter is exactly 5k/10k/20k tokens with maximum scan length 1.
Valid template uses produce 10k/20k/40k scanned tokens with maximum length 2;
the second speculative/committed probe is served by 5k/10k/20k cache hits.

A known-template relational comma chain at 2.5k/5k/10k terms performs one
failed scan over 10k/20k/40k tokens, then 2,499/4,999/9,999 cache hits; parse
time is 1.5/2.9/6.0 ms. This covers the overlapping trusted-probe case without
limiting valid template argument length.

A 2.5k/5k/10k-term left-associated comma expression parses in
9.3/17.8/36.9 ms. Its required indented dump is 25.3/100.7/401.3 MB and renders
in 305/1,147/4,550 ms, proving the remaining quadratic wall curve is exactly
quadratic output volume while parsing remains linear.

## Architecture Review

- **Representation/ownership:** the driver owns each immutable source until
  completion; PA5 streams post-token events into one compact `{kind, TextId}`
  vector; one arena owns all `{tag, payload}` nodes and ID edges; parser
  checkpoints truncate arena tails and undo identifier facts. Rendering retains
  no alternate tree and reparses no text.
- **Identity/lookup:** token spellings are interned on intake. Syntax tags,
  payloads, and parser facts use compact IDs. The interner and graph are flat;
  there are no ordered/node maps in PA10 hot paths. Name classification is only
  PA10 grammar scaffolding, not semantic lookup.
- **Templates/repeated work:** template declarations and bodies are parsed into
  the same graph once. PA10 has no specialization demand or instantiation.
  Angle ambiguity has bounded speculative scope and explicit work counters.
- **Allocation/scaling:** nodes and edges grow geometrically and are destroyed
  in bulk per TU; traversal is iterative. Counters show tokens, nodes, parser
  facts, probes, scans, and parse time proportional to their input.
- **Self-containment:** source intake, preprocessing, token preparation,
  parsing, and rendering all execute in process using shared compiler phases.

## Final Architecture Review

Representative trace: a dependent member-template source buffer enters PA5;
expanded tokens are delivered directly through `IPostTokenStream`; identifier
spellings such as `T`, `U`, `box`, `get`, and `call` become `TextId`s; the
parser creates template/class/function/return/call/member nodes with separate
tag and payload IDs; child IDs retain `this->template get<U>()`; the iterative
view writes the checked AST. There is one owning source, one retained token
buffer, and one syntax graph at the parse boundary. Demand-to-ELF questions are
not applicable until later assignments introduce semantics and object output.

Final checklist result: no unexplained text round trip, semantic
reconstruction, whole-program retry, global invalidation, repeated unbounded
failed lookahead, string-key hot map, per-node heap ownership, or external
fallback remains in the PA10 ownership path.

## Validation

- PA10 fixtures: 157/157 pass.
- ASan+UBSan (`-O1 -g`, leak detection and halt-on-error): 157/157 pass.
- File audit: `perl scripts/cppgm_file_audit.pl --stage pa10 --paths dev/src`
  passes (39 files checked, no warnings).
- Through-stage report: `make test-report-through-pa10` passes 576/576 tests
  and all 10 tracked stages.
- Final audit changes are committed and the post-commit worktree is clean.

## Checkpoint Ledger

| Checkpoint | Result |
|---|---|
| CP0 — implementation (`8e87e193`) | PA10 syntax mode implemented; checkpoint tests green. |
| CP1 — independent architecture/performance audit | F1–F4 found and fixed across lookahead, parser facts, syntax representation, rendering, and telemetry; fixture and sanitizer checks pass. |
| CP2 — final exit gates | File audit and through-PA10 report pass; cohesive audit commit and clean status close the checkpoint. |

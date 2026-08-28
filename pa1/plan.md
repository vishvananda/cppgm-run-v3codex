# PA1 Final Audit Plan

## Current Stage Design and Spec Alignment

PA1 owns the `immutable source bytes -> preprocessing-token event stream`
boundary.  `dev/pptoken.cpp` reads one source buffer and adapts the shared stage
to the required debug format.  `dev/src/preprocess/tokens/pp_tokenizer.cpp`
pulls each byte through
UTF-8 decoding, trigraph/UCN translation, line splicing, and a phase-3
maximal-munch lexer.  Raw-string contents temporarily read physical code points
so phase-1/2 transformations are reverted as required.  Header-name state is
carried only across the directive prefix that can make it relevant.

The source belongs to the adapter and remains immutable for the call.  The
tokenizer owns fixed lookahead queues, one reusable spelling buffer, and the
bounded raw delimiter; token data is synchronously emitted through
`IPPTokenStream`.  No translated-source or token vector is built or retained.
This is the PA1-applicable part of `spec.md` sections 1, 8, 9, and 10: one
forward stream, explicit phase-local ownership, geometric token-buffer growth,
O(source bytes + emitted tokens and token bytes) work, observable counters,
and self-contained output.  Interned semantic identities, typed lowering, and
backend checks have no owner or consumer in PA1 and therefore remain outside
this stage.

## Architecture Review

- Representation and ownership: one immutable source buffer is live with
  fixed translation/lexer lookahead and one token spelling.  The callback does
  not retain stage storage, and no text is rendered and reparsed.
- Identity and lookup: PA1 has no retained semantic entities.  Unicode
  classification is a binary search over fixed read-only metadata; punctuator,
  named-operator, and raw-terminator lookups have fixed bounds.
- Repeated work: every physical code point is decoded once.  Trigraph/UCN and
  splice cursors move only forward; each token is recognized and emitted once.
  Template, dependency, lowering, machine-IR, and ELF checklist items are not
  present at this boundary.
- Allocation and lifetime: fixed-capacity queues replace general deques, the
  16-code-point raw delimiter is stack storage, and one geometrically growing
  string is reused.  Comments and whitespace are consumed without spelling
  storage.  The source and longest token spelling are the only size-dependent
  retained buffers.
- Self-containment: the ownership path contains no process launch, reference
  executable, host compiler, fixture lookup, filename rule, or cached answer.

## Findings

| ID | Audit finding | Resolution |
|---|---|---|
| F1 | `DebugPPTokenStream` flushed `stdout` for every token, making rendering syscall-bound. | Emit newline characters without flushing and disable synchronized/tied standard streams in the adapter. |
| F2 | General deques, two vectors per raw token, and fresh token strings obscured bounded ownership and repeated allocations. | Use checked fixed queues, a fixed raw delimiter, and one reusable spelling buffer. |
| F3 | Eight-digit UCN accumulation shifted a signed `int`, permitting overflow/undefined behavior before rejection. | Accumulate in `uint32_t` and narrow only after scalar validation. |
| F4 | Existing telemetry could not distinguish translated work, emitted bytes, retained token storage, or stage time. | Add translated-code-point, token-byte, peak-buffer, and elapsed-time counters; counters remain optional. |

## Changes

The final audit keeps the original cursor/lexer ownership split and tightens
its full path: bounded cursor storage, allocation-free raw terminator matching,
reused token storage, defined UCN arithmetic, buffered debug output, and
stage-level work/memory/time observability.  Token grammar, event ordering, raw
phase bypass, directive context, and the public streaming callback remain
unchanged.

## Performance Evidence

The 30-byte repeated line `alpha 123.4e+5 /* c */ R"(x)"` exercises UTF-8,
identifiers, pp-numbers, comments, raw literals, whitespace, and newlines.
Before the audit it took 0.31 s, 1.22 s, and 4.92 s for 0.983 MB, 3.932 MB, and
15.729 MB.  After the audit the same points took 0.05 s, 0.22 s, and 0.79 s;
the additional 62.915 MB point took 3.23 s.  Successive 4x inputs take 4.40x,
3.59x, and 4.09x time, while throughput improves by about 6x at the first three
points.

`strace -c` on the 0.983 MB point records 196,608 token-triggered flushes in
the pre-audit renderer.  The audited renderer makes 488 buffered
`write`/`writev` calls.  Optional
work counters on the same workload report one decoded and one delivered code
point per source code point, six emitted tokens per line, and a 15-byte inline
peak spelling buffer.  A long-identifier workload takes 0.10 s, 0.41 s, and
1.63 s at 4.194 MB, 16.777 MB, and 67.109 MB, confirming linear long-token
scaling and spelling-buffer reuse.

## Final Architecture Review

The audited stage has one forward data flow and no duplicate owning token
representation, text round trip, process-global mutable cache, fallback lookup,
whole-input retry, or unbounded fixed-point loop.  Fixed capacities correspond
to the grammar's two-character phase-1 lookahead, one-character phase queues,
four-character punctuator lookahead, and 16-character raw delimiter.  Output
rendering is buffered, optional telemetry observes the same path, and memory
growth is limited to the immutable source and longest emitted spelling.  No
remaining PA1 correctness, architecture, performance, self-containment, or
file-ownership blocker was found.

## Checkpoint Ledger

| Checkpoint | Result | Evidence |
|---|---|---|
| CP1: complete phases 1-3 boundary | closed, 0/53 -> 53/53 | Commit `f3a8ddf0`; local, through-PA1, active report, file audit, and initial scaling series passed. |
| CP2: independent full-stage audit | closed | Normative trace; GCC/Clang and ASan/UBSan 53/53; 100,000-case deterministic stress; syscall profile; mixed and long-token scaling; required final gates below. |

## Validation

- `perl scripts/cppgm_file_audit.pl --stage pa1 --paths dev/src`: pass,
  12 files checked.
- `make test-report-through-pa1`: pass, 53/53 tests and 1/1 stages.
- GCC, Clang, ASan, and UBSan PA1 suites: pass, 53/53 each.
- Deterministic ASCII stress: pass, 100,000 cases with no sanitizer or internal
  invariant failure.
- Final `git status --short`: empty after the cohesive audit commit.

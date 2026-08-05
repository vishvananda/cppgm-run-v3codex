# PA2 Final Audit Plan

## Current Stage Design and Spec Alignment

PA2 owns the `preprocessing-token events -> post-token events` boundary and
reuses PA1 rather than replaying it:

```text
immutable input bytes
    -> TokenizePreprocessingFile / reusable PA1 spelling buffer
    -> synchronous IPPTokenStream callbacks
    -> PostTokenAnalyzer
    -> typed IPostTokenStream callbacks
    -> posttoken textual view
```

Ordinary tokens are classified and emitted synchronously.  Phase-6 string
concatenation retains only the current maximal sequence as one joined spelling,
compact content ranges, its selected encoding/suffix facts, and the encoded
result while it is emitted.  A representative input combining a UCN, numeric
escape, phase-2 splice, trigraph, and the literal-operator exception produced
the expected typed string UDL, integer, invalid `#`, keyword, empty string, and
identifier events in order.  Its counters were 44 source bytes, 35 translated
code points, 7 preprocessing tokens, 6 post-tokens, and a two-token maximum
string sequence.

This is the PA2-applicable part of `spec.md` sections 1, 8, 9, and 10: one
forward typed flow, no token-vector transport or text round trip, bounded
phase ownership, geometric variable storage, linear work, observable work and
storage, and self-contained output.  Semantic identities, scopes, templates,
dependency scheduling, lowering, machine IR, and ELF have no representation or
consumer in PA2 and are not simulated at this stage.

## Architecture Review

- Representation and ownership: the adapter owns one immutable source buffer;
  PA1 owns fixed lookahead plus one reusable spelling; PA2 retains no ordinary
  token.  A maximal string sequence has one transformed source spelling and
  compact ranges into it.  Encoded bytes coexist only while that sequence is
  flushed and are a distinct required result, not reparsed transport text.
- Identity and lookup: PA2 retains no semantic entity or equality key.  Simple
  token recognition is a bounded binary search over one fixed read-only table;
  no global declaration scan, ordered semantic container, or mutable cache is
  present.
- Templates and repeated work: no template surface exists.  Every
  preprocessing token is visited once, every string part is parsed once, and
  each retained content range is decoded once after the sequence encoding is
  known.  There is no retry loop, invalidation, exception-based candidate
  rejection, or deferred grammar replay.
- Allocation and lifetime: scalar ABI payloads use fixed stack storage.
  Joined string spelling, compact ranges, and encoded bytes grow geometrically
  and are reused only within the analyzer lifetime.  Empty string parts need no
  range record.  Optional telemetry exposes token/unit/sequence counts, maximum
  sequence size, payload bytes, encoded bytes, retained phase capacity, and
  elapsed time.
- Lowering and backend: no LowIR, machine IR, serializer, assembler, object
  writer, or complete-program validator is in the PA2 ownership path.
- Self-containment: source inspection and a process-only `strace` found one
  `execve` for `posttoken` and its exit, with no child process, reference tool,
  fixture lookup, filename rule, or cached-answer path.

## Findings

| ID | Audit finding | Resolution |
| --- | --- | --- |
| F1 | Maximal string sequences owned one `std::string` per token, then duplicated the complete spelling and built a large parsed-parts vector during flush. | Retain one joined spelling from arrival, parse each part once, and store only compact nonempty content ranges plus sequence facts.  Raw terminators now use a bounded allocation-free delimiter scan. |
| F2 | Every ordinary integer and character literal built a heap vector for an ABI payload of at most 8 bytes; geometric growth caused three allocations for a four-byte integer. | Encode scalar values into checked fixed stack storage; floating values use the same fixed payload buffer. |
| F3 | Existing counters reported string payload bytes but could not expose sequence count, token fan-in, encoded-byte peaks, or the retained PA2 capacity responsible for scaling. | Extend optional stage telemetry with those counters without changing semantic behavior when telemetry is enabled. |

No independent correctness, timeout, fallback, or self-containment defect was
found after the ownership fixes.

## Changes

The audit keeps the public typed callback boundary and all token semantics.
`PostTokenAnalyzer` now incrementally builds the required joined source view,
records offsets rather than owning token strings, resolves prefix and suffix
facts once, and decodes directly from those ranges.  Fixed-size scalar output
no longer enters the allocator, and one encoded-byte buffer is reused across
string sequences.  The refactor also removes temporary raw delimiter strings
and adds storage/work counters at the PA2 owner.

## Performance Evidence

The checked-in `700-hard-string-concat.t` is 429,984 bytes.  A sink-only run
reports 64,128 preprocessing tokens, 40,608 emitted non-EOF post-tokens,
19,104 string sequences, 32,142 literal units, maximum fan-in 3, and 33 payload
bytes retained, 64 encoded bytes peak, and 235 bytes peak PA2 capacity.
Repeating it
1/4/16/64 times produced 0.021/0.085/0.340/1.361 seconds of measured pipeline
time; work counters increased exactly 1x/4x/16x/64x and peak RSS was
4,088/5,444/11,236/34,368 KiB as the immutable input grew.

The architecture stress is one maximal sequence of empty strings.  At
200,000/800,000/1,600,000 tokens, the audited path takes
0.05/0.19/0.39 seconds and 5,436/11,460/19,556 KiB peak RSS.  Before the
refactor the same points took 0.07/0.31/0.62 seconds and
23,356/83,212/162,860 KiB.  At 1.6 million tokens this is 37% less time and
88% less peak RSS.

Valgrind on 100,000 four-byte integer literals reports 300,018 heap allocations
before and 18 after; the 4-million-literal wall time improves from 1.76 to
1.46 seconds.  A 20,000-part sequence of 32-character strings drops from
20,060 allocations to 74.  Both profiles report zero errors and no live blocks
at exit.

## Final Architecture Review

The audited PA2 path has one forward stage flow and no duplicate owning token
stream, joined-spelling copy, per-scalar heap allocation, text serialization
round trip, process-global mutable cache, whole-input retry, fallback lookup,
or external implementation dependency.  Required delayed ownership is exactly
one maximal string sequence; ranges cannot outlive its joined spelling, and
all callback payloads are consumed synchronously.  Counters and adversarial
scaling distinguish required source/output growth from retained phase state.
No PA1-through-PA2 correctness, architecture, performance, timeout,
self-containment, or file-ownership blocker remains.

## Checkpoint Ledger

| Checkpoint | Result | Evidence |
| --- | --- | --- |
| CP1: implement the complete PA2 boundary | closed, 0/26 -> 26/26 | Commit `e9c59a2a`; typed streaming recognition, literal decoding, phase-6 concatenation, PA1 53/53, PA2 26/26. |
| CP2: independent full-stage audit | closed | Normative ownership/data trace; compact sequence refactor; scalar allocation removal; GCC/Clang, debug-STL, ASan/UBSan, differential stress, allocation profiles, scaling series, self-containment trace, and final gates below. |

## Validation

- `perl scripts/cppgm_file_audit.pl --stage pa2 --paths dev/src`: pass,
  14 files checked.
- `make test-pa2`: pass, PA2 26/26.
- `make test-report-through-pa2`: pass, PA1 53/53, PA2 26/26, 79/79
  tests and 2/2 stages.
- GCC, Clang, and debug-STL PA2 suites: pass, 26/26 each; GCC and Clang PA1
  suites remain 53/53.
- ASan/UBSan: PA1 53/53 and PA2 26/26; a 200,000-token maximal string
  sequence also passes.
- Deterministic 50,000-token mixed differential stress: exact reference parity
  with no sanitizer or internal invariant failure.
- Process trace: no child process or implementation fallback.
- Final `git status --short`: empty after the cohesive audit commit.

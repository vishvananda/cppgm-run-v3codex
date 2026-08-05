# PA4 Final Audit Plan

## Stage Design and Spec Alignment

PA4 owns the `source bytes -> macro-expanded preprocessing-token events ->
post-token events` boundary.  `macro` retains one immutable source buffer and
runs it once through the shared PA1 tokenizer.  A logical-line adapter owns only
the current line; directives update an interned-ID macro registry, while text
enters one rescan deque.  Expanded events feed the shared PA2 post-tokenization
session directly, and only the tool adapter renders final events.

Identifiers have compact translation-unit IDs and one retained spelling.
Non-identifier source spellings remain token-local, macro definitions own only
their active normalized replacement spellings, and generated string/paste
spellings live only through rescan.  Expansion uses move-only heap frames,
contiguous raw/expanded argument arenas with index ranges, lazy one-time
prescan, and persistent compact paint roots.  The registry, parameter indexes,
identifier interner, and paint transition caches use flat open addressing.

A representative `WRAP(foo,A)` combining ordinary and variadic substitution,
stringizing, helper expansion, token pasting, and final object expansion flows
as 144 immutable source bytes -> 78 PA1 events -> 57 non-whitespace source
tokens -> five macro definitions -> six invocations/four demanded prescans ->
one pasted identifier -> four PA2 post-tokens (`11`, `"foo"`, `7`, `7`).  Peak
rescan was 9 tokens, peak depth was 2 frames, and argument storage was 496
bytes.  No token listing was serialized or reparsed; concatenating and
retokenizing the single spelling produced by `##` is required macro semantics,
not phase transport.

This is the PA4 application of `spec.md` sections 1, 2, 5, 8, 9, and 10:
forward typed events, interned identity, explicit rescan work, phase-local
ownership, flat hot indexes, output-proportional work, release telemetry, and
self-contained behavior.  Parsing, declarations, templates, semantic lookup,
lowering, machine IR, and ELF have no PA4 representation or consumer and are
not simulated in this stage.

## Findings

| ID | Severity | Finding | Resolution |
| --- | --- | --- | --- |
| PA4-A1 | architecture/performance | Every source spelling, including one-use numbers and literals, was interned twice and retained to EOF.  A 1.6M-line numeric stream used 189,912 KiB despite one live token. | Intern only identifiers once; keep ordinary non-identifiers transient, let active macro definitions own replacement spellings, and borrow those spellings only while the registry is mutation-safe. |
| PA4-A2 | architecture/performance | Parsed and bound arguments each owned deques; an unused 80k-argument call used 187,164 KiB. | Use one flat raw arena plus ranges and one flat expanded arena.  Release raw storage after its last consumer and directly hand off a sole ordinary argument to preserve deep linear behavior. |
| PA4-A3 | architecture | Macro/parameter/paint hot paths used node-allocating `unordered_*` containers, contrary to the flat-container requirement. | Replace them with geometrically grown open-addressed tables; paint-root observation uses a byte vector only when telemetry is enabled. |
| PA4-A4 | observability/allocation | Expansion built both a `Piece` vector and a result vector per invocation, while identifier lifetime, frame depth, and argument bytes were invisible. | Fold paste operations directly into one frame-reused replacement buffer and add identifier, frame, and argument-storage counters. |

No independent macro-correctness defect was found.  A temporary flat-arena
version retained moved parent arenas and reproduced quadratic deep-nesting
storage; it was rejected during the audit, and the final direct-handoff path is
linear and sanitizer-clean.

## Changes

- Split spelling ownership by semantic lifetime and made macro replacement
  tokens borrow definition-owned non-identifier spellings only during a drain;
  every directive drains expansion before a registry mutation can move them.
- Replaced per-argument deques with flat token arenas/ranges, move-only frames,
  explicit raw-consumer release, and a constant-time single-argument handoff.
- Added reusable flat hash maps for identifier IDs, macro definitions,
  parameter indexes, duplicate-parameter checks, and paint add/merge caches.
- Folded placemarkers, GNU variadic comma paste, and ordinary `##` directly
  into one reusable replacement vector, preserving generated-token retokenize
  and rescan behavior.
- Extended `CPPGM_FRONTEND_STATS` with interned identifier count/bytes, peak
  expansion frames, and peak argument storage bytes.

## Performance Evidence

Release measurements after the final refactor:

| Workload | Scale | Wall time | Work/storage evidence |
| --- | ---: | ---: | --- |
| Mixed helper, variadic, stringize, and paste lines | 25k / 100k / 400k | 0.23 / 0.87 / 3.62 s | invocations 0.15 / 0.60 / 2.40M; prescans 0.10 / 0.40 / 1.60M; peak 2 frames, 496 argument bytes, 10 rescan tokens |
| Unique numeric lines | 100k / 400k / 1.6M | 0.08 / 0.36 / 1.44 s | identifier count stays 3 and peak line/rescan stays 1; RSS 6.6 / 7.3 / 18.6 MiB |
| Unused high-arity invocation | 20k / 80k / 320k arguments | 0.07 / 0.27 / 1.13 s | argument storage 1.32 / 5.30 / 21.19 MB; RSS 14.0 / 45.5 / 171.1 MiB |
| Nested ordinary demand | depth 1k / 4k / 16k / 64k | 0.003 / 0.013 / 0.051 / 0.226 s measured front end | frame/invocation/prescan counts equal depth; argument bytes 0.048 / 0.192 / 0.768 / 3.072 MB; RSS 7.2 / 8.4 / 23.1 / 81.2 MiB |
| Indirect paint cycle | 2.5k / 10k / 40k macros | 0.011 / 0.046 / 0.196 s measured front end | invocations scale exactly 4x; paint nodes 0.16 / 0.64 / 2.56M; RSS 7.0 / 15.6 / 51.8 MiB |

At the audited ownership points, the 1.6M unique-number case improved from
3.54 seconds/189,912 KiB to 1.44 seconds/19,056 KiB, and the 80k-argument case
improved from 0.55 seconds/187,164 KiB to 0.27 seconds/46,548 KiB.  Successive
4x scales have proportional counters, time, and required retained data; no
unexplained retry, copy, or superlinear path remains.

## Architecture Review

- **Representation and ownership:** one immutable source, PA1 fixed lookahead
  and reusable spelling, one current logical line, one identifier table, active
  macro definitions, the rescan/frame worklist, and PA2's one pending string
  sequence.  Raw and expanded forms coexist only for parameters whose actual
  replacement use requires both.  Callback payloads are borrowed synchronously.
- **Identity and lookup:** identifiers and macro paint use 32-bit IDs; all hot
  maps are flat and average O(1).  Strings are payloads, never macro identity or
  paint keys.  Determinism does not impose ordered semantic containers.
- **Repeated work and demand:** a source token is lexed once, each invocation
  is instantiated once, and each demanded argument is prescanned once and
  cached for all ordinary uses.  Raw arguments are retained only for `#`/`##`.
  Explicit frames and token-local paint handle cycles without host recursion,
  global cutoffs, retries, or whole-program invalidation.
- **Allocation and scaling:** macro/argument/paste collections grow
  geometrically; no argument owns a deque or map node.  A sole nested argument
  transfers its queue in O(1), while general argument metadata and payload are
  contiguous.  Optional counters observe the same path; telemetry-only root
  flags are absent when counters are disabled.
- **Self-containment:** the PA4 source set is exactly PA1 + PA2 + PA4.  It has
  no subprocess, host/reference compiler, fixture lookup, filename/source-text
  special case, cached answer, or external semantic fallback.

## Final Architecture Review

The final boundary is forward-only and owns no duplicate complete token stream
or textual transport.  Stable identifier/macro identity, flat lookup, explicit
rescan frames, precise raw/expanded lifetimes, and measured linear work cover
all PA4-applicable checklist owners.  Required `##` retokenization is isolated
to one generated spelling and immediately rescanned.  There is no unexplained
text round trip, node-based hot map, recursive expansion, global retry,
fallback lookup, per-argument allocation, retained one-use spelling, or open
correctness/performance/self-containment defect.

## Validation

- 20,000 deterministic mixed valid macro cases: exact output and exit-status
  parity with the checked reference implementation (1,783,938 output bytes).
- GCC, Clang, and libstdc++ debug-iterator PA4 suites: 72/72 each.
- ASan/UBSan: PA4 72/72 plus depth-100,000 and arity-80,000 generated probes,
  with no sanitizer finding.
- Valgrind: 20,000 expansions of a 256-byte replacement, zero errors and zero
  definitely/indirectly/possibly lost bytes.
- Process trace: one `execve` for `dev/macro`, no child process or fallback.
- `perl scripts/cppgm_file_audit.pl --stage pa4 --paths dev/src`: pass, 18
  files checked.
- `make test-report-through-pa4`: pass, 171/171 tests and 4/4 stages.
- Repository: cohesive final-audit commit and empty tracked/untracked status.

## Checkpoint Ledger

| Checkpoint | Result | Evidence |
| --- | --- | --- |
| CP1: complete PA4 macro boundary | closed | Commit `1578166f`; PA4 0/72 -> 72/72 with directive, substitution, variadic, paste, rescan, and paint ownership. |
| CP2: independent spec/ownership audit | closed | Representative source-to-post-token trace and all applicable representation, identity, demand, allocation, and self-containment owners reviewed. |
| CP3: architecture and scaling repair | closed | Transient spellings, flat registries/caches, contiguous arguments, direct nested handoff, reusable paste buffer, and linear 4x probes. |
| CP4: final exit gates and repository state | closed | File audit and through-PA4 report pass; sanitizer/compiler variants and differential/Valgrind/process checks pass; audit committed cleanly. |

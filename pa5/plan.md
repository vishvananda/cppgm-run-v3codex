# PA5 Final Audit

## Stage Design and Spec Alignment

PA5 is one shared in-process front-end phase: an immutable primary buffer enters
the PA1 tokenizer, location-tagged preprocessing tokens are retained only for
the current logical line or active macro expansion, active output is sent
directly through the PA2 typed session, and only the PA5 tool renders token
text. Includes recurse through the same per-primary session without `sof`/`eof`;
each command-line source gets a fresh macro, conditional, include, presumed
location, and pragma-once state. The main-owned date/time is shared across those
otherwise independent sessions.

This is aligned with `spec.md` §§1, 8, 9, and 10 for the surfaces present at
PA5: file and identifier spellings have compact interned IDs; macro and file
identity lookup use flat hash storage; source/include buffers and short-lived
line, rescan, argument, condition, and PA2 string storage have explicit owners;
and required output is produced without a subprocess, reference executable,
cached answer, or test-specific path. The only spelling-to-token pass after
source tokenization is the language-required re-lex of a `##` result. Parser,
template, lowering, backend, and ELF checklist items are not PA5 surfaces.

## Performance Evidence

All measurements use the release `dev/preproc`, `/dev/null` output, and
`CPPGM_FRONTEND_STATS=1` where counters are quoted.

| Workload | Representative final evidence |
|---|---|
| Independent definitions/invocations, N=1k/2k/4k/8k/16k | 1k/2k/4k/8k/16k lookups, invocations, expansions, and post-tokens; 3.67/7.91/21.47/38.74/80.92 ms internal elapsed; 4,384/4,892/5,564/7,372/10,464 KiB peak RSS |
| 16k independent invocations, paint representation before -> after | 512,002 -> 2 trie nodes, with 16,000 allocation-free singleton roots; 15,272 -> 10,464 KiB peak RSS; 86.86 -> 80.92 ms internal elapsed |
| Checked-in 8,001-link expansion plus 64 substitutions | 190,206 bytes; 8,067 lookups, 8,002 invocations, one argument prescan, 24,065 expanded tokens, peak rescan 64, depth 2; 57.21 ms internal, 0.06 s wall, 12,432 KiB RSS |
| 16k controlling expressions | 16k nodes and 16k evaluator visits, 56.84 ms total stage elapsed, 7.49 ms condition handling, 92-byte peak condition storage; no retry growth |
| Telemetry separation at 16k definitions/invocations | stats off/on both 0.07 s wall; 9,892/10,348 KiB RSS; semantic path and work counts unchanged |

The counters scale with source and emitted work. `perf stat` reported 63.90 ms
task-clock on the checked-in chain; unavailable hardware counters were not used
to infer a bottleneck. The counter evidence identified and then closed the
singleton paint retention issue; no unexplained superlinear visit, retry,
lookup, rescan, or invalidation path remains.

## Architecture Review

- Representation/ownership: one immutable buffer per active include frame;
  one tokenizer spelling buffer; one logical-line vector; a rescan deque only
  while an invocation can continue; retained macro replacement lists; one PA2
  adjacent-string buffer; and one reusable PA3 condition workspace. No owning
  whole-file token vector or duplicate textual phase transport exists.
- Identity/lookup: identifiers and file names carry `SpellingId`; macro names
  and paint membership use compact integer identity; file-once identity is the
  `(device,inode)` pair; dominant maps/sets are open-addressed flat tables.
  Presentation strings are not macro or file-identity keys.
- Repeated work: arguments are prescanned once per demanded binding, expansion
  uses iterative frames, cross-line invocation scans resume from a saved
  cursor, condition storage is reused, and singleton paint sets require no trie
  allocation. Multi-name paint add/merge transitions remain memoized.
- Boundaries: expanded directive strings use PA2's shared typed decoder;
  controlling expressions use PA3's typed adapter; active text feeds PA2
  callbacks directly. `##` re-lexing and stringization are required language
  operations, not phase serialization.
- Self-containment: source access is `ifstream` plus `stat` file identity.
  Static searches found no compiler/reference invocation, fixture dispatch,
  global mutable translation-unit cache, or hardcoded output.

## Final Architecture Review

The full owner path is now coherent from source bytes through phase-7 events.
The audit found no remaining correctness, timeout, file-audit,
self-containment, architecture, or scaling blocker. Non-identifier spellings
are copied only where the PA1 callback lifetime requires retention; identifiers
and locations stay compact, and all ordinary live storage is bounded by source
buffers, one line, active include/expansion depth, retained definitions, or
produced output work.

## Checkpoint Ledger

| Checkpoint | Result |
|---|---|
| Contract, spec checklist, tests, stage commits, changed source, prior ledger | independently reviewed |
| Directives, inactive structure, predefined/location macros, includes, line control, pragmas, errors, source isolation | traced through tokenizer -> PA5 state -> macro expansion -> PA3/PA2 -> renderer |
| Representation, identity, allocation, scaling, and self-containment | reviewed; findings below closed across their owners |
| Focused PA4/PA5 regression and sanitizer pass | PA4 72/72; PA5 70/70; ASan/UBSan PA5 pass |
| Required file audit | pass: 18 PA5-owned `dev/src` files checked |
| Required through-PA5 report | pass: 241/241 tests; PA1-PA5 all pass |
| Commit and clean worktree | final audit commit; clean status verified after commit |

## Findings

1. `#include`/`#line` duplicated PA2 string escape decoding in PA5.
2. Every singleton macro paint set retained a 32-node trie path.
3. Pragma-once identities used a node-allocating `unordered_set` despite a
   compact key and an available flat table.
4. PA3 condition workspaces were rebuilt per directive and their node/visit and
   storage work was not observable from `preproc`.
5. Generated `__FILE__` spellings did not escape embedded control bytes, so a
   valid presumed file value could produce a lexically invalid presentation.

All findings are resolved; no open finding remains.

## Changes

- Added a shared PA2 ordinary-string decoder and routed directive consumers to
  it; include paths containing null are rejected before filesystem lookup.
- Added direct singleton paint encoding, retaining the memoized trie only for
  multi-name recursion sets, and exposed singleton/node counts.
- Replaced pragma-once node storage with the generic flat hash table.
- Reused one PA3 evaluator/PA2 adapter per preprocessing session and exposed
  condition, source, line, argument, interning, paint, and PA2 storage metrics.
- Escaped generated dynamic string literals and removed the duplicate owning
  physical path from include frames.

## Validation

- `perl scripts/cppgm_file_audit.pl --stage pa5 --paths dev/src`: pass, 18
  files checked.
- `make test-report-through-pa5`: pass, 241/241 tests and 5/5 stages.
- Focused `make test-pa4` and `make test-pa5`: pass, 72/72 and 70/70.
- Standalone `-Wall -Wextra -Werror` ASan/UBSan build running all PA5 tests:
  pass.
- `git diff --check`: pass; the final audit commit leaves
  `git status --short` empty.

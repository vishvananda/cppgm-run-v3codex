# PA8 Final Full-Stage Audit

## Stage Design and Spec Alignment

PA8's production path is:

`immutable source for one translation unit -> shared PA1-PA5 preprocessing and
post-token callbacks -> one compact PA8 token/literal buffer -> integrated
parser and semantic construction -> canonical whole-program graph -> typed
layout/relocation facts -> directly streamed mock image`.

`dev/nsinit.cpp` owns only command-line files and output. `InitializationProgram`
owns the invocation, `ProgramModel` owns canonical identifiers, types, scopes,
bindings, entities, initializers, linkage, and emission order, and each parser
owns its translation-unit token buffer, memo table, and scratch vectors. The
source and token buffer die after that translation unit is semantically
constructed. No syntax tree, rendered signature, textual IR, image-sized output
buffer, or subprocess lies on the path.

A representative two-translation-unit trace is `namespace N { extern int x; }`
followed by `namespace N { int x = 5; const int& r = x; }`. The preprocessor
delivers interned-name/literal facts; namespace and declarator lookup produce
compact `ScopeId`, `PathId`, and `NameId` keys; the external index reuses one
`EntityId` for `x`; reference conversion records `r` as a typed symbolic
`INITIAL_ADDRESS_ENTITY`; layout assigns stable entity offsets in first-
declaration order; relocation resolves the ID to the final offset; and the
writer emits padding and bytes directly. No later phase repeats lookup or
reconstructs a type from text.

The applicable normative surfaces are `spec.md` sections 1-3, 5-6, and 8-10:
one forward typed path, canonical identities, indexed scope/edge lookup,
precise cache ownership, direct consumption of semantic facts, compact
central storage, observable scaling, and self-contained output. PA8 has no
templates, class demand, LowIR, machine IR, or ELF output; those checklist
items are not synthesized into this stage.

## Findings

All independent-audit findings are closed:

1. Using-directive lookup selected the first reachable binding, missed required
   ambiguity/overload merging, repeatedly traversed overlapping namespace
   graphs, and initially failed to compose valid child-cache results.
2. Declarator branch retries grew exponentially for nested abstract function
   types. Linear memoization alone still left a sanitizer-visible recursive
   stack limit at extreme nesting.
3. Constant-expression state crossed translation-unit visibility boundaries;
   constant initialization and constant usability were conflated; an identifier
   whose value was zero was incorrectly treated as a C++11 null pointer
   constant; and a constexpr reference to a static mutable object was rejected.
4. Multi-level pointer qualification, reference compatibility, string/array/
   function conversions, pointer-to-bool state, and overloaded-function target
   selection had missing or unsafe cases.
5. Storage-class, cv, parameter, function, `constexpr`, `thread_local`, linkage,
   and redeclaration agreement checks had diagnostic-required gaps.
6. Recursive array size/alignment, unchecked size arithmetic, and an
   image-sized intermediate vector created avoidable depth, overflow, and peak
   memory risks.
7. The original monolithic internal header obscured ownership boundaries and
   triggered the file audit's body-weight warning; release telemetry could not
   explain lookup, parser, or retained-storage growth.

## Changes

- Replaced per-scope using-target lists with central deduplicated forward and
  reverse edge tables. Lookup now merges canonical binding identities and
  overload candidates, detects ambiguity, caches complete `(scope, name, kind)`
  results, invalidates only affected names/reverse-reachable scopes, and reuses
  valid cached subgraphs without global cache clearing.
- Added declarator-session memoization keyed by token position, scope, and
  named/abstract mode; moved parenthesized declarators to an explicit frame
  stack; and imposed a clean 512-call implementation limit on the remaining
  recursive grammar relation.
- Separated translation-unit constant usability from image constant
  initialization, retained expression use-unit identity, and corrected null
  pointer, pointer qualification, reference, overload, decay, and contextual-
  bool conversions across their parse/model/emission ownership path.
- Completed the relevant declaration and redeclaration checks, made type
  size/alignment walks iterative with overflow guards, bounded retained/output
  ranges, and stream the image after one typed layout pass.
- Split canonical type declarations into `pa8_types.h` and the program/parser
  interface into `pa8_program.h`; all implementations remain in responsibility-
  owned `.cpp` files.
- Added low-overhead opt-in counters for token, identifier, type, semantic,
  cache, edge, candidate, declarator, scratch, image, and elapsed work, plus
  seven focused course regressions for the corrected semantic paths.

## Performance Evidence

All figures use the final release binary with `CPPGM_FRONTEND_STATS=1`.
The using-chain workload contains N namespaces, N declarations, and N-1
overlapping using edges, interleaving graph growth with lookups:

| N | pre-fix edge visits / elapsed | final edge visits / cache hits / elapsed | final RSS |
| ---: | ---: | ---: | ---: |
| 1,600 | 1,279,200 / 35.23 ms | 1,599 / 1,598 / 17.23 ms | 5,648 KiB |
| 3,200 | 5,118,400 / 122.69 ms | 3,199 / 3,198 / 35.00 ms | 6,992 KiB |
| 6,400 | 20,476,800 / 472.88 ms | 6,399 / 6,398 / 74.79 ms | 10,436 KiB |
| 12,800 | 81,913,600 / 1,997.17 ms | 12,799 / 12,798 / 162.46 ms | 16,852 KiB |

The exact edge counter localized the quadratic slow path; after cache
composition, edge visits are N-1 and time/space track source, tokens, scopes,
and image bytes linearly. Exact-name and edge invalidation correctness is also
covered by the late-declaration/late-edge regression.

The nested abstract-function workload took 6.59 s at depth 24 before the fix.
It now takes 0.292 ms with 94 frames, 44 cache hits, 46 misses, and 46 memo
entries. Final depth scaling remains linear: depths 64, 128, 256, and 400 take
0.508, 0.761, 1.364, and 2.235 ms with 254, 510, 1,022, and 1,598 frames.
Depth 600 terminates with `declarator nesting limit exceeded`; depth 400 passes
under ASan/UBSan without stack exhaustion.

A 20,000-dimensional array (60,009 tokens and 20,001 canonical types) parses,
lays out, and emits in 49.85 ms with 18,948 KiB RSS. Its iterative type walk
produces an 8-byte image, reports 3,206,850 peak stage bytes, and does not retain
an image-sized duplicate buffer.

## Architecture Review

- **Representation and ownership:** one source buffer and one compact token
  buffer are live for the active translation unit; only canonical typed facts
  and required literal bytes cross into program ownership. There is no AST,
  semantic clone, text round trip, or pointer retaining parser storage.
- **Identity and lookup:** names, types, paths, scopes, bindings, entities,
  candidates, strings, temporaries, and using edges use compact IDs. Open-
  addressed flat tables provide direct average-O(1) identity and name lookup;
  source-order IDs, not ordered hot containers, determine output order.
- **Repeated work and invalidation:** visited generations terminate using-edge
  cycles. Complete typed cache keys admit positive and negative results;
  reverse edges propagate only name or graph mutations that can affect a
  cached start scope. Parser speculation returns compact success/failure and is
  memoized for one monotonic declarator session. Exceptions terminate an
  ill-formed program and are not retry control flow.
- **Typed lowering/emission:** PA8's mock-image boundary consumes selected
  `EntityId`, canonical `TypeId`, initializer kind, linkage, constant-state,
  and relocation target directly. Each entity/temporary/string is laid out
  once and emitted once; no lookup, name parsing, host compiler, assembler, or
  reference binary participates.
- **Allocation and scaling:** long-lived records and hash slots use central
  geometric vectors; candidate and edge relationships are ID-linked slices;
  short qualified names stay inline. Parser/memo/token storage is phase-local,
  recursive type sizing is eliminated, and counters account for dominant live
  storage and work.

## Final Architecture Review

**PASS.** The final PA8 architecture has one self-contained forward semantic
path, complete canonical ownership through whole-program linkage, precise
lookup invalidation, direct typed relocation/emission, bounded parser depth,
and measured linear behavior for the identified scale risks. No unexplained
whole-program retry, global invalidation, semantic text reconstruction,
per-node hot allocation, external-tool fallback, or test-specific path remains.

## Validation

- `perl scripts/cppgm_file_audit.pl --stage pa8 --paths dev/src`: PASS, 30
  files checked, no warnings.
- `make test-pa8`: PASS, 41/41 handout and 26/26 course tests (67/67 PA8).
- `make test-report-through-pa8`: PASS, 399/399 tests and 8/8 stages.
- ASan+UBSan: PASS, all 67 PA8 tests plus depth-400 declarator,
  20,000-dimensional array, and 12,800-namespace using-chain stress; excessive
  declarator nesting exits cleanly.
- `-std=gnu++11 -O3 -Wall -Wextra -Werror`: PASS.
- `git diff --check` and the self-containment dependency scan: PASS.

## Checkpoint Ledger

| Checkpoint | Evidence | Result |
| --- | --- | --- |
| CP0: baseline and history | `2f540b03`, original plan, clean 392/392 through PA8 | accepted as audit input, not conclusion |
| CP1: stage reconstruction | README, grammar/tests, source ownership, source-to-image trace, applicable `spec.md` checklist | complete |
| CP2: correctness audit | ambiguity, cross-TU constants, conversions, declarations, layout, reference probes | seven blocker classes found |
| CP3: architecture repair | canonical edge/cache model, declarator memo/frames, split interfaces, direct writer, telemetry | complete |
| CP4: performance and robustness | pre/post counters, Werror, ASan/UBSan, depth/array/using-chain stress | complete |
| CP5: final gates | zero-warning file audit, 67/67 PA8, 399/399 through PA8, clean diff/status | complete |

# PA39 Inception Plan

## First failing checkpoint

The initial blocker was `pa39SelfThroughPa10`: the host-seeded compiler could
build a `cppgm++-self`, but that compiler diverged while compiling the PA10
ladder.  PA39 adds no language surface, so each divergence was reduced to the
earliest owning assignment and fixed there.  The self ladder, pptoken
inception, and full compiler inception now pass.

Object probes established self miscompilation before changing source.  The
relevant earlier compiler surface was:

- PA10--PA26 parsing, semantic analysis, and lowering needed by the compiler's
  own C++11 source, including stable semantic-table IDs and explicitly
  sequenced LowIR emission.
- PA29 SysV register allocation and value materialization: incoming argument
  registers, zero-index remaps, frame addresses, call-result branches, and
  copied scalar values that remain live across calls.
- PA30 native exception lowering: protected calls with stack arguments require
  an unwind trampoline that restores the outgoing stack area before entering
  the source landing pad.
- PA37 optimization and bounded compilation: inlining uses a per-function
  instruction budget, escaped stack slots retain observable stores, and
  liveness/use-site indexes avoid self-only repeated-work growth.

The final pptoken divergence was traced to the self-built
`lowir_native.o`.  A copied comparison result retained a caller-saved source
register across a later call; call setup then replaced the value.  Copy reuse
is now permitted across a call only for callee-saved registers, with the PA29
regression preserving that lifetime directly.

## Relevant specification requirements

- Self and inception objects use the fixed source lists and are compiled by
  the immediately preceding compiler generation.
- The source-to-object path remains the in-process typed LowIR, bounded
  per-function machine-IR, and direct ELF pipeline; it does not invoke a host
  compiler or an earlier solution to produce compiler output.
- Host/self behavior, object bytes, and compilation resource use are the
  divergence evidence.  A self-only crash, changed branch, or unbounded
  repeated-work path is fixed in its owning compiler stage rather than avoided
  by rewriting otherwise valid compiler source.
- Register allocation preserves SysV values across calls, and host-EH paths
  establish the same stack state as the normal post-call path.
- Instruction order in LowIR is observable, and semantic analysis does not
  retain relocatable container pointers across recursive interning or template
  instantiation.
- Scratch probes are diagnostic only.  Canonical clean rebuilds and byte
  comparisons are the reproducibility oracle.

## Architecture Review

- Representation/ownership tracing confirmed one production flow from compact
  tokens and syntax storage through canonical semantic IDs, typed LowIR,
  function-local MIR, and direct ELF.  Text renderers/parsers remain endpoint
  tools and do not transport production state.
- Identity tracing found PA15 reconstructing internal linkage from semantic
  structure and anonymous-namespace spelling.  Linkage ownership was moved to
  semantic analysis and published through a typed dependency worklist; PA15 is
  now only a consumer.  Canonical identity flows one way into aliases/redecls,
  so an internal alias cannot contaminate an external declaration.
- Lookup tracing found two cached-lambda paths that converted `NameId` to
  spelling.  Both now use canonical `NamePath` lookup.
- Scaling tracing found repeated nested-body scans in PA10 name preindexing.
  A PA10-owned brace index restores linear source traversal.  Native backend
  tracing found an output-affecting `unordered_map` choice; source parameter
  order now supplies deterministic selection.
- Template/demand, lowering, backend, allocation, and build-graph review found
  no PA39-only path, global retry, textual recovery fallback, host/reference
  delegation, skipped correctness work, weakened comparison, or resource-limit
  exception.  All 49 reducers remain at their earliest observable assignment.

## Validation

- Focused regressions live in their earliest owning PA directories and pass as
  part of the through-PA38 report.
- `perl scripts/cppgm_file_audit.pl --stage pa39 --paths dev/src` passes with
  no fatal findings.
- `make test-report-through-pa38` passes all 5,138 tests.
- `make -C pa39 test-through-pa10 CXX=../dev/cppgm++ CPPGM_HOST_CXX=g++`
  passes the self ladder.
- `make -C pa39 compare-pptoken-inception CXX=../dev/cppgm++ CPPGM_HOST_CXX=g++`
  reports `MATCH pptoken`.
- `make -C pa39 compare-cppgm++-inception CXX=../dev/cppgm++ CPPGM_HOST_CXX=g++`
  reports matching objects and `MATCH cppgm++`.

## Final Architecture Review

The review findings are closed.  The final manifest has 136 shared compiler
sources plus the entry and test-runner objects.  All 138 host-seeded/inception
objects are byte-identical, and the final compiler and pptoken binaries have
matching sizes and SHA-256 hashes across generations.  Fresh PA1--PA38 tests,
self-built PA1--PA10 tests, both inception compares, and the file audit pass
without a timeout, OOM workaround, retry path, restored artifact, or relaxed
check.  The remaining 23 file-audit messages are pre-existing advisory
header-division warnings, not fatal findings or new PA39 ownership debt.

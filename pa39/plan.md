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

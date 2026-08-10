# PA22 Checkpoint Audit

## Current Checkpoint Review

The bounded review covers landed checkpoint `c230676a`: return-independent
function-template ordering, inactive anonymous-union construction, and
reference-bound aggregate prvalues. It does not claim the next captureless-closure
checkpoint. The three landed PA22 cases remain fixed and the full stage remains at
its 303/310 pass baseline.

The ordering path is retained function-template syntax -> canonical pattern and
argument IDs -> viable candidate-local comparison -> selected binding and conversion
facts -> ordinary demand/lowering. The audit reproduced one correctness leak: after
result-type equality was removed, the legacy “fewer template parameters” fallback
could select one of two mutually incomparable parameter patterns. The comparator now
permits that equality-constraint fallback only when both patterns accept each other;
canonical arguments of the same class-template entity are compared structurally by
typed identity. A crossed `box<T>, int` versus `box<int>, U` probe is rejected as
ambiguous, while `box<T>, box<T>` remains preferred to `box<T>, box<U>` even with
different result types. The earlier PA19 nested-type ordering regression also passes.

The other paths retain explicit ownership facts. Class- and block-scope anonymous
unions mark their canonical synthetic storage binding; constructor action building
skips only absent initialization of storage with no default active member, matching
N3485 §12.6.2 without a generated-name shortcut. Aggregate functional casts inspect
the canonical reference target and publish a typed temporary before binding, so
lifetime and lowering consume the same semantic node. These paths add no token
reparse, rendered semantic key, global lookup, external compiler, or alternate LowIR
route.

A five-run 16/32/64 combined family produced semantic medians of
8.819/16.746/32.872 ms, lowering medians of 2.177/4.294/8.455 ms, and peak semantic
storage of 1.661/3.029/6.008 MiB. Semantic nodes were 1027/2035/4051, overload
candidates 353/705/1409, order comparisons 96/192/384, constructor member actions
16/32/64, specialization requests 261/517/1029, demand pushes 97/193/385, and
instructions 442/874/1738. The representative work and storage are linear, with no
unrelated-candidate scan or repeated emission signal.

Validation preserves the checkpoint baseline: PA22 is 303/310 with the same seven
next-owner failures, PA1–PA21 pass 2329/2329, and the PA22 file audit passes with only
the pre-existing 13 header-division advisories.

## Checkpoint Audit Ledger

| Checkpoint | Audit disposition | Evidence |
|---|---|---|
| `5d70a120` canonical partial-specialization selection | Pass after checkpoint repair | Selected owner/revision/substitution survives shell completion; canonical typed identity and pack shape are retained; PA22 103 -> 111 with no regressions, prior 2329/2329, file audit pass. |
| `da807b9f` member-template attachment | Pass after checkpoint repair | Distinct template heads retain identity and each explicit call rebuilds its current specialization set; focused 10/10, PA22 145/310 with the original failures unchanged, prior 2329/2329, linear 16/32/64 evidence, file audit pass. |
| `c230676a` retained call/declaration acceptance | Pass after checkpoint repair | Mutually comparable typed parameter patterns, explicit anonymous-union provenance, and typed aggregate-prvalue lifetime preserve PA22 303/310 and prior 2329/2329; 16/32/64 work is linear and file audit passes. |

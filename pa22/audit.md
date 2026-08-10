# PA22 Checkpoint Audit

## Current Checkpoint Review

The bounded review covers landed checkpoint `3c0c79e2`, specialized-member scalar
conversion facts. An isolated parent build at `c71d08de` reproduced 305/310 and the
two widening-literal diffs; the checkpoint and repaired tree are 307/310, with both
cases passing and the exact three later-owner failures unchanged. `ApplyTarget`
computes the converted constant and transient canonical target `TypeId`; assignment
construction verifies the typed literal, integral source/target, and canonical member
specialization owner; `GraphLowerer` consumes the assignment-owned fact to emit the
already converted target-width immediate without lookup or type reconstruction.

The audit found one ownership/allocation defect in the landed representation: every
semantic `DumpNode` carried a second persistent target `TypeId`, although the
assignment node's canonical `type` already owns that target. This duplicated a fact
across the phase boundary and violated `spec.md` §§2 and 8's record-once/minimal-fact
requirements. The repair retains the target only in transient `ExpressionInfo` for
analysis-time validation and publishes one packed `target_typed_scalar_immediate`
bit on the assignment. The complete affected path uses only typed node kinds and
canonical `TypeId`/`BindingId`/`EntityId` facts; it has no rendered key, name or test
recognizer, unrelated registry scan, retry, invalidation, timeout exception, host or
reference invocation, or alternate LowIR route.

Five-run 16/32/64 assignment probes report 200/392/776 semantic nodes,
199/391/775 edges, 197/389/773 demand visits, 16/32/64 specialization requests with
15/31/63 hits, 17/33/65 conversion checks, and 65/129/257 instructions. Semantic peak
storage is 121,981/229,965/446,637 bytes, down from the landed
124,051/234,083/454,851 while typed storage remains 17,983/34,351/67,087 bytes;
semantic/lowering medians are 0.641/0.125, 0.990/0.171, and 1.675/0.286 ms. Work,
output, storage, and time are linear. PA1–PA21 pass 2329/2329; the required PA22
report preserves 307/310 and its exact residual set; focused cases pass 2/2; and file
audit passes with the same 13 advisory header-division warnings.

## Checkpoint Audit Ledger

| Checkpoint | Audit disposition | Evidence |
|---|---|---|
| `5d70a120` canonical partial-specialization selection | Pass after checkpoint repair | Selected owner/revision/substitution survives shell completion; canonical typed identity and pack shape are retained; PA22 103 -> 111 with no regressions, prior 2329/2329, file audit pass. |
| `da807b9f` member-template attachment | Pass after checkpoint repair | Distinct template heads retain identity and each explicit call rebuilds its current specialization set; focused 10/10, PA22 145/310 with the original failures unchanged, prior 2329/2329, linear 16/32/64 evidence, file audit pass. |
| `c230676a` retained call/declaration acceptance | Pass after checkpoint repair | Mutually comparable typed parameter patterns, explicit anonymous-union provenance, and typed aggregate-prvalue lifetime preserve PA22 303/310 and prior 2329/2329; 16/32/64 work is linear and file audit passes. |
| `cc87146e` canonical captureless closures | Pass after checkpoint repair | Callable-owned closure/ABI facts and graph-derived return cleanup preserve PA22 304/310 and prior 2329/2329; focused and mixed-owner probes pass, 16/32/64 work is linear, and file audit passes. |
| `605d7e99` discarded-result provenance and ordered demand | Pass after telemetry repair | Canonical temporary/constructor ownership and one source-order visit per reachable node advance PA22 304 -> 305; focused 23/23, prior 2329/2329, linear 16/32/64 evidence, and file audit pass. |
| `3c0c79e2` specialized-member scalar conversions | Pass after compact-fact repair | One assignment-owned immediate bit replaces the duplicate persistent target while preserving PA22 305 -> 307, prior 2329/2329, linear 16/32/64 evidence, and file audit pass. |

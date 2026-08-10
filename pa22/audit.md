# PA22 Checkpoint Audit

## Current Checkpoint Review

The bounded review covers landed checkpoint `5c2f9691`, specialization-owned
local-static initialization. Its parent reproduces 307/310 and the focused static case
fails; the landed and repaired trees are 308/310 with that case passing and the exact two
turn-start hidden-friend diffs unchanged. `AddLocalStaticObjectAction` owns the canonical
function `BindingId`, declaration ordinal, initializer root, and one recipe bit;
`GraphLowerer` emits one zero global and guarded typed initializer that consumes the
already-selected function-address bindings.

The audit found four connected ownership defects. Source-derived display text had also
become the weak ABI object identity; the post-token adapter retained a borrowed filename
pointer; packed presentation overflow rejected otherwise valid input; and initializer
analysis retained vector/node references across demand that may instantiate entities.
The last defect made a representative 64-static workload crash in
`AnalyzeAggregateInit`. The repair keeps ABI identity at canonical function plus
declaration ordinal while source location is display-only, owns callback filename state,
falls back to canonical display when provenance cannot be packed, snapshots initializer
node IDs before demand, and snapshots aggregate `BindingId`/`TypeId`/`NameId` facts before
recursive analysis. The complete path has no rendered semantic key, test/source
recognizer, global scan/retry, timeout exception, external compiler/reference call, or
alternate LowIR route.

Five-run 16/32/64 probes report 418/818/1618 semantic nodes,
287/559/1103 edges, 112/224/448 initializer visits, 132/260/516 specialization requests
with 99/195/387 hits, and 268/524/1036 instructions. Typed/semantic peak storage is
109,115/668,899, 215,227/1,329,971, and 427,564/2,652,159 bytes; semantic/lowering
medians are 3.248/0.686, 6.171/1.204, and 12.207/2.232 ms. Work, output, storage, and
time are linear; 64 passes 5/5 and is Valgrind-clean. Compiling the focused source through
two relative path spellings changes its display name but preserves identical `object=`
identity. Focused and adjacent cases pass 4/4, a 66,000-column fallback probe passes,
PA1–PA21 pass 2329/2329, PA22 preserves 308/310 with the same residual set, and file
audit passes with 13 advisory warnings.

## Checkpoint Audit Ledger

| Checkpoint | Audit disposition | Evidence |
|---|---|---|
| `5d70a120` canonical partial-specialization selection | Pass after checkpoint repair | Selected owner/revision/substitution survives shell completion; canonical typed identity and pack shape are retained; PA22 103 -> 111 with no regressions, prior 2329/2329, file audit pass. |
| `da807b9f` member-template attachment | Pass after checkpoint repair | Distinct template heads retain identity and each explicit call rebuilds its current specialization set; focused 10/10, PA22 145/310 with the original failures unchanged, prior 2329/2329, linear 16/32/64 evidence, file audit pass. |
| `c230676a` retained call/declaration acceptance | Pass after checkpoint repair | Mutually comparable typed parameter patterns, explicit anonymous-union provenance, and typed aggregate-prvalue lifetime preserve PA22 303/310 and prior 2329/2329; 16/32/64 work is linear and file audit passes. |
| `cc87146e` canonical captureless closures | Pass after checkpoint repair | Callable-owned closure/ABI facts and graph-derived return cleanup preserve PA22 304/310 and prior 2329/2329; focused and mixed-owner probes pass, 16/32/64 work is linear, and file audit passes. |
| `605d7e99` discarded-result provenance and ordered demand | Pass after telemetry repair | Canonical temporary/constructor ownership and one source-order visit per reachable node advance PA22 304 -> 305; focused 23/23, prior 2329/2329, linear 16/32/64 evidence, and file audit pass. |
| `3c0c79e2` specialized-member scalar conversions | Pass after compact-fact repair | One assignment-owned immediate bit replaces the duplicate persistent target while preserving PA22 305 -> 307, prior 2329/2329, linear 16/32/64 evidence, and file audit pass. |
| `5c2f9691` specialization-owned local statics | Pass after ownership/lifetime repair | Canonical ABI identity is separate from source presentation and recursive initializer analysis retains compact facts across specialization growth; PA22 remains 308/310, prior 2329/2329, repaired 16/32/64 work is linear and 64 is Valgrind-clean, and file audit passes. |

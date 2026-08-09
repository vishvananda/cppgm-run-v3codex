# PA21 Checkpoint Audit

## Current Checkpoint Review

The landed `44134d03` increment raised the PA21 handout baseline from 80/131 to
87/132 by constructing ordered direct-base values, evaluating base and
delegating initializers, and projecting immutable objects for base member calls
and references. The increment conflated the complete object with its selected
active subobject, however. Equal-valued repeated nonvirtual bases could compare
as one occurrence, converted receivers retained an unadjusted address, and
frames, reference locals/results, and completed-call keys did not carry the
selected branch through the full call path. That violated the canonical
identity, complete-key, recorded-conversion, lifetime, and observability rules
in `spec.md` §§2–3,5–6,8–9.

The repaired ownership path keeps two compact identities: the immutable
complete object and the active subobject selected by a recorded base
conversion. Constructor completion still interns members followed by direct
bases; projection walks only those typed base edges, treats a second matching
occurrence as ambiguous even when structural interning yields the same value
ID, and accumulates the layout's allocation-relative base offset. The selected
object, complete object, and adjusted address then travel together through the
converted receiver, `this`, reference locals and arguments, return facts, and
the call key/result cache. Inherited-constructor evaluation identifies its base
by the source constructor's owning entity rather than by an ordinal
assumption. Ordinary overload resolution remains the sole owner of selection
and conversions, and lowering consumes the resulting facts without semantic
replay, rendered-name lookup, text transport, external tools, or test-specific
recognition.

Release depth probes at 8/16/32/64 levels recorded 15/31/63/127 constructor-base
visits, 9/17/33/65 object-projection visits, 11/19/35/67 evaluator steps,
41/81/161/321 scratch nodes, and 123/235/459/907 LowIR instructions. Typed
storage was 41,484/80,246/157,016/311,576 bytes and peak-stage storage was
276,966/551,103/1,135,047/2,429,119 bytes, supporting linear work and storage in
base depth. The repeated-base regression makes two calls through distinct
branches and reports two requests, zero cache hits, 14 projection visits, 14
steps, four scratch nodes, and one emitted instruction; the zero hits are the
required separation of different receiver identities.

Validation preserves all 87/132 landed handout passes and adds the audit
regression for 88/133. The required PA1–PA20 report passes 2,185/2,185, and the
PA21 file audit passes with the same 12 header-division advisories. The unchanged
45 handout failures remain assigned to later PA21 checkpoints.

## Checkpoint Audit Ledger

| Checkpoint | Audit disposition | Evidence |
| --- | --- | --- |
| `dd3dd301` integral scalar invocation and demand | Pass after ownership repair | stack/scratch ownership, namespace-mutation rejection, fixed canonical graph, linear counters, PA1–20 clean, PA21 baseline preserved |
| `d4d44664` class-valued calls and conversions | Pass after completion-boundary repair | complete typed call keys/object results, transitive local-address escape rejection, constant repeated-call work, PA1–20 and checkpoint baselines preserved |
| `44134d03` base-subobject completion | Pass after active-subobject ownership repair | complete/active IDs and adjusted addresses through projection, calls, references, and caches; linear depth counters; PA1–20 and checkpoint baselines preserved |

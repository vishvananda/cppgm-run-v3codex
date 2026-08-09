# PA21 Checkpoint Audit

## Current Checkpoint Review

The landed `149f92db` increment raised the combined PA21 baseline from 88/133 to
100/133 by routing callable objects, overloaded operators, user/contextual
conversions, and scalar/reference/pointer/object results through ordinary
overload selection and the typed constexpr evaluator. The selected function,
receiver, converted arguments, and runtime demand were generally retained, but
the audit found three ownership breaks. Parser rollback left surviving syntax
nodes linked to abandoned edges and relied on a later append to repair them;
recursive `operator->` was used for data-member access but bypassed by direct
member calls and explicit destructor calls; and pointer/reference-valued call
results were deliberately excluded from the completed-call cache. The new
class-initializer path also dispatched through an operator-syntax whitelist,
and a constexpr callable surrogate discarded its already-selected argument
conversion facts before the indirect call. These violated `spec.md` §§1–6,8–10
requirements for bounded checkpoints, semantic rather than syntax-specific
behavior, one selected conversion owner, complete typed cache facts, and no
semantic replay.

The repaired path restores each speculative syntax-edge mutation in reverse at
rollback, then releases that compact parser journal before the syntax consumer.
One `ResolveArrowOperand` owner now repeatedly selects `operator->` until its
typed result is a pointer and feeds the resulting pointer/object/address facts
to data access, direct member-call overload resolution, or destructor lookup.
Class expression initialization analyzes every remaining expression shape by
semantics rather than a tag list. Callable-surrogate completion passes its
selected argument conversions to the canonical function-address consumer.
Finally, every completed call key owns the canonical function, complete/active
receiver and address, and typed scalar/object/address arguments; its success
fact now records the scalar-presence bit, address, active/complete object, or
scalar value for all return categories. Expected failures and recursive
in-progress states share that same owner. Lowering still consumes the selected
binding and recorded facts without lookup replay, rendered-name recovery, text
transport, external tools, or source/test recognition.

For 1/2/4/8 identical address-returning calls, release telemetry reports
1/2/4/8 requests, 0/1/3/7 cache hits, and a constant two evaluator steps, two
scratch nodes, 2,151 typed bytes, one LowIR instruction, and zero demanded
functions; semantic nodes grow only with the source uses at 12/18/30/54.
The same probes contain 41/48/62/90 retained syntax edges and use
1,165/1,222/1,334/2,326 parser bytes with geometric capacity growth; the
12-byte-per-edge rollback journal is released before semantic analysis.

Validation preserves all 100/133 handout passes and adds the recursive-arrow
and callable-surrogate audit regression for 101/134. The required PA1–PA20
report passes 2,185/2,185, and the PA21 file audit passes with the same 12
header-division advisories. The unchanged 33 handout failures remain assigned
to the checkpoint owners in `plan.md`.

## Checkpoint Audit Ledger

| Checkpoint | Audit disposition | Evidence |
| --- | --- | --- |
| `dd3dd301` integral scalar invocation and demand | Pass after ownership repair | stack/scratch ownership, namespace-mutation rejection, fixed canonical graph, linear counters, PA1–20 clean, PA21 baseline preserved |
| `d4d44664` class-valued calls and conversions | Pass after completion-boundary repair | complete typed call keys/object results, transitive local-address escape rejection, constant repeated-call work, PA1–20 and checkpoint baselines preserved |
| `44134d03` base-subobject completion | Pass after active-subobject ownership repair | complete/active IDs and adjusted addresses through projection, calls, references, and caches; linear depth counters; PA1–20 and checkpoint baselines preserved |
| `149f92db` callable and contextual conversions | Pass after parser/call ownership repair | exact rollback journal, shared recursive-arrow owner, semantic class initialization, retained surrogate conversions, cached address results; PA1–20 and checkpoint baselines preserved |

# PA21 Checkpoint Audit

## Current Checkpoint Review

The landed `49e62fbb` increment raised the combined PA21 report from 101/134 to
108/134 by completing canonical exception-specification facts, dependent
function-template specifications, and unevaluated `noexcept` expressions. The
audit found two correctness gaps in that owner: `IsNonthrowing` read an
expression's scalar field without performing the required contextual conversion
to `bool`, and the action walk omitted destruction of materialized class
temporaries. The former also evaluated specification-only constexpr calls
outside compile-time-only demand, and the walk exposed no direct work counter.
Those gaps violated PA21's declaration-validation and `noexcept` contract and
`spec.md` §§2–6,8–9 requirements for one selected conversion owner,
demand-separated completion, complete lifetime facts, and observable work.

The repaired declaration path keeps constant-expression-required depth active
through one shared contextual-bool conversion, so scalar, pointer, and explicit
class conversions produce a typed boolean fact without runtime emission demand.
Dependent specifications remain owned by the complete function-specialization
key and publish their result on the canonical binding. The `noexcept` operand
path suppresses evaluation and emission, consumes already-selected constructor,
call, allocation/deallocation, and destructor bindings, and now checks each
materialized temporary's canonical destructor before producing its literal
boolean result. A release counter records every node visited by that bounded
action walk. No path performs lookup during lowering, reconstructs semantics
from text or names, invokes an external tool, or recognizes a source/test.

For 1/2/4/8 successful temporary `noexcept` operands, semantic nodes were
8/11/17/29, nonthrowing-action visits were exactly 2/4/8/16, and overload
candidates were 2/4/8/16; conversion checks stayed at one, while demand pushes,
demanded functions, typed storage, and LowIR instructions stayed at
0/0/1,735/1. The repeated dependent probe retained 10/13/19/31 semantic nodes,
2/4/8/16 action visits, 6/10/18/34 specialization requests with 4/8/16/32
cache hits, two conversion checks, 1,735 typed bytes, one LowIR instruction,
and zero demanded functions. Required work is therefore linear in owned operand
nodes and source consumers, with exactly two specialization misses.

Validation preserves all 108/134 turn-start passes and adds the focused audit
regression for 109/135; all eight checkpoint/audit probes pass. PA1–PA20 passes
2,185/2,185, and the PA21 file audit passes with the same 12 header-division
advisories. The unchanged 26 handout failures remain assigned to later owners
in `plan.md`.

## Checkpoint Audit Ledger

| Checkpoint | Audit disposition | Evidence |
| --- | --- | --- |
| `dd3dd301` integral scalar invocation and demand | Pass after ownership repair | stack/scratch ownership, namespace-mutation rejection, fixed canonical graph, linear counters, PA1–20 clean, PA21 baseline preserved |
| `d4d44664` class-valued calls and conversions | Pass after completion-boundary repair | complete typed call keys/object results, transitive local-address escape rejection, constant repeated-call work, PA1–20 and checkpoint baselines preserved |
| `44134d03` base-subobject completion | Pass after active-subobject ownership repair | complete/active IDs and adjusted addresses through projection, calls, references, and caches; linear depth counters; PA1–20 and checkpoint baselines preserved |
| `149f92db` callable and contextual conversions | Pass after parser/call ownership repair | exact rollback journal, shared recursive-arrow owner, semantic class initialization, retained surrogate conversions, cached address results; PA1–20 and checkpoint baselines preserved |
| `49e62fbb` canonical exception and `noexcept` facts | Pass after conversion/lifetime ownership repair | shared contextual-bool fact, compile-time-only demand, temporary-destructor coverage, linear action counter; PA1–20 and 108/134 baseline preserved |

# PA21 Checkpoint Audit

## Current Checkpoint Review

The landed `d4d44664` class-valued call increment raised the PA21 checkpoint
baseline from 76/130 to 79/130 by carrying immutable object IDs through function
returns, converting constructor arguments, hidden-friend calls, and
constant-required consumers. Its completion boundary nevertheless had two
defects against `spec.md` §§2,4–5,8–10. Object results were deliberately excluded
from the completed-call cache, so identical calls replayed retained syntax, and
a returned object could retain an address owned by the completed invocation.
Immediate member access could therefore accept a dangling local pointer in a
`static_assert`.

The repaired path starts with the selected canonical function and recorded
conversions, builds one typed call key from the function binding, receiver, and
each normalized scalar/object/address argument, evaluates in a scratch dump and
frame-owned local overlay, and publishes only a completed scalar or interned
immutable object fact. Each frame records its first local-storage identity;
every interned object records the newest such identity reachable through nested
object/address elements. Constructor and function completion reject direct or
transitive addresses created within that invocation before a fact can escape or
be cached. Runtime ODR-use alone creates emission demand, while the
constant-only hidden-friend path remains evaluator-owned. Retained function and
template syntax is reused directly, member/object lookup uses binding IDs and
ordinals, and neither evaluation nor lowering introduces a text round trip,
whole-program scan, external tool, or spelling-based shortcut.

The existing `skip()` object-return/template probe changed from 9 requests,
1 hit, and 16 evaluator steps to 9 requests, 7 hits, and 4 steps. A release
1/2/4/8-use probe with class results and object-reference arguments reported
1/2/4/8 requests, 0/1/3/7 hits, fixed 5 evaluator steps, 17 scratch nodes,
5,294 typed-storage bytes, and 8 LowIR instructions. Semantic nodes grew
33/41/57/89 and peak-stage bytes 48,199/48,923/55,795/66,083 with the repeated
source consumers; completed evaluation work stayed constant after the first
complete key. A negative regression now rejects a class result whose nested
pointer names invocation-local storage.

Validation preserves the original 79/130 stage baseline and passes the new
regression for 80/131 overall. The required PA1–PA20 report passes 2,185/2,185;
the PA21 file audit passes with only its 12 pre-existing header-division
advisories. The same 51 handout failures remain assigned to later PA21
checkpoints, so this audit neither masks nor advances unrelated stage work.

## Checkpoint Audit Ledger

| Checkpoint | Audit disposition | Evidence |
| --- | --- | --- |
| `dd3dd301` integral scalar invocation and demand | Pass after ownership repair | stack/scratch ownership, namespace-mutation rejection, fixed canonical graph, linear counters, PA1–20 clean, PA21 baseline preserved |
| `d4d44664` class-valued calls and conversions | Pass after completion-boundary repair | complete typed call keys/object results, transitive local-address escape rejection, constant repeated-call work, PA1–20 and checkpoint baselines preserved |

# PA21 Checkpoint Audit

## Current Checkpoint Review

The landed `dd3dd301` integral scalar invocation increment raised the PA21
handout baseline from 20/129 to 41/129, but its invocation-local ownership claim
was not true. Each cache miss appended function, block, condition, and loop
scopes plus parameter/local bindings to the canonical `Program`; assignment
and increment updated `BindingRecord::constant/value` directly. Consequently,
attempted constant evaluation could mutate namespace storage, and recursive
evaluation retained its temporary semantic nodes. This violated `spec.md`
§§2,4,8–10 even though the checkpoint tests passed.

The complete scalar path now runs from retained function syntax and canonical
parameter types through stack-owned invocation values, block-scoped type/using
overlays, a complete canonical function/type/value call key, and a reusable
scratch dump arena. Lookup checks the overlay without publishing transient
scope indexes; only completed call facts escape. Mutating operators accept only
invocation-local identities, emission demand is suppressed while interpreting,
and constant-required consumers receive the folded typed value. One course
regression proves that attempted namespace mutation is rejected. Local typedef,
using-directive, function-pack, PA20 static-member, template-argument,
`static_assert`, and LowIR consumers remain intact.

Representative release probes at recursion depths 32/64/128/256 reported
66/130/258/514 steps, 33/65/129/257 peak locals, and
332/652/1,292/2,572 scratch nodes. Canonical scopes, declarations, and retained
semantic nodes stayed fixed at 5, 7, and 11; peak semantic-stage bytes were
105,030/180,934/333,924/643,556. Repeating depth 128 produced one cache hit and
left work at 258 steps. Required work and temporary storage are linear in
executed recursion depth, with no retained canonical-graph growth.

Validation is clean for the checkpoint: PA1–PA20 pass 2,185/2,185; PA21 keeps
all 41/129 handout passes and adds the audit regression for 42/130 overall; the
PA21 file audit passes with only the 12 pre-existing header-division advisories.
The remaining PA21 failures belong to later typed scalar, object/pointer, call,
validation, and local-static checkpoints, not this landed integral increment.

## Checkpoint Audit Ledger

| Checkpoint | Audit disposition | Evidence |
| --- | --- | --- |
| `dd3dd301` integral scalar invocation and demand | Pass after ownership repair | stack/scratch ownership, namespace-mutation rejection, fixed canonical graph, linear counters, PA1–20 clean, PA21 baseline preserved |

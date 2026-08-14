# PA35 Checkpoint Audit

## Current Checkpoint Review

The audit covered checkpoint commit `ab8d37e6`, its four changed semantic
modules, `spec.md`, the PA35 contract, the active plan, relevant tests, and the
primary failure log. The landed increment correctly restores retained
current-class context, predeclares the canonical owner's members and enclosing
parameters for qualified definitions, defers unresolved function-template
names through dependent type formation, and carries enclosing pack arguments
into canonical result identity. It advances PA35 compile coverage from 41 to
48/103 without changing the 4,756-test PA1-34 baseline.

One checkpoint-level §4/§9 violation was found and repaired. Each enclosing
pack had been copied into a parent-linked result environment, while discovery
performed a lexical-scope walk for each distinct result name. A result using
`d` independent enclosing packs therefore performed quadratic environment
probing. The complete ownership path is now retained template validation ->
canonical pack publication -> direct/per-scope pack index -> one lexical-owner
walk -> flat request-local result bindings -> canonical result identity -> ABI
publication/comparison. `TemplateArgumentPackBindingTable` stores each
`(scope, name)` fact once, links it into its dense scope index, and rejects
duplicate empty as well as nonempty bindings. Alias/default substitutions keep
their immutable parent overlays; enclosing packs no longer do.

On nested retained-owner depths 32/64, environment probes fell from 695/2,472
to 162/322. The corrected syntax counter is 202/394 because it now includes the
discovery walk; median semantic time is 18.85/67.93 ms, wall time 0.02/0.08 s,
peak RSS 8.7/10.6 MiB, and semantic peak storage 1.17/3.80 MB. The affected work
is linear in pack depth and remains proportional to the created semantic graph.

Validation is clean at the checkpoint boundary: the required PA35 report
preserves 48/103 compile passes and the same 55 known failures (48/104 when the
one out-of-target run test is included); PA1-34 passes 4,756/4,756; and the PA35
file audit passes with 22 inherited nonfatal header-division warnings. No other
landed correctness, ownership, bounded-work, self-containment, or file-audit
blocker remains.

## Checkpoint Audit Ledger

| Checkpoint commits | Audit disposition |
| --- | --- |
| `ab8d37e6` | Pass after consolidating retained-pack publication and lookup into one canonical direct/per-scope index; correctness baseline preserved. |

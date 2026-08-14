# PA35 Plan

## Stage Design and Spec Alignment

PA35 keeps the source -> streaming preprocessing/post-tokenization -> integrated
syntax/semantics -> typed LowIR -> native ELF architecture. Relevant `spec.md`
requirements are canonical typed identity and phase flow (§2, §6), demand-owned
template specialization (§4), and bounded, observable heavy-header work (§9).
The shared front end owns all changes; no hosted-only route is introduced.

Canonical template-argument shapes now own readiness: retained nested template
parameters remain deferred, while direct named-type demand remains the
authoritative completion path. Retained member replay borrows the canonical
specialization's class identity for lookup/access. An in-class defaulted
destructor publishes its implicit exception specification through the existing
function demand state and computes it from completed base/member facts only
when queried. The shape walk is isolated in a responsibility-named PA19 module.

## Current Failure Map

Current PA35 is 60/103 compile tests, up from 53/103, with 43 failures. The
complete remaining set groups by first owning behavior: explicit
specialization/instantiation identity 7; inherited alias/type identity 13;
expression/call/body demand 14; stream/heap stability 5; native object/register
lowering 3; and retained special-member exception identity 1.

## Active Checkpoint

**Next: canonical inherited alias/type identity.** Per `spec.md` §§2-5, retained
aliases and qualified nested types must resolve against the selected canonical
specialization, not a lexical pattern shell. Data should flow from retained
qualified syntax -> specialization-owned substitution -> indexed base/member
lookup -> one canonical type fact. Template semantic lookup owns the state;
overload and lowering consume it without retries. Expected work is O(visited
qualified components + base edges), memoized per specialization. Validate the
13-case `key_type`/`_Elements`/incomplete-name cluster, PA35, PA1-34, audit, and
doubled independent alias families.

## Performance Evidence

For 16/32 independent `std::pair` trait families, constexpr calls were 160/320,
cache hits 64/128, and evaluator steps 305/609. Template requests were
4,676/9,028; semantic time 212/431 ms, peak semantic storage 30.8/58.5 MB,
wall time 0.34/0.59 s, and peak RSS 32,092/56,772 KiB. Successful assertions
now perform no diagnostic rendering; demanded evaluation scales linearly and
reuses the canonical call cache.

For 8/16 independent nested defaulted-destructor families, template requests
were 1,276/2,292, cache hits 771/1,427, lookup queries 9,812/15,524, constexpr
calls 16/32, and steps 40/80. Semantic time was 58.3/83.8 ms, peak semantic
storage 10.7/16.2 MB, wall time 0.11/0.13 s, and RSS 16,836/21,240 KiB. The
shape walk visits only reachable type nodes; all doubled-family factors stayed
at or below 2x.

## Completed Checkpoints

| Checkpoint | Result | Validation |
|---|---|---|
| Hosted front-end ingress | PA35 6 -> 15/103 | focused utility/probes; PA1-34 and audit pass |
| Hosted attributed/template syntax | shared syntax barriers removed; PA35 15/103 | decltype-shift; parser scaling linear |
| Canonical using-function merge | 35 duplicate-import barriers removed; PA35 15/103 | repeated/distinct imports; scaling linear |
| Hosted class-template registration | 49 registration barriers removed; PA35 15/103 | attributed partial identity; scaling linear |
| Dependent nested-type lookup | prior 55-case barrier removed; PA35 15 -> 17/103 | focused shape replay and allocator cases advance |
| Dependent call/type disambiguation | dependent calls remain calls; scalar cast set completed | PA35 17 -> 18/103; qualified-base regressions pass |
| Explicit-instantiation operator identity | 36 routing barriers removed; PA35 18/103 | operator and genuine template-id targets distinct |
| Constexpr declaration/completion | 16 owner-literal barriers removed; PA35 18/103 | member/base/object negatives remain rejected |
| Retained template-default access | 19 access barriers removed; PA35 18/103 | member/friend defaults; external private alias rejected |
| Canonical retained declarations | tag/control/function/exception facts merge; PA35 18 -> 19/103 | 16 barriers advance; PA1-34, audit, scaling pass |
| Qualified nested-member replay | canonical current specialization, definition parameters, retained operator call, constexpr string array, and local-class access | PA35 19 -> 21/103; PA1-34 4756/4756; audit/focused/scaling pass |
| Demand-safe declarations and function-local lowering | incomplete parameter ABI deferred; nested cleanup bounded; shared node/binding slots reset per function; direct class calls retained as objects | PA35 21 -> 36/103; PA1-34 4756/4756; focused/scaling/audit pass |
| Target-typed casts and bounded hosted shapes | braced casts, specialization-local constexpr, symbolic packs, runtime atomic order, and cv-overloaded special members | PA35 36 -> 41/103; PA1-34 4756/4756; behavior/scaling/audit pass |
| Retained current-class ownership | canonical owner/member view, enclosing packs, and member-template result calls; audited pack facts use one direct/per-scope index | PA35 41 -> 48/103 compile (48/104 tracked); PA1-34 4756/4756; retained-pack probes linear; file audit pass |
| Mandatory static-assert evaluation roots | assertion-local suppression, compact provenance with error-only rendering, and compile-path constexpr counters | PA35 48 -> 53/103; five pass and 16 advance; PA1-34 4756/4756; focused/scaling/audit pass |
| Canonical completed-type trait demand | nested dependent shapes defer at readiness; member replay uses specialization access; defaulted-destructor exception facts are lazy | PA35 53 -> 60/103; all 13 barriers advance, seven tests pass; PA1-34 4756/4756; scaling/audit pass |

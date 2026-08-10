# PA22 Checkpoint Audit

## Current Checkpoint Review

The bounded review covers landed checkpoint `cc87146e`, canonical captureless
closures. Retained lambda syntax and the canonical enclosing function form one
compact indexed key; the resulting entity owns its typed call operator, local ABI
context, and source-order ordinal. Canonical template arguments carry that closure
type through deduction and demand, and ABI lowering consumes the entity/function IDs
without using the rendered closure name as a semantic key. The focused chained
two-lambda case records two closure requests, zero hits, distinct ABI discriminators,
and one demanded call operator.

The audit found two ownership leaks. A call with a closure argument had mutated a
class-wide result-ABI bit, so unrelated functions returning the same class could
change boundary after the call was analyzed. Function-template instantiation now
records closure participation once on the complete canonical specialization binding;
only that binding may own the indirect-result exception, and return-slot analysis plus
every call/function lowering consumer queries `(result TypeId, canonical BindingId)`.
A mixed probe leaves the ordinary function direct while its closure specialization is
indirect. The return path had also recognized closure entities to enable unwind
cleanup and reorder slots. It now derives the fact from the typed graph: a tracked
temporary nested below an enclosing call publishes one managed cleanup region,
including lexical unwind actions, and slot planning consumes that fact. The same
non-lambda call chain now installs its destructor dispatch.

The repaired path adds no token replay, rendered semantic key, namespace/program
scan, global invalidation, reference/host compiler, timeout exception, or alternate
LowIR route. Five-run 16/32/64 macro-expansion medians were
1.723/3.187/6.806 ms semantic and 0.752/1.236/2.306 ms lowering, with peak semantic
storage 0.274/0.532/1.007 MiB. Nodes were 431/847/1679, closure requests 16/32/64
(zero hits because every occurrence is distinct), temporary-dependency visits
265/521/1033, functions 33/65/129, and instructions 238/462/910. Work and storage are
linear in concrete closures and output. A separate 16/32/64 nested-return family
reported nodes 115/195/355, temporary visits 79/143/271, cleanup entries 17/33/65,
instructions 138/250/474, and semantic/lowering medians
0.392/0.534/0.779 and 0.276/0.325/0.480 ms; managed cleanup is likewise linear.

Validation preserves the turn-start baseline: the focused checkpoint passes; PA22 is
304/310 with the identical six next-owner failures; PA1–PA21 pass 2329/2329; and the
file audit passes with only the pre-existing 13 header-division advisories.

## Checkpoint Audit Ledger

| Checkpoint | Audit disposition | Evidence |
|---|---|---|
| `5d70a120` canonical partial-specialization selection | Pass after checkpoint repair | Selected owner/revision/substitution survives shell completion; canonical typed identity and pack shape are retained; PA22 103 -> 111 with no regressions, prior 2329/2329, file audit pass. |
| `da807b9f` member-template attachment | Pass after checkpoint repair | Distinct template heads retain identity and each explicit call rebuilds its current specialization set; focused 10/10, PA22 145/310 with the original failures unchanged, prior 2329/2329, linear 16/32/64 evidence, file audit pass. |
| `c230676a` retained call/declaration acceptance | Pass after checkpoint repair | Mutually comparable typed parameter patterns, explicit anonymous-union provenance, and typed aggregate-prvalue lifetime preserve PA22 303/310 and prior 2329/2329; 16/32/64 work is linear and file audit passes. |
| `cc87146e` canonical captureless closures | Pass after checkpoint repair | Callable-owned closure/ABI facts and graph-derived return cleanup preserve PA22 304/310 and prior 2329/2329; focused and mixed-owner probes pass, 16/32/64 work is linear, and file audit passes. |

# PA23 Final Audit Plan

## Stage Design and Spec Alignment

PA23 completes function-template deduction, ordering, substitution, SFINAE,
constructor/conversion participation, dependent calls, and demand-owned body
materialization. Its production surface ends at typed LowIR: source is parsed
once into a phase-local `SyntaxArena`, semantic analysis records canonical
types, declarations, specialization states, lookup/conversion facts, and
demanded bodies in `SemanticGraphStorage`, then the syntax/analyzer scratch is
destroyed before `LowerSemanticGraph` consumes a borrowed graph view. LowIR is
constructed in memory and rendered once as the PA23 tool result; backend and
ELF checks are outside this assignment surface.

The final architecture uses compact IDs for names, syntax tags/payloads,
types, declarations, entities, template arguments, result identities, and
emission units. Owner-local indexes supply lookup and overload candidates.
Specialization/default/exception/body states are monotonic and keyed by typed
identity. Recoverable candidate rejection is a compact scoped state, while
hard semantic failures still throw. Selected declarations, conversions,
layouts, lifetime actions, ABI facts, and demand facts cross into lowering
without semantic relookup or a textual transport.

## Performance Evidence

Alias/direct redeclaration pairs at 1/2/4/8/16/32/64 pairs produced identity
requests 2/4/8/16/32/64/128, cache hits 1/3/7/15/31/63/127, index probes equal
to requests, and atom visits 24/56/120/248/504/1,016/2,040. Semantic time was
approximately 0.30/0.36/0.47/0.78/1.23/2.26/4.21 ms, supporting linear work.

Nested alias depth 1/2/4/8/16/32 originally exposed 12/27/75/243/867/3,267
environment probes. A sparse name index ahead of immutable parent overlays
reduced the final series to 6/9/15/27/51/99. The final depth-32 run reports 232
syntax visits, 96 environment probes, 32 alias expansions, two interner
requests, one cache hit, two index probes, and 24 atom visits.

The representative checked redeclaration has 326 tokens, 51 semantic nodes,
21 lowered instructions, two result-identity requests with one hit, one
demand-worklist push, one demanded specialization emission, and 868 output
bytes. Final measured times were 1.68 ms semantic, 0.18 ms lowering, and 0.04
ms rendering.

## Architecture Review

- Representation and ownership: one phase-local parsed graph feeds semantic
  construction; parser, syntax, lookup, substitution, and demand scratch die
  before lowering. No rendered text is parsed back into semantics or LowIR.
- Identity and lookup: result redeclaration equality now interns typed atom
  sequences in a flat open-addressed table. Structured owner equivalence uses
  interned syntax/name IDs and canonical `TypeId`/declaration identity, not
  rendered source or token rescanning. Candidate-qualified ambiguity has an
  explicit typed lookup result.
- Templates and repeated work: alias environments are immutable parent-linked
  frames, specialization/default requests retain complete monotonic states,
  and recoverable expression/type/pack/layout failure propagates through the
  candidate frame rather than `runtime_error` catches.
- Lowering: demanded semantic graph nodes carry selected binding, conversion,
  object/lifetime, layout, ABI, and emission facts. Lowering performs no
  overload lookup, source parsing, LowIR reparsing, or compiler fallback.
- Allocation and scaling: hot identity storage is flat vectors plus an
  open-addressed index; environments borrow retained syntax and avoid copying
  visible maps. Release telemetry exposes requests, hits, probes, visits,
  expansions, phase bytes, phase time, candidate work, demand, and IR size.
- Self-containment: the production `cppgm++` source set contains no host or
  reference compiler invocation and no test/source-spelling dispatch.

## Final Architecture Review

No open PA23 correctness, architecture, performance, self-containment, or
file-audit blocker remains. The audit removed the two text-based semantic
identity paths, the measured quadratic alias-environment miss path, and broad
exception-based candidate classification. The file audit retains only its 13
pre-existing header-division advisories; no new source file or source-set
entry was required.

## Checkpoint Ledger

| Checkpoint | Commits | Final disposition |
| --- | --- | --- |
| Array bounds, defaults, and expression substitution | `b6d38290`-`d2b7ff91` | Canonical deductions and candidate-owned failure retained; complete-key default states pass. |
| Partial replay, packs, and lazy class identity | `0b81ecbc`-`c0704231` | Dependent shapes replay once after deduction; pack partitions and class demand remain typed and monotonic. |
| Calls, designators, initialization, and explicit specialization | `a69c8d5d`-`624c9995` | Target-aware selection and typed initialization publish facts consumed directly by lowering. |
| Result lookup, aliases, and candidate validity | `b71f3a5d`-`84a3f7c5` | First-declaration lookup survives replay; final audit replaces text equality and exception classification. |
| Callable, constructor, conversion, and NTTP identity | `36219639`-`9057c4c5` | Owner-local candidates, canonical pointer/value identity, and conversion materialization remain demand-owned. |
| Pack partition, class shells, and alias-expanded results | `24e637ef`-`d68594ae` | Complete specialization keys and alias-expanded result identity are now interned typed facts with indexed lookup. |
| Dependent calls, exception demand, storage, and enclosing replay | `40f206cf`-`63c7288e` | Typed call/exception/storage facts and dependency ownership remain bounded and cacheable. |
| Final substitution, constructor/conversion flow, and fixture | `8da6b98e`-`eba6ec3` | All landed behavior is preserved at 410/410 PA23 tests. |
| Final full-stage architecture audit | current audit | Typed result interning, indexed overlays, typed ambiguity/SFINAE propagation, completeness checks, telemetry, and file ownership pass final gates. |

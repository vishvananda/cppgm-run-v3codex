# PA19 Full-Stage Plan

## Stage Design and Spec Alignment

PA19 extends the PA11/PA12 canonical semantic graph and PA15-PA18 typed LowIR
path with template patterns and demanded specializations. A pattern retains the
single PA10 syntax body plus canonical parameter identities; a specialization
is keyed by pattern identity and canonical `TypeId` arguments, binds those
arguments in a parent-linked template scope, and reuses ordinary declaration,
overload, class, lifetime, and lowering owners.

```text
PA10 SyntaxArena -> PA11 Program IDs + PA12 template pattern/demand facts
                 -> borrowed SemanticGraphView -> PA15-PA19 TypedProgram
                 -> terminal LowIR text
```

This matches `spec.md` sections 2-4 and 9: identity comparisons and completed
fact lookup are O(1) average; definitions, bodies, and emission have distinct
monotonic demand; non-dependent retained syntax is shared; lowering consumes
the selected binding and typed body facts without lookup or text replay. The
retained PA10 tree and textual LowIR endpoint remain staged assignment
boundaries; native machine IR and ELF are later PAs.

## Current Failure Map

Turn-start baseline was 14/293; the completed function-template checkpoint is
32/293. The complete remaining 261-test failure set has two shared groups:

| Failures | Shared behavior | Owner |
|---:|---|---|
| 257 exit mismatches | Class patterns and their dependent names/layout are absent; the remaining function cases need structured complex type arguments, target-context deduction, ADL/using replay, or definition-time dependent checking. | PA12 template declaration, type, scope, lookup, and demand owners |
| 4 LowIR mismatches | Enum/operator fallback, hidden-friend visibility, template-local control flow, and member-call distractor paths select or lower the wrong retained fact. | PA12 overload/control-flow facts and typed lowering |

These failures divide architecturally into class-template identity/layout;
dependent/current-instantiation lookup and out-of-class definitions;
ADL/using/operator participation; remaining function deduction/type syntax;
and instantiated PA17-PA18 value/lifetime behavior. They are sequenced at
those stable owners, not treated as individual output cases.

## Active Checkpoint

Add class-template identity and on-demand layout for supported type parameters
and defaults. A class pattern owns its retained class syntax and declaration
scope; type lookup resolves a template-id to a specialization keyed by canonical
pattern/argument IDs; class completion substitutes into the existing
`AnalyzeClass` owner and then reuses PA16-PA18 layout, member, lifetime, and
lowering facts. Collection is O(parameters + class syntax), specialization
lookup is O(1) average, and each declaration/layout fact is monotonic and
computed once. Validate scalar fields/methods, namespace qualification,
forward-definition parameter renaming, default arguments, cache reuse,
PA1-PA18 preservation, file audit, and specialization-count scaling.

## Performance Evidence

| Probe | Baseline | Checkpoint result |
|---|---|---|
| Repeated calls to one function-template specialization | Existing declaration-only PA18 probe showed one demand push after cache reuse | 128/256/512 calls produce 128/256/512 requests, 127/255/511 cache hits, and exactly one demand push/emission. Semantic nodes are 783/1,551/3,087; instructions 519/1,031/2,055; semantic time 1.11/1.99/3.96 ms and lowering time 0.40/0.71/1.49 ms. |

## Completed Checkpoints

| Checkpoint | Final result | Principal evidence |
|---|---|---|
| PA18 handoff | Pass | Required PA1-PA18 through report at turn start |
| Demanded function-template definitions | Pass | Canonical shape merge, recursive direct deduction, cached specialization upgrade/emission; PA19 improved 14 to 32 and PA1-PA18 remained 1713/1713 |

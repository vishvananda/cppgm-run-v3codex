# PA23 Final Audit

## Final Findings

1. Fixed: alias-expanded function-result redeclaration equality rendered both
   results into strings and compared those strings. This violated canonical
   identity and repeated the expansion for every comparison.
2. Fixed: out-of-class template owner matching rescanned source payload text to
   substitute parameter ordinals and owner types. Structured parsed nodes and
   canonical type identities now own that comparison.
3. Fixed: immutable alias environments still performed a parent-chain scan for
   every ordinary name. Nested depth measurements exposed quadratic probes;
   a request-local sparse name index bounds misses before overlay traversal.
4. Fixed: several expected substitution failures were classified by broad
   `runtime_error` catches. Candidate-local lookup ambiguity, target-directed
   function designators, named values, call conversions, casts, member calls,
   `sizeof`/layout, partial replay, defaults, and pack expansion now propagate
   compact typed failure through their complete ownership paths.
5. No open PA23 correctness, architecture, performance, self-containment, or
   file-audit finding remains.

## Changes

- Added compact syntax tag/payload ID access and a canonical
  `FunctionTemplateResultIdentityId` on each retained function pattern.
- Added a flat open-addressed result-identity interner storing typed structural
  atoms for node kinds, interned names, parameter ordinals, canonical
  declarations/entities, substitutions, qualified components, and arguments.
  Equality after construction is one integer comparison.
- Replaced copied alias binding vectors with immutable parent-linked overlays,
  indexed possible names, and preserved borrowed syntax ownership.
- Removed source-text owner normalization; normalized syntax comparison uses
  interned IDs and canonical `TypeId` equality.
- Added nonthrowing qualified candidate lookup and repaired typed failure
  propagation through expression, call, pack, default, partial-selection, and
  completeness/layout paths. Exception catches left in candidate code only
  restore local state and rethrow hard failures.
- Added release counters for identity requests, cache hits, index probes, atom
  visits, syntax visits, environment probes, alias expansions, and storage;
  semantic and LowIR statistics aggregate and print them.
- Moved `sizeof` semantic ownership beside other operators to preserve the
  source/file-audit boundaries.

The representative path is source bytes -> shared preprocessing/token cursor
-> one parsed syntax arena -> function-template pattern and declaration-time
lookup facts -> canonical specialization/candidate selection -> monotonic body
demand -> typed semantic call/body graph -> borrowed `SemanticGraphView` ->
direct `TypedProgram` LowIR -> one textual PA23 view. Parser, syntax, lookup,
substitution, and demand scratch are destroyed before lowering. PA23 has no
machine-IR/ELF surface; those later checklist items are therefore not part of
this stage's exit criterion.

## Performance Evidence

| Workload | Evidence | Conclusion |
| --- | --- | --- |
| Alias/direct pairs, 1-64 | Requests 2-128; probes 2-128; hits 1-127; atom visits 24-2,040; semantic time about 0.30-4.21 ms. | Linear in declarations and produced atoms; repeated identities hit the flat table. |
| Nested aliases, depth 1-32 | Before: probes 12/27/75/243/867/3,267. After: 6/9/15/27/51/99; final depth-32 run has 232 syntax visits, 96 probes, and 32 expansions. | The unexplained quadratic miss path was removed; work follows syntax and expansion depth. |
| Checked representative | 326 tokens, 51 semantic nodes, 21 instructions, two identity requests/one hit, one demand push/emission, 868 bytes; 1.68/0.18/0.04 ms semantic/lowering/rendering. | One canonical result identity and one demanded specialization flow directly to LowIR. |

## Validation

- `make test-pa23`: pass, 400/400 assignment tests and 10/10 course tests.
- `perl scripts/cppgm_file_audit.pl --stage pa23 --paths dev/src`: pass with
  13 inherited header-division advisories and zero fatal findings.
- Baseline primary log: 3,049/3,049 through PA23, all 23 tracked stages pass.
- `make test-report-through-pa23`: pass, 3,049/3,049 tests and all 23 tracked
  stages.
- `git diff --check`: pass.

## Checkpoint Ledger

| Checkpoint | Audit disposition |
| --- | --- |
| Array-bound/default/expression substitution (`b6d38290`-`d2b7ff91`) | Preserved; typed candidate and complete request ownership verified. |
| Partial replay, packs, and lazy class demand (`0b81ecbc`-`c0704231`) | Preserved; dependent work is replayed only for complete keys. |
| Ordering, calls, initialization, explicit specialization (`a69c8d5d`-`624c9995`) | Preserved; selected facts cross directly to lowering. |
| Result lookup, alias replay, expression validity (`b71f3a5d`-`84a3f7c5`) | Strengthened by canonical result interning and typed candidate ambiguity. |
| Callable/constructor/conversion/NTTP flow (`36219639`-`9057c4c5`) | Preserved; target selection and demand remain owner-local. |
| Pack partitions, class shells, expanded results (`24e637ef`-`d68594ae`) | Strengthened by indexed immutable environments and O(1) completed identity comparison. |
| Calls, exception/storage demand, enclosing replay (`40f206cf`-`63c7288e`) | Preserved; no global retry or lowering reconstruction found. |
| Final conversion/materialization and fixture (`8da6b98e`-`eba6ec3`) | Preserved at 410/410 PA23 tests. |
| Final architecture audit | All four findings fixed across their ownership paths; file audit and 3,049-test through-stage report pass. |

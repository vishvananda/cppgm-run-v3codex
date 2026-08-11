# PA24 Final Audit

## Final Findings

1. Fixed: reentrant argument-dependent candidate canonicalization allocated a
   node-based `std::unordered_set<BindingId>` for every completed call. This was
   a hot compact-ID set, duplicated the flat candidate identity set in ordinary
   call lookup, and violated `spec.md` section 8.
2. Verified: specialization identity is complete for the PA24 surface. Pattern,
   canonical argument-list, and pack-partition IDs key open-addressed tables;
   not-started, in-progress, successful, and failed requests are distinct, and
   completed results cannot regress or change binding identity.
3. Verified: declaration, definition/layout, retained-member replay, defaults,
   exception specifications, runtime body demand, and ABI/emission facts have
   distinct owners. Append-only demand queues are deduplicated and drained by
   cursors; no complete-program retry loop was found.
4. Verified with the PA24 stage adaptation: the README-required PA10 token and
   syntax representation is parsed once and remains phase-local. Analyzer
   scratch is destroyed before the canonical graph is borrowed for direct
   typed LowIR construction. PA24 has no machine-IR or ELF surface.
5. No open PA24 correctness, architecture, performance, self-containment, or
   file-audit blocker remains.

## Changes

- Promoted the existing private flat binding identity set into the shared
  semantic-table layer and reused it for both ordinary function candidates and
  reentrant ADL completion.
- Kept first-seen overload order while replacing per-entry node allocation with
  contiguous `uint32_t` slots, open addressing, a 70% growth threshold, and
  geometric rehashing.
- Rechecked all PA24 stage commits and the 50-file aggregate source delta
  against the assignment boundary, LowIR contract, and architecture checklist.
  No test fixture, reference output, wrapper, or source-set workaround was
  introduced.

The nontrivial variable-template trace is source bytes -> PA10 interned
tokens/syntax -> `VariableTemplatePattern` and owner/name index -> canonical
argument formation and partial selection -> parent-linked substitution scope ->
memoized specialization `BindingId` with member/storage/initializer facts ->
typed semantic global -> direct `TypedProgram` global -> one LowIR rendering.

The demanded constructor-template trace is source bytes -> retained function
pattern/default/ABI recipe -> deduction and candidate-local SFINAE -> canonical
argument-list and partition key -> selected constructor binding and conversion
facts -> constructor action -> deduplicated definition demand -> typed body ->
evaluation-ordered variable/reference slot plan -> direct typed call/object
lowering -> one LowIR rendering. In the checked reducer this produces `$src`,
then `$refarg__1`, then `$dst`, and uses the recorded reference boundary and ABI
aliases without a lowering-time semantic lookup.

At the semantic-to-LowIR boundary the analyzer, token vector, parser,
`SyntaxArena`, lookup/substitution scratch, and demand queues are dead. The
borrowed `SemanticGraphView` lives only for the synchronous call to
`LowerSemanticGraph`; multi-translation-unit output accumulates only typed
LowIR. Text is never an in-process transport. Machine IR and ELF are deferred
to later assignments by the PA24 contract.

## Performance Evidence

| Workload | Evidence | Audit conclusion |
| --- | --- | --- |
| Competing explicit-id ADL, width 1-128 | Associated declarations 2-129, overload visits 8-516, conversion checks 10-772, peak stage bytes 73,324-2,708,598, and seven-run median semantic time 0.600-11.365 ms. | The repaired flat-set path is linear in associated scopes and real candidates. |
| Associated-scope-only ADL, width 1-128 | Scopes 2-129 while requests and overload candidates stayed at four; median semantic time 0.573-5.118 ms. | Scope discovery is owner-indexed and does not scan unrelated declarations. |
| Full PA24 input sweep | 422 source files in 8.8 s; maximum process wall time 27.3 ms including startup. | No timeout-adjacent or isolated slow path. |
| Five slowest checked-in semantics | 8.74-13.48 ms semantic, 0.11-0.45 ms lowering, 0.03-0.06 ms rendering; the largest counters were 3,247 declarations, 2,962 lookups, 1,298 partial-deduction visits, and 439 specialization requests. | Time is explained by required template/lookup work; lowering and rendering do not reconstruct semantics. |
| Prior checkpoint scaling | Explicit specialization, NTTP variable templates, recursive constructors, retained validation, defaults, SFINAE failures, empty lifecycle transfer, ABI recipes, and slot planning all remained linear through tested widths/depths of 32-128. | The independent audit found no counter or timing series with unexplained superlinear growth. |

## Validation

- `make test-pa24`: pass, 422/422 assignment tests.
- `perl scripts/cppgm_file_audit.pl --stage pa24 --paths dev/src`: pass with
  14 nonfatal header-division advisories and zero fatal findings.
- Baseline primary log: 3,471/3,471 through PA24; all 24 tracked stages pass.
- `make test-report-through-pa24`: pass, 3,471/3,471 tests and all 24 tracked
  stages.
- `git diff --check`: pass.

## Checkpoint Ledger

| Checkpoint | Audit disposition |
| --- | --- |
| Explicit specialization and canonical NTTP identity (`62f37ef1`, `6b06e092`) | Preserved; complete typed ownership and cache keys verified. |
| Constructor demand and explicit-id ADL/SFINAE (`6a1a66c7`, `4c694582`) | Strengthened by shared flat reentrant candidate deduplication. |
| Current-specialization, defaults, and dependent qualified types (`739eab0f`-`9ab41503`) | Preserved; retained syntax replays only after complete deduction. |
| Construction lowering, identity-only declarations, and direct references (`5be465c9`-`f3724c7e`) | Preserved; selected typed facts cross directly into LowIR. |
| Empty/nested construction and pack/default ordering (`6a238d0f`-`30fe0986`) | Preserved; demand and recipe traversal remain monotonic and linear. |
| Candidate-local calls, ABI recipes, local statics, and reference slots (`f18a8293`-`4f832f86`) | Preserved; runtime demand, semantic ABI identity, and evaluation order verified end to end. |
| Stage plan (`d32e64a6`) | Reconciled with source, tests, performance counters, and the normative checklist. |
| Final architecture audit | One hot-container defect fixed across both candidate paths; no remaining blocker; final file and through-stage gates pass. |

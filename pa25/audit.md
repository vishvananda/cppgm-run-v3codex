# PA25 Checkpoint Audit

## Current Checkpoint Review

Checkpoint `583b174a` (ordinary placeholder results) passes after audit repair.
The landed increment establishes ordinary variable, condition, static-member,
and visible non-template function/member placeholder results without changing
the 46/121 shipped-test baseline or any earlier assignment.

The audit found and fixed three checkpoint-level issues across the complete
ownership path:

1. Cv-qualified `auto&&` variables and function results incorrectly used the
   cv-unqualified forwarding-reference collapse and accepted lvalue
   initializers. Deduction now forms a cv-qualified rvalue-reference target and
   the shared conversion owner rejects the invalid binding.
2. Local `volatile auto` entered constant-initializer evaluation merely because
   it had a cv qualifier. A constexpr call was folded and its runtime demand
   removed, unlike the equivalent explicit declaration at `-O0`. Constant
   probing now follows the const-qualified case; volatile-auto and explicit
   volatile LowIR are identical in the checked reducer.
3. Typed placeholder initializers crossed declaration ownership through a
   node-based `unordered_map`, and retained member analysis/emission deep-copied
   `FunctionInfo` parameter vectors. The initializer is now returned directly
   to its immediate declaration owner, member validation uses a fixed-prefix
   indexed walk, and retained bodies are borrowed until the point that semantic
   demand may invalidate references.

The nontrivial declaration trace is source bytes -> shared interned
tokens/syntax -> one initializer analysis -> decay/reference deduction ->
canonical `TypeId` plus `ExpressionInfo` returned directly to the declaration
owner -> binding and typed semantic variable -> binding-indexed PA15 slot/call
lowering -> direct `TypedProgram` LowIR. No spelling, rendered type, or lookup
is reconstructed after deduction.

The demanded-member trace is parsed member body -> canonical `FunctionInfo`
with not-deduced/deduced result state -> one body analysis at class completion
-> retained typed parameter/body nodes -> deduplicated function demand -> the
same body nodes attached to one emission unit -> direct typed lowering. A
function-template/auto-variable reducer additionally followed canonical
template selection and one demanded specialization through LowIR, then through
the README's secondary `lowir2cy86`/`cy86` path to an x86-64 ELF executable
that returned zero. PA29 native lowering remains a later staged surface.

No relevant string key, source/test shortcut, global retry, textual in-process
transport, lowering-time semantic search, timeout-adjacent path, or unresolved
correctness/performance/file-audit issue remains in this checkpoint. The 14
file-audit warnings are unchanged inherited header-division advisories; there
are no fatal findings.

Representative scaling used classes with 25/100/400 placeholder-result
members. Seven-run medians recorded semantic nodes 144/519/2,019, function
signature probes 97/322/1,222, dependency visits 29/104/404, peak stage bytes
140,200/459,189/1,531,297, and semantic time 0.551/1.441/4.830 ms. Demand stayed
at two pushes and one member emission, while lowering stayed at two functions,
six instructions, and 0.119-0.179 ms. This supports linear member analysis and
constant emitted work rather than repeated body analysis or whole-class replay.

Validation:

- Focused shipped and audit regressions: 15/15 pass.
- `make test-report ACTIVE_TEST_REPORT_PAS='pa25'`: 50/125; all original 46
  passes and all four audit regressions pass, with the same 75 unfinished-stage
  failures.
- Prior-through-PA24 gate: 3,471/3,471 pass across all 24 stages.
- `perl scripts/cppgm_file_audit.pl --stage pa25 --paths dev/src`: pass with 14
  inherited nonfatal advisories.

## Checkpoint Audit Ledger

| Checkpoint | Audit disposition |
|---|---|
| Ordinary placeholder results (`583b174a`) | Pass after cv-reference, runtime-demand, direct-ownership, and retained-body copy repairs; shipped baseline and all earlier stages preserved; linear scaling and file audit verified. |

# PA17 Audit

## Current Checkpoint Review

**Checkpoint:** `1cc83de7` (`Implement PA17 ref-qualified member boundary`)

**Result:** Pass after audit fixes. The landed increment is bounded to canonical
function ref-qualifier identity, declaration compatibility, implicit-object
viability/ranking, xvalue propagation through non-reference data members, and
typed callable/ABI identity. Class-value transfer and conversion-operator tests
that also contain ref-qualified calls remain blocked in their later primary
owners; the scalar ref-qualified boundary itself is closed.

The complete ownership path is source declarator -> canonical PA11 `TypeId` ->
PA12 declaration and overload indexes -> binding-carrying call/member facts ->
PA15 typed callable identity and terminal ABI spelling. `TypeRecord` owns the
packed qualifier, the flat declaration-shape index enforces the language rule
without scanning a same-name overload set, and overload resolution retains the
selected binding and object conversion. Lowering strips member-only qualifiers
only when adapting the already-selected function to its explicit `%this`
callable shape; it performs no lookup or textual reconstruction.

Audit findings are closed:

1. The mixed qualified/unqualified index included member cv-qualification, so
   otherwise-identical `const`/`volatile` declarations could evade the rule.
   Its key now uses owner, name, adjusted parameter types, and variadic shape,
   while deliberately excluding return type, member cv, and ref-qualifier.
2. Implicit-object ranking compared cv subsets before reference binding. For an
   rvalue this could select `const &` over `const volatile &&`; rvalue-reference
   preference now precedes the cv tie-break in both ordinary and operator calls.
3. Static members were classified by the absence of an implicit `%this` and
   skipped declaration-shape validation. Validation now distinguishes class
   declaration ownership from non-static callable shape, so a static
   unqualified member cannot mix with an otherwise-identical ref-qualified
   member.

Representative release probes show proportional work. For 64/128/256 distinct
same-name parameter shapes, each declared in `&` and `&&` forms, signature
lookups were 646/1,286/2,566, lookup queries 452/900/1,796, dependency edges
128/256/512, peak semantic bytes 372,305/742,629/1,483,493, and five-run
semantic medians 1.269/2.401/4.830 ms. A separate 64/128/256-class probe with
one lvalue and one prvalue selected call per class recorded 384/768/1,536
candidates, 1,155/2,307/4,611 instructions, and semantic medians
2.920/5.944/11.815 ms. There is no overload-set rescan or superlinear counter.

Validation:

- Focused landed and audit regression set: 15/15 pass.
- `make test-report ACTIVE_TEST_REPORT_PAS='pa17'`: expected full-stage
  failure, 57/231. The checked-in 54/228 baseline and its same 174 future-stage
  failures are intact; the three additional passes are audit regressions.
- `make test-report-through-pa16`: 1,436/1,436 and 16/16 stages pass.
- `perl scripts/cppgm_file_audit.pl --stage pa17 --paths dev/src`: pass with
  the same six baseline header-division warnings; the touched headers add only
  declarations and packed identity fields, not implementation ownership.
- Valgrind reports no errors or definite/indirect leaks on an out-of-class
  ref-qualified definition. A process-only trace contains the compiler
  `execve` and `exit_group(0)` only, with no child process or external tool.

## Checkpoint Audit Ledger

| Checkpoint | Audit result | Closure evidence |
|---|---|---|
| Ref-qualified member identity and selection | Pass after audit fixes | Canonical declaration/call/ABI path; complete mixed-set key; correct object ranking; focused, baseline, scaling, and required gates pass |

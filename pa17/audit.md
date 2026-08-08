# PA17 Audit

## Current Checkpoint Review

**Checkpoint:** `d3f28efd` (`pa17: lower composite subobject transfers`)

**Result:** Pass after audit fixes. The landed increment is bounded to
synthesized composite copy/move recipes, leading trivial storage spans,
class-array element transfers, empty/reference/bit-field storage handling,
aggregate return helpers, and the corresponding class-value parameter shape.
Lookup and reference-binding closure remains the next checkpoint.

The ownership path is class source and canonical layout -> completed
`FunctionInfo` special-member facts -> an ordered recipe carrying canonical
`TypeId`, member `BindingId`, selected special-member `BindingId`, and optional
storage span -> demand-keyed helper/ABI emission -> existing source and `%this`
storage -> typed LowIR. Aggregate return helpers likewise retain member and
selected-constructor identities. Lowering consumes those identities directly;
PA17 introduces no template or ELF owner and stops at the assignment's typed
LowIR boundary.

Audit findings are closed:

1. Nontrivial array-member copy and assignment expanded the retained array
   bound into one typed call sequence per element. A 65,536-element transfer
   therefore created hundreds of thousands of instructions despite one
   semantic subobject action. Arrays now flatten to their canonical leaf type,
   use the exact inline path through eight elements, and otherwise emit one
   counted loop keyed by the action node. Nested arrays use the same loop over
   the flattened contiguous leaf range; exception-aware transfer cleanup is
   outside PA17's stated boundary.
2. The original lowering helper created a temporary `DumpNode` merely to pass
   a selected binding to shared call code, contrary to `spec.md` section 6.
   The call path now accepts the retained canonical `BindingId` directly. No
   lookup, rendered signature, semantic reconstruction, text round trip,
   whole-program retry, or fallback remains on the affected path.
3. Prefix spans remain one `copyobj`; later nontrivial members and array leaves
   use their selected helpers; empty subobjects invent no scalar payload; and
   bit-field assignment copies one canonical storage unit. The PA17 bit-field
   case remains outside the 207 pass set only because its constructor fixture
   requires the opposite instruction presentation from the identical passing
   PA16 fixture. Preserving PA16's checked output and the README's monotonic
   extension rule takes precedence over demand-sensitive presentation. The
   transfer function itself matches the PA17 oracle.
4. The changed source contains no test-name, source-spelling, reference-binary,
   host-compiler, subprocess, timeout, or cached-output shortcut. All files
   remain registered and within the audit limit.

Representative nontrivial array-member copy/assignment probes at extents
32/1,024/65,536 each recorded 6 subobject visits and 22 lowered nodes, with a
fixed 12 blocks, 74 instructions, and 21,183 typed bytes. Five-run semantic
medians were 0.246/0.222/0.250 ms and lowering medians were
0.253/0.222/0.251 ms; output was 3,781/3,785/3,787 bytes. A nested 32x32 array
retained the same shape with one 1,024-element loop, and the largest probe
passes the LowIR sanity validator. The landed mixed-prefix probe remains
linear in declared members while lowering stays fixed at one prefix transfer
plus the nontrivial tail.

Validation:

- `make test-report ACTIVE_TEST_REPORT_PAS='pa17'`: expected incomplete-stage
  failure, 207/239; the turn-start pass set is unchanged.
- Required through-stage command: PA1-PA16 pass 1,436/1,436.
- `perl scripts/cppgm_file_audit.pl --stage pa17 --paths dev/src`: pass with
  11 header-division warnings and no fatal issue.
- Landed focus passes 8/8; five PA16 bit-field compatibility cases pass; source
  and ownership searches find compact typed keys and no shortcut or fallback.

## Checkpoint Audit Ledger

| Checkpoint | Audit result | Closure evidence |
|---|---|---|
| Ref-qualified member identity and selection | Pass after audit fixes | Canonical declaration/call/ABI path; complete mixed-set key; correct object ranking; focused, baseline, scaling, and required gates pass |
| Branch-local class values and full-expression cleanup | Pass after audit fixes | Typed construction state on normal/unwind exits; exact flat and linked cleanup reuse; 173/231 baseline intact; linear probes and required gates pass |
| Loop full-expression regions | Pass after audit fixes | Typed discarded materialization; bounded-inline and linked-suffix cleanup; 174/231 baseline, linear probes, and required gates pass |
| Class direct-initialization recipes | Pass after audit fixes | Canonical list conversions and selected constructor are reused; original pass set, audit regressions, proportional probes, and required gates pass |
| Typed constructor delegation and qualified default completion | Pass after audit fixes | Canonical declaration/complete-constructor identities and typed action reuse; invalid/deleted defaults rejected; 193/233 baseline, six regressions, proportional probes, and required gates pass |
| Composite subobject copy/move storage transfer | Pass after audit fixes | Canonical recipe and direct selected-binding lowering; bounded array loops; 207/239 baseline, focused/through-stage gates, file audit, and fixed-shape extent probes pass |

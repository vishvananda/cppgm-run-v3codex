# PA17 Implementation Plan

## Stage Design and Spec Alignment

PA17 extends the PA16 typed semantic graph and LowIR. Canonical `TypeId`,
completed special-member facts, selected function identity, and typed actions
remain the phase boundary; lowering performs no lookup or text round trip.
Class completion owns copy/move/assignment facts. PA12 owns selection, demand,
stable temporary identity, conditional-construction facts, and ordered
destructor actions, including materialization of class-valued discarded calls,
conditionals, casts, and comma wrappers. Braced constructor operations analyze
each list clause once and retain recursive conversion and selected-constructor
facts in flat tables keyed by canonical `(NodeId, TypeId)` identity, with
separate direct/copy results and explicit in-progress state. PA15-PA17
materialize into destination storage or ABI result slots, using compact
action/slot IDs and function-local flat tables so normal and unwind exits share
one typed cleanup chain. Small cleanup sequences use a bounded exact path;
wider sequences intern one linked suffix per constructed temporary, and flat
maps clear only occupied slots. This follows `spec.md` sections 2, 3, 4, 6, 8,
and 9: monotonic demand, O(1)-average fact access, O(candidate count) selection,
O(subobject count) synthesis, and analysis, lowering, storage, and output
proportional to owned obligations and emitted IR. Qualified special-member
definitions reuse the class declaration's canonical binding. Explicitly
defaulted definitions validate their implicit signature and deleted state at
that owner; defaulted destructor completion visits each canonical base/member
destructor fact once. Delegation records one selected complete-constructor
identity and lowers one typed action into the existing destination. Synthesized
composite transfers retain a canonical member/type/selected-special-member
recipe plus an optional leading storage span. Lowering consumes those facts
directly: spans become one `copyobj`, arrays of up to eight leaf subobjects use
the exact inline path, and larger or nested arrays become one counted loop.
No lowering-only semantic node is synthesized. Compile work is therefore
O(owned subobjects + emitted loop IR), independent of a retained array extent;
runtime transfer work remains O(array elements).

Expression analysis also retains canonical value category, referred type,
selected declaration, and standard-conversion facts. Xvalue member calls stay
xvalues, reference binding materializes only prvalues, and derived-to-base
by-value staging records the typed base projection consumed by lowering.
Array-reference and no-op destructor handling use those same facts without
reconstructing an object shape. The `Program` model owns the inheritance edge
index: each class has canonical depth and access-prefix metadata plus a compact
binary-lifting row. Public ancestry and distance queries therefore avoid
repeated base-chain walks; context-sensitive private/protected/friend access
continues through the detailed access owner. Candidate conversion computes the
relationship once and reuses it for reference relatedness and rank.

Ordinary lookup retains compact canonical overload-set representatives rather
than collapsing distinct functions into value-style ambiguity. Using
directives are explicit indexed `(edge, NameId)` relations fed by per-scope
visible-name facts and reverse incoming edges; a deduplicated worklist
propagates late names and invalidation follows only reverse relations for the
changed name. Call analysis expands the representatives and flat-deduplicates
canonical `BindingId` values. Introduced base members retain both their
declaration/access owner for implicit-object ranking and the actual object
conversion for typed base projection. Callable-surrogate conversion facts and
pending lookup targets are flat indexed sequences, while the common merged
lookup result stays inline. This preserves O(visited relevant scopes and edges
+ result declarations + viable candidates) work and avoids per-candidate or
per-scope owning containers.

## Current Failure Map

Audited result: **224/241**. The complete non-overlapping failure map contains
17 tests; the turn-start 222-test pass set remains intact and both audit
regressions pass:

| Failures | Shared behavior | Primary owner |
| ---: | --- | --- |
| 3 | user-defined conversion and nonmember/built-in operator selection | PA12/PA16 operator resolution |
| 7 | residual special-member, aggregate, bit-field, array, base, and ABI/storage shapes | PA12 initialization + PA15-PA17 lowering |
| 5 | conditional/result temporary materialization and copy elision | PA12 lifetime facts + PA17 lowering |
| 2 | shadowed local identity and cleanup rebinding across control flow | PA12 scope facts + PA17 lowering |

## Active Checkpoint

**Next: conversion-aware operator closure (3 failures).** Apply `spec.md`
sections 2, 3, 6, and 9: ordinary, hidden-friend/ADL, conversion-function, and
built-in candidates retain canonical identities and complete conversion facts;
lowering consumes the selected function or built-in action directly. PA12/PA16
operator resolution owns candidate union, rank comparison, and converted
operands. Expected work is O(associated declarations + viable candidates ×
operand count), with generation-stamped deduplication and the existing typed
conversion cache. Validate reverse hidden-friend `operator+`, converting
pass-by-value overloads, class-to-pointer built-in subtraction, full PA17,
through PA16, file audit, and 32/64/128 candidate probes.

## Performance Evidence

- Using-directive function merges at 32/64/128 contributing namespaces
  recorded 134/262/518 scope visits, 32/64/128 relevant edge visits,
  32/64/128 candidates, 33/65/129 conversions, and
  263,007/525,935/1,051,779 semantic bytes. Five-run semantic medians were
  1.344/2.449/4.600 ms, proportional to the contributing relations and facts.
- Callable-surrogate sets at 32/64/128 candidates recorded 137/265/521 scope
  visits, no using-edge visits, 32/64/128 candidates, 66/130/258 conversions,
  and 241,408/482,280/963,916 semantic bytes. Five-run semantic medians were
  1.465/2.689/5.378 ms, confirming proportional candidate work and flat fact
  storage.
- Same-name declaration and selected-call probes scale proportionally from
  64/128/256 declarations; medians were 1.269/2.401/4.830 ms and
  2.920/5.944/11.815 ms respectively, with doubling counters and storage.
- Direct-transfer probes emit constant-count `copyobj` operations; a 24-byte
  indirect result remains one width-annotated instruction rather than a loop.
- Synthesized assignment at 32/64/128 members recorded 96/192/384 fact and
  subobject visits and 184/344/664 instructions.
- Synthesized construction at 32/64/128 members recorded 160/320/640 fact
  lookups, 161/321/641 subobject visits, and 271/527/1,039 instructions.
  Five-run semantic medians were 0.216/0.281/0.405 ms, confirming linear work.
- Conversion indexes at 32/64/128 candidates recorded 42/74/138 overload
  visits and 71/135/263 conversion checks. Five-run semantic medians were
  0.999/1.625/3.165 ms, proportional to the indexed candidate set.
- Built-in comparison over inherited conversion indexes at 32/64/128 entries
  recorded 67/131/259 overload visits and 134/262/518 conversion checks;
  five-run semantic medians were 0.948/1.800/3.658 ms and semantic peak storage
  was 268,397/532,493/1,060,770 bytes, proportional to the indexed input.
- Scalar `new` with 16/32/64 unrelated placement overloads recorded 17/33/65
  overload visits, a fixed 4 conversion checks after arity filtering, and
  19,401/36,137/69,609 typed bytes. Five-run semantic medians were
  0.504/0.938/1.723 ms, proportional to the actual candidate set.
- Nontrivial `new[]`/`delete[]` at constant bounds 16/1,024/65,536 retained
  fixed loop-shaped LowIR: 16 blocks, 65 instructions, and 16,444 typed bytes.
  Five-run semantic medians were 0.118/0.117/0.119 ms and lowering medians were
  0.113/0.111/0.115 ms; compile work is independent of runtime element count.
- Unions with 32/64/128 variants recorded 32/64/128 layout visits and
  128/256/512 special-member subobject visits. Semantic medians were
  0.165/0.229/0.374 ms, while whole-storage copy kept lowering fixed at 21
  instructions and 6,197 typed bytes rather than emitting per-variant IR.
- Full expressions with 16/32/64 materialized temporaries emitted
  97/161/289 LowIR lines (3,013/5,333/9,973 bytes). Five 500-compile batch
  medians were 1.60/1.76/2.06 s, confirming proportional analysis and output.
- Nested condition declarations at depth 16/32/64 emitted 560/1,056/2,048
  LowIR lines (12,473/23,950/46,990 bytes). Five 300-compile batch medians were
  1.10/1.28/1.65 s, proportional to reached scopes and emitted cleanup edges.
- Conditionally evaluated right-hand temporary chains at depth 16/32/64
  recorded 16/32/64 lifetime slots, marks, dispatch probes, and dispatch
  entries; 171/331/651 blocks; 619/1,211/2,395 instructions; and
  30,359/59,815/119,238 output bytes. Five 100-compile batch medians were
  0.43/0.50/0.65 s with 7.2-7.9 MiB peak RSS, confirming one cleanup node per
  obligation rather than repeated constructed-prefix expansion.
- Nested loop iteration cleanup at depth 16/32/64 recorded 16/32/64 indexed
  unwind-scope visits and actions, 32/64/128 dispatch probes, 437/853/1,685
  instructions, and 81,548/153,710/297,966 typed bytes. Five-run semantic
  medians were 0.629/1.056/1.884 ms and lowering medians 0.387/0.592/1.011 ms.
- Direct discarded loop-iteration temporaries at width 16/32/64 recorded
  17/33/65 cleanup probes and entries, 26/42/74 blocks, 139/251/475
  instructions, and 32,560/58,320/109,840 typed bytes. Five-run semantic
  medians were 0.330/0.389/0.577 ms and lowering medians
  0.247/0.357/0.399 ms, confirming one linked cleanup suffix per obligation.
- Braced-list construction with 32/64/128 indexed constructor candidates
  recorded 236/460/908 candidate visits, 240/464/912 conversion checks,
  1 cache hit and 36/68/132 cache misses, and
  261,656/519,736/1,035,952 semantic peak bytes. Five-run semantic medians
  were 1.115/2.065/4.029 ms; lowering medians were 0.105/0.105/0.134 ms with
  fixed 2,990 typed bytes and 262 output bytes. Required list facts and storage
  scale proportionally, while the retained destination recipe stays fixed.
- Same-arity delegating chains at 32/64/128 links recorded exactly 32/64/128
  typed delegation actions, 66/130/258 demand pushes, 296/584/1,160
  instructions, and 130,531/258,787/515,504 typed bytes. Five-run semantic
  medians were 3.073/8.948/29.744 ms. Required all-candidate work was
  6,732/25,740/100,620 visits and 9,770/37,962/149,642 conversions (roughly 4x
  per doubling because every link has the full same-arity overload set), while
  actions, demand, IR, storage, and output remained linear with no retry loop.
- Out-of-class defaulted destructors over 32/64/128 nontrivial members recorded
  64/128/256 destructor actions across the complete/base ABI entries,
  224/448/896 access checks, and 113,970/221,618/436,976 typed bytes. Five-run
  semantic medians were 0.228/0.363/0.572 ms, confirming one bounded
  subobject-fact traversal per completion/emission owner.
- Mixed classes with 32/64/128 scalar prefix members and one nontrivial tail
  recorded 34/66/130 layout visits, 144/272/528 fact lookups, and
  136/264/520 subobject visits. Five-run semantic medians were
  0.438/0.479/0.630 ms; lowering stayed at 57 nodes, 109 instructions, and
  32,343 typed bytes, confirming linear recipe construction and one retained
  whole-prefix transfer.
- Nontrivial array-member copy and assignment at extents 32/1,024/65,536 each
  recorded 6 subobject visits and 22 lowered nodes, and emitted 12 blocks,
  74 instructions, and 21,183 typed bytes. Five-run semantic medians were
  0.246/0.222/0.250 ms and lowering medians were 0.253/0.222/0.251 ms; output
  was 3,781/3,785/3,787 bytes (only the bound spelling changes). A nested
  32x32 array retained the same counts with one 1,024-element loop.
- Unrelated reference overload sets at 32/64/128 candidates recorded exactly
  32/64/128 candidate visits, 96/192/384 conversion checks, and 62/126/254
  indexed ancestry probes. Five-run semantic medians were
  1.160/2.121/4.222 ms and semantic peak storage was
  298,014/594,046/1,186,194 bytes, proportional to the retained candidates.
- Viable single-inheritance overload sets at 16/32/64/128 candidates recorded
  16/32/64/128 candidate visits, 30/62/126/254 comparisons, and
  48/96/192/384 conversions. The audit changed repeated base-chain visits of
  405/1,333/4,725/17,653 into 105/217/441/889 indexed ancestry probes.
  Five-run semantic medians were 0.668/1.213/2.271/4.503 ms and peak storage
  was 150,852/299,580/597,156/1,195,292 bytes, demonstrating proportional
  candidate work and storage after the index replacement.

## Completed Checkpoints

| Checkpoint | Result | Validation |
| --- | --- | --- |
| Ref-qualified member identity and selection | 42/228 -> 57/231 | Focused 15/15; through PA16; audit and proportional probes pass |
| Direct trivial class-value transfer and ABI | 57/231 -> 68/231 | Exact-copy focus; through PA16; constant-count transfer probes and audit pass |
| Synthesized copy/move assignment | 68/231 -> 79/231 | Implicit/defaulted/deleted focus; through PA16; linear probes and audit pass |
| Synthesized copy/move construction and materialization | 79/231 -> 97/231 | Deleted/defaulted/move-only and call/return focus; named return-slot reuse; through PA16 1,436/1,436; linear probes and audit pass |
| Indexed conversion functions and retained selection | 97/231 -> 110/231 | Implicit/explicit, inherited, ref-qualified, second-rank, alias-ID, and qualified-definition focus 10/10; through PA16 1,436/1,436; linear probes and audit pass |
| Built-in operators after class conversion | 110/231 -> 124/231 | Unary/arithmetic/pointer/comparison/logical/subscript/compound focus 14/15; rank-based overloaded-vs-built-in choice; through PA16 1,436/1,436; proportional probes and audit pass |
| Scalar allocation/deallocation typed actions | 124/231 -> 136/231 | Ordinary/class-specific/placement/nothrow/global new and destructor/usual/global delete focus 12/12; through PA16 1,436/1,436; proportional candidate probe and audit pass |
| Dynamic array allocation/deallocation typed actions | 136/231 -> 145/231 | Scalar/class/multidimensional arrays and direct array allocation calls 9/9; scalar compatibility 15/15; through PA16 1,436/1,436; constant-size extent probe and audit pass |
| Union declaration and object actions | 145/231 -> 151/231 | Anonymous injection, active/default variant validation, constructor precedence, empty inactive lifetime, and whole-storage copy 6/6; through PA16 1,436/1,436; linear member probe and audit pass |
| Typed temporary identity and linear lifetime regions | 151/231 -> 157/231 | Discarded direct/indirect class results, noexcept argument and throwing base-init suffixes, local reference extension, and rvalue-reference materialization 6/6; proportional 16/32/64-temporary probe |
| Condition-declaration lifetime regions | 157/231 -> 160/231 | Nested false-path containment, reference-extended temporary, class-to-bool and switch-to-int conversion 4/4; through PA16 1,436/1,436; audit and proportional depth probe pass |
| Branch-local class values and full-expression cleanup | 160/231 -> 173/231 | Branch lifetime plus right-hand short-circuit construction-state probes pass; original pass set intact; through PA16 1,436/1,436; file audit and linear 16/32/64 cleanup probes pass |
| Loop full-expression regions | 173/231 -> 174/231 | Direct/cast/comma/conditional discarded materialization; exact normal/unwind cleanup; focused 17/17; through PA16 1,436/1,436; file audit and linear nested/width probes pass |
| Class direct-initialization recipes | 174/231 -> 186/231; audit 188/233 | Landed pass set gains 12 tests; scalar-list rank and narrowing regressions pass without changing the original set; through PA16 1,436/1,436; file audit and proportional list/candidate probes pass |
| Typed constructor delegation and qualified default completion | 188/233 -> 193/233; audit 199/239 | Positive delegation/qualified definitions 5/5; cycle/mixed rejection 3/3; six defaulted-definition regressions; through PA16 1,436/1,436; file audit and proportional 32/64/128 probes pass |
| Composite subobject copy/move storage transfer | 199/239 -> 207/239; audit 207/239 | Prefix, array, empty, reference, move-only aggregate, implicit-move, and trivial ABI gains remain 8/8; direct typed lowering and bounded array loops audited; through PA16 1,436/1,436; fixed-shape extent probes and file audit pass |
| Value-category and reference-binding closure | 207/239 -> 216/239; audit 216/239 | Planned focus 7/7 plus two adjacent gains and rejection focus 3/3; canonical value/conversion facts feed direct typed lowering; indexed ancestry removes repeated base-chain work; through PA16 1,436/1,436, file audit, and proportional 16/32/64/128 probes pass |
| Canonical lookup and candidate identity | 216/239 -> 222/239; audit 224/241 | Focus 6/6 plus two using/inheritance regressions; indexed using-name propagation, compact canonical overload merging, retained object conversions, and direct selected-call lowering; through PA16 1,436/1,436, file audit, and proportional 32/64/128 probes pass |

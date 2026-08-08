# PA19 Checkpoint Audit

## Current Checkpoint Review

Checkpoint `6c1a56be` landed enum builtin/operator competition, directional
integral-to-enum rejection, enum conversion lowering, and empty class-value
default-argument demand. The audit started from its clean 1,713/1,713 PA1-PA18
and 264/296 PA19 baseline and was bounded to that increment.

The three landed cases were valid, but the complete ownership path had four
defects. Enum-only operator lookup admitted every same-name function instead of
applying N3485 13.3.1.2's exact corresponding-enum parameter rule; the landed
probe therefore performed 129/257/513 overload visits for one relevant
operator. Builtin comma was incorrectly ranked as a competing candidate rather
than used only as the paragraph 9 fallback. PA15 reconstructed an enum
conversion from a source type, and the class-value fix reran default-constructor
selection at `kNoScope` instead of retaining the source-selected action. All
findings are closed, with two audit regressions covering the language defects.

The repaired operator path is function publication -> a TU-owned dense index
keyed by canonical `(ScopeId, NameId, enum TypeId, operand)` -> ordinary lookup
anchors and associated-scope edges -> canonical candidate deduplication ->
conversion ranking -> one selected `BindingId` or builtin -> a typed enum
conversion fact -> direct typed lowering. Hidden friends and deduced template
specializations receive the same exact-parameter filter. Empty aggregate
functional casts retain their selected constructor node in the semantic arena;
the by-value argument boundary consumes and demands that node without lookup.
No source/token/semantic representation was duplicated, no text was parsed back
into structure, and a bounded changed-source scan found no host/reference
invocation, filename/test branch, cached answer, global invalidation, or retry
loop.

## Durable Architecture Decisions

- A class-specialization `EntityRecord` owns one slice in the shared canonical
  template-argument pool; function-specialization `BindingRecord`s use the same
  pool. Analyzer-only duplicate argument stores are removed.
- Explicit-instantiation state, weak ODR linkage, and object-emission roots are
  semantic facts. Lowering consumes them by compact identity and combines them
  monotonically when translation-unit symbols merge.
- ABI construction represents template owners and nested template argument
  types structurally. Synthetic specialization spellings remain presentation
  only and are not symbol-identity or mangling inputs.
- `object_root` is part of the documented textual LowIR adapter and is accepted
  by the shared parser; the in-process production boundary remains typed.
- Enum-only non-member operator candidates are indexed by scope, interned name,
  canonical enum type, and operand position. Ordinary visibility and ADL select
  index owners first; unrelated same-name declarations never reach conversion
  ranking.
- Standard enum conversions and source-selected class-value constructor actions
  are semantic facts. Lowering and call staging consume those facts rather than
  rediscovering type or constructor intent.

## Performance Evidence

For 128/256/512 unrelated indexed `operator&` declarations, the landed path
performed 128/256/512 associated declaration visits, 129/257/513 overload
visits, and 263/519/1,031 conversion checks. After the audit fix, ordinary plus
ADL observation stays at two declaration visits, two overload visits, nine
conversion checks, and one conversion-cache miss. Typed storage remains linear
at 132,259/263,203/525,091 bytes and semantic time was 2.88/4.59/9.56 ms,
reflecting source publication rather than candidate materialization.

A complementary 128/256/512 probe in which every exact-first-parameter operator
is language-required produced 129/257/513 declaration visits,
128/256/512 overload visits, 263/519/1,031 conversion checks, and
2.37/4.48/9.06 ms semantic time. This distinguishes eliminated unrelated work
from linear required candidate work.

## Validation

- PA19: 266/298 combined; the original 264/296 checkpoint baseline is intact,
  the two audit regressions pass, and the same 32 original tests remain.
- The three landed enum/default-argument cases and focused PA16 enum/value-init
  plus PA17 nothrow construction cases pass.
- PA1-PA18: 1,713/1,713.
- PA19 file audit: pass with the 11 pre-existing advisory header warnings.

## Checkpoint Audit Ledger

| Checkpoint | Audit disposition | Final evidence |
|---|---|---|
| Explicit class-instantiation completion and member demand (`5ca3aed9`) | Pass after audit fix | Canonical entity arguments, structured ABI identity, weak/root propagation and parser support, N3485 legality controls, linear member-demand probe, prior baseline retained |
| Canonical enum builtin competition and class default arguments (`6c1a56be`) | Pass after audit fix | Exact enum-parameter index, comma fallback, typed conversion/constructor facts, two regressions, constant unrelated-candidate work, linear required work, prior baseline retained |

# PA19 Checkpoint Audit

## Current Checkpoint Review

Checkpoint `e10d5439` landed declaration-owned local and qualified type replay.
It raised PA19 from a reproduced parent result of 266/298 to 272/298 while the
PA1-PA18 baseline remained 1,713/1,713. This audit is bounded to that increment
and its parser-to-specialization ownership path.

The six newly passing cases were valid, but the function-pointer case decoded
`F (*)()` from a rendered template-argument string in semantic analysis. The
nested-constructor path likewise recovered the current class's unqualified name
by scanning a rendered qualified name. Both violated `spec.md` sections 1-4 and
9: parsed structure and compact identity must survive to their consumers, and
semantic work must not round-trip through text.

The repaired path is token interning -> scoped PA10 name facts and cached angle
matching -> one retained `type-id` per supported explicit type argument plus
interned qualified-name components -> ordinary `BuildTypeId` canonicalization
-> indexed class-template lookup -> the existing canonical specialization
cache -> ordinary completion and lowering. Current-class tracking now compares
the terminal interned identifier directly. PA11 adopts the parser's interned ID
without rehashing its spelling. The retained structure is semantic-only at the
PA10 serialization boundary, preserving the earlier public syntax contract.
The semantic function-pointer text decoder added by the checkpoint is removed,
so `F (*)()` reaches specialization solely through the retained declarator
tree. Shape-first ambiguous-call recovery was adjusted to accept the new
structured child without adding speculative lookup, and inherited-constructor
helpers were moved to their existing special-member owner to preserve the
file-audit boundary.

A bounded changed-source scan found no reference/host invocation, filename or
test branch, cached output, full-program retry, or new unindexed semantic scan.
The remaining textual template resolver predates this checkpoint and is not on
the repaired explicit-type-argument path.

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
- Supported explicit type template arguments are retained as ordinary PA10
  `type-id` trees beneath compact qualified-name components and hidden only at
  the public PA10 serialization boundary. Rendered names are presentation
  payloads, not semantic inputs.
- Parser current-class state stores the terminal interned identifier. Nested
  constructors and destructors are classified by compact identity rather than
  by parsing a qualified class spelling.
- Structured template-name resolution adopts existing interned IDs and feeds
  argument trees through `BuildTypeId` and the canonical specialization cache;
  unresolved argument types cannot enter that cache.

## Performance Evidence

For 128/256/512 paired loop-local relational and qualified `api::item<int>`
uses, token counts were 3,911/7,751/15,431 and syntax nodes were
4,942/9,806/19,534. Template scans were 128/256/512 at exactly two tokens each,
with no failed scans; parser storage was 33,640/66,664/132,712 bytes and median
parse time was 1.176/2.336/4.716 ms. Retaining argument trees therefore remains
linear in source size while cached angle recognition stays bounded per use.

For 128/256/512 repeated `result_traits<F, F (*)()>::type` uses, specialization
requests were 128/256/512 with 127/255/511 cache hits, while canonical types
stayed at 36, class layouts at two, layout member visits at one, and lookup
misses at three. Semantic nodes were 133/261/517, storage was
125,486/236,974/464,046 bytes, and median semantic time was
1.246/2.329/4.531 ms. This demonstrates one canonical specialization and
completion with linear work only for the repeated source uses.

## Validation

- PA19: 272/298; the 272/298 turn-start baseline and exact 26-test residual set
  are intact.
- Ten focused declaration-owned replay and ambiguity cases pass, including
  the structural function-pointer and nested-constructor paths.
- PA1-PA18: 1,713/1,713.
- PA19 file audit: pass with the 11 pre-existing advisory header warnings.

## Checkpoint Audit Ledger

| Checkpoint | Audit disposition | Final evidence |
|---|---|---|
| Explicit class-instantiation completion and member demand (`5ca3aed9`) | Pass after audit fix | Canonical entity arguments, structured ABI identity, weak/root propagation and parser support, N3485 legality controls, linear member-demand probe, prior baseline retained |
| Canonical enum builtin competition and class default arguments (`6c1a56be`) | Pass after audit fix | Exact enum-parameter index, comma fallback, typed conversion/constructor facts, two regressions, constant unrelated-candidate work, linear required work, prior baseline retained |
| Declaration-owned local and qualified type replay (`e10d5439`) | Pass after audit fix | Retained type-argument trees, interned component/class identity, canonical specialization path, bounded parser scans, one-completion function-pointer probe, 272/298 PA19 and prior baseline retained |

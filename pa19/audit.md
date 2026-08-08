# PA19 Checkpoint Audit

## Current Checkpoint Review

Checkpoint `5ca3aed9` landed explicit class-instantiation parsing, canonical
specialization completion, indexed member demand, and LowIR object roots. The
audit started from its clean 1,713/1,713 PA1-PA18 baseline and 258/293 PA19
baseline and was bounded to that increment.

The increment's three new cases were valid, but the complete affected path had
four defects. Class-template arguments were duplicated in PA19 side tables and
lowering reconstructed class-template member identity from presentation names;
generated members therefore had strong linkage and synthetic Itanium owners.
Nested specialization arguments were also dropped by the ABI type adapter.
`object_root=yes` was emitted but rejected by the shared LowIR parser. Finally,
namespace placement, class/union agreement, and declaration-after-definition
ordering were not enforced. All findings are closed.

The resulting ownership path is source syntax -> `AnalyzeExplicitInstantiation`
-> canonical class specialization -> entity-owned `TypeId` argument slice ->
indexed member demand -> binding-owned weak/root facts -> typed LowIR symbol
identity and structured ABI facts -> LowIR renderer/parser. Qualified and inline
namespace placement use scope identity, class and struct remain compatible,
and union mismatches are rejected. Primitive, qualified, and nested template
arguments now produce the checked weak Itanium object identities; constructors,
destructors, conversion functions, and static/nonstatic members follow the same
path. The production path does not serialize and reparse LowIR, and the bounded
source scan found no new reference/host invocation, test-name branch, cached
answer, or whole-program retry.

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

## Performance Evidence

For explicit definitions with 64/128/256 defined members, one specialization
request produced exactly 64/128/256 demand pushes and emissions, 129/257/513
instructions, and 1.10/2.12/3.96 ms semantic time. Typed storage and rendered
output grew linearly (80,198/159,366/317,702 bytes and
11,788/23,584/47,392 bytes). An ordinary use followed by explicit demand kept
one cache hit and emitted the member body once.

## Validation

- PA19: 261/296 combined; the handout remains 258/293 with the same 35
  outstanding tests, and all three audit legality regressions pass.
- Exact weak object names match all three landed references; nested
  `box<holder<int>>` emits `_ZN3boxI6holderIiEE5valueEv`.
- Generated `object_root` LowIR parses through `lowir2cy86`, including lifecycle
  and conversion members.
- PA1-PA18: 1,713/1,713.
- PA19 file audit: pass with the 11 pre-existing advisory header warnings.

## Checkpoint Audit Ledger

| Checkpoint | Audit disposition | Final evidence |
|---|---|---|
| Explicit class-instantiation completion and member demand (`5ca3aed9`) | Pass after audit fix | Canonical entity arguments, structured ABI identity, weak/root propagation and parser support, N3485 legality controls, linear member-demand probe, prior baseline retained |

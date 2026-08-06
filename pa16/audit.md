# PA16 Audit

## Current Checkpoint Review

**Checkpoint:** Namespace/static lifetime spine.

**Result:** Complete. Semantic analysis owns one source-ordered
`NamespaceObjectAction` for every namespace definition, keyed by canonical
`BindingId` and retaining the resolved initializer and optional demanded
destructor action. Extern declarations own no action, references own storage but
not referent lifetime, and qualified static-member definitions merge into their
class binding while using the declaration scope for private nested types.

Lowering partitions static data from dynamic actions, emits declaration-order
`role=init` and reverse-order `role=fini` bodies, and emits isolated TLS
object/guard wrappers. A compiled static-initializer owner handles aggregate,
array, padding, string/address, and constructor-folded data; source-type lowering
and graph-driver ownership were also separated so the main lowering unit remains
under the file-audit limit. Local PA15 array emission retains its prior canonical
shape, while namespace aggregate arrays use the required dynamic action path.

Validation:

- All 18 gained tests are absent from the final failure list, with no PA16
  checkpoint-entry regression; the required report is 150/265 versus 132/265.
- Through PA15 is 1,145/1,145 across 15 stages. The file audit passes with only
  three pre-existing shared-header advisories.
- At 1k/2k dynamic namespace objects, counters are 1,000/2,000 actions,
  2,004/4,004 instructions, 2,001/4,001 binding probes, and
  605,026/1,208,130 typed bytes. Five-run medians are 3.79/7.62 ms semantic and
  2.89/5.55 ms lowering.

## Checkpoint Audit Ledger

| Checkpoint | Audit result | Closure evidence |
|---|---|---|
| Direct-member object spine | Pass after audit fixes | One-shot class completion, non-mergeable member IDs, sound implicit-construction conditions, typed field projection, linear curves, and all checkpoint gates preserved |
| Local aggregate-action spine | Pass after audit fixes | C++11 user-provided eligibility, one union active member, borrowed edge cursor, bounded typed projection reuse, 61/248 PA16 with no losses, and all audit gates preserved |
| Special-member initialization action spine | Pass after audit fixes | Init-mode-correct constructor selection, canonical member ordinals/actions, truthful exception facts, typed nested references, bounded projections, 91/255 with no existing loss, and all audit gates preserved |
| Single-base construction spine | Pass after audit fixes | Naming-class access, hiding-correct base initializers, recorded projection counts, no lowering lookup, 120/259 with no existing loss, exact linear chain evidence, and all audit gates preserved |
| Destruction and lexical-cleanup spine | Pass after audit fixes | Demand-correct destructor access/deletion, union/reverse nested-array lifetime, shared return/EH suffixes, 132/265 with no prior loss, exact linear array/destructor curves, and all audit gates preserved |
| Namespace/static lifetime spine | Pass | Ordered canonical namespace actions, static/dynamic partition, reverse finalization, reference/static-member correctness, isolated TLS families, 150/265 (+18) with no baseline loss, exact-linear namespace counters, and all audit gates preserved |

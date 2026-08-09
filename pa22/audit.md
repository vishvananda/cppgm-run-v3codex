# PA22 Checkpoint Audit

## Current Checkpoint Review

The bounded review covered landed checkpoint `5d70a120`, its canonical
class/variable partial-specialization matching and ordering path, and the tests that
exercise that increment. The checkpoint raised PA22 from 82/308 to 103/308, but its
selected partial was transient: a shell created from a forward partial could later
complete from the primary, and ordinary member-definition replay was skipped for
initially selected partials. Selection is now binding-owned state containing the
pattern index, declaration revision, and fixed/pack substitution overlay. Equivalent
forward-to-definition redeclarations advance the revision and refresh only the
overlay before the single completion path applies member definitions.

Selection readiness is now distinct from layout readiness. Ordinary incomplete class
arguments participate in partial matching, while dependent shape parameters and
deferred specializations still postpone selection or layout as required. Canonical
specialization identity remains in the typed specialization table; rendered names no
longer act as semantic lookup keys. Internal specialization slots provide collision-
free lookup, and emission presentation is selected separately, retaining canonical
internal context when local template arguments make source spelling ambiguous.

Retained arguments now encode whether an item is a pack expansion, so fixed and
expanding patterns cannot merge or match accidentally. The same typed construction
path handles dependent non-type parameter types, ambiguous non-type `Name...` syntax,
qualified function types, and dependent array shapes. Nested template-id deduction
walks canonical stored arguments by index rather than copying both argument vectors.
Shape materialization, cache reuse, and deduction visits are explicit counters.

Representative scaling with 8/16/32 related partials and the same number of unique
keys produced 64/256/1024 candidate visits, exactly 8/16/32 shape materializations,
194/422/813 KiB peak semantic storage, and five-run median semantic times of
1.269/2.272/4.313 ms. Checked-in ordering cases performed 2, 4, and 2 comparisons.
This supports the expected O(k*s) related-candidate work, match-only ordering, and
near-linear retained storage; no unrelated-template scan or repeated shape build was
observed.

Validation preserves the checkpoint baseline and all earlier stages: PA22 is
111/308 handout tests (112/309 including the new retained-owner regression), with
exactly eight former failures repaired and no new PA22 failures; PA1–PA21 pass
2329/2329. The PA22 file audit passes with 13 warning-only existing header-division
advisories, and `git diff --check` is clean.

## Checkpoint Audit Ledger

| Checkpoint | Audit disposition | Evidence |
|---|---|---|
| `5d70a120` canonical partial-specialization selection | Pass after checkpoint repair | Selected owner/revision/substitution survives shell completion; canonical typed identity and pack shape are retained; PA22 103 -> 111 with no regressions, prior 2329/2329, file audit pass. |

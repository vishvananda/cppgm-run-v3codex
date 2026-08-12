# PA27 Plan

## Stage Design and Spec Alignment

PA27 extends the PA26 pipeline in place:

`syntax -> canonical owner/member value -> selected binding/base path -> typed LowIR`

Per `spec.md` sections 2-4 and 6, member-pointer owners, targets, null state,
constexpr payloads, and base paths are semantic facts; lowering consumes them
without lookup or rendered-name parsing. Sections 8-9 require work proportional
to syntax, class edges, candidates, constexpr steps, and lowered nodes. Virtual
member-pointer ABI and multi-vptr RTTI remain outside PA27.

## Current Failure Map

The stage baseline was **19/96**, this checkpoint started at **83/96**, and the
current report is **88/96**. The complete remaining 8-test set is:

| Tests | Shared behavior | Owner |
|---:|---|---|
| 5 | constructor NTTP categories plus inherited, ref-qualified, access, and ambiguous-member SFINAE | PA19-PA24 substitution |
| 1 | qualified friend-template grant and ADL | PA22 friend lookup/access |
| 1 | direct `using` member-template hiding of inherited instantiations | PA11/PA19 lookup graph |
| 1 | canonical member-function-template NTTP deduplication | PA19 identity/demand |

## Active Checkpoint

**Member-pointer substitution viability (5 tests).** Per `spec.md` sections
2-4, substituted member access, ref qualification, ambiguity, and value
category become candidate-local semantic facts; failed formation must discard
only that candidate. PA19-PA24 own the flow from canonical template arguments
through bounded class lookup and `decltype`/constructor viability to overload
selection. Expected work is O(substituted syntax + lookup/base edges + viable
candidates), with no rendered-name keys. Validate the five focused cases,
PA27 report, through-PA26 report, a candidate-scaling witness, and file audit.

## Performance Evidence

`CPPGM_FRONTEND_STATS=1` measurements on Linux x86_64:

| Witness | Evidence |
|---|---:|
| declarators 64/128/256 | parser storage 18,578 / 37,010 / 73,874 B |
| overloads 32/64/128 | candidate visits 128 / 256 / 512; deduction visits 129 / 257 / 513 |
| forwarded member pointers | 30 constexpr calls, 8 cache hits, 31 steps, depth 3, 11 object projections |
| local-static function-member-pointer array | 23 semantic nodes, 3 runtime-initializer visits, 11 lowered nodes |
| dependent NTTP target | 3 candidate-index visits, 17 deduction visits, 13 specialization requests/8 cache hits, 1 dependency edge |
| retained recollection | 5 candidate-index visits, 8 deduction visits, 18 specialization requests/10 cache hits, 1 dependency edge |

Candidate replay scales linearly; forwarded values are cached by canonical
member identity/payload, and initializer relocation scanning is one subtree
walk.

## Completed Checkpoints

| Checkpoint | Result | Validation |
|---|---|---|
| Multi-base projection and single-vptr `dynamic_cast<void*>` | explicit offsets, lookup, pack bases, lifecycle | PA27 30/96; through PA26 3717/3717 |
| Member-pointer syntax, values, and runtime application | owner parsing, null/address ABI, conversions, bool, `.*`/`->*`, indirect calls, data NTTPs | PA27 60/96; through PA26 3717/3717 |
| Dependent owner deduction and target replay | partial matching, overloaded addresses, packs, retained demand | PA27 74/96; linear 32/64/128 visits; audit pass |
| Object semantics, constexpr forwarding, and static relocation | cv/ref categories, prvalues, canonical constexpr payloads, inherited conversions, dead demand, exact relocation and zero-fill | PA27 83/96; 9/9 focused; through PA26 3717/3717; stats above; audit pass |
| Retained class scope, dependent targets, and NTTP execution identity | validator scope publication, non-type explicit target deduction, canonical member binding, demand edges, indirect lowering | PA27 88/96; 5 new passes; through PA26 3717/3717; stats above; audit pass |

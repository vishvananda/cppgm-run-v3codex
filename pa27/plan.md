# PA27 Plan

## Stage Design and Spec Alignment

PA27 extends the PA26 pipeline in place:

`syntax -> canonical owner/member value -> selected binding/base path -> typed LowIR`

Per `spec.md` sections 2-4 and 6, member-pointer owners, targets, null state,
constexpr payloads, access, and base paths are semantic facts; lowering consumes
them without lookup or rendered-name parsing. Sections 8-9 require work
proportional to syntax, class edges, candidates, constexpr steps, and lowered
nodes. Virtual member-pointer ABI and multi-vptr RTTI remain outside PA27.

## Current Failure Map

The stage baseline was **19/96**, this turn started at **88/96**, and PA27 is
now **96/96**. The closed turn set grouped as follows:

| Tests | Shared behavior | Owner | Result |
|---:|---|---|---|
| 5 | member-pointer NTTP category, ambiguity, access, and invocation SFINAE | PA19-PA27 substitution | pass |
| 1 | qualified friend-template access through a secondary base | PA12/PA22 access | pass |
| 1 | structured `using` member-template publication and hiding | PA11/PA19 lookup | pass |
| 1 | direct member-template hiding and canonical NTTP target selection | PA19 lookup/identity | pass |

## Active Checkpoint

**Lookup/publication closure (complete; exit validation).** `spec.md` sections
2-4 require canonical identity and candidate-local failure, with direct member
declarations hiding inherited names before overload filtering. Owners are the
structured-name carrier scope and canonical binding; data flows through direct
and graph lookup, substitution, access, then lowering. Work is O(structured
syntax + visited base/using edges + viable candidates), using indexed scopes
and signatures. Validation is the exact PA27 report, through-PA26 report,
representative frontend counters, file audit, diff checks, and a clean commit.

## Performance Evidence

`CPPGM_FRONTEND_STATS=1` measurements on Linux x86_64:

| Witness | Evidence |
|---|---:|
| declarators 64/128/256 | parser storage 18,578 / 37,010 / 73,874 B |
| overloads 32/64/128 | candidate visits 128 / 256 / 512; deduction visits 129 / 257 / 513 |
| forwarded member pointers | 30 constexpr calls, 8 cache hits, 31 steps, depth 3, 11 object projections |
| dependent NTTP target | 3 candidate-index visits, 17 deduction visits, 13 specialization requests/8 hits |
| retained recollection | 5 candidate-index visits, 8 deduction visits, 18 specialization requests/10 hits |
| ambiguous member NTTP | 237 lookup queries, 248 scope visits, 40 edge visits, 9 candidate-index visits |
| structured using chain | 424 lookup queries, 619 scope visits, 170 edge visits, 53 specialization requests/39 hits |
| qualified friend through second base | 157 lookup queries, 163 scope visits, 4 edge visits, 18 access-path visits |

The scaling witnesses remain linear; the final cases traverse only indexed
lookup/base edges and reuse canonical specialization caches.

## Completed Checkpoints

| Checkpoint | Result | Validation |
|---|---|---|
| Multi-base projection and single-vptr `dynamic_cast<void*>` | explicit offsets, lookup, pack bases, lifecycle | PA27 30/96; through PA26 3717/3717 |
| Member-pointer syntax, values, and runtime application | owner parsing, null/address ABI, conversions, indirect calls, data NTTPs | PA27 60/96; through PA26 3717/3717 |
| Dependent owner deduction and target replay | partial matching, overloaded addresses, packs, retained demand | PA27 74/96; linear overload witness; audit pass |
| Object semantics, constexpr forwarding, and static relocation | cv/ref categories, prvalues, canonical payloads, demand, relocation | PA27 83/96; through PA26 3717/3717; audit pass |
| Retained scopes, dependent targets, and NTTP execution identity | publication, target deduction, member identity, lowering | PA27 88/96; through PA26 3717/3717; audit pass |
| Member-pointer substitution viability | NTTP prvalues, base/type ambiguity, protected objects, strict `.*` ref qualification | PA27 93/96; 5/5 focused |
| Lookup/publication closure | direct hiding, structured using owner, secondary-base friend access | PA27 96/96; 3/3 focused |

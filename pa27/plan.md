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

The stage baseline was **19/96**, this checkpoint started at **74/96**, and the
current report is **83/96**. The complete remaining 13-test set is:

| Tests | Shared behavior | Owner |
|---:|---|---|
| 7 | dependent target lookup, friend ADL, constructor NTTPs, inherited/ref-qualified SFINAE, recollection, and imported operator templates | PA19-PA24 lookup/substitution |
| 6 | NTTP partial-specialization identity, access SFINAE, ref-qualified calls, ambiguous arguments, and deduplicated execution demand | PA19-PA24 identity plus PA15 demand |

## Active Checkpoint

**Dependent member-pointer NTTP formation and SFINAE (7 tests).** Retain the
target owner/type through dependent lookup, friend ADL, constructor selection,
inherited collisions, ref qualifiers, recollection, and using-imported member
templates. The owner is PA19-PA24 substitution and lookup; data flows from
retained syntax and template arguments to one canonical specialization/member
binding, then to ordinary call selection. Expected work is O(retained syntax +
lookup edges + viable candidates), using existing indexes and bounded base
walks. Validate the seven compile failures, PA27 report, through-PA26 report,
and file audit.

## Performance Evidence

`CPPGM_FRONTEND_STATS=1` measurements on Linux x86_64:

| Witness | Evidence |
|---|---:|
| declarators 64/128/256 | parser storage 18,578 / 37,010 / 73,874 B |
| overloads 32/64/128 | candidate visits 128 / 256 / 512; deduction visits 129 / 257 / 513 |
| forwarded member pointers | 30 constexpr calls, 8 cache hits, 31 steps, depth 3, 11 object projections |
| local-static function-member-pointer array | 23 semantic nodes, 3 runtime-initializer visits, 11 lowered nodes |

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

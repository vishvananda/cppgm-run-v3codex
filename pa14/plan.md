# PA14 Implementation Plan

## Stage Design and Spec Alignment

PA14 owns the standalone `abimangle` adapter: normalized lines are parsed once
into `AbiFactFile`, resolved per case into stable typed ABI entities, and passed
directly to one Itanium encoder. `dev/abimangle.cpp` owns only CLI/file I/O;
`dev/src/abi_mangle.cpp` owns parsing, typed resolution, canonical identity,
substitution, and emission. This applies `spec.md` §§2, 6, 8, and 9: ABI facts
have compact per-case identities, strings are emitted presentation rather than
semantic keys, later stages can call the typed encoder without a text round
trip, mutable substitution state is name-local, and work is linear or
amortized-linear in input facts plus emitted structure.

## Current Failure Map

No current failures: 111/111 pass. The turn-start 109-case failure set was
resolved at its shared boundaries:

| Group | Tests | Shared behavior / owner |
|---|---:|---|
| Normalized reader and basic targets/types | 23 `100-*` | Complete |
| Functions and ABI entry points | 25 `200-*` | Complete |
| Templates and substitution | 37 `300-*` | Complete |
| Dependent owner/type forms | 4 `400-*` | Complete |
| Dependent expressions | 13 `500-*` | Complete |
| Nested/local handoff cases | 7 `600-*` | Complete |

## Active Checkpoint

Checkpoint complete. The strict reader validates case-local binders and
indices; the resolver interns equivalent types, arguments, and expressions by
typed child IDs; the encoder consumes those IDs, records substitutions in ABI
encounter order, and emits every PA14 target. Hash-indexed lookup and canonical
IDs give expected O(F + E) time and O(F) live typed state for F facts and E
emitted bytes. Canonical serialize/parse round trips preserve the mangled output
of all 109 positive fixtures.

## Performance Evidence

Substitution-heavy `300-std-vector-string-substitution.t` was processed as
1,000/2,000/4,000 ordered input cases in 0.24/0.51/0.90 seconds (user time
0.18/0.38/0.71 seconds), with peak RSS 4,332/4,600/5,112 KiB. Doubling work is
approximately linear while memory growth is sublinear, consistent with
per-case typed ownership and expected O(F + E) work.

## Completed Checkpoints

| Date | Checkpoint | Result |
|---|---|---|
| 2026-08-06 | Typed fact graph and complete Itanium encoder | 111/111 PA14; 1031/1031 through PA14; file audit pass |

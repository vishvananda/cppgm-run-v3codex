# PA9 Implementation Plan

## Stage Design and Spec Alignment

`cy86` reuses the PA5 streaming preprocessor/post-token cursor, parses each
semicolon-bounded CY86 statement once into typed operands, interns identifiers,
and resolves labels through one direct index.  The backend lowers those facts
directly to a compact byte buffer plus absolute-address fixups and writes one
ELF load segment; no textual IR, assembly, host compiler, or external assembler
is on the production path.  Source buffers die after each translation unit,
statement-token scratch is cleared at each semicolon, and typed program facts
remain owned by one compilation.

Relevant `spec.md` requirements are the forward production flow and streaming
cursor (§1), stable interned identities and O(1)-average equality (§2), direct
typed lowering with one emission per unit (§6), direct ELF emission with linear
selection/encoding (§7), explicit phase ownership (§8), linear parsing/lowering
and observable work counters (§9), and self-contained output (§10).

## Current Failure Map

| Group | Tests | Resolution |
|---|---:|---|
| Frontend, rejection, and layout | 7 | Typed parser, semantic checks, and deterministic aligned layout; 7/7 pass |
| Integer execution | 8 | Direct x86 integer/control/syscall lowering; 8/8 pass |
| Floating execution | 3 | Bounded x87 lowering with unsigned-64 correction paths; 3/3 pass |

Turn-start baseline: **0/18**.  Current state: **18/18**, with PA1–PA8 at
399/399 and the file audit passing.

## Active Checkpoint

**PA9 full stage — complete.**  Typed parsing, semantic validation, direct
integer/x87 lowering, fixups, and ELF writing are implemented.  Final evidence:
PA9 18/18, PA1–PA8 399/399, through-PA9 417/417, file audit 35 files clean, and
the raw 80-bit move smoke check exits zero.  Only commit and clean-worktree
verification remain.

## Performance Evidence

`CPPGM_FRONTEND_STATS=1` measurements on the final architecture show bounded
statement scratch (peak 4 tokens for noop, 16 for both calculators).  Noop:
5 tokens, 1 statement, 28 instruction bytes, 1.07 ms, 4.3 MiB RSS.  Integer
calculator: 5,513 tokens, 884 statements, 961 fixups, 22,414 instruction bytes,
7.22 ms, 5.1 MiB RSS.  Floating calculator: 2,702 tokens, 448 statements, 376
fixups, 10,322 instruction bytes, 4.82 ms, 4.8 MiB RSS.  Emitted bytes and work
counters track typed input/output linearly; no unexpected slow path appeared.

## Completed Checkpoints

| Checkpoint | Result | Validation |
|---|---|---|
| Typed frontend and direct integral backend | Complete; PA9 improved from 0/18 to 15/18 | 8/8 non-floating assignment programs and 7/7 course parser/layout cases pass |
| Typed x87 floating backend | Complete; PA9 improved from 15/18 to 18/18 | All three million-vector floating fixtures pass; full PA9 report is 18/18 |

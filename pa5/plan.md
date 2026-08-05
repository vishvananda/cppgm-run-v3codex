# PA5 Plan

## Stage Design and Spec Alignment

PA5 owns translation phases 4–6 and phase-7 token recognition. A fresh preprocessing session per primary source owns the macro table, conditional stack, include/pragma-once state, presumed source locations, and short-lived logical-line storage. Immutable source buffers stream through PA1 tokenization; active lines are macro-expanded once and feed the shared PA2 post-token session directly. Includes recurse through the same session without emitting `sof`/`eof`; only primary files create those boundaries.

This applies `spec.md` §§1, 8, and 9: one forward token cursor, interned identifier identity, bounded line/expansion storage, explicit per-primary ownership, O(source bytes + emitted expansion tokens) preprocessing, and observable work counters. It preserves the later production path by exposing preprocessing as a shared phase rather than a textual subprocess.

## Current Failure Map

Baseline was 0/70 because `preproc` returned `EXIT_NOT_IMPLEMENTED`. Current map: no PA5 failures (70/70) and no PA1–PA4 regressions (171/171). The resolved groups were active text/PA2 output; macro replacement and diagnostics; conditionals/`defined`; includes/pragma-once; predefined and presumed-location macros/`#line`; pragmas/`_Pragma`; source isolation and directive errors.

## Active Checkpoint

Complete: the preprocessing-session boundary and `preproc` entry point implement all listed directives and inactive-group structural rules; macro-expanded `#if`, `#include`, and `#line`; predefined macros; physical/presumed locations; recursive include search and file-identity pragma-once; post-expansion `_Pragma`; invalid-token/error failure; independent primary-source state and exact PA5 framing.

Owner/data flow: `preproc` owns CLI, immutable primary input, timestamp, and rendering; the shared front end owns interned tokens, macro/conditional/include state, expansion, typed expression evaluation, and direct PA2 emission. Complexity remains O(B + E) average over source bytes B and expanded tokens E, with average-O(1) macro/file-identity lookup; live ordinary storage is one source buffer, one logical line, and bounded expansion frames, plus buffers on the active include stack. Validation is complete via focused tests, `make test-pa5`, the required PA5 report, through-PA4 report, and file audit.

## Performance Evidence

The 190,206-byte repeated-argument fixture completed in 0.06 s wall and 14,264 KiB peak RSS. Counters reported 88,158 preprocessing tokens, 8,002 definitions/invocations, 8,067 macro lookups, one argument prescan, 24,065 expanded tokens, peak rescan 64, and peak expansion depth 2. Lookups/invocations track the 8,001-link chain linearly, while the argument is prescanned once before 64 substitutions.

## Completed Checkpoints

| Checkpoint | Result |
|---|---|
| Full PA5 preprocessing session | 70/70 PA5; 171/171 PA1–PA4; file audit pass; stress counters linear in required work |

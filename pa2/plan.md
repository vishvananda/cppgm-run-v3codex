# PA2 Plan

## Stage Design and Spec Alignment

PA2 extends the shared PA1 streaming boundary: immutable input buffer ->
`TokenizePreprocessingFile` -> `IPPTokenStream` analyzer -> typed
`IPostTokenStream`; `posttoken` renders the final events as a textual tool view.
The analyzer owns literal recognition and ABI byte encoding, emits ordinary tokens
immediately, and retains only the current maximal string-literal sequence.  This
applies `spec.md` sections 1, 8, 9, and 10: one forward flow, a typed phase
boundary, bounded phase-local ownership, O(source bytes + token/code-unit output)
work, and no compiler/reference subprocesses or test-specific answers.

## Current Failure Map

No open failure groups.  Turn-start baseline was PA1 53/53 and PA2 0/26;
current reports are PA1 53/53, PA2 26/26, and through-PA2 79/79.

## Active Checkpoint

No active checkpoint: the full PA2 stage checkpoint is complete and validated.
The next assignment can consume the typed stream without parsing PA2 text.

## Performance Evidence

`700-hard-string-concat.t` has 429,984 bytes, 14,400 lines, and 40,609 expected
tokens.  Direct runs to `/dev/null` measured 1x/2x/4x input at
0.06/0.16/0.27 seconds and 4,064/4,576/5,320 KiB peak RSS.  Work scales with
input/output; memory growth tracks the immutable source buffer while adjacent
string buffering remains bounded by one maximal sequence.

## Completed Checkpoints

| Checkpoint | Result |
| --- | --- |
| Typed streaming post-token recognition, literal decoding, and phase-6 string concatenation | PA2 26/26; PA1 53/53; through-PA2 79/79; file audit pass |

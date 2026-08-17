# Proposed PA29 Tests

This directory holds native-lowering regression candidates whose behavior is
valid but whose machine-IR shape differs from the course reference compiler.
They are not part of the active golden suite.

`discarded-slots-do-not-reserve-frame.t` checks that scalar slots proven to
contain only dead stores do not consume frame space.  The current compiler
removes the three dead stores and emits one retained slot in a 16-byte frame.
The reference compiler retains all four slots and emits an 80-byte frame, so
its generated MIR cannot be used as the oracle for this optimization.

`constant-byte-store-coalescing.t` checks the runtime result of initializing
16 adjacent bytes.  It exercises the native encoder's coalescing of repeated
constant byte stores without changing MIR.  The reference program exits
successfully, but its MIR assigns the preserved base and derived addresses to
different physical registers (and retains unrelated `main` scratch space), so
the reference dump cannot serve as this compiler's structural fixture.

`flag-safe-zero-materialization.t` exercises zero-valued register results and
a compare-to-branch consumer.  Its only new purpose is to inspect that dead
condition flags permit the shorter `xor r32,r32` encoding while the textual
MIR remains a `mov` of zero.  Program behavior alone would duplicate active
coverage, and the course harness does not compare native instruction bytes,
so the representation-specific candidate remains proposed.

`zero-compare-test-encoding.t` covers u32 and i64 comparisons with zero across
unsigned and equality predicates.  It is intended to inspect that the native
bytes use width-correct `test reg,reg` instructions while the baseline MIR
remains `cmp ..., 0`.  Active PA29 tests already cover the behavior and the
harness does not compare native bytes, so this representation-only candidate
remains proposed.

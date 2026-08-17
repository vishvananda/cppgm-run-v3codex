# Proposed PA29 Tests

This directory holds native-lowering regression candidates whose behavior is
valid but whose machine-IR shape differs from the course reference compiler.
They are not part of the active golden suite.

`discarded-slots-do-not-reserve-frame.t` checks that scalar slots proven to
contain only dead stores do not consume frame space.  The current compiler
removes the three dead stores and emits one retained slot in a 16-byte frame.
The reference compiler retains all four slots and emits an 80-byte frame, so
its generated MIR cannot be used as the oracle for this optimization.

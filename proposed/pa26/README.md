# Proposed PA26 Tests

This directory holds PA26 regression candidates that are not part of the active
golden suite.  A test belongs here when it exercises required semantics but the
course reference compiler uses a different, semantically valid LowIR layout.

Promoting a test into `cppgm.tests/course/pa26/` requires an explicit decision
about the intended LowIR shape.  Generate any active `.ref` files only through
the documented `make ref-test-pa26` target.

`200-constructor-unwind-shares-generated-suffix.t` verifies the runtime meaning
of a shared constructor-unwind cleanup chain: if construction of the eighth
member throws, exactly the first seven completed members must be destroyed.  The
current compiler shares those destructor suffixes, while the reference compiler
duplicates each preceding destructor prefix in a separate dispatch block.

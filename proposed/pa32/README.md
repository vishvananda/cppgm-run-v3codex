# Proposed PA32 object-shape tests

These tests record useful native-object properties whose fixture ownership is
still being compared with the pinned assignment reference.  Move a test into
`cppgm.tests/course/pa32/` only when the reference agrees with the structural
expectation; runtime behavior alone does not prove an emission-size property.

`unreachable-internal-function-pruning.t` remains proposed because the pinned
reference defines both internal functions, while the candidate deliberately
omits the unreferenced one.  Both retain and execute the address-used control.

`post-optional-inline-weak-pruning.t` records the tempting per-object O1/O2
optimization of dropping a weak inline helper after its last local call is
inlined.  The pinned reference agrees when invoked explicitly with `-O2`, but
the documented PA32 default-mode reference and existing PA32 symbol fixtures
retain weak/COMDAT definitions.  A prototype failed 50 PA32/PA33 report tests,
so the test and optimization remain proposed until cross-object ownership can
be proved without weakening those contracts.

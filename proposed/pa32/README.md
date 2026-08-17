# Proposed PA32 object-shape tests

These tests record useful native-object properties whose fixture ownership is
still being compared with the pinned assignment reference.  Move a test into
`cppgm.tests/course/pa32/` only when the reference agrees with the structural
expectation; runtime behavior alone does not prove an emission-size property.

`unreachable-internal-function-pruning.t` remains proposed because the pinned
reference defines both internal functions, while the candidate deliberately
omits the unreferenced one.  Both retain and execute the address-used control.

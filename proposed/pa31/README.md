# Proposed PA31 Tests

This directory holds host-EH metadata regression candidates that do not have
an authoritative PA31 reference generator.  PA31 has no standalone reference
binary, so an exact LSDA-layout check stays proposed unless the upstream
assignment fixtures already establish the same representation.

`adjacent-lsda-call-site-coalescing.t` places two potentially throwing calls
under the same destructor cleanup with only non-throwing arithmetic between
them.  The generated program verifies that the second call still unwinds
through the destructor.  With `cppgm++ --stats`, the structural proof is that
`eh_call_sites` exceeds `eh_lsda_call_sites` while LowIR and MIR remain
unchanged.  The coalesced range may include the arithmetic gap because it
cannot unwind; a potentially throwing unprotected call is an explicit barrier
in the backend's final-layout scan.

`lsda-call-site-coalescing-unit.cpp` is the representation-only boundary
reducer.  It checks that equal landing/action identities coalesce across a
no-throw gap, while an unprotected potentially throwing call, a different
landing pad, or a different action identity prevents the merge.  It can be
built directly with `dev/src/lowir_native_lsda.cpp` and the `dev/src` include
directory; it is proposed rather than an active course test because it binds
the internal metadata representation and PA31 has no authority for that exact
layout.

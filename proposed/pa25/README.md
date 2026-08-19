# Proposed PA25 tests

These inputs exercise useful PA25 behavior but are not active course fixtures
because the pinned reference and the implementation choose different serialized
LowIR presentations.

- `lambda-local-static-per-specialization.t` keeps local-static identities
  distinct across two closures and two function-template specializations.  The
  reference includes source-location material and repeats the lambda component;
  the implementation uses its established compact local-static presentation.
  Both produce distinct objects and equivalent behavior, but the relaxed LowIR
  comparison intentionally still treats symbol spelling as observable.

# Proposed PA11 tests

These inputs preserve earlier fixture forms that are not active because they
are ill-formed C++11. GCC and Clang reject both forms in strict C++11 mode.

- `scoped-enum-integral-comparison.t` compares a scoped enumerator directly
  with an integer. Scoped enumerations do not implicitly convert to integers.
- `namespace-nonstatic-anonymous-union.t` declares an anonymous union at
  namespace scope without `static`, which the language requires.

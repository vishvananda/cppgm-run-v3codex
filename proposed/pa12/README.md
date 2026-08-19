# Proposed PA12 tests

These inputs exercise useful PA12 behavior but are not active course fixtures
because the pinned reference disagrees.

- `local-type-generated-identity-not-in-lookup.t` declares a block-scope user
  type spelled `__local_type1` beside a named anonymous-union variable whose
  generated identity uses the same spelling.  The identifier is reserved to
  the implementation, so no conforming program is affected either way.  The
  implementation keeps generated identities out of ordinary lookup and
  accepts the program; the reference resolves its generated spelling through
  lookup and rejects it as an incompatible redeclaration.  The related
  typedef-named anonymous-struct case is also accepted by both, but the
  reference presents such classes by their typedef linkage name while the
  implementation uses its generated `__local_typeN` presentation, so an exact
  semantic-dump fixture cannot gate it.

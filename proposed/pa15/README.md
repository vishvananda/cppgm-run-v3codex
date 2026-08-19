# Proposed PA15 tests

These inputs exercise useful PA15 behavior but are not active course fixtures
because the pinned reference disagrees.

- `reserved-pattern-local-names.t` gives user locals spellings that collide
  with compiler-generated temporary and force-inline names.  Serialized
  LowIR must keep every user spelling and steer generated temporaries away
  from collisions; both implementations do, but the reference reserves
  colliding `tN` ordinals incrementally while the implementation reserves
  every source ordinal in the function up front, so the generated numbering
  differs and an exact fixture cannot gate it.  Object-only compilation is
  reservation-free in the implementation: the T5a probe compiled this input
  byte-identically with the source scan enabled and disabled.

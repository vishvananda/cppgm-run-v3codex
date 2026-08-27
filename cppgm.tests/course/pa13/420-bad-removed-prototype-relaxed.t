declare function @legacy() -> i64 [arity=prototype_relaxed]

function @main() -> i64 [role=entry] {
  block ^entry:
    return i64 0
}

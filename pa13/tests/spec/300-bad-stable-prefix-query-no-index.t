declare function @bad() -> i64 [query=stable_prefix]

function @main() -> i64 [role=entry] {
  block ^entry:
    return i64 0
}

function @main() -> i64 [role=entry] {
  block ^entry:
    branch 0, ^impossible, ^normal

  block ^impossible:
    unreachable

  block ^normal:
    return i64 0
}

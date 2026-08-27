global @bad : i64 [role=unreachable] = 0

function @main() -> i64 [role=entry] {
  block ^entry:
    return i64 0
}

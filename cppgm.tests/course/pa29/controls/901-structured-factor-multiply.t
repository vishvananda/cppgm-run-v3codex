function @main() -> i64 [role=entry, unwind=no] {
  block ^entry:
    %result = binary mul i64 7, 24
    return i64 %result
}

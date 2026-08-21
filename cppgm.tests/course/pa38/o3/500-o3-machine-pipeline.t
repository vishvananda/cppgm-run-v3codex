function @main() -> i64 [role=entry] {
  block ^entry:
    %value = binary add i64 40, 2
    jump ^next

  block ^next:
    %same = copy i64 %value
    %bad = cmp ne i64 %same, 42
    return i64 %bad
}

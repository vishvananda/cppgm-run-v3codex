function @choose(%condition : i64) -> i64 [unwind=no] {
  block ^entry:
    branch %condition, ^yes, ^no

  block ^yes:
    return i64 11

  block ^no:
    return i64 22
}

function @main() -> i64 [role=entry] {
  block ^entry:
    %value = call i64 @choose(1)
    %bad = cmp ne i64 %value, 11
    return i64 %bad
}

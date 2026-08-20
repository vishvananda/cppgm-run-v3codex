function @zero() -> i64 [binding=strong] {
  block ^entry:
    return i64 0
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %value = call i64 @zero()
    %bad = cmp ne i64 %value, 0
    branch %bad, ^bad, ^ok

  block ^bad:
    return i32 1

  block ^ok:
    return i32 0
}

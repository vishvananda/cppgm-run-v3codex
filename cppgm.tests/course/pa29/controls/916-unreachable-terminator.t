function @guarded(%condition : i64) -> i64 [no_inline=yes] {
  block ^entry:
    branch %condition, ^impossible, ^normal

  block ^impossible:
    unreachable

  block ^normal:
    return i64 0
}

function @main() -> i64 [role=entry] {
  block ^entry:
    %result = call i64 @guarded(0)
    return i64 %result
}

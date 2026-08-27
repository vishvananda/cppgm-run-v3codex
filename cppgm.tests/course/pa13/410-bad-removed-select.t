function @main(%condition : i64) -> i64 [role=entry] {
  block ^entry:
    %picked = select i64 %condition, 1, 0
    return i64 %picked
}

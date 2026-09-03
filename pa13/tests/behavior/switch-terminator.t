function @main() -> i64 [role=entry] {
  block ^entry:
    %which = const i64 1
    %case0 = const i64 0
    %case1 = const i64 1
    switch %which, ^miss, %case0:^zero, %case1:^one

  block ^zero:
    return i64 5

  block ^one:
    return i64 0

  block ^miss:
    return i64 9
}

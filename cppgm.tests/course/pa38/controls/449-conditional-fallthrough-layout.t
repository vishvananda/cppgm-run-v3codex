function @pure_trace() -> i64 [unwind=no] {
  block ^entry:
    jump ^done

  block ^cold:
    return i64 1

  block ^done:
    return i64 0
}

function @conditional_fallthrough(%enter : i64, %take_other : i64,
                                  %choose : i64) -> i64 [unwind=no] {
  block ^entry:
    branch %enter, ^steal_hot, ^dispatch

  block ^steal_hot:
    jump ^hot

  block ^dispatch:
    branch %take_other, ^steal_other, ^guard

  block ^steal_other:
    jump ^other

  block ^guard:
    branch %choose, ^hot, ^other

  block ^hot:
    return i64 0

  block ^other:
    return i64 1
}

function @main() -> i64 [role=entry, unwind=no] {
  block ^entry:
    %trace = call i64 @pure_trace()
    %guarded = call i64 @conditional_fallthrough(0, 0, 1)
    %bad = binary or i64 %trace, %guarded
    return i64 %bad
}

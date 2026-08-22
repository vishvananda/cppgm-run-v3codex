declare function @fail(%code : i64) -> void [return=noreturn]

function @guarded(%value : i64) -> i64 {
  block ^entry:
    %bad = cmp ugt i64 %value, 100
    branch %bad, ^raise, ^ok

  block ^raise:
    call void @fail(7)
    return i64 0

  block ^ok:
    %sum = binary add i64 %value, 1
    return i64 %sum
}

function @chained(%value : i64) -> i64 {
  block ^entry:
    %bad = cmp ugt i64 %value, 100
    branch %bad, ^prepare, ^ok

  block ^prepare:
    %code = binary add i64 %value, 9
    jump ^raise

  block ^raise:
    call void @fail(%code)
    return i64 0

  block ^ok:
    %sum = binary add i64 %value, 2
    return i64 %sum
}

function @keeps_live_successor(%value : i64) -> i64 {
  block ^entry:
    %bad = cmp ugt i64 %value, 100
    branch %bad, ^observe, ^ok

  block ^observe:
    call void @fail(%value)
    jump ^ok

  block ^ok:
    %sum = binary add i64 %value, 3
    return i64 %sum
}

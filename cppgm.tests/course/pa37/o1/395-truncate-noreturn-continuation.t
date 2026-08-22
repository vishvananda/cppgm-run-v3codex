declare function @fail(%code : i64) -> void [return=noreturn]

function @select(%value : i64) -> i64 {
  block ^entry:
    %bad = cmp ugt i64 %value, 100
    branch %bad, ^raise, ^ok

  block ^raise:
    call void @fail(%value)
    jump ^merge

  block ^ok:
    %sum = binary add i64 %value, 5
    jump ^merge

  block ^merge:
    %result = phi i64 [^raise: 0, ^ok: %sum]
    return i64 %result
}

function @keeps_reachable_tail(%value : i64, %gate : u8) -> i64 {
  block ^entry:
    branch %gate, ^observe, ^ok

  block ^observe:
    %code = binary add i64 %value, 1
    jump ^merge

  block ^ok:
    %sum = binary add i64 %value, 5
    jump ^merge

  block ^merge:
    %result = phi i64 [^observe: %code, ^ok: %sum]
    return i64 %result
}

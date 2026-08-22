declare function @fail(%message : ptr) -> void [return=noreturn]
declare function @observe(%message : ptr) -> void

global @message = {
  zero 8
}

function @single_cold_use(%value : i64) -> i64 {
  block ^entry:
    %text = addr @message
    %bad = cmp ugt i64 %value, 100
    branch %bad, ^raise, ^ok

  block ^raise:
    call void @fail(%text)
    return i64 0

  block ^ok:
    %sum = binary add i64 %value, 1
    return i64 %sum
}

function @shared_cold_uses(%value : i64) -> i64 {
  block ^entry:
    %text = addr @message
    %bad = cmp ugt i64 %value, 100
    branch %bad, ^first, ^middle

  block ^first:
    call void @fail(%text)
    return i64 0

  block ^middle:
    %worse = cmp ugt i64 %value, 200
    branch %worse, ^second, ^ok

  block ^second:
    call void @fail(%text)
    return i64 0

  block ^ok:
    %sum = binary add i64 %value, 2
    return i64 %sum
}

function @keeps_hot_use(%value : i64) -> i64 {
  block ^entry:
    %text = addr @message
    call void @observe(%text)
    %bad = cmp ugt i64 %value, 100
    branch %bad, ^raise, ^ok

  block ^raise:
    call void @fail(%text)
    return i64 0

  block ^ok:
    %sum = binary add i64 %value, 3
    return i64 %sum
}

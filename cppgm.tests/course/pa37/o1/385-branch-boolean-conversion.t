function @truth_trunc(%value : i64) -> i64 {
  block ^entry:
    %cmp = cmp eq i64 %value, 0
    %flag = convert trunc u8 i64 %cmp
    branch %flag, ^zero, ^nonzero

  block ^zero:
    return i64 7

  block ^nonzero:
    return i64 9
}

function @truth_widen(%flag : u8) -> i64 {
  block ^entry:
    %wide = convert zext i64 u8 %flag
    branch %wide, ^set, ^clear

  block ^set:
    return i64 1

  block ^clear:
    return i64 2
}

function @keeps_value_truncation(%value : i64) -> i64 {
  block ^entry:
    %narrow = convert trunc u8 i64 %value
    branch %narrow, ^set, ^clear

  block ^set:
    return i64 3

  block ^clear:
    return i64 4
}

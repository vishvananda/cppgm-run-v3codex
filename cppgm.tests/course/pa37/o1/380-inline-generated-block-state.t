function @entry(%value : i64) -> i64 {
  block ^entry:
    %result = call i64 @branch(%value)
    return i64 %result
}

function @branch(%value : i64) -> i64 {
  block ^entry:
    %positive = cmp gt i64 %value, 0
    branch %positive, ^yes, ^no

  block ^yes:
    %adjusted = call i64 @leaf(%value)
    return i64 %adjusted

  block ^no:
    return i64 0
}

function @leaf(%value : i64) -> i64 {
  block ^entry:
    %adjusted = binary add i64 %value, 1
    return i64 %adjusted
}

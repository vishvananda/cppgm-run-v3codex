function @main() -> i64 {
  slot $value : i64

  block ^entry:
    store i64 7, $value
    jump ^loop

  block ^loop:
    %current = load i64 $value
    %done = cmp ge i64 %current, 7
    branch %done, ^exit, ^latch

  block ^latch:
    %prior = load i64 $value
    %next = binary add i64 %prior, 0
    store i64 %next, $value
    jump ^loop

  block ^exit:
    %result = load i64 $value
    return i64 %result
}

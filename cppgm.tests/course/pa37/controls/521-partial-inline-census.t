global @sink : i64 = 0

function @guarded(%value : i64) -> i64 [binding=internal, unwind=no] {
  block ^entry:
    branch %value, ^fast, ^slow

  block ^fast:
    return i64 7

  block ^slow:
    jump ^loop

  block ^loop:
    store i64 %value, @sink
    jump ^loop
}

function @caller(%value : i64) -> i64 [binding=strong] {
  block ^entry:
    %first = call i64 @guarded(%value)
    %second = call i64 @guarded(%value)
    %result = binary add i64 %first, %second
    return i64 %result
}

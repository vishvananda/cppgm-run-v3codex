global @result : i64 = 0

function @strength_reduce() -> i64 [no_inline=yes] {
  block ^entry:
    jump ^header

  block ^header:
    %i = phi i64 [^entry: 0, ^latch: %next]
    %done = cmp ge i64 %i, 4
    branch %done, ^exit, ^body

  block ^body:
    %scaled = binary mul i64 %i, 8
    store i64 %scaled, @result
    jump ^latch

  block ^latch:
    %next = binary add i64 %i, 1
    jump ^header

  block ^exit:
    %answer = load i64 @result
    return i64 %answer
}

function @remove_finite_empty_loop() -> i64 [no_inline=yes] {
  block ^entry:
    jump ^header

  block ^header:
    %i = phi i64 [^entry: 0, ^latch: %next]
    %more = cmp lt i64 %i, 3
    branch %more, ^latch, ^exit

  block ^latch:
    %next = binary add i64 %i, 1
    jump ^header

  block ^exit:
    return i64 7
}

function @keep_unproven_loop(%start : i64) -> i64 [no_inline=yes] {
  block ^entry:
    jump ^header

  block ^header:
    %i = phi i64 [^entry: %start, ^latch: %next]
    %more = cmp ne i64 %i, 7
    branch %more, ^latch, ^exit

  block ^latch:
    %next = binary add i64 %i, 2
    jump ^header

  block ^exit:
    return i64 0
}

function @remove_zero_trip_loop() -> i64 [no_inline=yes] {
  block ^entry:
    jump ^header

  block ^header:
    %i = phi i64 [^entry: 3, ^latch: %next]
    %more = cmp lt i64 %i, 3
    branch %more, ^latch, ^exit

  block ^latch:
    %next = binary add i64 %i, 1
    jump ^header

  block ^exit:
    return i64 9
}

function @remove_one_trip_loop() -> i64 [no_inline=yes] {
  block ^entry:
    jump ^header

  block ^header:
    %i = phi i64 [^entry: 2, ^latch: %next]
    %more = cmp lt i64 %i, 3
    branch %more, ^latch, ^exit

  block ^latch:
    %next = binary add i64 %i, 1
    jump ^header

  block ^exit:
    return i64 11
}

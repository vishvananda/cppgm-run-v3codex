global @left : i64 = 6
global @right : i64 = 4
global @sink : i64 = 0

function @nested() -> i64 [no_inline=yes] {
  block ^entry:
    %left = load i64 @left
    %right = load i64 @right
    jump ^outer_header

  block ^outer_header:
    %outer = phi i64 [^entry: 0, ^outer_latch: %outer_next]
    %outer_more = cmp lt i64 %outer, 2
    branch %outer_more, ^inner_entry, ^exit

  block ^inner_entry:
    jump ^inner_header

  block ^inner_header:
    %inner = phi i64 [^inner_entry: 0, ^inner_latch: %inner_next]
    %inner_more = cmp lt i64 %inner, 2
    branch %inner_more, ^inner_body, ^outer_latch

  block ^inner_body:
    %sum = binary add i64 %left, %right
    store i64 %sum, @sink
    jump ^inner_latch

  block ^inner_latch:
    %inner_next = binary add i64 %inner, 1
    jump ^inner_header

  block ^outer_latch:
    %outer_next = binary add i64 %outer, 1
    jump ^outer_header

  block ^exit:
    %answer = load i64 @sink
    return i64 %answer
}

function @multiple_exits(%limit : i64, %stop : i64,
                         %value : i64) -> i64 [no_inline=yes] {
  block ^entry:
    jump ^header

  block ^header:
    %i = phi i64 [^entry: 0, ^latch: %next]
    %more = cmp lt i64 %i, %limit
    branch %more, ^body, ^normal_exit

  block ^body:
    %adjusted = binary add i64 %value, 5
    store i64 %adjusted, @sink
    branch %stop, ^early_exit, ^latch

  block ^latch:
    %next = binary add i64 %i, 1
    jump ^header

  block ^normal_exit:
    return i64 0

  block ^early_exit:
    return i64 1
}

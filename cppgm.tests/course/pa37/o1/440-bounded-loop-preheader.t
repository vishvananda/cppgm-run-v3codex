global @sink : i64 = 0

function @split_entry_edge(%choose : i64, %left : i64,
                           %right : i64) -> i64 [no_inline=yes] {
  block ^entry:
    branch %choose, ^header, ^bypass

  block ^header:
    %i = phi i64 [^entry: 0, ^latch: %next]
    %more = cmp lt i64 %i, 3
    branch %more, ^body, ^exit

  block ^body:
    %sum = binary add i64 %left, %right
    %difference = binary sub i64 %left, %right
    store i64 %sum, @sink
    store i64 %difference, @sink
    jump ^latch

  block ^latch:
    %next = binary add i64 %i, 1
    jump ^header

  block ^exit:
    return i64 0

  block ^bypass:
    return i64 1
}

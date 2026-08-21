global @debug_sink : i64 = 0

function @keep_location(%limit : i64, %value : i64) -> i64
    [no_inline=yes] !dbg(loop.cpp, 1, 1) {
  block ^entry:
    jump ^header

  block ^header:
    %i = phi i64 [^entry: 0, ^latch: %next]
    %more = cmp lt i64 %i, %limit
    branch %more, ^body, ^exit

  block ^body:
    %adjusted = binary add i64 %value, 1 !dbg(loop.cpp, 7, 5)
    store i64 %adjusted, @debug_sink
    jump ^latch

  block ^latch:
    %next = binary add i64 %i, 1
    jump ^header

  block ^exit:
    return i64 0
}

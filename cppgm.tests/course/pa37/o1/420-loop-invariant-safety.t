declare function @may_write() -> void
global @sink : i64 = 0

function @keep_trapping_divide(%limit : i64, %left : i64,
                               %right : i64) -> i64 [no_inline=yes] {
  block ^entry:
    jump ^header

  block ^header:
    %i = phi i64 [^entry: 0, ^latch: %next]
    %more = cmp lt i64 %i, %limit
    branch %more, ^body, ^exit

  block ^body:
    %quotient = binary div i64 %left, %right
    store i64 %quotient, @sink
    jump ^latch

  block ^latch:
    %next = binary add i64 %i, 1
    jump ^header

  block ^exit:
    return i64 0
}

function @keep_without_preheader(%choose : i64, %limit : i64,
                                 %value : i64) -> i64 [no_inline=yes] {
  block ^entry:
    branch %choose, ^left, ^right

  block ^left:
    jump ^header

  block ^right:
    jump ^header

  block ^header:
    %i = phi i64 [^left: 0, ^right: 0, ^latch: %next]
    %adjusted = binary add i64 %value, 3
    %more = cmp lt i64 %i, %limit
    branch %more, ^latch, ^exit

  block ^latch:
    store i64 %adjusted, @sink
    %next = binary add i64 %i, 1
    jump ^header

  block ^exit:
    return i64 0
}

function @keep_across_eh(%limit : i64, %value : i64) -> i64
    [no_inline=yes] {
  block ^entry:
    jump ^header

  block ^header:
    %i = phi i64 [^entry: 0, ^latch: %next]
    %more = cmp lt i64 %i, %limit
    branch %more, ^body, ^exit

  block ^body:
    eh_cleanup ^cleanup
    %adjusted = binary add i64 %value, 4
    store i64 %adjusted, @sink
    call void @may_write()
    eh_end
    jump ^latch

  block ^latch:
    %next = binary add i64 %i, 1
    jump ^header

  block ^cleanup:
    resume

  block ^exit:
    return i64 0
}

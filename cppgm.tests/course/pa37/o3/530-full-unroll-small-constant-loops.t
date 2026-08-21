declare function @touch(%value : i64) -> void [unwind=no]

global @observed : i64 = 0

function @unroll_four() -> i64 [no_inline=yes] {
  block ^entry:
    jump ^header

  block ^header:
    %i = phi i64 [^entry: 0, ^latch: %next]
    %sum = phi i64 [^entry: 0, ^latch: %next_sum]
    %more = cmp lt i64 %i, 4
    branch %more, ^body, ^exit

  block ^body:
    %next_sum = binary add i64 %sum, %i
    store i64 %next_sum, @observed
    jump ^latch

  block ^latch:
    %next = binary add i64 %i, 1
    jump ^header

  block ^exit:
    return i64 %sum
}

function @unroll_decrement() -> i64 [no_inline=yes] {
  block ^entry:
    jump ^header

  block ^header:
    %i = phi i32 [^entry: 3, ^body: %next]
    %sum = phi i32 [^entry: 0, ^body: %next_sum]
    %more = cmp gt i32 %i, 0
    branch %more, ^body, ^exit

  block ^body:
    %next_sum = binary add i32 %sum, %i
    %next = binary sub i32 %i, 1
    jump ^header

  block ^exit:
    %wide = convert sext i64 i32 %sum
    return i64 %wide
}

function @unroll_inverted_exit() -> i64 [no_inline=yes] {
  block ^entry:
    jump ^header

  block ^header:
    %i = phi i16 [^entry: 1, ^body: %next]
    %done = cmp ge i16 %i, 4
    branch %done, ^exit, ^body

  block ^body:
    %next = binary add i16 %i, 1
    jump ^header

  block ^exit:
    %wide = convert sext i64 i16 %i
    return i64 %wide
}

function @unroll_unsigned() -> i64 [no_inline=yes] {
  block ^entry:
    jump ^header

  block ^header:
    %i = phi i32 [^entry: 0, ^body: %next]
    %more = cmp ult i32 %i, 4
    branch %more, ^body, ^exit

  block ^body:
    %next = binary add i32 %i, 1
    jump ^header

  block ^exit:
    %wide = convert zext i64 i32 %i
    return i64 %wide
}

function @unroll_zero_trip() -> i64 [no_inline=yes] {
  block ^entry:
    jump ^header

  block ^header:
    %i = phi i64 [^entry: 7, ^body: %next]
    %more = cmp lt i64 %i, 7
    branch %more, ^body, ^exit

  block ^body:
    call void @touch(%i)
    %next = binary add i64 %i, 1
    jump ^header

  block ^exit:
    return i64 %i
}

function @keep_five_trips() -> i64 [no_inline=yes] {
  block ^entry:
    jump ^header

  block ^header:
    %i = phi i64 [^entry: 0, ^body: %next]
    %more = cmp lt i64 %i, 5
    branch %more, ^body, ^exit

  block ^body:
    call void @touch(%i)
    %next = binary add i64 %i, 1
    jump ^header

  block ^exit:
    return i64 %i
}

function @keep_branching_body(%choose : i64) -> i64 [no_inline=yes] {
  block ^entry:
    jump ^header

  block ^header:
    %i = phi i64 [^entry: 0, ^latch: %next]
    %more = cmp lt i64 %i, 2
    branch %more, ^body, ^exit

  block ^body:
    branch %choose, ^left, ^right

  block ^left:
    call void @touch(1)
    jump ^latch

  block ^right:
    call void @touch(2)
    jump ^latch

  block ^latch:
    %next = binary add i64 %i, 1
    jump ^header

  block ^exit:
    return i64 %i
}

function @keep_clone_budget() -> i64 [no_inline=yes] {
  block ^entry:
    jump ^header

  block ^header:
    %i = phi i64 [^entry: 0, ^body: %next]
    %more = cmp lt i64 %i, 4
    branch %more, ^body, ^exit

  block ^body:
    call void @touch(0)
    call void @touch(1)
    call void @touch(2)
    call void @touch(3)
    call void @touch(4)
    call void @touch(5)
    call void @touch(6)
    call void @touch(7)
    call void @touch(8)
    call void @touch(9)
    call void @touch(10)
    call void @touch(11)
    call void @touch(12)
    call void @touch(13)
    call void @touch(14)
    call void @touch(15)
    %next = binary add i64 %i, 1
    jump ^header

  block ^exit:
    return i64 %i
}

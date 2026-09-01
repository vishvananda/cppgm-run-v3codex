global @seen : i64 [binding=internal] = 0
global @written : i64 [binding=internal] = 0

function @record(%first : i64, %second : i64) -> void
    [binding=internal, no_inline=yes, unwind=no] {
  block ^entry:
    store i64 %first, @seen
    store i64 %second, @written
    return void
}

function @tail_transfer(%first : i64, %second : i64) -> void
    [binding=internal, no_inline=yes, unwind=no] {
  block ^entry:
    %take_fast = cmp ne i64 %first, 0
    branch %take_fast, ^fast, ^slow

  block ^fast:
    %sum0 = binary add i64 %first, %second
    %sum1 = binary add i64 %sum0, 1
    %sum2 = binary add i64 %sum1, 2
    %sum3 = binary add i64 %sum2, 3
    %sum4 = binary add i64 %sum3, 4
    %sum5 = binary add i64 %sum4, 5
    store i64 %sum5, @written
    return void

  block ^slow:
    call void @record(%first, %second)
    return void
}

function @changed_argument(%first : i64, %second : i64) -> void
    [binding=internal, no_inline=yes, unwind=no] {
  block ^entry:
    %changed = binary add i64 %first, 1
    call void @record(%changed, %second)
    return void
}

function @local_storage(%first : i64, %second : i64) -> void
    [binding=internal, no_inline=yes, unwind=no] {
  slot $temporary : i64
  block ^entry:
    store i64 %first, $temporary
    call void @record(%first, %second)
    return void
}

function @main() -> i32 [role=entry, unwind=no] {
  block ^entry:
    call void @changed_argument(20, 23)
    call void @local_storage(31, 37)
    call void @tail_transfer(0, 13)
    %slow_first = load i64 @seen
    %slow_second = load i64 @written
    %slow_first_bad = cmp ne i64 %slow_first, 0
    %slow_second_bad = cmp ne i64 %slow_second, 13
    call void @tail_transfer(1, 2)
    %fast_result = load i64 @written
    %fast_bad = cmp ne i64 %fast_result, 18
    %bad0 = binary or i32 %slow_first_bad, %slow_second_bad
    %bad = binary or i32 %bad0, %fast_bad
    return i32 %bad
}

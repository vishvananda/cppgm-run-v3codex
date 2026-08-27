global @cell : i64 = 5

function @inspect() -> i64
    [binding=strong, no_inline=yes, effects=readonly, unwind=no] {
  block ^entry:
    %value = load i64 @cell
    return i64 %value
}

function @mutate() -> void
    [binding=strong, no_inline=yes, unwind=no] {
  block ^entry:
    store i64 11, @cell
    return void
}

function @reuse_hot(%take : i64) -> i64
    [binding=strong, inline_hint=yes, no_inline=yes, unwind=no] {
  block ^entry:
    %first = load i64 @cell
    branch %take, ^again, ^done

  block ^again:
    %second = load i64 @cell
    %sum = binary add i64 %first, %second
    return i64 %sum

  block ^done:
    return i64 %first
}

function @reuse_lifetime_bounded(%take : i64) -> i64
    [binding=strong, no_inline=yes, unwind=no] {
  block ^entry:
    %first = load i64 @cell
    branch %take, ^again, ^done

  block ^again:
    %second = load i64 @cell
    %sum = binary add i64 %first, %second
    return i64 %sum

  block ^done:
    return i64 %first
}

function @retain_lifetime_extension(%take : i64) -> i64
    [binding=strong, no_inline=yes, unwind=no] {
  block ^entry:
    %first = load i64 @cell
    %early = binary add i64 %first, 1
    branch %take, ^again, ^done

  block ^again:
    %second = load i64 @cell
    %sum = binary add i64 %early, %second
    return i64 %sum

  block ^done:
    return i64 %early
}

function @retain_store_barrier() -> i64
    [binding=strong, inline_hint=yes, no_inline=yes, unwind=no] {
  block ^entry:
    %first = load i64 @cell
    store i64 9, @cell
    %second = load i64 @cell
    %sum = binary add i64 %first, %second
    return i64 %sum
}

function @retain_writing_call_barrier() -> i64
    [binding=strong, inline_hint=yes, no_inline=yes, unwind=no] {
  block ^entry:
    %first = load i64 @cell
    call void @mutate()
    %second = load i64 @cell
    %sum = binary add i64 %first, %second
    return i64 %sum
}

function @reuse_across_readonly_call() -> i64
    [binding=strong, inline_hint=yes, no_inline=yes, unwind=no] {
  block ^entry:
    %first = load i64 @cell
    %observed = call i64 @inspect()
    %second = load i64 @cell
    %sum = binary add i64 %first, %second
    %total = binary add i64 %sum, %observed
    return i64 %total
}

function @main() -> i32 [role=entry, unwind=no] {
  block ^entry:
    store i64 5, @cell
    %hot = call i64 @reuse_hot(1)
    %bounded = call i64 @reuse_lifetime_bounded(1)
    %extended = call i64 @retain_lifetime_extension(1)
    %stored = call i64 @retain_store_barrier()
    store i64 5, @cell
    %called = call i64 @retain_writing_call_barrier()
    store i64 5, @cell
    %readonly = call i64 @reuse_across_readonly_call()
    %bad0 = cmp ne i64 %hot, 10
    %bad1 = cmp ne i64 %bounded, 10
    %bad5 = cmp ne i64 %extended, 11
    %bad2 = cmp ne i64 %stored, 14
    %bad3 = cmp ne i64 %called, 16
    %bad4 = cmp ne i64 %readonly, 15
    %bad01 = binary or i32 %bad0, %bad1
    %bad23 = binary or i32 %bad2, %bad3
    %bad0123 = binary or i32 %bad01, %bad23
    %bad04 = binary or i32 %bad4, %bad5
    %bad = binary or i32 %bad0123, %bad04
    return i32 %bad
}

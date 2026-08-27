global @cell : i64 = 0
global @other : i64 = 0

function @mutate() -> void [binding=strong, no_inline=yes, unwind=no] {
  block ^entry:
    store i64 99, @cell
    return void
}

function @forward_exact_store(%limit : i64) -> i64
    [binding=strong, inline_hint=yes, no_inline=yes, unwind=no] {
  block ^entry:
    jump ^header

  block ^header:
    %current = load i64 @cell
    %more = cmp ult i64 %current, %limit
    branch %more, ^latch, ^done

  block ^latch:
    %next = binary add i64 %current, 1
    store i64 %next, @cell
    jump ^header

  block ^done:
    return i64 %current
}

function @retain_ordinary_exact_store(%limit : i64) -> i64
    [binding=strong, no_inline=yes, unwind=no] {
  block ^entry:
    jump ^header

  block ^header:
    %current = load i64 @cell
    %more = cmp ult i64 %current, %limit
    branch %more, ^latch, ^done

  block ^latch:
    %next = binary add i64 %current, 1
    store i64 %next, @cell
    jump ^header

  block ^done:
    return i64 %current
}

function @retain_different_store(%limit : i64) -> i64
    [binding=strong, inline_hint=yes, no_inline=yes, unwind=no] {
  block ^entry:
    jump ^header

  block ^header:
    %current = load i64 @cell
    %more = cmp ult i64 %current, %limit
    branch %more, ^latch, ^done

  block ^latch:
    %next = binary add i64 %current, 1
    store i64 %next, @other
    jump ^header

  block ^done:
    return i64 %current
}

function @retain_post_store_call(%limit : i64) -> i64
    [binding=strong, inline_hint=yes, no_inline=yes, unwind=no] {
  block ^entry:
    jump ^header

  block ^header:
    %current = load i64 @cell
    %more = cmp ult i64 %current, %limit
    branch %more, ^latch, ^done

  block ^latch:
    %next = binary add i64 %current, 1
    store i64 %next, @cell
    call void @mutate()
    jump ^header

  block ^done:
    return i64 %current
}

function @main() -> i32 [role=entry, unwind=no] {
  block ^entry:
    store i64 0, @cell
    %result = call i64 @forward_exact_store(5)
    %stored = load i64 @cell
    %bad_result = cmp ne i64 %result, 5
    %bad_store = cmp ne i64 %stored, 5
    %bad = binary or i32 %bad_result, %bad_store
    return i32 %bad
}

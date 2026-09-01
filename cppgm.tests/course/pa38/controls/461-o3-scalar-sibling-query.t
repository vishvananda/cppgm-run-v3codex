global @productions : i64 [binding=internal] = 0

function @produce(%state : ptr) -> i32
    [binding=internal, no_inline=yes, unwind=no] {
  block ^entry:
    %before = load i64 @productions
    %after = binary add i64 %before, 1
    store i64 %after, @productions
    return i32 41
}

function @slow_suffix(%state : ptr [object_bytes=64]) -> i32
    [binding=internal, no_inline=yes, unwind=no] {
  block ^entry:
    %value = call i32 @produce(%state)
    %index_address = index i8 [projection=field] %state, 48
    %index = load i64 %index_address
    %bounded = binary and i64 %index, 1
    %target = index obj<4x4> [projection=array_element] %state, %bounded
    store i32 %value, %target
    %count_address = index i8 [projection=field] %state, 56
    store i64 1, %count_address
    return i32 %value
}

function @query(%state : ptr [object_bytes=64]) -> i32
    [binding=internal, no_inline=yes, unwind=no] {
  block ^entry:
    %count_address = index i8 [projection=field] %state, 56
    %count = load i64 %count_address
    %empty = cmp eq i64 %count, 0
    branch %empty, ^slow, ^ready

  block ^slow:
    %value = call i32 @slow_suffix(%state)
    return i32 %value

  block ^ready:
    %index_address = index i8 [projection=field] %state, 48
    %index = load i64 %index_address
    %bounded = binary and i64 %index, 1
    %target = index obj<4x4> [projection=array_element] %state, %bounded
    %result = load i32 %target
    return i32 %result
}

function @changed_result(%state : ptr [object_bytes=64]) -> i32
    [binding=internal, no_inline=yes, unwind=no] {
  block ^entry:
    %count_address = index i8 [projection=field] %state, 56
    %count = load i64 %count_address
    %empty = cmp eq i64 %count, 0
    branch %empty, ^slow, ^ready

  block ^slow:
    %value = call i32 @slow_suffix(%state)
    %changed = binary add i32 %value, 1
    return i32 %changed

  block ^ready:
    %index_address = index i8 [projection=field] %state, 48
    %index = load i64 %index_address
    %bounded = binary and i64 %index, 1
    %target = index obj<4x4> [projection=array_element] %state, %bounded
    %result = load i32 %target
    return i32 %result
}

function @main() -> i32 [role=entry, unwind=no] {
  slot $exact : obj<64x8>
  slot $changed : obj<64x8>
  block ^entry:
    %exact_state = addr $exact
    zeroinit 64x8 %exact_state
    %exact_index = index i8 [projection=field] %exact_state, 48
    store i64 1, %exact_index
    %exact_slow = call i32 @query(%exact_state)
    %exact_fast = call i32 @query(%exact_state)

    %changed_state = addr $changed
    zeroinit 64x8 %changed_state
    %changed_index = index i8 [projection=field] %changed_state, 48
    store i64 1, %changed_index
    %changed_slow = call i32 @changed_result(%changed_state)
    %changed_fast = call i32 @changed_result(%changed_state)

    %count = load i64 @productions
    %bad_exact_slow = cmp ne i32 %exact_slow, 41
    %bad_exact_fast = cmp ne i32 %exact_fast, 41
    %bad_changed_slow = cmp ne i32 %changed_slow, 42
    %bad_changed_fast = cmp ne i32 %changed_fast, 41
    %bad_count = cmp ne i64 %count, 2
    %bad0 = binary or i32 %bad_exact_slow, %bad_exact_fast
    %bad1 = binary or i32 %bad_changed_slow, %bad_changed_fast
    %bad2 = binary or i32 %bad0, %bad1
    %bad = binary or i32 %bad2, %bad_count
    return i32 %bad
}

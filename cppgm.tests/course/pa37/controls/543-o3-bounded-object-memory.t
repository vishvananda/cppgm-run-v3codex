function @return_wide_region(%object : ptr [object_bytes=16]) -> ptr
    [binding=internal, no_inline=yes, unwind=no] {
  block ^entry:
    return ptr %object
}

function @return_narrow_region(%object : ptr [object_bytes=8]) -> ptr
    [binding=internal, no_inline=yes, unwind=no] {
  block ^entry:
    return ptr %object
}

function @return_unbounded_region(%object : ptr) -> ptr
    [binding=internal, no_inline=yes, unwind=no] {
  block ^entry:
    return ptr %object
}

function @inspect_bounded_head(%object : ptr [object_bytes=48]) -> i64
    [binding=internal, no_inline=yes, unwind=no] {
  block ^entry:
    %value = load i64 %object
    return i64 %value
}

function @reuse_across_precise_body(%object : ptr [object_bytes=48]) -> i64
    [binding=internal, no_inline=yes, unwind=no] {
  block ^entry:
    %middle = index i8 [projection=field] %object, 16
    %before = load i64 %middle
    %head = call i64 @inspect_bounded_head(%object)
    %after = load i64 %middle
    %sum0 = binary add i64 %before, %after
    %sum1 = binary add i64 %sum0, %head
    return i64 %sum1
}

function @reuse_between_disjoint_captures(%object : ptr [object_bytes=48]) -> i64
    [binding=internal, no_inline=yes, unwind=no] {
  block ^entry:
    %middle = index i8 [projection=field] %object, 16
    %before = load i64 %middle
    %head = call ptr @return_wide_region(%object)
    %tail = index i8 [projection=field] %object, 24
    %end = call ptr @return_narrow_region(%tail)
    %head_value = load i64 %head
    %end_value = load i64 %end
    %after = load i64 %middle
    %sum0 = binary add i64 %before, %after
    %sum1 = binary add i64 %sum0, %head_value
    %sum2 = binary add i64 %sum1, %end_value
    return i64 %sum2
}

function @retain_overlapping_capture(%object : ptr [object_bytes=48]) -> i64
    [binding=internal, no_inline=yes, unwind=no] {
  block ^entry:
    %middle = index i8 [projection=field] %object, 16
    %before = load i64 %middle
    %overlap = index i8 [projection=field] %object, 12
    %escaped = call ptr @return_narrow_region(%overlap)
    %after = load i64 %middle
    %sum = binary add i64 %before, %after
    return i64 %sum
}

function @retain_unbounded_capture(%object : ptr [object_bytes=48]) -> i64
    [binding=internal, no_inline=yes, unwind=no] {
  block ^entry:
    %middle = index i8 [projection=field] %object, 16
    %before = load i64 %middle
    %escaped = call ptr @return_unbounded_region(%object)
    %after = load i64 %middle
    %sum = binary add i64 %before, %after
    return i64 %sum
}

function @reuse_after_masked_store(%object : ptr [object_bytes=48],
                                   %selector : i64) -> i64
    [binding=internal, no_inline=yes, unwind=no] {
  block ^entry:
    %tail = index i8 [projection=field] %object, 40
    %before = load i64 %tail
    %bounded = binary and i64 %selector, 3
    %element = index obj<8x1> [projection=array_element] %object, %bounded
    store i64 99, %element
    %after = load i64 %tail
    %sum = binary add i64 %before, %after
    return i64 %sum
}

function @retain_after_unbounded_store(%object : ptr [object_bytes=48],
                                       %selector : i64) -> i64
    [binding=internal, no_inline=yes, unwind=no] {
  block ^entry:
    %tail = index i8 [projection=field] %object, 40
    %before = load i64 %tail
    %element = index obj<8x1> [projection=array_element] %object, %selector
    store i64 77, %element
    %after = load i64 %tail
    %sum = binary add i64 %before, %after
    return i64 %sum
}

function @retain_direct_write_barriers(%object : ptr [object_bytes=48]) -> i64
    [binding=internal, no_inline=yes, unwind=no] {
  block ^entry:
    %middle = index i8 [projection=field] %object, 16
    %before = load i64 %middle
    zeroinit 8x8 %middle
    %after_zero = load i64 %middle
    atomic_store i64 5, %middle, 0
    %after_atomic = load i64 %middle
    %sum0 = binary add i64 %before, %after_zero
    %sum1 = binary add i64 %sum0, %after_atomic
    return i64 %sum1
}

function @read_addressed_scalar(%value : ptr [pass=by_address, object_bytes=8])
    -> i64 [binding=internal, no_inline=yes, unwind=no] {
  block ^entry:
    %result = load i64 %value
    return i64 %result
}

function @retain_equal_value_address(%object : ptr [object_bytes=8]) -> i64
    [binding=internal, no_inline=yes, unwind=no] {
  block ^entry:
    %value = load i64 %object
    %is_zero = cmp eq i64 %value, 0
    branch %is_zero, ^known, ^other

  block ^known:
    %result = call i64 @read_addressed_scalar(%value)
    return i64 %result

  block ^other:
    return i64 9
}

function @close_single_iteration(%object : ptr [object_bytes=48],
                                 %gate : i64) -> i64
    [binding=internal, no_inline=yes, unwind=no] {
  block ^entry:
    %middle = index i8 [projection=field] %object, 16
    %before = load i64 %middle
    %late = index i8 [projection=field] %object, 24
    %escaped = call ptr @return_wide_region(%object)
    %after = load i64 %middle
    %is_zero = cmp eq i64 %gate, 0
    branch %is_zero, ^preheader, ^fail

  block ^preheader:
    %known = binary add i64 %gate, 1
    %size = index i8 [projection=field] %object, 32
    jump ^header

  block ^header:
    %current = load i64 %size
    %empty = cmp ule i64 %current, 0
    branch %empty, ^latch, ^done

  block ^latch:
    store i64 1, %size
    jump ^header

  block ^done:
    %late_value = load i64 %late
    %sum0 = binary add i64 %before, %after
    %sum1 = binary add i64 %sum0, %known
    %sum2 = binary add i64 %sum1, %late_value
    return i64 %sum2

  block ^fail:
    return i64 999
}

function @main() -> i32 [role=entry, unwind=no] {
  slot $safe : obj<48x8>
  slot $overlap : obj<48x8>
  slot $unknown : obj<48x8>
  slot $masked : obj<48x8>
  slot $dynamic : obj<48x8>
  slot $writes : obj<48x8>
  slot $addressable : i64
  slot $loop : obj<48x8>
  block ^entry:
    %safe_address = addr $safe
    store i64 2, %safe_address
    %safe_middle = index i8 [projection=field] %safe_address, 16
    store i64 5, %safe_middle
    %safe_tail = index i8 [projection=field] %safe_address, 24
    store i64 7, %safe_tail
    %safe_result = call i64 @reuse_between_disjoint_captures(%safe_address)
    %precise_result = call i64 @reuse_across_precise_body(%safe_address)

    %overlap_address = addr $overlap
    %overlap_middle = index i8 [projection=field] %overlap_address, 16
    store i64 5, %overlap_middle
    %overlap_result = call i64 @retain_overlapping_capture(%overlap_address)

    %unknown_address = addr $unknown
    %unknown_middle = index i8 [projection=field] %unknown_address, 16
    store i64 5, %unknown_middle
    %unknown_result = call i64 @retain_unbounded_capture(%unknown_address)

    %masked_address = addr $masked
    %masked_tail = index i8 [projection=field] %masked_address, 40
    store i64 11, %masked_tail
    %masked_result = call i64 @reuse_after_masked_store(%masked_address, 3)
    %masked_second = call i64 @reuse_after_masked_store(%masked_address, 4)

    %dynamic_address = addr $dynamic
    %dynamic_tail = index i8 [projection=field] %dynamic_address, 40
    store i64 11, %dynamic_tail
    %dynamic_result = call i64 @retain_after_unbounded_store(%dynamic_address, 2)
    %dynamic_second = call i64 @retain_after_unbounded_store(%dynamic_address, 5)

    %writes_address = addr $writes
    %writes_middle = index i8 [projection=field] %writes_address, 16
    store i64 9, %writes_middle
    %writes_result = call i64 @retain_direct_write_barriers(%writes_address)

    store i64 0, $addressable
    %addressable_address = addr $addressable
    %addressable_result = call i64 @retain_equal_value_address(%addressable_address)

    %loop_address = addr $loop
    %loop_middle = index i8 [projection=field] %loop_address, 16
    store i64 5, %loop_middle
    %loop_late = index i8 [projection=field] %loop_address, 24
    store i64 7, %loop_late
    %loop_size = index i8 [projection=field] %loop_address, 32
    store i64 0, %loop_size
    %loop_result = call i64 @close_single_iteration(%loop_address, 0)
    %loop_guard_result = call i64 @close_single_iteration(%loop_address, 1)

    %safe_bad = cmp ne i64 %safe_result, 19
    %precise_bad = cmp ne i64 %precise_result, 12
    %overlap_bad = cmp ne i64 %overlap_result, 10
    %unknown_bad = cmp ne i64 %unknown_result, 10
    %masked_bad = cmp ne i64 %masked_result, 22
    %masked_second_bad = cmp ne i64 %masked_second, 22
    %dynamic_bad = cmp ne i64 %dynamic_result, 22
    %dynamic_second_bad = cmp ne i64 %dynamic_second, 88
    %writes_bad = cmp ne i64 %writes_result, 14
    %addressable_bad = cmp ne i64 %addressable_result, 0
    %loop_bad = cmp ne i64 %loop_result, 18
    %loop_guard_bad = cmp ne i64 %loop_guard_result, 999
    %bad0 = binary or i32 %safe_bad, %overlap_bad
    %bad0a = binary or i32 %bad0, %precise_bad
    %bad1 = binary or i32 %unknown_bad, %masked_bad
    %bad1a = binary or i32 %bad1, %masked_second_bad
    %bad2 = binary or i32 %dynamic_bad, %dynamic_second_bad
    %bad2a = binary or i32 %bad2, %writes_bad
    %bad2b = binary or i32 %bad2a, %addressable_bad
    %bad3 = binary or i32 %bad0a, %bad1a
    %bad4 = binary or i32 %bad2b, %loop_bad
    %bad5 = binary or i32 %bad4, %loop_guard_bad
    %bad = binary or i32 %bad3, %bad5
    return i32 %bad
}

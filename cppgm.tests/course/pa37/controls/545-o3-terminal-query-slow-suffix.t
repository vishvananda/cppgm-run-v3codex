global @refill_count : i64 [binding=internal] = 0

function @refill(%state : ptr) -> i32
    [binding=internal, no_inline=yes, unwind=no] {
  block ^entry:
    %before = load i64 @refill_count
    %after = binary add i64 %before, 1
    store i64 %after, @refill_count
    return i32 41
}

function @bounded_query(%state : ptr [object_bytes=64]) -> i32
    [binding=internal, unwind=no] {
  block ^entry:
    %count_address = index i8 [projection=field] %state, 56
    %count = load i64 %count_address
    %empty = cmp eq i64 %count, 0
    branch %empty, ^slow, ^ready

  block ^slow:
    %value = call i32 @refill(%state)
    %index_address = index i8 [projection=field] %state, 48
    %index = load i64 %index_address
    %bounded = binary and i64 %index, 1
    %target = index obj<4x4> [projection=array_element] %state, %bounded
    %scratch_address = index i8 [projection=field] %state, 16
    %p0 = binary add i64 %index, 1
    %p1 = binary add i64 %p0, 1
    %p2 = binary add i64 %p1, 1
    %p3 = binary add i64 %p2, 1
    %p4 = binary add i64 %p3, 1
    %p5 = binary add i64 %p4, 1
    %p6 = binary add i64 %p5, 1
    %p7 = binary add i64 %p6, 1
    %p8 = binary add i64 %p7, 1
    %p9 = binary add i64 %p8, 1
    %p10 = binary add i64 %p9, 1
    %p11 = binary add i64 %p10, 1
    %p12 = binary add i64 %p11, 1
    %p13 = binary add i64 %p12, 1
    %p14 = binary add i64 %p13, 1
    %p15 = binary add i64 %p14, 1
    %p16 = binary add i64 %p15, 1
    %p17 = binary add i64 %p16, 1
    %p18 = binary add i64 %p17, 1
    %p19 = binary add i64 %p18, 1
    %p20 = binary add i64 %p19, 1
    %p21 = binary add i64 %p20, 1
    %p22 = binary add i64 %p21, 1
    %p23 = binary add i64 %p22, 1
    store i64 %p23, %scratch_address
    store i32 %value, %target
    store i64 1, %count_address
    jump ^ready

  block ^ready:
    %ready_index_address = index i8 [projection=field] %state, 48
    %ready_index = load i64 %ready_index_address
    %ready_bounded = binary and i64 %ready_index, 1
    %ready_target = index obj<4x4> [projection=array_element] %state, %ready_bounded
    %result = load i32 %ready_target
    return i32 %result
}

function @unbounded_query(%state : ptr) -> i32
    [binding=internal, unwind=no] {
  block ^entry:
    %count_address = index i8 [projection=field] %state, 56
    %count = load i64 %count_address
    %empty = cmp eq i64 %count, 0
    branch %empty, ^slow, ^ready

  block ^slow:
    %value = call i32 @refill(%state)
    %index_address = index i8 [projection=field] %state, 48
    %index = load i64 %index_address
    %bounded = binary and i64 %index, 1
    %target = index obj<4x4> [projection=array_element] %state, %bounded
    %scratch_address = index i8 [projection=field] %state, 16
    %p0 = binary add i64 %index, 1
    %p1 = binary add i64 %p0, 1
    %p2 = binary add i64 %p1, 1
    %p3 = binary add i64 %p2, 1
    %p4 = binary add i64 %p3, 1
    %p5 = binary add i64 %p4, 1
    %p6 = binary add i64 %p5, 1
    %p7 = binary add i64 %p6, 1
    %p8 = binary add i64 %p7, 1
    %p9 = binary add i64 %p8, 1
    %p10 = binary add i64 %p9, 1
    %p11 = binary add i64 %p10, 1
    %p12 = binary add i64 %p11, 1
    %p13 = binary add i64 %p12, 1
    %p14 = binary add i64 %p13, 1
    %p15 = binary add i64 %p14, 1
    %p16 = binary add i64 %p15, 1
    %p17 = binary add i64 %p16, 1
    %p18 = binary add i64 %p17, 1
    %p19 = binary add i64 %p18, 1
    %p20 = binary add i64 %p19, 1
    %p21 = binary add i64 %p20, 1
    %p22 = binary add i64 %p21, 1
    %p23 = binary add i64 %p22, 1
    store i64 %p23, %scratch_address
    store i32 %value, %target
    store i64 1, %count_address
    jump ^ready

  block ^ready:
    %ready_index_address = index i8 [projection=field] %state, 48
    %ready_index = load i64 %ready_index_address
    %ready_bounded = binary and i64 %ready_index, 1
    %ready_target = index obj<4x4> [projection=array_element] %state, %ready_bounded
    %result = load i32 %ready_target
    return i32 %result
}

function @overwritten_query(%state : ptr [object_bytes=64]) -> i32
    [binding=internal, unwind=no] {
  block ^entry:
    %count_address = index i8 [projection=field] %state, 56
    %count = load i64 %count_address
    %empty = cmp eq i64 %count, 0
    branch %empty, ^slow, ^ready

  block ^slow:
    %value = call i32 @refill(%state)
    %index_address = index i8 [projection=field] %state, 48
    %index = load i64 %index_address
    %bounded = binary and i64 %index, 1
    %target = index obj<4x4> [projection=array_element] %state, %bounded
    %scratch_address = index i8 [projection=field] %state, 16
    %p0 = binary add i64 %index, 1
    %p1 = binary add i64 %p0, 1
    %p2 = binary add i64 %p1, 1
    %p3 = binary add i64 %p2, 1
    %p4 = binary add i64 %p3, 1
    %p5 = binary add i64 %p4, 1
    %p6 = binary add i64 %p5, 1
    %p7 = binary add i64 %p6, 1
    %p8 = binary add i64 %p7, 1
    %p9 = binary add i64 %p8, 1
    %p10 = binary add i64 %p9, 1
    %p11 = binary add i64 %p10, 1
    %p12 = binary add i64 %p11, 1
    %p13 = binary add i64 %p12, 1
    %p14 = binary add i64 %p13, 1
    %p15 = binary add i64 %p14, 1
    %p16 = binary add i64 %p15, 1
    %p17 = binary add i64 %p16, 1
    %p18 = binary add i64 %p17, 1
    %p19 = binary add i64 %p18, 1
    %p20 = binary add i64 %p19, 1
    %p21 = binary add i64 %p20, 1
    %p22 = binary add i64 %p21, 1
    %p23 = binary add i64 %p22, 1
    store i64 %p23, %scratch_address
    store i32 %value, %target
    store i32 9, %target
    store i64 1, %count_address
    jump ^ready

  block ^ready:
    %ready_index_address = index i8 [projection=field] %state, 48
    %ready_index = load i64 %ready_index_address
    %ready_bounded = binary and i64 %ready_index, 1
    %ready_target = index obj<4x4> [projection=array_element] %state, %ready_bounded
    %result = load i32 %ready_target
    return i32 %result
}

function @main() -> i32 [role=entry, unwind=no] {
  slot $bounded : obj<64x8>
  slot $unbounded : obj<64x8>
  slot $overwritten : obj<64x8>
  block ^entry:
    %bounded_state = addr $bounded
    zeroinit 64x8 %bounded_state
    %bounded_index = index i8 [projection=field] %bounded_state, 48
    store i64 1, %bounded_index
    %b0 = call i32 @bounded_query(%bounded_state)
    %b1 = call i32 @bounded_query(%bounded_state)
    %b2 = call i32 @bounded_query(%bounded_state)
    %b3 = call i32 @bounded_query(%bounded_state)
    %b4 = call i32 @bounded_query(%bounded_state)
    %b5 = call i32 @bounded_query(%bounded_state)
    %b6 = call i32 @bounded_query(%bounded_state)
    %b7 = call i32 @bounded_query(%bounded_state)

    %unbounded_state = addr $unbounded
    zeroinit 64x8 %unbounded_state
    %unbounded_index = index i8 [projection=field] %unbounded_state, 48
    store i64 1, %unbounded_index
    %u0 = call i32 @unbounded_query(%unbounded_state)
    %u1 = call i32 @unbounded_query(%unbounded_state)
    %u2 = call i32 @unbounded_query(%unbounded_state)
    %u3 = call i32 @unbounded_query(%unbounded_state)
    %u4 = call i32 @unbounded_query(%unbounded_state)
    %u5 = call i32 @unbounded_query(%unbounded_state)
    %u6 = call i32 @unbounded_query(%unbounded_state)
    %u7 = call i32 @unbounded_query(%unbounded_state)

    %overwritten_state = addr $overwritten
    zeroinit 64x8 %overwritten_state
    %overwritten_index = index i8 [projection=field] %overwritten_state, 48
    store i64 1, %overwritten_index
    %o0 = call i32 @overwritten_query(%overwritten_state)
    %o1 = call i32 @overwritten_query(%overwritten_state)
    %o2 = call i32 @overwritten_query(%overwritten_state)
    %o3 = call i32 @overwritten_query(%overwritten_state)
    %o4 = call i32 @overwritten_query(%overwritten_state)
    %o5 = call i32 @overwritten_query(%overwritten_state)
    %o6 = call i32 @overwritten_query(%overwritten_state)
    %o7 = call i32 @overwritten_query(%overwritten_state)

    %bs0 = binary add i32 %b0, %b1
    %bs1 = binary add i32 %b2, %b3
    %bs2 = binary add i32 %b4, %b5
    %bs3 = binary add i32 %b6, %b7
    %bs4 = binary add i32 %bs0, %bs1
    %bs5 = binary add i32 %bs2, %bs3
    %bounded_sum = binary add i32 %bs4, %bs5
    %us0 = binary add i32 %u0, %u1
    %us1 = binary add i32 %u2, %u3
    %us2 = binary add i32 %u4, %u5
    %us3 = binary add i32 %u6, %u7
    %us4 = binary add i32 %us0, %us1
    %us5 = binary add i32 %us2, %us3
    %unbounded_sum = binary add i32 %us4, %us5
    %os0 = binary add i32 %o0, %o1
    %os1 = binary add i32 %o2, %o3
    %os2 = binary add i32 %o4, %o5
    %os3 = binary add i32 %o6, %o7
    %os4 = binary add i32 %os0, %os1
    %os5 = binary add i32 %os2, %os3
    %overwritten_sum = binary add i32 %os4, %os5
    %refills = load i64 @refill_count
    %bad_bounded = cmp ne i32 %bounded_sum, 328
    %bad_unbounded = cmp ne i32 %unbounded_sum, 328
    %bad_overwritten = cmp ne i32 %overwritten_sum, 72
    %bad_refills = cmp ne i64 %refills, 3
    %bad0 = binary or i32 %bad_bounded, %bad_unbounded
    %bad1 = binary or i32 %bad_overwritten, %bad_refills
    %bad = binary or i32 %bad0, %bad1
    return i32 %bad
}

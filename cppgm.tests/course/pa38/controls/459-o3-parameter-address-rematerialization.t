function @observe_head(%object : ptr [object_bytes=64]) -> i64
    [binding=internal, no_inline=yes, unwind=no] {
  block ^entry:
    %value = load i64 %object
    return i64 %value
}

function @sum_fields_after_call(%object : ptr [object_bytes=64]) -> i64
    [binding=internal, no_inline=yes, unwind=no] {
  block ^entry:
    %first = index i8 [projection=field] %object, 8
    %second = index i8 [projection=field] %object, 16
    %third = index i8 [projection=field] %object, 24
    %fourth = index i8 [projection=field] %object, 32
    %head = call i64 @observe_head(%object)
    %first_value = load i64 %first
    %second_value = load i64 %second
    %third_value = load i64 %third
    %fourth_value = load i64 %fourth
    %sum0 = binary add i64 %head, %first_value
    %sum1 = binary add i64 %sum0, %second_value
    %sum2 = binary add i64 %sum1, %third_value
    %sum3 = binary add i64 %sum2, %fourth_value
    return i64 %sum3
}

function @main() -> i32 [role=entry, unwind=no] {
  slot $object : obj<64x8>
  block ^entry:
    %address = addr $object
    store i64 1, %address
    %first = index i8 [projection=field] %address, 8
    store i64 2, %first
    %second = index i8 [projection=field] %address, 16
    store i64 3, %second
    %third = index i8 [projection=field] %address, 24
    store i64 4, %third
    %fourth = index i8 [projection=field] %address, 32
    store i64 5, %fourth
    %result = call i64 @sum_fields_after_call(%address)
    %bad = cmp ne i64 %result, 15
    return i32 %bad
}

global @safe_ranges [binding=internal] = {
  i32 10
  i32 12
  i32 20
  i32 22
}

global @escaped_ranges [binding=internal] = {
  i32 30
  i32 32
  i32 40
  i32 42
}

global @fallback_ranges [binding=internal] = {
  i32 1000
  i32 1000
  i32 1000
}

global @unaligned_ranges [binding=internal] = {
  i32 50
  i32 52
  i32 60
  i32 62
  i32 70
  i32 72
}

function @observe(%address : ptr) -> void [no_inline=yes, unwind=no] {
  block ^entry:
    return void
}

function @private_table_search(
    %table : ptr, %count : i64, %key : i32) -> u8
    [binding=internal, unwind=no] {
  block ^entry:
    jump ^loop

  block ^loop:
    %high = phi i64 [^entry: %count, ^lower_half: %middle, ^advance: %high]
    %low = phi i64 [^entry: 0, ^lower_half: %low, ^advance: %next]
    %active = cmp ult i64 %low, %high
    branch %active, ^body, ^miss

  block ^body:
    %span = binary sub i64 %high, %low
    %half = binary ushr i64 %span, 1
    %middle = binary add i64 %low, %half
    %scaled = binary shl i64 %middle, 3
    %range = index i8 [projection=array_element] %table, %scaled
    %lower = load i32 %range
    %below = cmp lt i32 %key, %lower
    branch %below, ^lower_half, ^upper_check

  block ^lower_half:
    jump ^loop

  block ^upper_check:
    %upper_address = index i8 [projection=field] %range, 4
    %upper = load i32 %upper_address
    %above = cmp gt i32 %key, %upper
    branch %above, ^advance, ^found

  block ^advance:
    %next = binary add i64 %middle, 1
    jump ^loop

  block ^found:
    return u8 1

  block ^miss:
    return u8 0
}

function @escaped_table_search(
    %table : ptr, %count : i64, %key : i32) -> u8
    [binding=internal, unwind=no] {
  block ^entry:
    jump ^loop

  block ^loop:
    %high = phi i64 [^entry: %count, ^lower_half: %middle, ^advance: %high]
    %low = phi i64 [^entry: 0, ^lower_half: %low, ^advance: %next]
    %active = cmp ult i64 %low, %high
    branch %active, ^body, ^miss

  block ^body:
    %span = binary sub i64 %high, %low
    %half = binary ushr i64 %span, 1
    %middle = binary add i64 %low, %half
    %scaled = binary shl i64 %middle, 3
    %range = index i8 [projection=array_element] %table, %scaled
    %lower = load i32 %range
    %below = cmp lt i32 %key, %lower
    branch %below, ^lower_half, ^upper_check

  block ^lower_half:
    jump ^loop

  block ^upper_check:
    %upper_address = index i8 [projection=field] %range, 4
    %upper = load i32 %upper_address
    %above = cmp gt i32 %key, %upper
    branch %above, ^advance, ^found

  block ^advance:
    %next = binary add i64 %middle, 1
    jump ^loop

  block ^found:
    return u8 1

  block ^miss:
    return u8 0
}

function @unaligned_table_search(
    %table : ptr, %count : i64, %key : i32) -> u8
    [binding=internal, unwind=no] {
  block ^entry:
    jump ^loop

  block ^loop:
    %high = phi i64 [^entry: %count, ^lower_half: %middle, ^advance: %high]
    %low = phi i64 [^entry: 0, ^lower_half: %low, ^advance: %next]
    %active = cmp ult i64 %low, %high
    branch %active, ^body, ^miss

  block ^body:
    %span = binary sub i64 %high, %low
    %half = binary ushr i64 %span, 1
    %middle = binary add i64 %low, %half
    %scaled = binary shl i64 %middle, 3
    %range = index i8 [projection=array_element] %table, %scaled
    %misaligned = index i8 [projection=field] %range, 1
    %lower = load i32 %misaligned
    %below = cmp lt i32 %key, %lower
    branch %below, ^lower_half, ^upper_check

  block ^lower_half:
    jump ^loop

  block ^upper_check:
    %upper_address = index i8 [projection=field] %misaligned, 4
    %upper = load i32 %upper_address
    %above = cmp gt i32 %key, %upper
    branch %above, ^advance, ^found

  block ^advance:
    %next = binary add i64 %middle, 1
    jump ^loop

  block ^found:
    return u8 1

  block ^miss:
    return u8 0
}

function @main() -> i32 [role=entry, unwind=no] {
  block ^entry:
    %safe_address = addr @safe_ranges
    %safe_copy = copy ptr %safe_address
    %safe0 = call u8 @private_table_search(%safe_copy, 2, 5)
    %safe1 = call u8 @private_table_search(%safe_copy, 2, 10)
    %safe2 = call u8 @private_table_search(%safe_copy, 2, 21)
    %safe3 = call u8 @private_table_search(%safe_copy, 2, 99)
    %fallback_address = addr @fallback_ranges
    %fallback0 = call u8 @private_table_search(%fallback_address, 1, 1000)

    %escaped_address = addr @escaped_ranges
    %escaped_copy = copy ptr %escaped_address
    %escaped0 = call u8 @escaped_table_search(%escaped_copy, 2, 29)
    %escaped1 = call u8 @escaped_table_search(%escaped_copy, 2, 30)
    %escaped2 = call u8 @escaped_table_search(%escaped_copy, 2, 41)
    %escaped3 = call u8 @escaped_table_search(%escaped_copy, 2, 50)
    %fallback1 = call u8 @escaped_table_search(%fallback_address, 1, 1000)
    call void @observe(%escaped_copy)

    %unaligned_address = addr @unaligned_ranges
    %unaligned_copy = copy ptr %unaligned_address
    %unaligned0 = call u8 @unaligned_table_search(%unaligned_copy, 2, 49)
    %unaligned1 = call u8 @unaligned_table_search(%unaligned_copy, 2, 50)
    %unaligned2 = call u8 @unaligned_table_search(%unaligned_copy, 2, 61)
    %unaligned3 = call u8 @unaligned_table_search(%unaligned_copy, 2, 80)
    %fallback2 = call u8 @unaligned_table_search(%fallback_address, 1, -1)

    %s0 = binary add i8 %safe0, %safe1
    %s1 = binary add i8 %s0, %safe2
    %s2 = binary add i8 %s1, %safe3
    %s3 = binary add i8 %s2, %fallback0
    %e0 = binary add i8 %escaped0, %escaped1
    %e1 = binary add i8 %e0, %escaped2
    %e2 = binary add i8 %e1, %escaped3
    %e3 = binary add i8 %e2, %fallback1
    %total = binary add i8 %s3, %e3
    %bad = cmp ne i8 %total, 6
    %status = convert zext i32 u8 %bad
    return i32 %status
}

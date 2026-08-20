function @return_i8() -> i8 {
  slot $home : i8

  block ^entry:
    %wide = copy i64 -1
    %value = convert trunc i8 i64 %wide
    store i8 %value, $home
    %reloaded = load i8 $home
    return i8 %reloaded
}

function @extend_i8() -> i64 {
  slot $home : i8

  block ^entry:
    %wide = copy i64 -1
    %value = convert trunc i8 i64 %wide
    store i8 %value, $home
    %reloaded = load i8 $home
    %extended = convert zext i64 i8 %reloaded
    return i64 %extended
}

function @extend_u16() -> i64 {
  slot $home : u16

  block ^entry:
    %wide = copy i64 48879
    %value = convert trunc u16 i64 %wide
    store u16 %value, $home
    %reloaded = load u16 $home
    %extended = convert zext i64 u16 %reloaded
    return i64 %extended
}

function @extend_i32() -> i64 {
  slot $home : i32

  block ^entry:
    %wide = copy i64 -1
    %value = convert trunc i32 i64 %wide
    store i32 %value, $home
    %reloaded = load i32 $home
    %extended = convert zext i64 i32 %reloaded
    return i64 %extended
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %raw_i8 = call i8 @return_i8()
    %i8 = convert zext i64 i8 %raw_i8
    %same_i8 = call i64 @extend_i8()
    %u16 = call i64 @extend_u16()
    %i32 = call i64 @extend_i32()
    %bad_raw_i8 = cmp ne i64 %i8, 255
    %bad_same_i8 = cmp ne i64 %same_i8, 255
    %bad_u16 = cmp ne i64 %u16, 48879
    %bad_i32 = cmp ne i64 %i32, 4294967295
    %bad_a = binary or i64 %bad_raw_i8, %bad_same_i8
    %bad_b = binary or i64 %bad_u16, %bad_i32
    %bad = binary or i64 %bad_a, %bad_b
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}

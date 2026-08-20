function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %z8 = convert zext i64 i8 -1
    %bad_z8 = cmp ne i64 %z8, 255
    %s8 = convert sext i64 u8 255
    %bad_s8 = cmp ne i64 %s8, -1
    %z16 = convert zext i32 i16 -1
    %bad_z16 = cmp ne i32 %z16, 65535
    %s16 = convert sext i32 u16 65535
    %bad_s16 = cmp ne i32 %s16, -1
    %bad_a = binary or i64 %bad_z8, %bad_s8
    %bad_b = binary or i64 %bad_z16, %bad_s16
    %bad = binary or i64 %bad_a, %bad_b
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}

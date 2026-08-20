function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %z8 = convert zext i64 u8 255
    %bad8 = cmp ne i64 %z8, 255
    %z16 = convert zext i64 u16 65535
    %bad16 = cmp ne i64 %z16, 65535
    %bad = binary or i64 %bad8, %bad16
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}

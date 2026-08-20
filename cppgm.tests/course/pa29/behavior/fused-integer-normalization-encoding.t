function @signed_source() -> i32 {
  block ^entry:
    return i32 -17
}

function @unsigned_source() -> u8 {
  block ^entry:
    return u8 250
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %signed = call i32 @signed_source()
    %signed_wide = convert sext i64 i32 %signed
    %unsigned = call u8 @unsigned_source()
    %unsigned_wide = convert zext i64 u8 %unsigned
    %constant = copy i64 17
    %constant_narrow = convert trunc i32 i64 %constant
    %bad_signed = cmp ne i64 %signed_wide, -17
    %bad_unsigned = cmp ne i64 %unsigned_wide, 250
    %bad_constant = cmp ne i32 %constant_narrow, 17
    %bad_sources = binary or i64 %bad_signed, %bad_unsigned
    %bad_constant_wide = convert zext i64 i1 %bad_constant
    %bad = binary or i64 %bad_sources, %bad_constant_wide
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}

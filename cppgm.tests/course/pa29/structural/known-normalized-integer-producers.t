function @shift_with_typed_count() -> i32 {
  block ^entry:
    %value = const i32 3
    %count = const i32 2
    %shifted = binary shl i32 %value, %count
    return i32 %shifted
}

function @explicit_truncation() -> i32 {
  block ^entry:
    %wide = const i64 4294967297
    %narrow = convert trunc i32 i64 %wide
    return i32 %narrow
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %shifted = call i32 @shift_with_typed_count()
    %truncated = call i32 @explicit_truncation()
    %shift_bad = cmp ne i32 %shifted, 12
    %trunc_bad = cmp ne i32 %truncated, 1
    %bad = binary or i64 %shift_bad, %trunc_bad
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}

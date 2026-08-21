function @preserve_third(%first : i64, %second : i64, %third : i64) -> i64 {
  block ^entry:
    %first_wide = convert sext i128 i64 %first
    %second_wide = convert sext i128 i64 %second
    %third_wide = convert sext i128 i64 %third
    %result = convert trunc i64 i128 %third_wide
    return i64 %result
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %result = call i64 @preserve_third(1, 2, 14)
    %wrong = cmp ne i64 %result, 14
    %exit = convert trunc i32 i64 %wrong
    return i32 %exit
}

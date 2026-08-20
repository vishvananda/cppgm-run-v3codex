function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %left = binary shl i64 3, 4
    %logical = binary ushr i64 -16, 2
    %arithmetic = binary shr i64 -16, 1
    %zero = binary shl i64 7, 0
    %bad_left = cmp ne i64 %left, 48
    %bad_logical = cmp ne i64 %logical, 4611686018427387900
    %bad_arithmetic = cmp ne i64 %arithmetic, -8
    %bad_zero = cmp ne i64 %zero, 7
    %bad_signed = binary or i64 %bad_arithmetic, %bad_zero
    %bad_right = binary or i64 %bad_logical, %bad_signed
    %bad = binary or i64 %bad_left, %bad_right
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}

function @main() -> i32 [role=entry] {
  block ^entry:
    %low = binary and i64 -1, 3
    %high = binary and i64 -1, 4294967299
    %low_bad = cmp ne i64 %low, 3
    %high_bad = cmp ne i64 %high, 4294967299
    %bad = binary or i64 %low_bad, %high_bad
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}

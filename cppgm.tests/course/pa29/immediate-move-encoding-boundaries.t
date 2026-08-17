function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %u32_max = copy i64 4294967295
    %u32_bad = cmp ne i64 %u32_max, 4294967295
    branch %u32_bad, ^bad_u32, ^check_above_u32

  block ^bad_u32:
    return i32 1

  block ^check_above_u32:
    %above_u32 = copy i64 4294967296
    %above_u32_bad = cmp ne i64 %above_u32, 4294967296
    branch %above_u32_bad, ^bad_above_u32, ^check_i32_min

  block ^bad_above_u32:
    return i32 2

  block ^check_i32_min:
    %i32_min = copy i64 -2147483648
    %i32_min_bad = cmp ne i64 %i32_min, -2147483648
    branch %i32_min_bad, ^bad_i32_min, ^check_below_i32

  block ^bad_i32_min:
    return i32 3

  block ^check_below_i32:
    %below_i32 = copy i64 -2147483649
    %below_i32_bad = cmp ne i64 %below_i32, -2147483649
    branch %below_i32_bad, ^bad_below_i32, ^ok

  block ^bad_below_i32:
    return i32 4

  block ^ok:
    return i32 0
}

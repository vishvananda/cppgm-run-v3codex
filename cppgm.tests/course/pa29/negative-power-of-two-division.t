function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %q2 = binary div i64 100, -2
    %bad_q2 = cmp ne i64 %q2, -50
    %q4 = binary div i64 -99, -4
    %bad_q4 = cmp ne i64 %q4, 24
    %r4 = binary mod i64 -99, -4
    %bad_r4 = cmp ne i64 %r4, -3
    %q_high = binary div i64 -9223372036854775808, -9223372036854775808
    %bad_q_high = cmp ne i64 %q_high, 1
    %r_high = binary mod i64 9223372036854775807, -9223372036854775808
    %bad_r_high = cmp ne i64 %r_high, 9223372036854775807

    %q7 = binary div i64 -100, -7
    %bad_q7 = cmp ne i64 %q7, 14
    %r7 = binary mod i64 -100, -7
    %bad_r7 = cmp ne i64 %r7, -2
    %q1 = binary div i64 100, -1
    %bad_q1 = cmp ne i64 %q1, -100

    %bad_a = binary or i64 %bad_q2, %bad_q4
    %bad_b = binary or i64 %bad_r4, %bad_q_high
    %bad_c = binary or i64 %bad_r_high, %bad_q7
    %bad_d = binary or i64 %bad_r7, %bad_q1
    %bad_ab = binary or i64 %bad_a, %bad_b
    %bad_cd = binary or i64 %bad_c, %bad_d
    %bad = binary or i64 %bad_ab, %bad_cd
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}

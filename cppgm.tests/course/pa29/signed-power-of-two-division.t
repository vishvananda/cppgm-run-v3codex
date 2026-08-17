function @div2(%value : i64) -> i64 [binding=strong] {
  block ^entry:
    %result = binary div i64 %value, 2
    return i64 %result
}

function @mod8(%value : i64) -> i64 [binding=strong] {
  block ^entry:
    %result = binary mod i64 %value, 8
    return i64 %result
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %q1 = binary div i64 -9223372036854775807, 1
    %bad_q1 = cmp ne i64 %q1, -9223372036854775807
    %r1 = binary mod i64 -9223372036854775807, 1
    %bad_r1 = cmp ne i64 %r1, 0
    %q2 = call i64 @div2(-9223372036854775807)
    %bad_q2 = cmp ne i64 %q2, -4611686018427387903
    %r8 = call i64 @mod8(-9223372036854775807)
    %bad_r8 = cmp ne i64 %r8, -7
    %q_high = binary div i64 -9223372036854775808, 4611686018427387904
    %bad_q_high = cmp ne i64 %q_high, -2
    %r_high = binary mod i64 9223372036854775807, 4611686018427387904
    %bad_r_high = cmp ne i64 %r_high, 4611686018427387903

    %q8 = binary div i8 -7, 4
    %q8_bits = convert zext i64 i8 %q8
    %bad_q8 = cmp ne i64 %q8_bits, 255
    %r8_narrow = binary mod i8 -7, 4
    %r8_bits = convert zext i64 i8 %r8_narrow
    %bad_r8_narrow = cmp ne i64 %r8_bits, 253
    %q16 = binary div i16 -32768, 256
    %q16_bits = convert zext i64 i16 %q16
    %bad_q16 = cmp ne i64 %q16_bits, 65408
    %r32 = binary mod i32 -2147483647, 65536
    %r32_bits = convert zext i64 i32 %r32
    %bad_r32 = cmp ne i64 %r32_bits, 4294901761
    %q4_positive = binary div i64 7, 4
    %bad_q4_positive = cmp ne i64 %q4_positive, 1
    %r4_positive = binary mod i64 7, 4
    %bad_r4_positive = cmp ne i64 %r4_positive, 3
    %q4_zero = binary div i64 0, 4
    %bad_q4_zero = cmp ne i64 %q4_zero, 0

    %q7 = binary div i64 -100, 7
    %bad_q7 = cmp ne i64 %q7, -14
    %r7 = binary mod i64 -100, 7
    %bad_r7 = cmp ne i64 %r7, -2
    %q_neg4 = binary div i64 -100, -4
    %bad_q_neg4 = cmp ne i64 %q_neg4, 25

    %bad_a = binary or i64 %bad_q1, %bad_r1
    %bad_a2 = binary or i64 %bad_q2, %bad_r8
    %bad_b = binary or i64 %bad_q_high, %bad_r_high
    %bad_c = binary or i64 %bad_q8, %bad_r8_narrow
    %bad_d = binary or i64 %bad_q16, %bad_r32
    %bad_e = binary or i64 %bad_q4_positive, %bad_r4_positive
    %bad_f = binary or i64 %bad_q4_zero, %bad_q7
    %bad_g = binary or i64 %bad_r7, %bad_q_neg4
    %bad_ab = binary or i64 %bad_a, %bad_a2
    %bad_cd = binary or i64 %bad_b, %bad_c
    %bad_ef = binary or i64 %bad_d, %bad_e
    %bad_fg = binary or i64 %bad_f, %bad_g
    %bad_abcd = binary or i64 %bad_ab, %bad_cd
    %bad_efg = binary or i64 %bad_ef, %bad_fg
    %bad = binary or i64 %bad_abcd, %bad_efg
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}

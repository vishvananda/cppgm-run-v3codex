function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %q1 = binary udiv i64 -1, 1
    %bad_q1 = cmp ne i64 %q1, -1
    %q2 = binary udiv i64 -1, 2
    %bad_q2 = cmp ne i64 %q2, 9223372036854775807
    %r8 = binary umod i64 -1, 8
    %bad_r8 = cmp ne i64 %r8, 7
    %q_high = binary udiv i64 -1, -9223372036854775808
    %bad_q_high = cmp ne i64 %q_high, 1
    %r_high = binary umod i64 -1, -9223372036854775808
    %bad_r_high = cmp ne i64 %r_high, 9223372036854775807

    %q8 = binary udiv u8 255, 4
    %bad_q8 = cmp ne u8 %q8, 63
    %r16 = binary umod u16 65535, 256
    %bad_r16 = cmp ne u16 %r16, 255
    %q32 = binary udiv u32 4294967295, 65536
    %bad_q32 = cmp ne u32 %q32, 65535
    %q7 = binary udiv i64 100, 7
    %bad_q7 = cmp ne i64 %q7, 14
    %r7 = binary umod i64 100, 7
    %bad_r7 = cmp ne i64 %r7, 2

    %bad_a = binary or i64 %bad_q1, %bad_q2
    %bad_b = binary or i64 %bad_r8, %bad_q_high
    %bad_c = binary or i64 %bad_r_high, %bad_q8
    %bad_d = binary or i64 %bad_r16, %bad_q32
    %bad_e = binary or i64 %bad_q7, %bad_r7
    %bad_ab = binary or i64 %bad_a, %bad_b
    %bad_cd = binary or i64 %bad_c, %bad_d
    %bad_abcd = binary or i64 %bad_ab, %bad_cd
    %bad = binary or i64 %bad_abcd, %bad_e
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}

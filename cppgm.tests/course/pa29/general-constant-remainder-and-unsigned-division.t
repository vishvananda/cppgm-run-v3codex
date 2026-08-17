function @smod3(%value : i64) -> i64 [binding=strong] {
  block ^entry:
    %result = binary mod i64 %value, 3
    return i64 %result
}

function @smod_negative5(%value : i64) -> i64 [binding=strong] {
  block ^entry:
    %result = binary mod i64 %value, -5
    return i64 %result
}

function @smod_narrow3(%value : i8) -> i8 [binding=strong] {
  block ^entry:
    %result = binary mod i8 %value, 3
    return i8 %result
}

function @sdiv_i16_7(%value : i16) -> i16 [binding=strong] {
  block ^entry:
    %result = binary div i16 %value, 7
    return i16 %result
}

function @smod_i16_7(%value : i16) -> i16 [binding=strong] {
  block ^entry:
    %result = binary mod i16 %value, 7
    return i16 %result
}

function @sdiv_i32_negative31(%value : i32) -> i32 [binding=strong] {
  block ^entry:
    %result = binary div i32 %value, -31
    return i32 %result
}

function @smod_i32_negative31(%value : i32) -> i32 [binding=strong] {
  block ^entry:
    %result = binary mod i32 %value, -31
    return i32 %result
}

function @udiv3(%value : i64) -> i64 [binding=strong] {
  block ^entry:
    %result = binary udiv i64 %value, 3
    return i64 %result
}

function @udiv7(%value : i64) -> i64 [binding=strong] {
  block ^entry:
    %result = binary udiv i64 %value, 7
    return i64 %result
}

function @umod7(%value : i64) -> i64 [binding=strong] {
  block ^entry:
    %result = binary umod i64 %value, 7
    return i64 %result
}

function @udiv_high(%value : i64) -> i64 [binding=strong] {
  block ^entry:
    %result = binary udiv i64 %value, -9223372036854775807
    return i64 %result
}

function @umod_max(%value : i64) -> i64 [binding=strong] {
  block ^entry:
    %result = binary umod i64 %value, -1
    return i64 %result
}

function @udiv_u8_7(%value : u8) -> u8 [binding=strong] {
  block ^entry:
    %result = binary udiv u8 %value, 7
    return u8 %result
}

function @umod_u8_7(%value : u8) -> u8 [binding=strong] {
  block ^entry:
    %result = binary umod u8 %value, 7
    return u8 %result
}

function @udiv_u16_1000(%value : u16) -> u16 [binding=strong] {
  block ^entry:
    %result = binary udiv u16 %value, 1000
    return u16 %result
}

function @umod_u16_1000(%value : u16) -> u16 [binding=strong] {
  block ^entry:
    %result = binary umod u16 %value, 1000
    return u16 %result
}

function @udiv_u32_31(%value : u32) -> u32 [binding=strong] {
  block ^entry:
    %result = binary udiv u32 %value, 31
    return u32 %result
}

function @umod_u32_31(%value : u32) -> u32 [binding=strong] {
  block ^entry:
    %result = binary umod u32 %value, 31
    return u32 %result
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %sr3 = call i64 @smod3(-9223372036854775808)
    %bad_sr3 = cmp ne i64 %sr3, -2
    %srn5 = call i64 @smod_negative5(9223372036854775807)
    %bad_srn5 = cmp ne i64 %srn5, 2
    %sr8 = call i8 @smod_narrow3(-128)
    %bad_sr8 = cmp ne i8 %sr8, -2
    %sq16 = call i16 @sdiv_i16_7(-32768)
    %bad_sq16 = cmp ne i16 %sq16, -4681
    %sr16 = call i16 @smod_i16_7(-32768)
    %bad_sr16 = cmp ne i16 %sr16, -1
    %sq32 = call i32 @sdiv_i32_negative31(-2147483648)
    %bad_sq32 = cmp ne i32 %sq32, 69273666
    %sr32 = call i32 @smod_i32_negative31(-2147483648)
    %bad_sr32 = cmp ne i32 %sr32, -2

    %uq3 = call i64 @udiv3(-1)
    %bad_uq3 = cmp ne i64 %uq3, 6148914691236517205
    %uq7 = call i64 @udiv7(-1)
    %bad_uq7 = cmp ne i64 %uq7, 2635249153387078802
    %ur7 = call i64 @umod7(-1)
    %bad_ur7 = cmp ne i64 %ur7, 1
    %uq_high = call i64 @udiv_high(-1)
    %bad_uq_high = cmp ne i64 %uq_high, 1
    %ur_max = call i64 @umod_max(-2)
    %bad_ur_max = cmp ne i64 %ur_max, -2

    %uq8 = call u8 @udiv_u8_7(255)
    %bad_uq8 = cmp ne u8 %uq8, 36
    %ur8 = call u8 @umod_u8_7(255)
    %bad_ur8 = cmp ne u8 %ur8, 3
    %uq16 = call u16 @udiv_u16_1000(65535)
    %bad_uq16 = cmp ne u16 %uq16, 65
    %ur16 = call u16 @umod_u16_1000(65535)
    %bad_ur16 = cmp ne u16 %ur16, 535
    %uq32 = call u32 @udiv_u32_31(4294967295)
    %bad_uq32 = cmp ne u32 %uq32, 138547332
    %ur32 = call u32 @umod_u32_31(4294967295)
    %bad_ur32 = cmp ne u32 %ur32, 3

    %bad_a = binary or i64 %bad_sr3, %bad_srn5
    %bad_b = binary or i64 %bad_sr8, %bad_sq16
    %bad_c = binary or i64 %bad_sr16, %bad_sq32
    %bad_d = binary or i64 %bad_sr32, %bad_uq3
    %bad_e = binary or i64 %bad_uq7, %bad_ur7
    %bad_f = binary or i64 %bad_uq_high, %bad_ur_max
    %bad_g = binary or i64 %bad_uq8, %bad_ur8
    %bad_h = binary or i64 %bad_uq16, %bad_ur16
    %bad_i = binary or i64 %bad_uq32, %bad_ur32
    %bad_ab = binary or i64 %bad_a, %bad_b
    %bad_cd = binary or i64 %bad_c, %bad_d
    %bad_ef = binary or i64 %bad_e, %bad_f
    %bad_gh = binary or i64 %bad_g, %bad_h
    %bad_abcd = binary or i64 %bad_ab, %bad_cd
    %bad_efgh = binary or i64 %bad_ef, %bad_gh
    %bad_abcdefg = binary or i64 %bad_abcd, %bad_efgh
    %bad = binary or i64 %bad_abcdefg, %bad_i
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}

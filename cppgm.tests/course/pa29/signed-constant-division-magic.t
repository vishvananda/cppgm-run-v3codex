function @div3(%value : i64) -> i64 [binding=strong] {
  block ^entry:
    %result = binary div i64 %value, 3
    return i64 %result
}

function @div7(%value : i64) -> i64 [binding=strong] {
  block ^entry:
    %result = binary div i64 %value, 7
    return i64 %result
}

function @div_large(%value : i64) -> i64 [binding=strong] {
  block ^entry:
    %result = binary div i64 %value, 2147483647
    return i64 %result
}

function @div_negative3(%value : i64) -> i64 [binding=strong] {
  block ^entry:
    %result = binary div i64 %value, -3
    return i64 %result
}

function @div_negative5(%value : i64) -> i64 [binding=strong] {
  block ^entry:
    %result = binary div i64 %value, -5
    return i64 %result
}

function @div_narrow3(%value : i8) -> i8 [binding=strong] {
  block ^entry:
    %result = binary div i8 %value, 3
    return i8 %result
}

function @div_narrow_negative3(%value : i8) -> i8 [binding=strong] {
  block ^entry:
    %result = binary div i8 %value, -3
    return i8 %result
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %q3 = call i64 @div3(-9223372036854775808)
    %bad_q3 = cmp ne i64 %q3, -3074457345618258602
    %q7 = call i64 @div7(9223372036854775807)
    %bad_q7 = cmp ne i64 %q7, 1317624576693539401
    %q_large = call i64 @div_large(9223372036854775807)
    %bad_q_large = cmp ne i64 %q_large, 4294967298
    %q_negative3 = call i64 @div_negative3(-9223372036854775808)
    %bad_q_negative3 = cmp ne i64 %q_negative3, 3074457345618258602
    %q_negative5 = call i64 @div_negative5(9223372036854775807)
    %bad_q_negative5 = cmp ne i64 %q_negative5, -1844674407370955161
    %q_narrow3 = call i8 @div_narrow3(-128)
    %bad_q_narrow3 = cmp ne i8 %q_narrow3, -42
    %q_narrow_negative3 = call i8 @div_narrow_negative3(127)
    %bad_q_narrow_negative3 = cmp ne i8 %q_narrow_negative3, -42

    %bad_a = binary or i64 %bad_q3, %bad_q7
    %bad_b = binary or i64 %bad_q_large, %bad_q_negative3
    %bad_c = binary or i64 %bad_q_negative5, %bad_q_narrow3
    %bad_d = binary or i64 %bad_c, %bad_q_narrow_negative3
    %bad_ab = binary or i64 %bad_a, %bad_b
    %bad = binary or i64 %bad_ab, %bad_d
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}

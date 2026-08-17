function @smod7(%value : i64) -> i64 [binding=strong] {
  block ^entry:
    %result = binary mod i64 %value, 7
    return i64 %result
}

function @udiv7(%value : i64) -> i64 [binding=strong] {
  block ^entry:
    %result = binary udiv i64 %value, 7
    return i64 %result
}

function @umod31(%value : i64) -> i64 [binding=strong] {
  block ^entry:
    %result = binary umod i64 %value, 31
    return i64 %result
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %sr = call i64 @smod7(-9223372036854775808)
    %bad_sr = cmp ne i64 %sr, -1
    %uq = call i64 @udiv7(-1)
    %bad_uq = cmp ne i64 %uq, 2635249153387078802
    %ur = call i64 @umod31(-1)
    %bad_ur = cmp ne i64 %ur, 15
    %bad_a = binary or i64 %bad_sr, %bad_uq
    %bad = binary or i64 %bad_a, %bad_ur
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}

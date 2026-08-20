global @single : f32 = 0x1.000002p0
global @double : f64 = 0x1.0000000000001p0
global @extended : f80 = 0x1.0000000000000002p0L
global @wide : i128 = -1

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %single = load f32 @single
    %bad_single = cmp ne f32 %single, 0x1.000002p0
    %double = load f64 @double
    %bad_double = cmp ne f64 %double, 0x1.0000000000001p0
    %extended = load f80 @extended
    %bad_extended = cmp ne f80 %extended, 0x1.0000000000000002p0L
    %wide = load i128 @wide
    %bad_wide = cmp ne i128 %wide, -1
    %bad_a = binary or i64 %bad_single, %bad_double
    %bad_b = binary or i64 %bad_extended, %bad_wide
    %bad = binary or i64 %bad_a, %bad_b
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}

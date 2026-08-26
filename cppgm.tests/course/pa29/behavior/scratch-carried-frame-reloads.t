global @v0 : i64 = 1
global @v1 : i64 = 2
global @v2 : i64 = 3
global @v3 : i64 = 4
global @v4 : i64 = 5
global @v5 : i64 = 6
global @v6 : i64 = 7
global @v7 : i64 = 8
global @v8 : i64 = 9
global @v9 : i64 = 10
global @v10 : i64 = 11
global @v11 : i64 = 12
global @v12 : i64 = 13
global @v13 : i64 = 14
global @v14 : i64 = 15
global @v15 : i64 = 16

function @sum_under_pressure() -> i64 {
  block ^entry:
    %v0 = load i64 @v0
    %v1 = load i64 @v1
    %v2 = load i64 @v2
    %v3 = load i64 @v3
    %v4 = load i64 @v4
    %v5 = load i64 @v5
    %v6 = load i64 @v6
    %v7 = load i64 @v7
    %v8 = load i64 @v8
    %v9 = load i64 @v9
    %v10 = load i64 @v10
    %v11 = load i64 @v11
    %v12 = load i64 @v12
    %v13 = load i64 @v13
    %v14 = load i64 @v14
    %v15 = load i64 @v15
    %x0 = binary add i64 %v0, %v1
    %x1 = binary add i64 %v2, %v3
    %x2 = binary add i64 %v4, %v5
    %x3 = binary add i64 %v6, %v7
    %x4 = binary add i64 %v8, %v9
    %x5 = binary add i64 %v10, %v11
    %x6 = binary add i64 %v12, %v13
    %x7 = binary add i64 %v14, %v15
    %s0 = binary add i64 %v0, %v1
    %s1 = binary add i64 %v2, %v3
    %s2 = binary add i64 %v4, %v5
    %s3 = binary add i64 %v6, %v7
    %s4 = binary add i64 %v8, %v9
    %s5 = binary add i64 %v10, %v11
    %s6 = binary add i64 %v12, %v13
    %s7 = binary add i64 %v14, %v15
    %s8 = binary add i64 %s0, %s1
    %s9 = binary add i64 %s2, %s3
    %s10 = binary add i64 %s4, %s5
    %s11 = binary add i64 %s6, %s7
    %s12 = binary add i64 %s8, %s9
    %s13 = binary add i64 %s10, %s11
    %original_sum = binary add i64 %s12, %s13
    %x8 = binary add i64 %x0, %x1
    %x9 = binary add i64 %x2, %x3
    %x10 = binary add i64 %x4, %x5
    %x11 = binary add i64 %x6, %x7
    %x12 = binary add i64 %x8, %x9
    %x13 = binary add i64 %x10, %x11
    %xsum = binary add i64 %x12, %x13
    %sum = binary add i64 %original_sum, %xsum
    return i64 %sum
}

function @sum_under_float_pressure() -> i64 {
  block ^entry:
    %v0 = load i64 @v0
    %v1 = load i64 @v1
    %v2 = load i64 @v2
    %v3 = load i64 @v3
    %v4 = load i64 @v4
    %v5 = load i64 @v5
    %v6 = load i64 @v6
    %v7 = load i64 @v7
    %v8 = load i64 @v8
    %v9 = load i64 @v9
    %v10 = load i64 @v10
    %v11 = load i64 @v11
    %v12 = load i64 @v12
    %v13 = load i64 @v13
    %v14 = load i64 @v14
    %v15 = load i64 @v15
    %x0 = binary add i64 %v0, %v1
    %x1 = binary add i64 %v2, %v3
    %x2 = binary add i64 %v4, %v5
    %x3 = binary add i64 %v6, %v7
    %x4 = binary add i64 %v8, %v9
    %x5 = binary add i64 %v10, %v11
    %x6 = binary add i64 %v12, %v13
    %x7 = binary add i64 %v14, %v15
    %x8 = binary add i64 %x0, %x1
    %x10 = binary add i64 %x4, %x5
    %x11 = binary add i64 %x6, %x7
    %floating = binary add f64 1.5, 2.5
    %s0 = binary add i64 %v0, %v1
    %s1 = binary add i64 %v2, %v3
    %s2 = binary add i64 %v4, %v5
    %s3 = binary add i64 %v6, %v7
    %s4 = binary add i64 %v8, %v9
    %s5 = binary add i64 %v10, %v11
    %s6 = binary add i64 %v12, %v13
    %s7 = binary add i64 %v14, %v15
    %s8 = binary add i64 %s0, %s1
    %s9 = binary add i64 %s2, %s3
    %s10 = binary add i64 %s4, %s5
    %s11 = binary add i64 %s6, %s7
    %s12 = binary add i64 %s8, %s9
    %s13 = binary add i64 %s10, %s11
    %original_sum = binary add i64 %s12, %s13
    %x9 = binary add i64 %x2, %x3
    %x12 = binary add i64 %x8, %x9
    %x13 = binary add i64 %x10, %x11
    %xsum = binary add i64 %x12, %x13
    %integer_sum = binary add i64 %original_sum, %xsum
    %float_bad = cmp ne f64 %floating, 4.0
    %sum = binary add i64 %integer_sum, %float_bad
    return i64 %sum
}

function @main() -> i64 [role=entry] {
  block ^entry:
    %sum = call i64 @sum_under_pressure()
    %float_sum = call i64 @sum_under_float_pressure()
    %sum_bad = cmp ne i64 %sum, 272
    %float_bad = cmp ne i64 %float_sum, 272
    %bad = binary or i64 %sum_bad, %float_bad
    return i64 %bad
}

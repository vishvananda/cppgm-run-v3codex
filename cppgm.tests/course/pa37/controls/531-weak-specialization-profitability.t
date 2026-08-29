function @control_target(%mode : i64, %value : i64) -> i64
    [binding=weak, unwind=no] {
  block ^entry:
    branch %mode, ^selected, ^large_fallback
  block ^selected:
    %selected_value = binary add i64 %value, 1
    return i64 %selected_value
  block ^large_fallback:
    %c0 = binary add i64 %value, 1
    %c1 = binary add i64 %c0, 1
    %c2 = binary add i64 %c1, 1
    %c3 = binary add i64 %c2, 1
    %c4 = binary add i64 %c3, 1
    %c5 = binary add i64 %c4, 1
    %c6 = binary add i64 %c5, 1
    %c7 = binary add i64 %c6, 1
    %c8 = binary add i64 %c7, 1
    %c9 = binary add i64 %c8, 1
    %c10 = binary add i64 %c9, 1
    %c11 = binary add i64 %c10, 1
    %c12 = binary add i64 %c11, 1
    %c13 = binary add i64 %c12, 1
    %c14 = binary add i64 %c13, 1
    %c15 = binary add i64 %c14, 1
    %c16 = binary add i64 %c15, 1
    %c17 = binary add i64 %c16, 1
    %c18 = binary add i64 %c17, 1
    %c19 = binary add i64 %c18, 1
    %c20 = binary add i64 %c19, 1
    %c21 = binary add i64 %c20, 1
    %c22 = binary add i64 %c21, 1
    %c23 = binary add i64 %c22, 1
    %c24 = binary add i64 %c23, 1
    %c25 = binary add i64 %c24, 1
    %c26 = binary add i64 %c25, 1
    %c27 = binary add i64 %c26, 1
    %c28 = binary add i64 %c27, 1
    %c29 = binary add i64 %c28, 1
    %c30 = binary add i64 %c29, 1
    %c31 = binary add i64 %c30, 1
    %c32 = binary add i64 %c31, 1
    %c33 = binary add i64 %c32, 1
    %c34 = binary add i64 %c33, 1
    %c35 = binary add i64 %c34, 1
    %c36 = binary add i64 %c35, 1
    %c37 = binary add i64 %c36, 1
    %c38 = binary add i64 %c37, 1
    %c39 = binary add i64 %c38, 1
    %c40 = binary add i64 %c39, 1
    return i64 %c40
}

function @data_target(%bias : i64, %value : i64) -> i64
    [binding=weak, unwind=no] {
  block ^entry:
    %d0 = binary add i64 %value, %bias
    %d1 = binary add i64 %d0, %bias
    %d2 = binary add i64 %d1, %bias
    %d3 = binary add i64 %d2, %bias
    %d4 = binary add i64 %d3, %bias
    %d5 = binary add i64 %d4, %bias
    %d6 = binary add i64 %d5, %bias
    %d7 = binary add i64 %d6, %bias
    %d8 = binary add i64 %d7, %bias
    %d9 = binary add i64 %d8, %bias
    %d10 = binary add i64 %d9, %bias
    %d11 = binary add i64 %d10, %bias
    %d12 = binary add i64 %d11, %bias
    %d13 = binary add i64 %d12, %bias
    %d14 = binary add i64 %d13, %bias
    %d15 = binary add i64 %d14, %bias
    %d16 = binary add i64 %d15, %bias
    %d17 = binary add i64 %d16, %bias
    %d18 = binary add i64 %d17, %bias
    %d19 = binary add i64 %d18, %bias
    %d20 = binary add i64 %d19, %bias
    %d21 = binary add i64 %d20, %bias
    %d22 = binary add i64 %d21, %bias
    %d23 = binary add i64 %d22, %bias
    %d24 = binary add i64 %d23, %bias
    %d25 = binary add i64 %d24, %bias
    %d26 = binary add i64 %d25, %bias
    %d27 = binary add i64 %d26, %bias
    %d28 = binary add i64 %d27, %bias
    %d29 = binary add i64 %d28, %bias
    %d30 = binary add i64 %d29, %bias
    %d31 = binary add i64 %d30, %bias
    %d32 = binary add i64 %d31, %bias
    %d33 = binary add i64 %d32, %bias
    %d34 = binary add i64 %d33, %bias
    %d35 = binary add i64 %d34, %bias
    %d36 = binary add i64 %d35, %bias
    %d37 = binary add i64 %d36, %bias
    %d38 = binary add i64 %d37, %bias
    %d39 = binary add i64 %d38, %bias
    %d40 = binary add i64 %d39, %bias
    return i64 %d40
}

function @main() -> i64 [role=entry, unwind=no] {
  block ^entry:
    %control0 = call i64 @control_target(1, 10)
    %control1 = call i64 @control_target(1, 20)
    %data0 = call i64 @data_target(7, 3)
    %data1 = call i64 @data_target(7, 4)
    %control_sum = binary add i64 %control0, %control1
    %data_sum = binary add i64 %data0, %data1
    %sum = binary add i64 %control_sum, %data_sum
    %bad = cmp ne i64 %sum, 613
    return i64 %bad
}

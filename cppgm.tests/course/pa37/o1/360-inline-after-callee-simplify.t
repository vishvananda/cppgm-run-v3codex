function @callee(%x : i64) -> i64 {
  block ^entry:
    %d01 = binary add i64 %x, 1
    %d02 = binary add i64 %d01, 1
    %d03 = binary add i64 %d02, 1
    %d04 = binary add i64 %d03, 1
    %d05 = binary add i64 %d04, 1
    %d06 = binary add i64 %d05, 1
    %d07 = binary add i64 %d06, 1
    %d08 = binary add i64 %d07, 1
    %d09 = binary add i64 %d08, 1
    %d10 = binary add i64 %d09, 1
    %d11 = binary add i64 %d10, 1
    %d12 = binary add i64 %d11, 1
    %d13 = binary add i64 %d12, 1
    %d14 = binary add i64 %d13, 1
    %d15 = binary add i64 %d14, 1
    %d16 = binary add i64 %d15, 1
    %d17 = binary add i64 %d16, 1
    %d18 = binary add i64 %d17, 1
    %d19 = binary add i64 %d18, 1
    %d20 = binary add i64 %d19, 1
    %d21 = binary add i64 %d20, 1
    %d22 = binary add i64 %d21, 1
    %d23 = binary add i64 %d22, 1
    %d24 = binary add i64 %d23, 1
    %d25 = binary add i64 %d24, 1
    %d26 = binary add i64 %d25, 1
    %d27 = binary add i64 %d26, 1
    %d28 = binary add i64 %d27, 1
    %d29 = binary add i64 %d28, 1
    %d30 = binary add i64 %d29, 1
    %d31 = binary add i64 %d30, 1
    %d32 = binary add i64 %d31, 1
    %d33 = binary add i64 %d32, 1
    %d34 = binary add i64 %d33, 1
    %d35 = binary add i64 %d34, 1
    %d36 = binary add i64 %d35, 1
    %d37 = binary add i64 %d36, 1
    %d38 = binary add i64 %d37, 1
    %d39 = binary add i64 %d38, 1
    %d40 = binary add i64 %d39, 1
    %d41 = binary add i64 %d40, 1
    return i64 %x
}

function @caller(%x : i64) -> i64 {
  block ^entry:
    %result = call i64 @callee(%x)
    return i64 %result
}

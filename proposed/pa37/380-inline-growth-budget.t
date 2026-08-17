function @add_nine(%x : i64) -> i64 {
  block ^entry:
    %a1 = binary add i64 %x, 1
    %a2 = binary add i64 %a1, 2
    %a3 = binary add i64 %a2, 3
    %a4 = binary add i64 %a3, 4
    %a5 = binary add i64 %a4, 5
    %a6 = binary add i64 %a5, 6
    %a7 = binary add i64 %a6, 7
    %a8 = binary add i64 %a7, 8
    %a9 = binary add i64 %a8, 9
    return i64 %a9
}

function @many_calls(%x : i64) -> i64 {
  block ^entry:
    %r01 = call i64 @add_nine(%x)
    %r02 = call i64 @add_nine(%x)
    %r03 = call i64 @add_nine(%x)
    %r04 = call i64 @add_nine(%x)
    %r05 = call i64 @add_nine(%x)
    %r06 = call i64 @add_nine(%x)
    %r07 = call i64 @add_nine(%x)
    %r08 = call i64 @add_nine(%x)
    %r09 = call i64 @add_nine(%x)
    %r10 = call i64 @add_nine(%x)
    %r11 = call i64 @add_nine(%x)
    %r12 = call i64 @add_nine(%x)
    %r13 = call i64 @add_nine(%x)
    %r14 = call i64 @add_nine(%x)
    %r15 = call i64 @add_nine(%x)
    %s02 = binary add i64 %r01, %r02
    %s03 = binary add i64 %s02, %r03
    %s04 = binary add i64 %s03, %r04
    %s05 = binary add i64 %s04, %r05
    %s06 = binary add i64 %s05, %r06
    %s07 = binary add i64 %s06, %r07
    %s08 = binary add i64 %s07, %r08
    %s09 = binary add i64 %s08, %r09
    %s10 = binary add i64 %s09, %r10
    %s11 = binary add i64 %s10, %r11
    %s12 = binary add i64 %s11, %r12
    %s13 = binary add i64 %s12, %r13
    %s14 = binary add i64 %s13, %r14
    %s15 = binary add i64 %s14, %r15
    return i64 %s15
}

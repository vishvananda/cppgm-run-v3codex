function @first(%a : i64, %b : i64, %c : i64, %d : i64, %e : i64, %f : i64) -> i64 {
  block ^entry:
    return i64 %a
}

function @pressure(%a : i64, %b : i64, %c : i64, %d : i64, %e : i64, %f : i64, %g : i64, %h : i64, %i : i64, %j : i64) -> i64 {
  block ^entry:
    %t1 = binary add i64 %a, 1
    %t2 = binary add i64 %b, 2
    %t3 = binary add i64 %c, 3
    %t4 = binary add i64 %d, 4
    %t5 = binary add i64 %e, 5
    %t6 = binary add i64 %f, 6
    %t7 = binary add i64 %g, 7
    %t8 = binary add i64 %h, 8
    %t9 = binary add i64 %i, 9
    %t10 = binary add i64 %j, 10
    %original = call i64 @first(21, 22, 23, 24, 25, 26)
    %saved = copy i64 %original
    %replacement = call i64 @first(31, 32, 33, 34, 35, 36)
    %s1 = binary add i64 %t1, %t2
    %s2 = binary add i64 %s1, %t3
    %s3 = binary add i64 %s2, %t4
    %s4 = binary add i64 %s3, %t5
    %s5 = binary add i64 %s4, %t6
    %s6 = binary add i64 %s5, %t7
    %s7 = binary add i64 %s6, %t8
    %s8 = binary add i64 %s7, %t9
    %s9 = binary add i64 %s8, %t10
    %s10 = binary add i64 %s9, %saved
    %result = binary add i64 %s10, %replacement
    return i64 %result
}

function @main() -> i32 [role=entry] {
  block ^entry:
    %result = call i64 @pressure(1, 2, 3, 4, 5, 6, 7, 8, 9, 10)
    %ok = cmp eq i64 %result, 162
    %bad = cmp eq i64 %ok, 0
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}

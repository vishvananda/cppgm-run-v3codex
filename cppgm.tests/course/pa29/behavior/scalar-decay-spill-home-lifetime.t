global @bytes = {
  zero 128
}

function @length_of(%value : ptr) -> i64 {
  block ^entry:
    return i64 3
}

function @observe(%p1 : ptr, %p2 : ptr, %p3 : ptr, %p4 : ptr, %p5 : ptr, %p6 : ptr, %p7 : ptr, %p8 : ptr, %p9 : ptr, %q1 : i64, %q2 : i64, %q3 : i64, %q4 : i64, %q5 : i64, %q6 : i64, %q7 : i64, %q8 : i64, %q9 : i64) -> i64 {
  block ^entry:
    %base = addr @bytes
    %expected6 = index i8 %base, 48
    %ok6 = cmp eq ptr %p6, %expected6
    %expected7 = index i8 %base, 56
    %ok7 = cmp eq ptr %p7, %expected7
    %expected8 = index i8 %base, 64
    %ok8 = cmp eq ptr %p8, %expected8
    %expected9 = index i8 %base, 72
    %ok9 = cmp eq ptr %p9, %expected9
    %count_ok = cmp eq i64 %q9, 12
    %ok67 = binary and i64 %ok6, %ok7
    %ok89 = binary and i64 %ok8, %ok9
    %pointers_ok = binary and i64 %ok67, %ok89
    %all_ok = binary and i64 %pointers_ok, %count_ok
    %result = binary mul i64 %all_ok, 100
    return i64 %result
}

function @exercise(%base : ptr) -> i64 {
  block ^entry:
    %p1 = index i8 %base, 8
    %p2 = index i8 %base, 16
    %p3 = index i8 %base, 24
    %p4 = index i8 %base, 32
    %p5 = index i8 %base, 40
    %p6 = index i8 %base, 48
    %p7 = index i8 %base, 56
    %p8 = index i8 %base, 64
    %p9 = index i8 %base, 72
    %a1 = copy ptr %p1
    %a2 = copy ptr %p2
    %a3 = copy ptr %p3
    %a4 = copy ptr %p4
    %a5 = copy ptr %p5
    %a6 = copy ptr %p6
    %a7 = copy ptr %p7
    %a8 = copy ptr %p8
    %a9 = copy ptr %p9
    %length = call i64 @length_of(%base)
    %q1 = binary add i64 %length, 1
    %q2 = binary add i64 %length, 2
    %q3 = binary add i64 %length, 3
    %q4 = binary add i64 %length, 4
    %q5 = binary add i64 %length, 5
    %q6 = binary add i64 %length, 6
    %q7 = binary add i64 %length, 7
    %q8 = binary add i64 %length, 8
    %q9 = binary add i64 %length, 9
    %observed = call i64 @observe(%a1, %a2, %a3, %a4, %a5, %a6, %a7, %a8, %a9, %q1, %q2, %q3, %q4, %q5, %q6, %q7, %q8, %q9)
    return i64 %observed
}

function @main() -> i32 [role=entry] {
  block ^entry:
    %base = addr @bytes
    %actual = call i64 @exercise(%base)
    %wrong = cmp ne i64 %actual, 100
    %exit = convert trunc i32 i64 %wrong
    return i32 %exit
}

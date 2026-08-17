global @bytes = {
  zero 16
}

function @initialize(%base : ptr) -> void {
  block ^entry:
    %p0 = index i8 %base, 0
    store i8 1, %p0
    %p1 = index i8 %base, 1
    store i8 35, %p1
    %p2 = index i8 %base, 2
    store i8 69, %p2
    %p3 = index i8 %base, 3
    store i8 103, %p3
    %p4 = index i8 %base, 4
    store i8 137, %p4
    %p5 = index i8 %base, 5
    store i8 171, %p5
    %p6 = index i8 %base, 6
    store i8 205, %p6
    %p7 = index i8 %base, 7
    store i8 239, %p7
    %p8 = index i8 %base, 8
    store i8 254, %p8
    %p9 = index i8 %base, 9
    store i8 220, %p9
    %p10 = index i8 %base, 10
    store i8 186, %p10
    %p11 = index i8 %base, 11
    store i8 152, %p11
    %p12 = index i8 %base, 12
    store i8 118, %p12
    %p13 = index i8 %base, 13
    store i8 84, %p13
    %p14 = index i8 %base, 14
    store i8 50, %p14
    %p15 = index i8 %base, 15
    store i8 16, %p15
    return void
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %base = addr @bytes
    call void @initialize(%base)
    %first = load i64 @bytes
    %first_bad = cmp ne i64 %first, -1167088121787636991
    branch %first_bad, ^bad, ^second

  block ^second:
    %base2 = addr @bytes
    %high_address = index i8 %base2, 8
    %high = load i64 %high_address
    %high_bad = cmp ne i64 %high, 1167088121787636990
    branch %high_bad, ^bad, ^ok

  block ^bad:
    return i32 1

  block ^ok:
    return i32 0
}

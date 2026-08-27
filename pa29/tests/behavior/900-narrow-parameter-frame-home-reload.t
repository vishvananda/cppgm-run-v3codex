global @table [binding=strong] = {
  i64 11
  i64 22
  i64 33
}

function @dirty_stack() -> void {
  slot $junk : obj<64x8>

  block ^entry:
    %base = addr $junk
    store i64 -1, %base
    %p1 = index i8 %base, 8
    store i64 -1, %p1
    %p2 = index i8 %base, 16
    store i64 -1, %p2
    %p3 = index i8 %base, 24
    store i64 -1, %p3
    %p4 = index i8 %base, 32
    store i64 -1, %p4
    %p5 = index i8 %base, 40
    store i64 -1, %p5
    %p6 = index i8 %base, 48
    store i64 -1, %p6
    %p7 = index i8 %base, 56
    store i64 -1, %p7
    return void
}

function @side_effect() -> void {
  block ^entry:
    return void
}

function @lookup(%a : i32, %b : i32, %c : i32, %d : i32, %e : i32, %level : i32) -> i64 {
  block ^entry:
    call void @side_effect()
    %table = addr @table
    %data = copy ptr %table
    %offset = binary mul i64 %level, 8
    %entry = index i8 %data, %offset
    %value = load i64 %entry
    %a64 = binary mul i64 %a, 1
    %b64 = binary mul i64 %b, 1
    %c64 = binary mul i64 %c, 1
    %d64 = binary mul i64 %d, 1
    %e64 = binary mul i64 %e, 1
    %sum1 = binary add i64 %value, %a64
    %sum2 = binary add i64 %sum1, %b64
    %sum3 = binary add i64 %sum2, %c64
    %sum4 = binary add i64 %sum3, %d64
    %sum5 = binary add i64 %sum4, %e64
    return i64 %sum5
}

function @main() -> i32 [role=entry, binding=strong] {
  block ^entry:
    call void @dirty_stack()
    %result = call i64 @lookup(2, 3, 4, 5, 6, 1)
    %ok = cmp eq i64 %result, 42
    branch %ok, ^good, ^bad

  block ^good:
    return i32 0

  block ^bad:
    return i32 1
}

function @sink(%a : i64, %b : i64, %c : i64, %d : i64, %e : i64) -> void [unwind=no] {
  block ^entry:
    return void
}

function @probe(%p : ptr, %q : ptr) -> i64 [unwind=no] {
  block ^entry:
    %y = load i64 %q
    %pv = index i8 [projection=field] %p, 8
    %x = load i64 %pv
    %c = cmp ne i64 %y, %x
    call void @sink(0, 0, 0, 0, 0)
    branch %c, ^differ, ^same

  block ^differ:
    return i64 1

  block ^same:
    return i64 0
}

function @main() -> i64 [role=entry] {
  slot $left : obj<16x8>
  slot $right : i64

  block ^entry:
    %base = addr $left
    %field = index i8 [projection=field] %base, 8
    store i64 41, %field
    store i64 7, $right
    %raddr = addr $right
    %differ = call i64 @probe(%base, %raddr)
    store i64 41, $right
    %same = call i64 @probe(%base, %raddr)
    %differ_bad = cmp ne i64 %differ, 1
    %same_bad = cmp ne i64 %same, 0
    %bad = binary or i64 %differ_bad, %same_bad
    return i64 %bad
}

global @cells = {
  i64 0
  i64 0
}

function @produce() -> i64 {
  block ^entry:
    return i64 29
}

function @write_second(%base : ptr) -> void {
  block ^entry:
    %second = index i8 %base, 8
    jump ^write

  block ^write:
    %value = call i64 @produce()
    store i64 %value, %second
    store i64 11, %base
    return void
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %base = addr @cells
    call void @write_second(%base)
    %second = index i8 %base, 8
    %value = load i64 %second
    %bad = cmp ne i64 %value, 29
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}

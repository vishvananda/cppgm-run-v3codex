global @values = {
  i64 10
  i64 20
  i64 30
  i64 40
}

function @load_at(%base : ptr, %which : i64) -> i64 {
  block ^entry:
    %address = index i64 %base, %which
    %value = load i64 %address
    return i64 %value
}

function @store_at(%base : ptr, %which : i64, %value : i64) -> void {
  block ^entry:
    %address = index i64 %base, %which
    store i64 %value, %address
    return void
}

function @main() -> i32 [role=entry] {
  block ^entry:
    %base = addr @values
    call void @store_at(%base, 2, 77)
    %value = call i64 @load_at(%base, 2)
    %bad = cmp ne i64 %value, 77
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}

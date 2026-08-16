function @write_value(%p : ptr) -> void [unwind=no] {
  block ^entry:
    store i32 7, %p
    return void
}
function @main() -> i32 {
  slot $value : i32

  block ^entry:
    store i32 0, $value
    %p = addr $value
    call void @write_value(%p)
    %result = load i32 $value
    return i32 %result
}

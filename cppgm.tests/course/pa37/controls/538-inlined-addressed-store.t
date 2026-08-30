function @write_inlined_value(%address : ptr) -> void
    [binding=internal, unwind=no] {
  block ^entry:
    store i32 7, %address
    return void
}

function @main() -> i32 {
  slot $value : i32

  block ^entry:
    store i32 0, $value
    %address = addr $value
    call void @write_inlined_value(%address)
    %result = load i32 $value
    %correct = cmp eq i32 %result, 7
    branch %correct, ^pass, ^fail

  block ^pass:
    return i32 0

  block ^fail:
    return i32 1
}

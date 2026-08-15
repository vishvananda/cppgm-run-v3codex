function @read_local_address() -> i32 {
  slot $value : i32
  slot $pointer : ptr

  block ^entry:
    %value = addr $value
    store i32 77, %value
    %pointer = addr $pointer
    store ptr %value, %pointer
    %stored_pointer = load ptr %pointer
    %stored_value = load i32 %stored_pointer
    return i32 %stored_value
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %value = call i32 @read_local_address()
    %wrong = cmp ne i32 %value, 77
    %exit = convert zext i32 i1 %wrong
    return i32 %exit
}

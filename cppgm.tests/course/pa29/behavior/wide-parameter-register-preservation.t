global @observed : u32 = 0

function @sink(%ret : ptr, %this : ptr, %initializer : u32, %scope : u32, %type : u32, %local : u8) -> void {
  block ^entry:
    store u32 %type, @observed
    return void
}

function @wide(%ret : ptr, %this : ptr, %initializer : u32, %scope : u32, %type : u32, %local : u8, %require : u8, %preserve : u8) -> void {
  block ^entry:
    %address = index i8 [projection=field] %this, 8
    %value = load i64 %address
    %next = binary add i64 %value, 1
    store i64 %next, %address
    call void @sink(%ret, %this, %initializer, %scope, %type, %local)
    return void
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  slot $object : obj<16x8>

  block ^entry:
    %object = addr $object
    %value = index i8 [projection=field] %object, 8
    store i64 0, %value
    call void @wide(%object, %object, 1, 2, 77, 0, 1, 1)
    %observed = load u32 @observed
    %wrong_type = cmp ne u32 %observed, 77
    %stored = load i64 %value
    %wrong_value = cmp ne i64 %stored, 1
    %wrong = binary or i64 %wrong_type, %wrong_value
    %exit = convert trunc i32 i64 %wrong
    return i32 %exit
}

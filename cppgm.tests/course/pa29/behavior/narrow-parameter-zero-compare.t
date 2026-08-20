function @check(%unused0 : i64, %unused1 : i64, %value : u8) -> i32 {
  block ^entry:
    %is_zero = cmp eq u8 %value, 0
    branch %is_zero, ^zero, ^nonzero

  block ^zero:
    return i32 1

  block ^nonzero:
    return i32 0
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %exit = call i32 @check(0, 0, 1)
    return i32 %exit
}

function @read_fifth(%unused0 : ptr, %unused1 : i64, %unused2 : i64, %unused3 : i64, %value : ptr, %unused5 : i64, %unused6 : i64) -> i64 {
  slot $scratch1 : i64
  slot $scratch2 : i64

  block ^entry:
    %scratch1 = addr $scratch1
    %scratch2 = addr $scratch2
    %address = index i8 %value, 0
    %result = load i64 %address
    store i64 11, %scratch1
    store i64 22, %scratch2
    return i64 %result
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  slot $value : i64

  block ^entry:
    %value = addr $value
    store i64 77, %value
    %result = call i64 @read_fifth(%value, 1, 2, 3, %value, 5, 6)
    %wrong = cmp ne i64 %result, 77
    %exit = convert trunc i32 i64 %wrong
    return i32 %exit
}

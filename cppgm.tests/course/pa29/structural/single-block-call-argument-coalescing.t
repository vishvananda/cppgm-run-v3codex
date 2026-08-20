global @value = {
  i64 47
}

function @read(%address : ptr) -> i64 {
  block ^entry:
    %value = load i64 %address
    return i64 %value
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %address = addr @value
    %value = call i64 @read(%address)
    %bad = cmp ne i64 %value, 47
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}

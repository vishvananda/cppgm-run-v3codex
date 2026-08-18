global @values = {
  i64 11
  i64 22
  i64 33
}

function @read_value(%element : i64) -> i64 {
  block ^entry:
    %base = addr @values
    %scaled = binary mul i64 %element, 8
    %address = index i8 [projection=array_element] %base, %scaled
    %field = index i8 [projection=field] %address, 0
    %value = load i64 %field
    return i64 %value
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %actual = call i64 @read_value(2)
    %failed = cmp ne i64 %actual, 33
    %exit = convert trunc i32 i64 %failed
    return i32 %exit
}

global @values = {
  i64 17
  i64 0
  i64 0
}

global @other = {
  i64 25
}

function @write_second(%base : ptr, %value : i64) -> i64 {
  block ^entry:
    %copied_base = copy ptr %base
    %second_address = index i64 %copied_base, 1
    store i64 %value, %second_address
    %third_address = index i64 %base, 2
    store i64 %value, %third_address
    %other_address = addr @other
    %other_value = load i64 %other_address
    return i64 %other_value
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %base = addr @values
    %other_value = call i64 @write_second(%base, 25)
    %base_again = addr @values
    %second_address = index i64 %base_again, 1
    %stored = load i64 %second_address
    %bad_stored = cmp ne i64 %stored, 25
    %bad_other = cmp ne i64 %other_value, 25
    %bad = binary or i64 %bad_stored, %bad_other
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}

global @result = {
  i64 0
}

global @other = {
  i64 25
}

function @produce() -> i64 {
  block ^entry:
    return i64 17
}

function @write(%base : ptr) -> i64 {
  block ^entry:
    %value = call i64 @produce()
    %address = copy ptr %base
    store i64 %value, %address
    %other_address = addr @other
    %other_value = load i64 %other_address
    %sum = binary add i64 %value, %other_value
    return i64 %sum
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %base = addr @result
    %sum = call i64 @write(%base)
    %stored = load i64 @result
    %bad_stored = cmp ne i64 %stored, 17
    %bad_other = cmp ne i64 %sum, 42
    %bad = binary or i64 %bad_stored, %bad_other
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}

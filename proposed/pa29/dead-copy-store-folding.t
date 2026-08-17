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

function @write(%output : ptr) -> i64 {
  block ^entry:
    %value = call i64 @produce()
    store i64 %value, %output
    %other_address = addr @other
    %other_value = load i64 %other_address
    return i64 %other_value
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %output = addr @result
    %other_value = call i64 @write(%output)
    %stored = load i64 @result
    %bad_stored = cmp ne i64 %stored, 17
    %bad_other = cmp ne i64 %other_value, 25
    %bad = binary or i64 %bad_stored, %bad_other
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}

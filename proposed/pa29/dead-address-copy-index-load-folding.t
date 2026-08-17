global @values = {
  i64 17
  i64 25
}

function @identity(%address : ptr) -> ptr {
  block ^entry:
    return ptr %address
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %base = addr @values
    %returned_base = call ptr @identity(%base)
    %second_address = index i64 %returned_base, 1
    %second = load i64 %second_address
    %base_again = addr @values
    %first = load i64 %base_again
    %sum = binary add i64 %first, %second
    %bad = cmp ne i64 %sum, 42
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}

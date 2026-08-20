global @values = {
  i64 10
  i64 20
  i64 30
  i64 40
}

function @variable_address(%base : ptr, %which : i64) -> ptr {
  block ^entry:
    %address = index i64 %base, %which
    return ptr %address
}

function @constant_address(%base : ptr) -> ptr {
  block ^entry:
    %address = index i64 %base, 3
    return ptr %address
}

function @main() -> i32 [role=entry] {
  block ^entry:
    %base = addr @values
    %variable = call ptr @variable_address(%base, 2)
    %variable_value = load i64 %variable
    %constant = call ptr @constant_address(%base)
    %constant_value = load i64 %constant
    %variable_bad = cmp ne i64 %variable_value, 30
    %constant_bad = cmp ne i64 %constant_value, 40
    %bad = binary or i64 %variable_bad, %constant_bad
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}

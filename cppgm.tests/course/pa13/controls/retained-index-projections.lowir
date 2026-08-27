global @values [binding=internal] = {
  i64 9
  i64 37
}

function @main() -> i64 [role=entry, binding=strong] {
  block ^entry:
    %base = addr @values
    %field = index i8 [projection=field] %base, 8
    %element = index i64 [projection=array_element] %base, 1
    %field_value = load i64 %field
    %element_value = load i64 %element
    %bad = cmp ne i64 %field_value, %element_value
    return i64 %bad
}

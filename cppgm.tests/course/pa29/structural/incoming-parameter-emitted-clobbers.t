global @cells = {
  i64 0
  i64 0
  i64 0
}
global @source : i64 = 99

function @write_fields(%base : ptr, %first : ptr, %number : i64, %last : ptr [pass=by_address]) -> void {
  slot $base : ptr
  slot $first : ptr
  slot $number : i64
  slot $last : ptr

  block ^entry:
    store ptr %base, $base
    store ptr %first, $first
    store i64 %number, $number
    store ptr %last, $last
    %first_value = load ptr $first
    %base0 = load ptr $base
    %field0 = index i8 %base0, 0
    store ptr %first_value, %field0
    %number_value = load i64 $number
    %base1 = load ptr $base
    %field1 = index i8 %base1, 8
    store i64 %number_value, %field1
    %last_value = load ptr $last
    %base2 = load ptr $base
    %field2 = index i8 %base2, 16
    store ptr %last_value, %field2
    return void
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %cells = addr @cells
    %source = addr @source
    call void @write_fields(%cells, %source, 17, %source)
    %last_address = index i8 %cells, 16
    %last = load ptr %last_address
    %value = load i64 %last
    %wrong = cmp ne i64 %value, 99
    %exit = convert trunc i32 i64 %wrong
    return i32 %exit
}

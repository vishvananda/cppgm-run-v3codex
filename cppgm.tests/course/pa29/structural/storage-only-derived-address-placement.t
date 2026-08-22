global @cells = {
  i64 0
  i64 0
  i64 0
  i64 0
}

function @produce() -> i64 {
  block ^entry:
    return i64 29
}

function @write_second(%base : ptr) -> void {
  block ^entry:
    %second = index i8 %base, 8
    jump ^write

  block ^write:
    %value = call i64 @produce()
    store i64 %value, %second
    store i64 11, %base
    return void
}

function @initialize_tail(%base : ptr) -> void {
  block ^entry:
    %tail = index i8 %base, 16
    jump ^condition

  block ^condition:
    %index = phi i64 [^entry: 0, ^body: %next]
    %more = cmp ult i64 %index, 2
    branch %more, ^body, ^done

  block ^body:
    %element = index i64 %tail, %index
    %value = binary add i64 %index, 40
    store i64 %value, %element
    %next = binary add i64 %index, 1
    jump ^condition

  block ^done:
    return void
}

function @read_before_call(%base : ptr, %x : i64, %y : i64) -> i64 {
  block ^entry:
    %field = index i8 %base, 8
    jump ^compute

  block ^compute:
    %a = binary add i64 %x, 1
    %b = binary add i64 %y, 2
    %c = binary add i64 10, 20
    %d = binary add i64 30, 40
    %ab = binary add i64 %a, %b
    %value = load i64 %field
    %cd = binary add i64 %c, %d
    %abcd = binary add i64 %ab, %cd
    %before = binary add i64 %abcd, %value
    %produced = call i64 @produce()
    store i64 %produced, %base
    %result = binary add i64 %before, %produced
    return i64 %result
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %base = addr @cells
    call void @write_second(%base)
    call void @initialize_tail(%base)
    %result = call i64 @read_before_call(%base, 3, 5)
    %second = index i8 %base, 8
    %value = load i64 %second
    %third = index i8 %base, 16
    %third_value = load i64 %third
    %fourth = index i8 %base, 24
    %fourth_value = load i64 %fourth
    %second_bad = cmp ne i64 %value, 29
    %third_bad = cmp ne i64 %third_value, 40
    %fourth_bad = cmp ne i64 %fourth_value, 41
    %result_bad = cmp ne i64 %result, 169
    %tail_bad = binary or i64 %third_bad, %fourth_bad
    %values_bad = binary or i64 %second_bad, %result_bad
    %bad = binary or i64 %values_bad, %tail_bad
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}

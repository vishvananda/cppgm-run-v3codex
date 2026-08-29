global @cells = {
  i64 0
  i64 7
}

function @pointer_value() -> ptr [unwind=no] {
  block ^entry:
    %result = addr @cells
    return ptr %result
}

function @touch(%value : i64) -> i64 [unwind=no] {
  block ^entry:
    %result = binary add i64 %value, 1
    return i64 %result
}

function @read_choice(%value : i64) -> i64 [unwind=no] {
  block ^entry:
    %result = binary and i64 %value, 3
    return i64 %result
}

function @carrier_lifetimes() -> i64 [unwind=no] {
  slot $counter : i64
  slot $sum : i64

  block ^entry:
    store i64 0, $counter
    store i64 0, $sum
    jump ^loop

  block ^loop:
    %index = load i64 $counter
    %done = cmp uge i64 %index, 2
    branch %done, ^exit, ^body

  block ^body:
    %p0 = call ptr @pointer_value()
    %p1 = call ptr @pointer_value()
    %p2 = call ptr @pointer_value()
    %p3 = call ptr @pointer_value()
    %p4 = call ptr @pointer_value()
    %ignored0 = call i64 @touch(%index)
    %a0 = index i64 %p0, 1
    %v0 = load i64 %a0
    %a1 = index i64 %p1, 1
    %v1 = load i64 %a1
    %a2 = index i64 %p2, 1
    %v2 = load i64 %a2
    %a3 = index i64 %p3, 1
    %v3 = load i64 %a3
    %a4 = index i64 %p4, 1
    %v4 = load i64 %a4
    %values01 = binary add i64 %v0, %v1
    %values23 = binary add i64 %v2, %v3
    %values03 = binary add i64 %values01, %values23
    %values = binary add i64 %values03, %v4
    %first = call i64 @read_choice(%index)
    %first_zero = cmp eq i64 %first, 0
    branch %first_zero, ^first_selected, ^first_check_one

  block ^first_check_one:
    %ignored1 = call i64 @touch(%index)
    %first_one = cmp eq i64 %first, 1
    branch %first_one, ^first_selected, ^first_check_two

  block ^first_check_two:
    %first_two = cmp eq i64 %first, 2
    branch %first_two, ^first_selected, ^first_selected

  block ^first_selected:
    %second = call i64 @read_choice(%index)
    %second_zero = cmp eq i64 %second, 0
    branch %second_zero, ^second_selected, ^second_check_one

  block ^second_check_one:
    %ignored2 = call i64 @touch(%index)
    %second_one = cmp eq i64 %second, 1
    branch %second_one, ^second_selected, ^second_check_two

  block ^second_check_two:
    %second_two = cmp eq i64 %second, 2
    branch %second_two, ^second_selected, ^second_selected

  block ^second_selected:
    %sum = load i64 $sum
    %next_sum = binary add i64 %sum, %values
    %next = binary add i64 %index, 1
    store i64 %next_sum, $sum
    store i64 %next, $counter
    jump ^loop

  block ^exit:
    %result = load i64 $sum
    return i64 %result
}

function @main() -> i64 [role=entry, unwind=no] {
  block ^entry:
    %result = call i64 @carrier_lifetimes()
    %bad = cmp ne i64 %result, 70
    return i64 %bad
}

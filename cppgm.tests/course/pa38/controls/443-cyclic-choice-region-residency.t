function @read_choice(%index : i64) -> i64 [unwind=no] {
  block ^entry:
    %choice = binary and i64 %index, 3
    return i64 %choice
}

function @touch(%value : i64) -> i64 [unwind=no] {
  block ^entry:
    %result = binary add i64 %value, 1
    return i64 %result
}

function @cyclic_choice(%count : i64) -> i64 [unwind=no] {
  block ^entry:
    jump ^loop

  block ^loop:
    %index = phi i64 [^entry: 0, ^selected: %next]
    %sum = phi i64 [^entry: 0, ^selected: %next_sum]
    %done = cmp uge i64 %index, %count
    branch %done, ^exit, ^choose

  block ^choose:
    %choice = call i64 @read_choice(%index)
    %is_zero = cmp eq i64 %choice, 0
    branch %is_zero, ^zero, ^check_one

  block ^zero:
    jump ^selected

  block ^check_one:
    %ignored = call i64 @touch(%index)
    %is_one = cmp eq i64 %choice, 1
    branch %is_one, ^one, ^check_two

  block ^one:
    jump ^selected

  block ^check_two:
    %is_two = cmp eq i64 %choice, 2
    branch %is_two, ^two, ^other

  block ^two:
    jump ^selected

  block ^other:
    jump ^selected

  block ^selected:
    %amount = phi i64 [^zero: 10, ^one: 20, ^two: 30, ^other: 40]
    %next_sum = binary add i64 %sum, %amount
    %next = binary add i64 %index, 1
    jump ^loop

  block ^exit:
    return i64 %sum
}

function @loop_invariant_choice(%count : i64) -> i64 [unwind=no] {
  block ^entry:
    %choice = call i64 @read_choice(2)
    jump ^loop

  block ^loop:
    %index = phi i64 [^entry: 0, ^selected: %next]
    %sum = phi i64 [^entry: 0, ^selected: %next_sum]
    %done = cmp uge i64 %index, %count
    branch %done, ^exit, ^choose

  block ^choose:
    %is_zero = cmp eq i64 %choice, 0
    branch %is_zero, ^zero, ^check_one

  block ^zero:
    jump ^selected

  block ^check_one:
    %ignored = call i64 @touch(%index)
    %is_one = cmp eq i64 %choice, 1
    branch %is_one, ^one, ^check_two

  block ^one:
    jump ^selected

  block ^check_two:
    %is_two = cmp eq i64 %choice, 2
    branch %is_two, ^two, ^other

  block ^two:
    jump ^selected

  block ^other:
    jump ^selected

  block ^selected:
    %amount = phi i64 [^zero: 10, ^one: 20, ^two: 30, ^other: 40]
    %next_sum = binary add i64 %sum, %amount
    %next = binary add i64 %index, 1
    jump ^loop

  block ^exit:
    return i64 %sum
}

function @main() -> i64 [role=entry] {
  block ^entry:
    %local = call i64 @cyclic_choice(8)
    %invariant = call i64 @loop_invariant_choice(3)
    %local_bad = cmp ne i64 %local, 200
    %invariant_bad = cmp ne i64 %invariant, 90
    %bad = binary or i64 %local_bad, %invariant_bad
    return i64 %bad
}

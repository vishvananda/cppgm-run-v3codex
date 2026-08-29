function @touch(%value : i64) -> i64 [unwind=no] {
  block ^entry:
    %result = binary add i64 %value, 1
    return i64 %result
}

function @read_choice(%index : i64) -> i64 [unwind=no] {
  block ^entry:
    %choice = binary and i64 %index, 3
    return i64 %choice
}

function @read_choice_six(%index : i64, %a : i64, %b : i64,
                          %c : i64, %d : i64, %e : i64) -> i64 [unwind=no] {
  block ^entry:
    %choice = binary and i64 %index, 3
    return i64 %choice
}

function @future_claimed_region(%count : i64) -> i64 [unwind=no] {
  block ^entry:
    %held = call i64 @touch(%count)
    %held_next = call i64 @touch(%held)
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
    %with_held = binary add i64 %sum, %held
    %result = binary add i64 %with_held, %held_next
    return i64 %result
}

function @six_argument_region(%count : i64) -> i64 [unwind=no] {
  block ^entry:
    jump ^loop

  block ^loop:
    %index = phi i64 [^entry: 0, ^selected: %next]
    %sum = phi i64 [^entry: 0, ^selected: %next_sum]
    %done = cmp uge i64 %index, %count
    branch %done, ^exit, ^choose

  block ^choose:
    %choice = call i64 @read_choice_six(%index, 1, 2, 3, 4, 5)
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

function @main() -> i64 [role=entry, unwind=no] {
  block ^entry:
    %future = call i64 @future_claimed_region(8)
    %six = call i64 @six_argument_region(8)
    %future_bad = cmp ne i64 %future, 219
    %six_bad = cmp ne i64 %six, 200
    %bad = binary or i64 %future_bad, %six_bad
    return i64 %bad
}

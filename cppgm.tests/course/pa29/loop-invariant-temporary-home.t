global @state = {
  i64 0
  i64 0
}
global @shift_state = {
  i64 0
  i64 0
}

function @loop_shift(%result : ptr, %value : i64, %limit : i64) -> void {
  block ^entry:
    %counter = index i8 %result, 8
    store i64 0, %counter
    jump ^condition

  block ^condition:
    %iteration = load i64 %counter
    %continue = cmp ult i64 %iteration, %limit
    branch %continue, ^body, ^done

  block ^body:
    %amount = binary mul i64 %iteration, 8
    %shifted = binary ushr i64 %value, %amount
    %byte = binary and i64 %shifted, 255
    store i64 %byte, %result
    %next = binary add i64 %iteration, 1
    store i64 %next, %counter
    jump ^condition

  block ^done:
    return void
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %state = addr @state
    %stable = index i8 %state, 0
    %counter = index i8 %state, 8
    store i64 0, %counter
    jump ^condition

  block ^condition:
    %iteration = load i64 %counter
    %continue = cmp ult i64 %iteration, 2
    branch %continue, ^body, ^done

  block ^body:
    store i64 7, %stable
    %scratch = copy i64 99
    %next = binary add i64 %iteration, 1
    store i64 %next, %counter
    jump ^condition

  block ^done:
    %check_state = addr @state
    %check = index i8 %check_state, 0
    %value = load i64 %check
    %wrong_home = cmp ne i64 %value, 7
    %shift_state = addr @shift_state
    call void @loop_shift(%shift_state, 118046, 3)
    %shifted_byte = load i64 %shift_state
    %wrong_shift = cmp ne i64 %shifted_byte, 1
    %wrong = binary or i64 %wrong_home, %wrong_shift
    %exit = convert trunc i32 i64 %wrong
    return i32 %exit
}

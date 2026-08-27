global @operand = {
  i32 0
  i32 7
  i32 13
  i32 41
}

global @fact = {
  zero 72
}

function @copy_argument(%ret : ptr [pass=indirect_result], %context : ptr,
                        %value : obj<4x4>) -> void [unwind=no] {
  block ^entry:
    copyobj 4x4 %value, %ret
    return void
}

function @copy_indexed_parameter(%operand : ptr [pass=by_address],
                                 %context0 : ptr [pass=by_address],
                                 %context1 : ptr, %fact : ptr) -> u8
    [unwind=no] {
  slot $result : obj<72x8>
  slot $argument : obj<4x4>

  block ^entry:
    %kind = load i32 %operand
    %wrong = cmp ne i32 %kind, 0
    branch %wrong, ^early, ^body

  block ^early:
    return u8 0

  block ^body:
    %result = addr $result
    %argument = addr $argument
    %field = index i8 [projection=field] %operand, 12
    copyobj 4x4 %field, %argument
    call void @copy_argument(%result, %context1, $argument)
    copyobj 72x8 %result, %fact
    return u8 1
}

function @main() -> i64 [role=entry, unwind=no] {
  block ^entry:
    %operand = addr @operand
    %fact = addr @fact
    %used = call u8 @copy_indexed_parameter(%operand, %operand, %operand,
                                             %fact)
    %value = load i32 @fact
    %bad = cmp ne i32 %value, 41
    return i64 %bad
}

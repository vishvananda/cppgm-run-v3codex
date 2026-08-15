global @values = {
  i64 1
  i64 0
}

function @probe(%base : ptr, %a : i64, %b : i64, %c : i64, %d : i64, %e : i64) -> i64 {
  slot $iteration : i64

  block ^entry:
    store i64 0, $iteration
    %limit_address = index i8 %base, 0
    %sink_address = index i8 %base, 8
    jump ^condition

  block ^condition:
    %pressure = binary add i64 %a, %b
    %iteration = load i64 $iteration
    %limit = load i64 %limit_address
    %continue = cmp ult i64 %iteration, %limit
    branch %continue, ^body, ^done

  block ^body:
    %cd = binary add i64 %c, %d
    %abcd = binary add i64 %pressure, %cd
    %sum_a = binary add i64 %abcd, %a
    %sum_b = binary add i64 %sum_a, %b
    %sum = binary add i64 %sum_b, %e
    store i64 %sum, %sink_address
    %next = binary add i64 %iteration, 1
    store i64 %next, $iteration
    jump ^condition

  block ^done:
    return i64 %iteration
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %values = addr @values
    %result = call i64 @probe(%values, 1, 2, 3, 4, 5)
    %wrong = cmp ne i64 %result, 1
    %exit = convert trunc i32 i64 %wrong
    return i32 %exit
}

function @make_value(%value : i64) -> i64 {
  block ^entry:
    return i64 %value
}

function @probe() -> i64 {
  slot $outer : i64
  slot $inner : i64
  slot $total : i64

  block ^entry:
    %original = call i64 @make_value(91)
    store i64 2, $outer
    store i64 0, $total
    jump ^outer_cond

  block ^outer_cond:
    %outer = load i64 $outer
    %outer_next = binary sub i64 %outer, 1
    store i64 %outer_next, $outer
    branch %outer, ^outer_body, ^exit

  block ^outer_body:
    %old_total = load i64 $total
    %new_total = binary add i64 %old_total, %original
    store i64 %new_total, $total
    store i64 1, $inner
    jump ^inner_cond

  block ^inner_cond:
    %inner = load i64 $inner
    %inner_next = binary sub i64 %inner, 1
    store i64 %inner_next, $inner
    branch %inner, ^inner_body, ^outer_cond

  block ^inner_body:
    %replacement = call i64 @make_value(7)
    jump ^after_replacement

  block ^after_replacement:
    %noise = call i64 @make_value(3)
    %discard = binary add i64 %replacement, %noise
    jump ^inner_cond

  block ^exit:
    %result = load i64 $total
    return i64 %result
}

function @main() -> i32 [role=entry] {
  block ^entry:
    %result = call i64 @probe()
    %bad = cmp ne i64 %result, 182
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}

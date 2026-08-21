function @touch() -> void [force_inline=yes] {
  block ^entry:
    return void
}

function @main() -> i64 {
  block ^entry:
    %take_left = const i64 1
    branch %take_left, ^left, ^right

  block ^left:
    call void @touch()
    %base = const i64 40
    %left_value = binary add i64 %base, 2
    jump ^join

  block ^right:
    %right_value = const i64 7
    jump ^join

  block ^join:
    %result = phi i64 [^left: %left_value, ^right: %right_value]
    return i64 %result
}

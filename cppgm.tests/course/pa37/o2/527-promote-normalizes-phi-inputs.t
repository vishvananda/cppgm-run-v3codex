function @choose_boolean(%choose_left : i64, %left_value : i64, %right_value : i64) -> u8 [no_inline=yes] {
  slot $value : u8

  block ^entry:
    branch %choose_left, ^left, ^right

  block ^left:
    %left_boolean = cmp eq i64 %left_value, 0
    store u8 %left_boolean, $value
    jump ^join

  block ^right:
    %right_boolean = cmp ne i64 %right_value, 0
    store u8 %right_boolean, $value
    jump ^join

  block ^join:
    %result = load u8 $value
    return u8 %result
}

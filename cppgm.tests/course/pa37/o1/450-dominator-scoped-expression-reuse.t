function @main(%value : i64, %choose : i64, %inner : i64) -> i64 [role=entry] {
  block ^entry:
    branch %choose, ^left, ^right

  block ^left:
    %left_value = binary add i64 %value, 7
    branch %inner, ^left_child, ^join

  block ^right:
    %right_value = binary add i64 %value, 7
    jump ^join

  block ^left_child:
    %child_value = binary add i64 %value, 7
    jump ^join

  block ^join:
    %result = phi i64 [^left: %left_value, ^right: %right_value, ^left_child: %child_value]
    return i64 %result
}

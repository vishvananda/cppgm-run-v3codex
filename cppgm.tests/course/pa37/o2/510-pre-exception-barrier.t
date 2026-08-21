declare function @may_throw() -> void

function @keep_expression_in_exception_region(%value : i64, %choose : i64) -> i64 {
  block ^entry:
    eh_try ^cleanup
    branch %choose, ^left, ^right

  block ^left:
    %left_value = binary and i64 %value, 63
    jump ^join

  block ^right:
    %right_value = binary and i64 %value, 63
    jump ^join

  block ^join:
    %selected = phi i64 [^left: %left_value, ^right: %right_value]
    %redundant = binary and i64 %value, 63
    %result = binary add i64 %selected, %redundant
    call void @may_throw()
    eh_end
    return i64 %result

  block ^cleanup:
    resume
}

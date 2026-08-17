declare function @may_throw(%value : i64) -> void
declare function @cleanup_extra() -> void [unwind=no]
declare function @cleanup_value(%value : i64) -> void [unwind=no]

function @share_tail(%choose : i64, %value : i64) -> void {
  block ^entry:
    branch %choose, ^left, ^right

  block ^left:
    eh_cleanup ^cleanup_left
    call void @may_throw(1)
    eh_end
    return void

  block ^right:
    eh_cleanup ^cleanup_right
    call void @may_throw(2)
    eh_end
    return void

  block ^cleanup_left:
    call void @cleanup_extra()
    call void @cleanup_value(%value)
    resume

  block ^cleanup_right:
    call void @cleanup_value(%value)
    resume
}

function @keep_different_tail(%choose : i64) -> void {
  block ^entry:
    branch %choose, ^left, ^right

  block ^left:
    eh_cleanup ^cleanup_left
    call void @may_throw(3)
    eh_end
    return void

  block ^right:
    eh_cleanup ^cleanup_right
    call void @may_throw(4)
    eh_end
    return void

  block ^cleanup_left:
    call void @cleanup_value(1)
    resume

  block ^cleanup_right:
    call void @cleanup_value(2)
    resume
}

function @keep_different_context() -> void {
  block ^entry:
    eh_try ^outer_cleanup
    eh_try ^inner_cleanup
    call void @may_throw(5)
    eh_end
    eh_end
    return void

  block ^inner_cleanup:
    call void @cleanup_value(3)
    resume

  block ^outer_cleanup:
    call void @cleanup_value(3)
    resume
}

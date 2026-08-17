declare function @may_throw(%value : i64) -> void
declare function @cleanup() -> void [unwind=no]

function @work(%choose : i64) -> void {
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
    call void @cleanup()
    jump ^resume_left

  block ^cleanup_right:
    call void @cleanup()
    jump ^resume_right

  block ^resume_left:
    resume

  block ^resume_right:
    resume
}

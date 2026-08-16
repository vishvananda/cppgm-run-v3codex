declare function @may_throw() -> void
declare function @sink(%value : ptr) -> void [unwind=no]

function @probe(%condition : i64) -> void {
  slot $object : obj<8x8>
  slot $saved : ptr

  block ^entry:
    eh_try ^cleanup_start
    call void @may_throw()
    eh_end
    return void

  block ^use:
    %loaded = load ptr $saved
    call void @sink(%loaded)
    jump ^resume

  block ^cleanup_next:
    %address = addr $object
    store ptr %address, $saved
    branch %condition, ^use, ^resume

  block ^cleanup_start:
    jump ^cleanup_next

  block ^resume:
    resume
}

function @main() -> i32 [role=entry] {
  block ^entry:
    return i32 0
}

declare function @may_unwind() -> void

function @cleanup_helper() -> void [unwind=no, binding=weak] {
  block ^entry:
    return void
}

function @first() -> void [binding=strong, no_inline=yes] {
  block ^entry:
    eh_cleanup ^cleanup
    call void @may_unwind()
    eh_end
    return void

  block ^cleanup:
    call void @cleanup_helper()
    eh_end
    resume
}

function @second() -> void [binding=strong, no_inline=yes] {
  block ^entry:
    eh_cleanup ^cleanup
    call void @may_unwind()
    eh_end
    return void

  block ^cleanup:
    call void @cleanup_helper()
    eh_end
    resume
}

function @main() -> void [role=entry] {
  block ^entry:
    call void @first()
    call void @second()
    return void
}

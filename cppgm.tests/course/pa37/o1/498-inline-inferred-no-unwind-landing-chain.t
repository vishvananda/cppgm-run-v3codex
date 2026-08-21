declare function @may_unwind() -> void

function @consumer() -> void [binding=strong, no_inline=yes] {
  block ^entry:
    eh_cleanup ^cleanup
    call void @may_unwind()
    eh_end
    return void

  block ^cleanup:
    call void @wrapper()
    eh_end
    resume
}

function @wrapper() -> void [binding=weak] {
  block ^entry:
    call void @leaf()
    return void
}

function @leaf() -> void [binding=weak] {
  block ^entry:
    return void
}

function @main() -> void [role=entry] {
  block ^entry:
    call void @consumer()
    return void
}

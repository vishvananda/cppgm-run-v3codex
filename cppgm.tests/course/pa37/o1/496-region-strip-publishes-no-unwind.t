declare function @safe() -> void [unwind=no]

function @outer() -> void [binding=strong, no_inline=yes] {
  block ^entry:
    eh_cleanup ^outer_cleanup
    call void @inner()
    eh_end
    return void

  block ^outer_cleanup:
    call void @safe()
    eh_end
    resume
}

function @inner() -> void [binding=strong, no_inline=yes] {
  block ^entry:
    eh_cleanup ^inner_cleanup
    call void @safe()
    eh_end
    return void

  block ^inner_cleanup:
    call void @safe()
    eh_end
    resume
}

function @main() -> void [role=entry] {
  block ^entry:
    call void @outer()
    return void
}

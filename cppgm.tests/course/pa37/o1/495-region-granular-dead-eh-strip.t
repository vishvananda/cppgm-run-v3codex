declare function @safe() -> void [unwind=no]
declare function @may_unwind() -> void

function @mixed() -> void [binding=strong, no_inline=yes] {
  block ^entry:
    eh_cleanup ^unsafe_cleanup
    call void @may_unwind()
    eh_end
    eh_cleanup ^safe_cleanup
    call void @safe()
    eh_end
    return void

  block ^unsafe_cleanup:
    call void @safe()
    eh_end
    resume

  block ^safe_cleanup:
    call void @safe()
    eh_end
    resume
}

function @main() -> void [role=entry] {
  block ^entry:
    call void @mixed()
    return void
}

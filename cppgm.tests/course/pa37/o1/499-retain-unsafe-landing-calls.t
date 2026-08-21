declare function @may_unwind() -> void

function @explicit_noinline() -> void
    [unwind=no, binding=weak, no_inline=yes] {
  block ^entry:
    return void
}

function @throws() -> void [binding=weak] {
  block ^entry:
    throw i64 7
}

function @eh_bearing() -> void [unwind=no, binding=weak] {
  block ^entry:
    eh_cleanup ^cleanup
    call void @may_unwind()
    eh_end
    return void

  block ^cleanup:
    eh_end
    resume
}

function @negative(%indirect : ptr) -> void
    [binding=strong, no_inline=yes] {
  block ^entry:
    eh_cleanup ^may_unwind_landing
    call void @may_unwind()
    eh_end
    eh_cleanup ^indirect_landing
    call void @may_unwind()
    eh_end
    eh_cleanup ^throw_landing
    call void @may_unwind()
    eh_end
    eh_cleanup ^eh_landing
    call void @may_unwind()
    eh_end
    eh_cleanup ^noinline_landing
    call void @may_unwind()
    eh_end
    return void

  block ^may_unwind_landing:
    call void @may_unwind()
    eh_end
    resume

  block ^indirect_landing:
    call void %indirect() as () -> void
    eh_end
    resume

  block ^throw_landing:
    call void @throws()
    eh_end
    resume

  block ^eh_landing:
    call void @eh_bearing()
    eh_end
    resume

  block ^noinline_landing:
    call void @explicit_noinline()
    eh_end
    resume
}

function @main() -> void [role=entry] {
  block ^entry:
    %address = copy ptr @may_unwind
    call void @negative(%address)
    return void
}

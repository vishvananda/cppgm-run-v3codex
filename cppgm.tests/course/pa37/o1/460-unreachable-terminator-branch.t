declare function @ordinary_noreturn() -> void [effects=readnone, unwind=no, return=noreturn]

function @drop_guard(%condition : i64) -> i64 [binding=strong, no_inline=yes] {
  block ^entry:
    branch %condition, ^undefined, ^good

  block ^undefined:
    unreachable

  block ^good:
    return i64 7
}

function @keep_ordinary_call(%condition : i64) -> i64 [binding=strong, no_inline=yes] {
  block ^entry:
    branch %condition, ^ordinary, ^good

  block ^ordinary:
    call void @ordinary_noreturn()
    jump ^good

  block ^good:
    return i64 9
}

function @main() -> i64 [role=entry] {
  block ^entry:
    return i64 0
}

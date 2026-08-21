global @observed : i64 = 0

function @main() -> i64 [role=entry] {
  block ^entry:
    jump ^header !dbg(loop.cpp, 3, 3)

  block ^header:
    %i = phi i64 [^entry: 0, ^body: %next] !dbg(loop.cpp, 4, 3)
    %more = cmp lt i64 %i, 3 !dbg(loop.cpp, 4, 10)
    branch %more, ^body, ^exit !dbg(loop.cpp, 4, 15)

  block ^body:
    store i64 %i, @observed !dbg(loop.cpp, 5, 5)
    %next = binary add i64 %i, 1 !dbg(loop.cpp, 4, 20)
    jump ^header !dbg(loop.cpp, 4, 25)

  block ^exit:
    return i64 0 !dbg(loop.cpp, 7, 3)
}

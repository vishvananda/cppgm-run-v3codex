declare function @may_throw() -> void
declare function @sink(%arg0 : ptr [pass=by_address]) -> void

function @f() -> i64 {
  slot $x : obj<16x8>

  block ^entry:
    eh_try ^catch
    call void @may_throw()
    eh_end
    %late = addr $x
    call void @sink(%late)
    return i64 1

  block ^catch:
    %p = addr $x
    %field = index i8 [projection=field] %p, 0
    call void @sink(%field)
    return i64 0
}

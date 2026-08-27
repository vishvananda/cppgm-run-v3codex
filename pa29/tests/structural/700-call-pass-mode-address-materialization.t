function @copy_ref(%ret : ptr [pass=indirect_result], %src : ptr [pass=by_address]) -> void {
  block ^entry:
    %v = load i64 %src
    store i64 %v, %ret
    return void
}

function @main() -> i64 [role=entry] {
  slot $src : i64
  slot $dst : i64

  block ^entry:
    store i64 7, $src
    store i64 0, $dst
    call void @copy_ref($dst, $src)
    %v = load i64 $dst
    %ok = cmp eq i64 %v, 7
    %fail = cmp eq i64 %ok, 0
    return i64 %fail
}

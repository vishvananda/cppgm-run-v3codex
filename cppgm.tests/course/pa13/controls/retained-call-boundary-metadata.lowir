function @helper(%ret : ptr [pass=indirect_result], %obj : ptr [pass=by_address], %ordinary : ptr, %x : i64) -> void {
  block ^entry:
    store i64 %x, %ret
    return void
}

function @main() -> i64 [role=entry] {
  slot $out : i64

  block ^entry:
    zeroinit 8x8 $out
    %out = addr $out
    call void @helper(%out, %out, %out, 5)
    %actual = load i64 $out
    %wrong = cmp ne i64 %actual, 5
    return i64 %wrong
}

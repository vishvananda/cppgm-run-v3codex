function @copy(%dst : ptr [alias=noalias], %src : ptr [alias=noalias], %n : i64) -> void [unwind=no] {
  block ^entry:
    return void
}

function @helper(%fn : ptr, %dst : ptr [alias=noalias], %src : ptr [alias=noalias]) -> void {
  block ^entry:
    %n = const i64 4
    call void %fn(%dst, %src, %n) as (%arg0 : ptr [alias=noalias], %arg1 : ptr [alias=noalias], %arg2 : i64) -> void [unwind=no]
    return void
}

function @main(%dst : ptr [alias=noalias], %src : ptr [alias=noalias]) -> i64 {
  block ^entry:
    %n = const i64 4
    call void @copy(%dst, %src, %n)
    return i64 0
}

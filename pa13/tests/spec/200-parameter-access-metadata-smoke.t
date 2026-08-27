function @sink(%p : ptr [capture=nocapture, access=read]) -> void [effects=readonly, unwind=no] {
  block ^entry:
    return void
}

function @copy(%fn : ptr, %dst : ptr [capture=nocapture, access=write], %src : ptr [capture=nocapture, access=read]) -> void {
  block ^entry:
    call void %fn(%dst, %src) as (%arg0 : ptr [capture=nocapture, access=write], %arg1 : ptr [capture=nocapture, access=read]) -> void [unwind=no]
    return void
}

function @main(%p : ptr [access=read]) -> i64 {
  block ^entry:
    call void @sink(%p)
    return i64 0
}

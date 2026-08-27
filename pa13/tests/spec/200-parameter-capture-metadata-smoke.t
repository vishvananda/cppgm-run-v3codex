function @sink(%p : ptr [capture=nocapture]) -> void [effects=readonly, unwind=no] {
  block ^entry:
    return void
}

function @helper(%fn : ptr, %p : ptr [capture=nocapture]) -> void {
  block ^entry:
    call void %fn(%p) as (%arg0 : ptr [capture=nocapture]) -> void [effects=readonly, unwind=no]
    return void
}

function @main(%p : ptr) -> i64 {
  block ^entry:
    call void @sink(%p)
    return i64 0
}

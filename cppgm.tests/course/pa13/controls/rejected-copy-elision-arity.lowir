function @destroy(%object : ptr) -> void {
  block ^entry:
    return void
}

function @main() -> i64 [role=entry] {
  slot $object : i64

  block ^entry:
    %object = addr $object
    call void @destroy(%object) [elision=copy]
    return i64 0
}
